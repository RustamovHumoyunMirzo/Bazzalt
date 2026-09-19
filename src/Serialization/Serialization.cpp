#include "Bazzalt/Serialization.h"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

#include "Bazzalt/Components/Hierarchy.h"
#include "Bazzalt/Components/Name.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Scene.h"

namespace Bazzalt {
namespace {

struct EntityRecord {
    UUID Id;
    UUID Parent;
    std::string Name;
    std::vector<SerializedComponent> Components;
};

std::string WriteFloat(float value) {
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    return stream.str();
}

bool ReadFloat(const PropertyMap& values, const char* key, float& result) {
    const auto found = values.find(key);
    if (found == values.end()) return false;
    const char* begin = found->second.data();
    const char* end = begin + found->second.size();
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool ReadUUID(std::istream& stream, UUID& result) {
    std::string value;
    return static_cast<bool>(stream >> std::quoted(value)) && UUID::TryParse(value, result);
}

void WriteComponent(std::ostream& stream, const SerializedComponent& component) {
    stream << "COMPONENT " << std::quoted(component.Type) << ' ' << component.Version << ' '
           << component.Properties.size() << '\n';
    for (const auto& [key, value] : component.Properties)
        stream << "PROPERTY " << std::quoted(key) << ' ' << std::quoted(value) << '\n';
    stream << "END_COMPONENT\n";
}

} // namespace

void ComponentSerializationRegistry::RegisterDescriptor(Descriptor descriptor) {
    for (Descriptor& existing : m_descriptors) {
        if (existing.Type == descriptor.Type) {
            existing = std::move(descriptor);
            return;
        }
    }
    m_descriptors.emplace_back(std::move(descriptor));
}

const ComponentSerializationRegistry::Descriptor*
ComponentSerializationRegistry::Find(const std::string& type) const {
    for (const Descriptor& descriptor : m_descriptors)
        if (descriptor.Type == type) return &descriptor;
    return nullptr;
}

SceneSerializer::SceneSerializer() {
    m_components.Register<Transform>("Bazzalt.Transform", 1,
        [](const Transform& value, PropertyMap& output) {
            output["Position.X"] = WriteFloat(value.Position.X);
            output["Position.Y"] = WriteFloat(value.Position.Y);
            output["Position.Z"] = WriteFloat(value.Position.Z);
            output["Rotation.X"] = WriteFloat(value.Rotation.X);
            output["Rotation.Y"] = WriteFloat(value.Rotation.Y);
            output["Rotation.Z"] = WriteFloat(value.Rotation.Z);
            output["Rotation.W"] = WriteFloat(value.Rotation.W);
            output["Scale.X"] = WriteFloat(value.Scale.X);
            output["Scale.Y"] = WriteFloat(value.Scale.Y);
            output["Scale.Z"] = WriteFloat(value.Scale.Z);
        },
        [](Transform& value, const PropertyMap& input, std::uint32_t version) {
            return version == 1 &&
                ReadFloat(input, "Position.X", value.Position.X) &&
                ReadFloat(input, "Position.Y", value.Position.Y) &&
                ReadFloat(input, "Position.Z", value.Position.Z) &&
                ReadFloat(input, "Rotation.X", value.Rotation.X) &&
                ReadFloat(input, "Rotation.Y", value.Rotation.Y) &&
                ReadFloat(input, "Rotation.Z", value.Rotation.Z) &&
                ReadFloat(input, "Rotation.W", value.Rotation.W) &&
                ReadFloat(input, "Scale.X", value.Scale.X) &&
                ReadFloat(input, "Scale.Y", value.Scale.Y) &&
                ReadFloat(input, "Scale.Z", value.Scale.Z);
        });
}

bool SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path) {
    m_lastError.clear();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) { m_lastError = "Could not open scene file for writing: " + path.string(); return false; }

    stream << "BAZZALT_SCENE " << CurrentFormatVersion << '\n';
    stream << "SCENE " << std::quoted(scene.GetUUID().ToString()) << '\n';
    auto view = scene.GetRegistry().view<Identity, Name, Hierarchy>();
    std::size_t entityCount = 0;
    for (auto handle : view) if (!view.get<Identity>(handle).Value.IsRoot()) ++entityCount;
    stream << "ENTITY_COUNT " << entityCount << '\n';

    for (auto handle : view) {
        Entity entity = const_cast<Scene&>(scene).GetEntity(static_cast<Entity::Id>(handle));
        if (entity.GetUUID().IsRoot()) continue;
        const auto& hierarchy = entity.GetComponent<Hierarchy>();
        const auto& name = entity.GetComponent<Name>();
        std::vector<SerializedComponent> serialized;
        for (const auto& descriptor : m_components.GetDescriptors()) {
            SerializedComponent component{descriptor.Type, descriptor.Version, {}};
            if (descriptor.Serialize(entity, component.Properties)) serialized.push_back(std::move(component));
        }
        if (const auto* unresolved = entity.TryGetComponent<UnresolvedComponents>()) {
            for (const auto& component : unresolved->Values) {
                // Once a serializer becomes available, its live component is
                // authoritative and stale preserved data must not be emitted.
                if (m_components.Find(component.Type) == nullptr)
                    serialized.push_back(component);
            }
        }

        stream << "ENTITY " << std::quoted(entity.GetUUID().ToString()) << ' '
               << std::quoted(hierarchy.Parent.ToString()) << ' ' << std::quoted(name.Value) << ' '
               << serialized.size() << '\n';
        for (const auto& component : serialized) WriteComponent(stream, component);
        stream << "END_ENTITY\n";
    }
    stream << "END_SCENE\n";
    if (!stream) { m_lastError = "Failed while writing scene file: " + path.string(); return false; }
    return true;
}

bool SceneSerializer::Load(Scene& scene, const std::filesystem::path& path) {
    m_lastError.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { m_lastError = "Could not open scene file: " + path.string(); return false; }

    std::string token;
    std::uint32_t formatVersion = 0;
    UUID sceneUuid;
    std::size_t entityCount = 0;
    if (!(stream >> token >> formatVersion) || token != "BAZZALT_SCENE" || formatVersion > CurrentFormatVersion || formatVersion == 0 ||
        !(stream >> token) || token != "SCENE" || !ReadUUID(stream, sceneUuid) ||
        !(stream >> token >> entityCount) || token != "ENTITY_COUNT") {
        m_lastError = "Invalid or unsupported scene header";
        return false;
    }

    std::vector<EntityRecord> records;
    records.reserve(entityCount);
    std::unordered_set<UUID> ids;
    for (std::size_t entityIndex = 0; entityIndex < entityCount; ++entityIndex) {
        EntityRecord record;
        std::size_t componentCount = 0;
        if (!(stream >> token) || token != "ENTITY" || !ReadUUID(stream, record.Id) ||
            !ReadUUID(stream, record.Parent) || !(stream >> std::quoted(record.Name) >> componentCount) ||
            record.Id.IsRoot() || !ids.insert(record.Id).second) {
            m_lastError = "Invalid or duplicate entity record";
            return false;
        }
        for (std::size_t componentIndex = 0; componentIndex < componentCount; ++componentIndex) {
            SerializedComponent component;
            std::size_t propertyCount = 0;
            if (!(stream >> token) || token != "COMPONENT" ||
                !(stream >> std::quoted(component.Type) >> component.Version >> propertyCount)) {
                m_lastError = "Invalid component record"; return false;
            }
            for (std::size_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex) {
                std::string key, value;
                if (!(stream >> token) || token != "PROPERTY" ||
                    !(stream >> std::quoted(key) >> std::quoted(value))) {
                    m_lastError = "Invalid component property"; return false;
                }
                component.Properties[std::move(key)] = std::move(value);
            }
            if (!(stream >> token) || token != "END_COMPONENT") { m_lastError = "Missing END_COMPONENT"; return false; }
            record.Components.push_back(std::move(component));
        }
        if (!(stream >> token) || token != "END_ENTITY") { m_lastError = "Missing END_ENTITY"; return false; }
        records.push_back(std::move(record));
    }
    if (!(stream >> token) || token != "END_SCENE") { m_lastError = "Missing END_SCENE"; return false; }
    for (const auto& record : records) {
        if (!record.Parent.IsRoot() && ids.find(record.Parent) == ids.end()) {
            m_lastError = "Entity references a missing parent"; return false;
        }
    }

    scene.Clear();
    scene.m_uuid = sceneUuid;
    for (const auto& record : records) scene.CreateEntity(record.Id, record.Name);
    for (const auto& record : records) {
        Entity entity = scene.GetEntity(record.Id);
        if (!record.Parent.IsRoot() && !scene.SetParent(entity, scene.GetEntity(record.Parent))) {
            m_lastError = "Invalid hierarchy or hierarchy cycle"; return false;
        }
        for (const auto& component : record.Components) {
            const auto* descriptor = m_components.Find(component.Type);
            if (descriptor != nullptr) {
                if (!descriptor->Deserialize(entity, component.Properties, component.Version)) {
                    m_lastError = "Could not deserialize component: " + component.Type; return false;
                }
            } else {
                auto* unresolved = entity.TryGetComponent<UnresolvedComponents>();
                if (unresolved == nullptr) unresolved = &entity.AddComponent<UnresolvedComponents>();
                unresolved->Values.push_back(component);
            }
        }
    }
    return true;
}

} // namespace Bazzalt
