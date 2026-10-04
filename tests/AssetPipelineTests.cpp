#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Mesh.h"
#include "Runtime/Engine.h"

namespace {

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(stream.good());
}

void WritePng(const std::filesystem::path& path) {
    // One opaque white pixel.
    constexpr auto bytes = std::to_array<unsigned char>({
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
        0x89,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x08,0xd7,0x63,0xf8,0xff,0xff,0x3f,
        0x00,0x05,0xfe,0x02,0xfe,0xdc,0xcc,0x59,0xe7,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
        0x44,0xae,0x42,0x60,0x82
    });
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    assert(stream.good());
}

} // namespace

int main() {
    using namespace Bazzalt;
    const auto directory = std::filesystem::temp_directory_path() / "bazzalt_asset_pipeline_tests";
    const auto assets = directory / "Assets";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(assets);

    WriteText(directory / "Pipeline.bproject",
        "FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000001\"\n"
        "Name: \"Pipeline\"\nAssetDirectory: \"Assets\"\nStartupScene: \"\"\nProperties:\n");
    WriteText(assets / "empty.gltf",
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"payload.bin","byteLength":1}],"scenes":[{}],"scene":0})");
    WriteText(assets / "human.gltf",
        R"({"asset":{"version":"2.0"},"nodes":[{"name":"Arm","children":[1]},)"
        R"({"name":"Hand","mesh":0,"translation":[0,2,0]},{"name":"Head"}],)"
        R"("scenes":[{"nodes":[0,2]}],"scene":0})");
    WriteText(assets / "payload.bin", "x");
    WritePng(assets / "white.png");
    WriteText(assets / "unlit.mat", R"(material {
    name : BazzaltUnlit,
    shadingModel : unlit
}
fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = vec4(1.0);
    }
})");
    // Precompiled files are accepted directly; OBJ/FBX take the same importer path
    // but are compiled by the SDK's filamesh tool.
    WriteText(assets / "fixture.filamesh", "filamesh-test-fixture");

    Runtime::Engine engine;
    if (!engine.LoadProject(directory / "Pipeline.bproject", false)) {
        std::cerr << engine.GetLastError() << '\n';
        return 1;
    }
    const auto gltf = AssetManager::GetAsset(assets / "empty.gltf");
    const auto texture = AssetManager::GetAsset(assets / "white.png");
    const auto material = AssetManager::GetAsset(assets / "unlit.mat");
    const auto mesh = AssetManager::GetAsset(assets / "fixture.filamesh");
    const auto human = AssetManager::GetAsset(assets / "human.gltf");
    assert(gltf && gltf->Importer == "Bazzalt.glTF" && gltf->CachePath.extension() == ".gltf");
    // Scans and public lookups must agree even when Windows supplies a short
    // TEMP path (e.g. RUNNER~1) or callers use another spelling of the path.
    const auto canonicalGltf = AssetManager::GetAsset(std::filesystem::weakly_canonical(assets / "empty.gltf"));
    assert(canonicalGltf && canonicalGltf->Id == gltf->Id);
    const auto dottedGltf = AssetManager::GetAsset(assets / ".." / "Assets" / "empty.gltf");
    assert(dottedGltf && dottedGltf->Id == gltf->Id);
#ifdef _WIN32
    const auto sourcePath = (assets / "empty.gltf").wstring();
    const auto required = GetShortPathNameW(sourcePath.c_str(), nullptr, 0);
    if (required) {
        std::wstring shortPath(required, L'\0');
        const auto written = GetShortPathNameW(sourcePath.c_str(), shortPath.data(), required);
        assert(written && written < required);
        shortPath.resize(written);
        const auto shortGltf = AssetManager::GetAsset(std::filesystem::path(shortPath));
        assert(shortGltf && shortGltf->Id == gltf->Id);
    }
#endif
    assert(std::filesystem::exists(gltf->CachePath.parent_path() / "payload.bin"));
    assert(texture && texture->Importer == "Bazzalt.Texture" && texture->CachePath.extension() == ".png");
    assert(material && material->Importer == "Bazzalt.FilamentMaterial" &&
           material->CachePath.extension() == ".mat");
    assert(!std::filesystem::exists(std::filesystem::path(material->CachePath.string()+".filamat")));
    assert(mesh && mesh->Importer == "Bazzalt.Filamesh" && mesh->CachePath.extension() == ".filamesh");
    assert(human && human->Importer == "Bazzalt.glTF");
    const auto modelAsset = AssetManager::LoadModel(human->Id);
    assert(modelAsset && modelAsset->Nodes.size() == 3 && modelAsset->Roots.size() == 2);
    assert(modelAsset->Nodes[0].Children == std::vector<std::uint32_t>{1});
    assert(modelAsset->Nodes[1].Name == "Hand" && modelAsset->Nodes[1].HasMesh());
    assert(modelAsset->Nodes[1].StablePath == "Arm/Hand");
    const auto instance = engine.GetScene().InstantiateModel(human->Id);
    assert(instance.GetChildren().size() == 2);
    assert(std::filesystem::file_size(material->CachePath) > 0);
    assert(std::filesystem::file_size(mesh->CachePath) > 0);

    assert(engine.Init(true));
    auto model = engine.GetScene().CreateEntity("glTF model");
    model.AddComponent<Mesh>().MeshAsset = gltf->Id;
    engine.Update();
    engine.Shutdown();

    std::filesystem::remove_all(directory);
    return 0;
}
