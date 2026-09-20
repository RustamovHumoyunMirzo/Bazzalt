#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

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
    assert(gltf && gltf->Importer == "Bazzalt.glTF" && gltf->CachePath.extension() == ".gltf");
    assert(std::filesystem::exists(gltf->CachePath.parent_path() / "payload.bin"));
    assert(texture && texture->Importer == "Bazzalt.Texture" && texture->CachePath.extension() == ".png");
    assert(material && material->Importer == "Bazzalt.FilamentMaterial" &&
           material->CachePath.extension() == ".filamat");
    assert(mesh && mesh->Importer == "Bazzalt.Filamesh" && mesh->CachePath.extension() == ".filamesh");
    assert(std::filesystem::file_size(material->CachePath) > 0);
    assert(std::filesystem::file_size(mesh->CachePath) > 0);

    assert(engine.Init());
    auto model = engine.GetScene().CreateEntity("glTF model");
    model.AddComponent<Mesh>().MeshAsset = gltf->Id;
    engine.Update();
    engine.Shutdown();

    std::filesystem::remove_all(directory);
    return 0;
}
