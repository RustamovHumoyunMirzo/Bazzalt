#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include "Bazzalt/AssetManager.h"
#include "Runtime/Engine.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Rendering/RenderSystems.h"
#include "Rendering/ModelGeometry.h"

struct ModelSystemTestAccess : Bazzalt::System {
    static void Tick(Bazzalt::System& system,Bazzalt::Scene& scene) {
        constexpr auto update=&ModelSystemTestAccess::OnUpdate;
        (system.*update)(scene,0);
    }
};

#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
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
        model<<R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin","byteLength":72}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1}}]}],"nodes":[{"children":[2,1]},{"mesh":0,"name":"Duplicate name"},{"mesh":0,"name":"Duplicate name"}],"scenes":[{"nodes":[0]}],"scene":0})";
        const std::array<float,18> data{-1,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,1};
        std::ofstream buffer(assets/"geometry.bin",std::ios::binary);
        buffer.write(reinterpret_cast<const char*>(data.data()),sizeof(data));
    }
    if(argc>1)std::filesystem::copy_file(std::filesystem::path(argv[1]),assets/"production.glb");
    {
        Runtime::Engine engine;
        assert(engine.GetEditorMeshBounds().empty());
        assert(engine.PickEditorPrimitive({0,0,2},{0,0,-1}).IsRoot());
        assert(engine.LoadProject(directory/"test.bproject",false));
        assert(engine.GetEditorMeshBounds().empty());
        const auto asset=AssetManager::GetAsset(assets/"two.gltf");assert(asset);
        Runtime::RenderBackend backend;assert(backend.Initialize(true));
        auto& renderer=backend.GetAssets();
        auto& manager=backend.GetEngine().getRenderableManager();
        const auto baseline=manager.getComponentCount();
        Mesh mesh;mesh.MeshAsset=asset->Id;mesh.ModelNodeIndex=1;
        const auto owner=UUID::Generate();
        const auto first=renderer.CreateMesh(mesh,owner);assert(first!=Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline+2);
        mesh.ModelNodeIndex=2;
        const auto second=renderer.CreateMesh(mesh,owner);assert(second!=Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline+2); // No second complete model.
        const auto geometry=renderer.GetEditorMeshes();assert(geometry.size()==2);
        assert(geometry[0].Geometry==geometry[1].Geometry); // CPU geometry is shared too.
        float hitDistance=100;assert(geometry[0].Geometry->Raycast({0,.25f,2},{0,0,-1},hitDistance));assert(std::abs(hitDistance-2)<.001f);
        hitDistance=100;assert(!geometry[0].Geometry->Raycast({10,10,2},{0,0,-1},hitDistance));
        assert(!geometry[0].Geometry->Nodes.empty());
        // Both children enter one filled selection scene; original materials
        // are restored before normal scene rendering, including repeated use.
        renderer.SetEditorOwner(first,owner);renderer.SetEditorOwner(second,owner);
        auto* maskScene=backend.GetEngine().createScene();
        std::vector<std::pair<utils::Entity,const filament::MaterialInstance*>> originals;
        backend.GetScene().forEach([&](utils::Entity entity){auto ri=manager.getInstance(entity);if(ri&&manager.getPrimitiveCount(ri))originals.emplace_back(entity,manager.getMaterialInstanceAt(ri,0));});
        assert(!originals.empty());
        auto* mask=originals.front().second->getMaterial()->createInstance();
        for(int pass=0;pass<2;++pass){
            assert(renderer.BeginSelectionMask(*maskScene,{owner},{},mask,mask));
            std::size_t count=0;maskScene->forEach([&](utils::Entity entity){++count;assert(manager.getMaterialInstanceAt(manager.getInstance(entity),0)==mask);});assert(count==2);
            renderer.EndSelectionMask(*maskScene);
            for(const auto& [entity,material]:originals)assert(manager.getMaterialInstanceAt(manager.getInstance(entity),0)==material);
            count=0;maskScene->forEach([&](utils::Entity){++count;});assert(count==0);
        }
        assert(!renderer.BeginSelectionMask(*maskScene,{}, {},mask,mask));
        backend.GetEngine().destroy(maskScene);backend.GetEngine().destroy(mask);
        renderer.DestroyMesh(first);assert(manager.getComponentCount()==baseline+2);
        renderer.UpdateMesh(second,Mat4::Identity(),mesh);
        renderer.DestroyMesh(second);assert(manager.getComponentCount()==baseline);

        mesh.ModelNodeIndex=1;
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
        mesh.ModelNodeIndex=1;
        const auto cachedBuffer=asset->CachePath.parent_path()/"geometry.bin";
        std::filesystem::remove(cachedBuffer);
        assert(renderer.CreateMesh(mesh,owner)==Runtime::RenderAssets::InvalidHandle);
        assert(manager.getComponentCount()==baseline);
        std::filesystem::copy_file(assets/"geometry.bin",cachedBuffer);

        // The actual ECS path groups children by their model-instance root.
        auto& scene=engine.GetScene();auto fixtureRoot=scene.InstantiateModel(asset->Id);
        auto& system=scene.AddSystem<Runtime::MeshSystem>(backend);ModelSystemTestAccess::Tick(system,scene);
        assert(manager.getComponentCount()==baseline+2);
        ModelSystemTestAccess::Tick(system,scene);assert(manager.getComponentCount()==baseline+2);
        scene.RemoveSystem<Runtime::MeshSystem>();assert(manager.getComponentCount()==baseline);
        UUID productionRoot{};
        if(argc>1){
            const auto production=AssetManager::GetAsset(assets/"production.glb");assert(production);
            auto root=scene.InstantiateModel(production->Id);assert(root);
            auto& productionSystem=scene.AddSystem<Runtime::MeshSystem>(backend);ModelSystemTestAccess::Tick(productionSystem,scene);
            const auto productionMeshes=renderer.GetEditorMeshes();
            std::size_t meshCount=0;auto meshView=scene.GetRegistry().view<Mesh>();for(auto handle:meshView)if(meshView.get<Mesh>(handle).MeshAsset==production->Id)++meshCount;
            assert(productionMeshes.size()>=meshCount&&meshCount>0);
            for(const auto& item:productionMeshes){assert(!item.Geometry->Triangles.empty());assert(!item.Owner.IsRoot());}
            scene.RemoveSystem<Runtime::MeshSystem>();assert(manager.getComponentCount()==baseline);
            productionRoot=root.GetUUID();root.GetComponent<Transform>().Position={10000,0,0};
        }
        backend.Shutdown(); // Must not terminate with live material instances.
        if(argc>2)assert(engine.ConfigureRenderingBackend("vulkan"));
        assert(engine.Init(argc<=2));engine.RenderEditorFrame();
        if(productionRoot){engine.SetEditorSelection({productionRoot});engine.SetEditorObjectHover(productionRoot,{10006,4,8});engine.RenderEditorFrame();engine.RenderEditorFrame();engine.SetEditorSelection({});}
        auto picked=engine.PickEditorPrimitive({0,.25f,2},{0,0,-1});assert(picked);
        assert(scene.GetEntity(picked).HasComponent<Mesh>());
        assert(scene.GetEntity(picked).GetComponent<Mesh>().Materials.size()==1);
        assert(engine.GetEditorMeshBounds().contains(fixtureRoot.GetUUID()));
        fixtureRoot.GetComponent<Transform>().Position={5,0,0};engine.RenderEditorFrame();
        assert(engine.PickEditorPrimitive({0,.25f,2},{0,0,-1}).IsRoot());
        assert(engine.PickEditorPrimitive({5,.25f,2},{0,0,-1}));
        engine.SetEditorSelection({fixtureRoot.GetUUID()});engine.RenderEditorFrame(); // Root outlines all mesh descendants.
        std::vector<UUID> meshIds;for(auto handle:scene.GetRegistry().view<Mesh>())meshIds.push_back(scene.GetEntity(static_cast<Entity::Id>(handle)).GetUUID());
        engine.SetEditorEntityState(meshIds,{});assert(engine.PickEditorPrimitive({5,.25f,2},{0,0,-1}).IsRoot());
        engine.SetEditorEntityState({},meshIds);assert(engine.PickEditorPrimitive({5,.25f,2},{0,0,-1}).IsRoot());
        engine.SetEditorEntityState({},{});
        for(auto handle:scene.GetRegistry().view<Mesh>())scene.GetRegistry().get<Mesh>(handle).Visible=false;
        engine.RenderEditorFrame();assert(engine.PickEditorPrimitive({5,.25f,2},{0,0,-1}).IsRoot());
        engine.Shutdown();
        assert(engine.GetEditorMeshBounds().empty());
    }
    std::filesystem::remove_all(directory);
}
