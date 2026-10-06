#include "Runtime/LuaRuntime.h"
#include "Runtime/Engine.h"
#include "Runtime/AssetDatabase.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include "Bazzalt/Serialization.h"
#include <cassert>
#include <fstream>
using namespace Bazzalt;
int main(){
    const auto directory=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-lua-"+UUID::Generate().ToString());
    std::filesystem::create_directories(directory/"Assets");
    struct Cleanup{std::filesystem::path Path;~Cleanup(){std::error_code error;std::filesystem::remove_all(Path,error);}} cleanup{directory};
    auto source=directory/"Assets/Walk.lua";auto output=directory/"Walk.blua";std::string error;
    const auto write=[&](const std::string& text){std::ofstream file(source);file<<text;};
    write(R"lua(
local B = Bazzalt
local Behavior = { Speed = 1.0 }
function Behavior:OnCreate()
    assert(os == nil and io == nil and package == nil and debug == nil and loadfile == nil)
    assert(B.Time.GetTimeScale() == 1)
    assert(B.ToDegrees(B.ToRadians(90)) > 89)
    assert(B.Vec3.new(1,2,3):LengthSquared() == 14)
    local object = self.Entity:AddComponent("Light")
    object.Intensity = 200
    self.Entity:SetComponent("Light", object)
    local system = { Owner = self.Entity }
    function system:OnCreate(scene)
        local name = self.Owner:GetComponent("Name")
        name.Value = "SystemCreated"
        self.Owner:SetComponent("Name", name)
    end
    function system:OnDestroy(scene)
        self.Owner:SetComponentEnabled("Light", false)
    end
    self.Entity:GetScene():AddSystem("WalkSystem", system)
    assert(self.Entity:GetScene():HasSystem("WalkSystem"))
end
function Behavior:OnUpdate(dt)
    local transform = self.Entity:GetWorldTransform()
    transform.Position = transform.Position + B.Vec3.new(self.Speed * dt,0,0)
    assert(self.Entity:SetWorldTransform(transform))
    assert(self.Entity:GetScene():Raycast(B.SceneRay.new()).Target:IsValid() == false)
end
function Behavior:OnFixedUpdate(dt)
    local object = self.Entity:GetComponent("Light")
    object.Range = object.Range + dt
    self.Entity:SetComponent("Light", object)
end
function Behavior:OnDestroy()
    local name = self.Entity:GetComponent("Name")
    name.Value = "Destroyed"
    self.Entity:SetComponent("Name", name)
end
return Behavior
)lua");
    Runtime::AssetDatabase database;assert(database.Open(directory,"Assets"));
    auto asset=database.Find(source);assert(asset&&asset->State==AssetState::Ready&&asset->Importer=="LuaBytecode"&&asset->CachePath.extension()==".blua");
    assert(Runtime::LuaRuntime::Import(source,output,error));
    std::ifstream bytecode(output,std::ios::binary);assert(bytecode.get()==0x1b);
    Runtime::Engine engine;auto entity=engine.GetScene().CreateEntity("Cube");
    Runtime::LuaRuntime runtime;Runtime::ScriptBinding binding;binding.Module=output;binding.Entity=entity.GetUUID().ToString();binding.TypeName="Walk";binding.Properties["Speed"]="4";
    assert(runtime.Configure({binding},error));assert(runtime.Start(error));assert(entity.GetComponent<Light>().Intensity==200);
    assert(entity.GetComponent<Name>().Value=="SystemCreated");
    runtime.Update(.5f);assert(std::fabs(entity.GetWorldTransform().Position.X-2)<.001f);
    runtime.FixedUpdate(.1f);assert(entity.GetComponent<Light>().Range>10);
    runtime.Stop();assert(entity.GetComponent<Name>().Value=="Destroyed");assert(!entity.GetComponent<Light>().IsEnabled());
    auto& attachments=entity.AddComponent<ScriptComponents>();ScriptAttachment attachment;
    attachment.Source=asset->Id.ToString();attachment.TypeName="Walk";attachment.Lua=true;attachment.Properties.push_back({"Speed","float","4"});attachments.Values.push_back(attachment);
    auto sceneFile=directory/"Scene.bscene";assert(engine.SaveScene(sceneFile));
    auto restored=engine.LoadSceneAsset(sceneFile);assert(restored);auto restoredEntity=restored->GetEntity(entity.GetUUID());
    assert(restoredEntity.GetComponent<ScriptComponents>().Values.front().Source==asset->Id.ToString());
    assert(restoredEntity.GetComponent<ScriptComponents>().Values.front().Lua);
    auto projectFile=directory/"Game.bproject";assert(engine.SaveProject(projectFile));
    assert(engine.LoadProject(projectFile,false));entity.RemoveComponent<Light>();
    assert(engine.Init(true));const float before=entity.GetWorldTransform().Position.X;
    engine.Update();assert(entity.GetWorldTransform().Position.X>before);engine.Shutdown();

    // A source-free game retains the imported asset UUID and only ships bytecode.
    const auto exported=directory/"Exported";std::filesystem::create_directories(exported/"Assets");
    std::filesystem::copy_file(asset->CachePath,exported/"Assets/Walk.blua");
    std::filesystem::copy_file(asset->MetaPath,exported/"Assets/Walk.blua.meta");
    Runtime::AssetDatabase exportedDatabase;assert(exportedDatabase.Open(exported,"Assets"));
    auto exportedAsset=exportedDatabase.Find(asset->Id);assert(exportedAsset&&exportedAsset->State==AssetState::Ready&&exportedAsset->SourcePath.extension()==".blua");
    binding.Module=exportedAsset->CachePath;entity.RemoveComponent<Light>();
    assert(runtime.Configure({binding},error));assert(runtime.Start(error));runtime.Update(.25f);runtime.Stop();
    binding.Module=output;
    write(R"(local B=Bazzalt
return {OnCreate=function(self)
 local m=B.MaterialBuilder.new():SetColor('baseColor',B.Vec4.new(.2,.4,.6,1)):SetFloat('roughness',.25):Build()
 assert(m:IsRuntime() and m:HasOverride('roughness'))
 local state=B.MaterialRenderState.new();state.DepthWrite=false;m:SetRenderState(state)
 assert(not m:GetRenderState().DepthWrite)
 m:ResetParameter('roughness');assert(m:GetFloat('roughness')==.5)
 m:SetShader(B.Shader.Builtin(B.ShaderPreset.Unlit));assert(not m:HasParameter('roughness'))
 local copy=B.Material.Create();copy:CopyPropertiesFrom(m);assert(math.abs(copy:GetColor('baseColor').Y-.4)<.0001)
 local mesh=B.Mesh.new();mesh:SetMaterial(0,m);assert(mesh:GetMaterialCount()==1)
 local primitive=B.PrimitiveObject.new();primitive:SetMaterial(m);assert(primitive:GetMaterial():IsValid())
 assert(m:Destroy() and not m:IsValid());assert(copy:Destroy())
end})");
    assert(Runtime::LuaRuntime::Import(source,output,error));assert(runtime.Configure({binding},error));assert(runtime.Start(error));runtime.Stop();
    write("return {OnCreate=function(self) while true do end end}");assert(Runtime::LuaRuntime::Import(source,output,error));assert(runtime.Configure({binding},error));assert(!runtime.Start(error));assert(error.find("instruction budget")!=std::string::npos);
    write("this is invalid Lua");assert(!Runtime::LuaRuntime::Import(source,output,error));assert(database.Refresh());
    const auto failedAsset=database.Find(source);assert(failedAsset&&failedAsset->State==AssetState::Failed&&!failedAsset->LastError.empty());
    write("return {} ");assert(runtime.Configure({binding},error));binding.Module=source;assert(runtime.Configure({binding},error));assert(!runtime.Start(error)); // Source must not execute as runtime bytecode.
}
