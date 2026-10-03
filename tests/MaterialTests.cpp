#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <array>
#include <utils/Entity.h>
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/RenderableManager.h>
#include <filament/MaterialInstance.h>
#include "Rendering/RenderAssets.h"
#include "Bazzalt/Material.h"
#include "Bazzalt/Components/Mesh.h"
#include "Runtime/Engine.h"
#include "Runtime/MaterialLibrary.h"
#include "Runtime/NativeScriptRuntime.h"

static void Write(const std::filesystem::path& path,const std::string& value){std::ofstream stream(path);stream<<value;assert(stream.good());}
int main(int argc,char** argv){
    using namespace Bazzalt;
    const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path("bazzalt-material-test-"+UUID::Generate().ToString());
    std::filesystem::create_directories(directory/"Assets");
    Write(directory/"Material.bproject","FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000001\"\nName: \"Materials\"\nAssetDirectory: \"Assets\"\nStartupScene: \"\"\nProperties:\n");
    Write(directory/"Assets"/"surface.mat","material {name:Surface}");
    Runtime::Engine engine;assert(engine.LoadProject(directory/"Material.bproject",false));
    const auto shaderAsset=AssetManager::GetAsset(directory/"Assets"/"surface.mat");assert(shaderAsset);
    auto reflection=shaderAsset->CachePath;reflection+=".reflection.json";
    Write(reflection,R"({"version":1,"parameters":[{"name":"roughness","kind":0,"default":0.5},{"name":"tint","kind":3,"default":[1,1,1,1]},{"name":"enabled","kind":5,"default":true},{"name":"albedo","kind":6,"default":"00000000-0000-0000-0000-000000000000"}]})");
    Write(directory/"Assets"/"surface.matinst","{\"version\":1,\"shader\":\""+shaderAsset->Id.ToString()+"\",\"properties\":{\"roughness\":0.25}}");
    assert(engine.RefreshAssets());const auto asset=AssetManager::GetAsset(directory/"Assets"/"surface.matinst");assert(asset);
    auto material=Material::Load(asset->Id);assert(material.IsValid());assert(material.GetShader().IsValid());assert(material.GetParameters().size()==4);
    assert(material.GetFloat("roughness")==0.25f);material.SetFloat("roughness",0.8f);assert(Material::Load(asset->Id).GetFloat("roughness")==0.8f);
    auto copy=material.Instantiate();assert(copy.IsValid());assert(copy.GetAssetUUID()!=asset->Id);copy.SetFloat("roughness",0.1f);assert(material.GetFloat("roughness")==0.8f);
    copy.SetColor("tint",{0.2f,0.4f,0.8f,1});assert(copy.GetColor("tint").Y==0.4f);
    copy.SetBoolean("enabled",false);assert(!copy.GetBoolean("enabled"));
    bool rejected=false;try{copy.SetFloat("enabled",0.0f);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    rejected=false;try{copy.SetFloat("roughness",std::numeric_limits<float>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    auto created=Material::Create(material.GetShader());assert(created.IsValid());assert(created.GetFloat("roughness")==0.5f);
    auto parent=engine.GetScene().CreateEntity("Parent");parent.AddComponent<Mesh>().MaterialAsset=material.GetAssetUUID();
    auto child=engine.GetScene().CreateEntity("Child");child.SetParent(parent);assert(!child.AddComponent<Mesh>().MaterialAsset);
    Runtime::ResetRuntimeMaterials();assert(!copy.IsValid());assert(material.GetFloat("roughness")==0.25f);
    if(argc>1){Runtime::NativeScriptRuntime scripts;Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(argv[1]);binding.TypeName="LifecycleProbe";binding.Entity=parent.GetUUID().ToString();binding.Properties["Surface"]=asset->Id.ToString();binding.Properties["LogPath"]=(directory/"lifecycle.txt").string();std::string error;assert(scripts.Configure({binding},error));assert(scripts.Start(error));assert(material.GetFloat("roughness")==0.1f);scripts.Stop();}
    // Real gltfio / Filament material assignment, without a platform window.
    Write(directory/"Assets"/"triangle.gltf",R"({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":96}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
    {const std::array<float,24> vertices{-1,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,1,0,0,1,0,0.5f,1};std::ofstream stream(directory/"Assets"/"triangle.bin",std::ios::binary);stream.write(reinterpret_cast<const char*>(vertices.data()),sizeof(vertices));}
    assert(engine.RefreshAssets());
    Write(reflection,R"({"version":1,"parameters":[{"name":"baseColor","kind":3,"default":[1,1,1,1]}]})");
    Runtime::ResetMaterialLibrary();material=Material::Load(asset->Id);material.SetColor("baseColor",{0.2f,0.4f,0.8f,1});
    auto package=shaderAsset->CachePath;package+=".filamat";std::filesystem::copy_file(BAZZALT_MATERIAL_TEST_PACKAGE,package,std::filesystem::copy_options::overwrite_existing);
    auto* graphics=filament::Engine::create(filament::Engine::Backend::NOOP);assert(graphics);auto* renderScene=graphics->createScene();
    {
        Runtime::RenderAssets renderer(*graphics,*renderScene);Mesh mesh;mesh.MeshAsset=AssetManager::GetAsset(directory/"Assets"/"triangle.gltf")->Id;mesh.MaterialAsset=asset->Id;
        auto handle=renderer.CreateMesh(mesh);assert(handle!=Runtime::RenderAssets::InvalidHandle);renderer.UpdateMesh(handle,Mat4::Identity(),mesh);
        int checked=0;renderScene->forEach([&](utils::Entity entity){auto ri=graphics->getRenderableManager().getInstance(entity);if(!ri)return;auto* mi=graphics->getRenderableManager().getMaterialInstanceAt(ri,0);assert(mi->getParameter<filament::math::float4>("baseColor").y==0.4f);++checked;});assert(checked==1);
        material.SetColor("baseColor",{0.7f,0.9f,0.1f,1});renderer.UpdateMesh(handle,Mat4::Identity(),mesh);
        renderScene->forEach([&](utils::Entity entity){auto ri=graphics->getRenderableManager().getInstance(entity);if(ri)assert(graphics->getRenderableManager().getMaterialInstanceAt(ri,0)->getParameter<filament::math::float4>("baseColor").y==0.9f);});
        assert(renderer.SetDebugMode("unlit"));renderer.UpdateMesh(handle,Mat4::Identity(),mesh);assert(renderer.SetDebugMode("lit"));renderer.DestroyMesh(handle);
    }
    graphics->destroy(renderScene);filament::Engine::destroy(&graphics);
    engine.Shutdown();std::filesystem::remove_all(directory);
}
