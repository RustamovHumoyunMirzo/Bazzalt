#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "Bazzalt/Component.h"
#include "Bazzalt/Entity.h"

namespace Bazzalt {

class Scene;
namespace Runtime { class Engine; }

using PropertyMap = std::map<std::string, std::string>;

struct SerializedComponent {
    std::string Type;
    std::uint32_t Version = 1;
    PropertyMap Properties;
};

// Attached during loading when a component type is unavailable. Saving the
// scene again preserves these records verbatim for forward/plugin compatibility.
struct UnresolvedComponents : Component {
    std::vector<SerializedComponent> Values;
};

class ComponentSerializationRegistry final {
public:
    struct Descriptor {
        std::string Type;
        std::uint32_t Version;
        std::function<bool(Entity, PropertyMap&)> Serialize;
        std::function<bool(Entity, const PropertyMap&, std::uint32_t)> Deserialize;
    };

    template<typename ComponentType, typename SerializeFunction, typename DeserializeFunction>
    void Register(std::string type, std::uint32_t version,
                  SerializeFunction&& serialize, DeserializeFunction&& deserialize) {
        static_assert(std::is_base_of_v<Component, ComponentType>,
                      "ComponentType must derive from Bazzalt::Component");
        Descriptor descriptor;
        descriptor.Type = std::move(type);
        descriptor.Version = version;
        descriptor.Serialize = [function = std::forward<SerializeFunction>(serialize)]
            (Entity entity, PropertyMap& properties) mutable {
                const ComponentType* component = entity.TryGetComponent<ComponentType>();
                if (component == nullptr) return false;
                function(*component, properties);
                return true;
            };
        descriptor.Deserialize = [function = std::forward<DeserializeFunction>(deserialize)]
            (Entity entity, const PropertyMap& properties, std::uint32_t storedVersion) mutable {
                ComponentType value{};
                if (!function(value, properties, storedVersion)) return false;
                entity.AddOrReplaceComponent<ComponentType>(std::move(value));
                return true;
            };
        RegisterDescriptor(std::move(descriptor));
    }

    void RegisterDescriptor(Descriptor descriptor);
    [[nodiscard]] const Descriptor* Find(const std::string& type) const;
    [[nodiscard]] const std::vector<Descriptor>& GetDescriptors() const { return m_descriptors; }

private:
    std::vector<Descriptor> m_descriptors;
};

class SceneSerializer final {
public:
    static constexpr std::uint32_t CurrentFormatVersion = 1;

    [[nodiscard]] ComponentSerializationRegistry& GetComponents() { return m_components; }
    [[nodiscard]] const ComponentSerializationRegistry& GetComponents() const { return m_components; }
    [[nodiscard]] const std::string& GetLastError() const { return m_lastError; }

private:
    friend class Runtime::Engine;
    SceneSerializer();
    bool Save(const Scene& scene, const std::filesystem::path& path);
    bool Load(Scene& scene, const std::filesystem::path& path);

    ComponentSerializationRegistry m_components;
    std::string m_lastError;
};

} // namespace Bazzalt
