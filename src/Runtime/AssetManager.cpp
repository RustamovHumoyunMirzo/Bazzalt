#include "Bazzalt/AssetManager.h"
#include "Runtime/TextureLibrary.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ryml.hpp>

#include "Runtime/Engine.h"
#include "Runtime/MaterialLibrary.h"

namespace Bazzalt {
namespace {
std::vector<Runtime::Engine*> Engines;
Runtime::Engine* ActiveEngine() { return Engines.empty() ? nullptr : Engines.back(); }
std::string PathUtf8(const std::filesystem::path& path) { const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()}; }

std::string NodeText(ryml::ConstNodeRef node) {
    const auto value = node.val();
    return std::string(value.str, value.len);
}

bool ReadIndex(ryml::ConstNodeRef node, std::uint32_t& value) {
    const std::string text = NodeText(node);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool ReadFloat(ryml::ConstNodeRef node, float& value) {
    const std::string text = NodeText(node);
    char* end = nullptr;
    value = std::strtof(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(value);
}

template<std::size_t Size>
bool ReadArray(ryml::ConstNodeRef node, std::array<float, Size>& values) {
    if (!node.valid() || node.num_children() != Size) return false;
    std::size_t index = 0;
    for (const auto child : node.children())
        if (!ReadFloat(child, values[index++])) return false;
    return true;
}

std::string ReadModelJson(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::string extension = PathUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".glb")
        return std::string((std::istreambuf_iterator<char>(stream)), {});
    std::uint32_t magic = 0, version = 0, totalLength = 0;
    std::uint32_t chunkLength = 0, chunkType = 0;
    stream.read(reinterpret_cast<char*>(&magic), 4);
    stream.read(reinterpret_cast<char*>(&version), 4);
    stream.read(reinterpret_cast<char*>(&totalLength), 4);
    stream.read(reinterpret_cast<char*>(&chunkLength), 4);
    stream.read(reinterpret_cast<char*>(&chunkType), 4);
    constexpr std::uint32_t GlbMagic = 0x46546c67;
    constexpr std::uint32_t JsonChunk = 0x4e4f534a;
    if (!stream || magic != GlbMagic || version != 2 || chunkType != JsonChunk ||
        chunkLength > 64u * 1024u * 1024u || totalLength < 20u + chunkLength) return {};
    std::string json(chunkLength, '\0');
    stream.read(json.data(), static_cast<std::streamsize>(chunkLength));
    return stream ? json : std::string{};
}

Quaternion RotationFromMatrix(const std::array<float, 16>& matrix, Vec3 scale) {
    const float m00 = matrix[0] / scale.X, m01 = matrix[4] / scale.Y, m02 = matrix[8] / scale.Z;
    const float m10 = matrix[1] / scale.X, m11 = matrix[5] / scale.Y, m12 = matrix[9] / scale.Z;
    const float m20 = matrix[2] / scale.X, m21 = matrix[6] / scale.Y, m22 = matrix[10] / scale.Z;
    Quaternion result;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        result = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        result = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        result = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        result = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return result.Normalized();
}

std::string EscapePathSegment(std::string value) {
    std::string result;
    for (const char character : value) {
        if (character == '%') result += "%25";
        else if (character == '/') result += "%2F";
        else result += character;
    }
    return result.empty() ? "Node" : result;
}

bool BuildStablePaths(ModelAsset& model, std::uint32_t index, const std::string& path,
                      std::vector<bool>& visiting, std::vector<bool>& visited) {
    if (index >= model.Nodes.size() || visiting[index] || visited[index]) return false;
    visiting[index] = true;
    auto& node = model.Nodes[index];
    node.StablePath = path;
    std::unordered_map<std::string, std::size_t> occurrences;
    for (const auto child : node.Children) {
        const std::string base = EscapePathSegment(model.Nodes[child].Name);
        const std::size_t occurrence = occurrences[base]++;
        const std::string segment = occurrence == 0 ? base : base + "#" + std::to_string(occurrence);
        if (!BuildStablePaths(model, child, node.StablePath + "/" + segment,
                              visiting, visited)) return false;
    }
    visiting[index] = false; visited[index] = true;
    return true;
}
}

std::optional<AssetInfo> AssetManager::GetAsset(UUID id) {
    auto* engine = ActiveEngine();
    return engine != nullptr ? engine->FindAsset(id) : std::nullopt;
}

std::optional<AssetInfo> AssetManager::GetAsset(const std::filesystem::path& path) {
    auto* engine = ActiveEngine();
    return engine != nullptr ? engine->FindAsset(path) : std::nullopt;
}

bool AssetManager::IsAssetReady(UUID id) {
    const auto asset = GetAsset(id);
    return asset && asset->State == AssetState::Ready;
}

std::optional<ModelAsset> AssetManager::LoadModel(UUID id) {
    const auto asset = GetAsset(id);
    if (!asset || asset->State != AssetState::Ready) return std::nullopt;
    std::string extension = PathUtf8(asset->SourcePath.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".gltf" && extension != ".glb")
        return std::nullopt;
    std::string json = ReadModelJson(asset->CachePath);
    if (json.empty()) return std::nullopt;
    try {
        auto tree = ryml::parse_in_arena(ryml::csubstr(json.data(), json.size()));
        const auto root = tree.rootref();
        if (!root.has_child("nodes")) return std::nullopt;
        ModelAsset model; model.Id = id; model.Name = PathUtf8(asset->SourcePath.stem());
        const auto sourceNodes = root["nodes"];
        model.Nodes.resize(sourceNodes.num_children());
        std::uint32_t index = 0;
        for (const auto source : sourceNodes.children()) {
            auto& node = model.Nodes[index]; node.SourceIndex = index;
            node.Name = source.has_child("name") ? NodeText(source["name"])
                                                  : "Node " + std::to_string(index);
            if (source.has_child("mesh") && !ReadIndex(source["mesh"], node.MeshIndex))
                return std::nullopt;
            if(node.HasMesh()) {
                if(!root.has_child("meshes")||node.MeshIndex>=root["meshes"].num_children())return std::nullopt;
                const auto mesh=root["meshes"][node.MeshIndex];
                if(mesh.has_child("primitives"))for(const auto primitive:mesh["primitives"].children()){
                    std::string name="Default Material";std::uint32_t material=0;
                    if(primitive.has_child("material")&&ReadIndex(primitive["material"],material)&&root.has_child("materials")&&material<root["materials"].num_children()){
                        const auto value=root["materials"][material];name=value.has_child("name")?NodeText(value["name"]):"Material "+std::to_string(material);
                    }
                    node.MaterialNames.push_back(std::move(name));
                }
            }
            if (source.has_child("children")) for (const auto child : source["children"].children()) {
                std::uint32_t childIndex = 0;
                if (!ReadIndex(child, childIndex) || childIndex >= model.Nodes.size()) return std::nullopt;
                node.Children.push_back(childIndex);
            }
            std::array<float, 3> vector{};
            if (source.has_child("translation")) {
                if (!ReadArray(source["translation"], vector)) return std::nullopt;
                node.Position = {vector[0], vector[1], vector[2]};
            }
            if (source.has_child("scale")) {
                if (!ReadArray(source["scale"], vector)) return std::nullopt;
                node.Scale = {vector[0], vector[1], vector[2]};
            }
            std::array<float, 4> rotation{};
            if (source.has_child("rotation")) {
                if (!ReadArray(source["rotation"], rotation)) return std::nullopt;
                node.Rotation = Quaternion{rotation[0], rotation[1], rotation[2], rotation[3]}.Normalized();
            }
            if (source.has_child("matrix")) {
                std::array<float, 16> matrix{};
                if (!ReadArray(source["matrix"], matrix)) return std::nullopt;
                node.Position = {matrix[12], matrix[13], matrix[14]};
                node.Scale = {
                    Vec3{matrix[0], matrix[1], matrix[2]}.Length(),
                    Vec3{matrix[4], matrix[5], matrix[6]}.Length(),
                    Vec3{matrix[8], matrix[9], matrix[10]}.Length()};
                if (node.Scale.X <= Epsilon || node.Scale.Y <= Epsilon || node.Scale.Z <= Epsilon)
                    return std::nullopt;
                node.Rotation = RotationFromMatrix(matrix, node.Scale);
            }
            ++index;
        }
        std::vector<std::uint32_t> parentCount(model.Nodes.size(), 0);
        for (const auto& node : model.Nodes) for (const auto child : node.Children)
            if (++parentCount[child] > 1) return std::nullopt;
        if (root.has_child("scenes") && root["scenes"].num_children() > 0) {
            std::uint32_t sceneIndex = 0;
            if (root.has_child("scene") && !ReadIndex(root["scene"], sceneIndex)) return std::nullopt;
            if (sceneIndex >= root["scenes"].num_children()) return std::nullopt;
            const auto scene = root["scenes"][sceneIndex];
            if (scene.has_child("nodes")) for (const auto item : scene["nodes"].children()) {
                std::uint32_t rootIndex = 0;
                if (!ReadIndex(item, rootIndex) || rootIndex >= model.Nodes.size()) return std::nullopt;
                model.Roots.push_back(rootIndex);
            }
        }
        if (model.Roots.empty()) for (std::uint32_t i = 0; i < parentCount.size(); ++i)
            if (parentCount[i] == 0) model.Roots.push_back(i);
        std::vector<bool> visiting(model.Nodes.size()), visited(model.Nodes.size());
        std::unordered_map<std::string, std::size_t> rootOccurrences;
        for (const auto rootIndex : model.Roots) {
            const std::string base = EscapePathSegment(model.Nodes[rootIndex].Name);
            const std::size_t occurrence = rootOccurrences[base]++;
            const std::string path = occurrence == 0 ? base : base + "#" + std::to_string(occurrence);
            if (!BuildStablePaths(model, rootIndex, path, visiting, visited)) return std::nullopt;
        }
        if (std::find(visited.begin(), visited.end(), false) != visited.end()) return std::nullopt;
        return model;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void AssetManager::Bind(Runtime::Engine* engine) {
    Detail::BoundMaterialServices=Runtime::GetMaterialServices();
    if (engine && std::find(Engines.begin(), Engines.end(), engine) == Engines.end())
        Engines.push_back(engine);
}
void AssetManager::Unbind(Runtime::Engine* engine) {
    Engines.erase(std::remove(Engines.begin(), Engines.end(), engine), Engines.end());
    if(Engines.empty()){Runtime::ResetMaterialLibrary();Runtime::ResetTextureLibrary();Detail::BoundMaterialServices=nullptr;}
}

} // namespace Bazzalt
