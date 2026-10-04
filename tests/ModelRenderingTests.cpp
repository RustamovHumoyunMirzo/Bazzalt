#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include "Bazzalt/AssetManager.h"
#include "Runtime/Engine.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Rendering/RenderSystems.h"

struct ModelSystemTestAccess : Bazzalt::System {
    static void Tick(Bazzalt::System& system,Bazzalt::Scene& scene) {
        constexpr auto update=&ModelSystemTestAccess::OnUpdate;
        (system.*update)(scene,0);
    }
};

int main() {
    using namespace Bazzalt;
    auto directory=std::filesystem::temp_directory_path()/std::filesystem::path("bazzalt-model-"+UUID::Generate().ToString());
#ifdef _WIN32
    directory/=std::filesystem::path(L"Проект машины");
#endif
    const auto assets=directory/"Assets";
    std::filesystem::create_directories(assets);
    {
        std::ofstream project(directory/"test.bproject");
        project<<"FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000001\"\nName: Models\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties:\n";
        std::ofstream model(assets/"two.gltf");
        model<<R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin","byteLength":72}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1}}]}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"scene":0})";
        const std::array<float,18> data{-1,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,1};
        std::ofstream buffer(assets/"geometry.bin",std::ios::binary);
        buffer.write(reinterpret_cast<const char*>(data.data()),sizeof(data));
    }
    {
        Runtime::Engine engine;
        assert(engine.LoadProject(directory/"test.bproject",false));
        const auto asset=AssetManager::GetAsset(assets/"two.gltf");assert(asset);
        Runtime::RenderBackend backend;assert(backend.Initialize(true));
        auto& renderer=backend.GetAssets();
        auto& manager=backend.GetEngine().getRenderableManager();
        const auto baseline=manager.getComponentCount();
        Mesh mesh;mesh.MeshAsset=asset->Id;mesh.ModelNodeIndex=0;
        const auto owner=UUID::Generate();
        const auto first=renderer.CreateMesh(mesh,owner);assert(first!=Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline+2);
        mesh.ModelNodeIndex=1;
        const auto second=renderer.CreateMesh(mesh,owner);assert(second!=Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline+2); // No second complete model.
        renderer.DestroyMesh(first);assert(manager.getComponentCount()==baseline+2);
        renderer.UpdateMesh(second,Mat4::Identity(),mesh);
        renderer.DestroyMesh(second);assert(manager.getComponentCount()==baseline);

        mesh.ModelNodeIndex=0;
        const auto a=renderer.CreateMesh(mesh,owner);
        const auto duplicate=renderer.CreateMesh(mesh,owner);
        const auto independent=renderer.CreateMesh(mesh,UUID::Generate());
        assert(a!=Runtime::RenderAssets::InvalidHandle&&duplicate!=Runtime::RenderAssets::InvalidHandle&&independent!=Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline+6);
        renderer.DestroyMesh(a);renderer.DestroyMesh(duplicate);renderer.DestroyMesh(independent);
        assert(manager.getComponentCount()==baseline);

        mesh.ModelNodeIndex=100000;
        for (int attempt=0;attempt<3;++attempt) {
            assert(renderer.CreateMesh(mesh,owner)==Runtime::RenderAssets::InvalidHandle);
            assert(manager.getComponentCount()==baseline);
        }
        mesh.ModelNodeIndex=0;
        const auto cachedBuffer=asset->CachePath.parent_path()/"geometry.bin";
        std::filesystem::remove(cachedBuffer);
        assert(renderer.CreateMesh(mesh,owner)==Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline);
        std::filesystem::copy_file(assets/"geometry.bin",cachedBuffer);

        // The actual ECS path groups children by their model-instance root.
        auto& scene=engine.GetScene();scene.InstantiateModel(asset->Id);
        auto& system=scene.AddSystem<Runtime::MeshSystem>(backend);ModelSystemTestAccess::Tick(system,scene);
        assert(manager.getComponentCount()==baseline+2);
        ModelSystemTestAccess::Tick(system,scene);assert(manager.getComponentCount()==baseline+2);
        scene.RemoveSystem<Runtime::MeshSystem>();assert(manager.getComponentCount()==baseline);
        backend.Shutdown(); // Must not terminate with live material instances.
    }
    std::filesystem::remove_all(directory);
}
