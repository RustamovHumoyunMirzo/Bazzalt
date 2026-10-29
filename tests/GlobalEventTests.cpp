#include "Bazzalt/GlobalEvent.h"
#include "Bazzalt/AssetManager.h"
#include "Runtime/Engine.h"
#include "Runtime/LuaRuntime.h"
#include "Runtime/GlobalEventsAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/ApplicationAccess.h"
#include "Runtime/TimeAccess.h"
#include "Runtime/GraphicsAccess.h"
#include <cassert>
#include <fstream>
#include <thread>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
template<class F> bool Throws(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){
    assert(argc==2);
    auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-events-"+UUID::Generate().ToString());std::filesystem::create_directories(folder/"Assets");
    struct Cleanup{std::filesystem::path Folder;~Cleanup(){std::error_code ec;std::filesystem::remove_all(Folder,ec);}} cleanup{folder};
    auto write=[](const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;};
    write(folder/"Test.bproject","FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000001\"\nName: Events\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n");
    write(folder/"Assets/Main.bscene","FormatVersion: 1\nSceneUUID: \"00000000-0000-0002-0000-000000000001\"\nEntities: []\n");
    const auto source=folder/"Assets/Damage.event";
    write(source,"Version: 1\nName: Damage\nArguments:\n  - Name: Amount\n    Type: Number\n  - Name: Label\n    Type: String\n");
    const auto typedSource=folder/"Assets/Types.event";write(typedSource,"Version: 1\nName: Types\nArguments:\n - {Name: Flag, Type: Boolean}\n - {Name: Count, Type: Integer}\n - {Name: Amount, Type: Number}\n - {Name: Text, Type: String}\n - {Name: Point2, Type: Vec2}\n - {Name: Point3, Type: Vec3}\n - {Name: Point4, Type: Vec4}\n - {Name: Rotation, Type: Quaternion}\n - {Name: Target, Type: UUID}\n");
    Engine engine;assert(engine.LoadProject(folder/"Test.bproject",false));auto asset=AssetManager::GetAsset(source);assert(asset&&asset->Importer=="Bazzalt.GlobalEvent"&&asset->State==AssetState::Ready);
    auto event=GlobalEvent::Load(asset->Id);assert(event.IsValid()&&event.GetDefinition().Arguments.size()==2);
    auto typed=GlobalEvent::Load(AssetManager::GetAsset(typedSource)->Id);bool allTypes=false;auto typeToken=typed.Subscribe([&](const EventInvocation& e){allTypes=e.Get<bool>("Flag")&&e.Get<std::int64_t>("Count")==9&&e.Get<Vec3>("Point3").Y==2&&e.Get<UUID>("Target")==asset->Id;});
    typed.Fire({true,std::int64_t(9),3.5,std::string("text"),Vec2{1,2},Vec3{1,2,3},Vec4{1,2,3,4},Quaternion{},asset->Id});assert(allTypes);typed.Unsubscribe(typeToken);
    auto fire=[&](double n=1){event.Fire({n,std::string("hit")});};assert(Throws([&]{event.Fire();}));assert(Throws([&]{event.Fire({1.0,std::string(65537,'x')});}));assert(Throws([&]{event.Fire({true,std::string("hit")});}));assert(Throws([&]{event.Fire({std::numeric_limits<double>::infinity(),std::string("hit")});}));
    std::vector<int> order;GlobalEvent::Subscription first=0,second=0,late=0;
    first=event.Subscribe([&](const EventInvocation& e){order.push_back(1);assert(e.Get<double>("Amount")==1);assert(e.Get<std::string>("Label")=="hit");event.Unsubscribe(second);event.Unsubscribe(first);late=event.Subscribe([&](const auto&){order.push_back(3);});fire();});
    second=event.Subscribe([&](const auto&){order.push_back(2);});fire();assert((order==std::vector<int>{1,3}));assert(event.Unsubscribe(late));
    int calls=0;auto loop=event.Subscribe([&](const auto&){++calls;fire();});fire();assert(calls==33);assert(!event.Unsubscribe(loop)); // Faulting recursive listener is disabled; root drain completes safely.
    auto owner=engine.GetScene().CreateEntity();auto owned=event.Subscribe([&](const auto&){++calls;},owner);engine.GetScene().DestroyEntity(owner);fire();assert(calls==33&&!event.Unsubscribe(owned));
    int scope=0;auto previous=ApplicationEventsAccess::SetOwner(&scope);(void)event.Subscribe([&](const auto&){++scope;});ApplicationEventsAccess::SetOwner(previous);GlobalEventsAccess::DisconnectOwner(&scope);fire();assert(scope==0);
    bool wrongThread=false;std::thread thread([&]{wrongThread=Throws(fire);});thread.join();assert(wrongThread);
    ApplicationAccess::SetServicesReady(true);auto level=ApplicationEventsAccess::SetSubsystemLevel(4);assert(Throws([&]{(void)GlobalEvent::Load(asset->Id);}));ApplicationEventsAccess::SetAssetAccess(true);assert(GlobalEvent::Load(asset->Id).IsValid());assert(Throws([&]{(void)AssetManager::GetAsset(source);}));
    bool permission=false;auto listener=event.Subscribe([&](const auto&){permission=AssetManager::IsAssetReady(asset->Id);});ApplicationEventsAccess::SetAssetAccess(false);fire();assert(permission);event.Unsubscribe(listener);ApplicationEventsAccess::SetSubsystemLevel(level);ApplicationAccess::SetServicesReady(false);
    GraphicsAccess::BindScene(&engine.GetScene());int graphicsOwner=0;ApplicationAccess::SetServicesReady(true);level=ApplicationEventsAccess::SetSubsystemLevel(4);auto access=ApplicationEventsAccess::SetAssetAccess(true);previous=ApplicationEventsAccess::SetOwner(&graphicsOwner);
    Graphics::OnBeginFrame([&](const auto&){assert(AssetManager::IsAssetReady(asset->Id));++graphicsOwner;});ApplicationEventsAccess::SetOwner(previous);ApplicationEventsAccess::SetSubsystemLevel(level);ApplicationEventsAccess::SetAssetAccess(access);
    GraphicsAccess::BeginFrame();GraphicsAccess::EndFrame();assert(graphicsOwner==1);ApplicationEventsAccess::DisconnectOwner(&graphicsOwner);GraphicsAccess::BeginFrame();GraphicsAccess::EndFrame();assert(graphicsOwner==1);GraphicsAccess::BindScene(nullptr);ApplicationAccess::SetServicesReady(false);
    // Same-frame budget is shared by separate root fires and recovers next frame.
    TimeAccess::Advance(.016);for(unsigned i=0;i<4096;++i)fire();assert(Throws(fire));TimeAccess::Advance(.016);fire();
    auto script=folder/"Assets/EventConsumer.lua";
    write(script,R"lua(local B=Bazzalt
local C=COMPONENT("EventConsumer")
C.Signal=PROPERTY("GlobalEvent",B.GlobalEvent.new())
C.Typed=PROPERTY("GlobalEvent",B.GlobalEvent.new())
function C:OnCreate()
 self.Signal:Subscribe(function(e)
  assert(e.Name=='Damage' and e:GetValue('Label')=='hit');self.LastEvent=e;self.LastValues=e.Values;self.LastArguments=e.Arguments
  local t=self.Entity:GetWorldTransform();t.Position.X=e:GetValue('Amount');self.Entity:SetWorldTransform(t)
 end)
 self.Signal:Fire({Amount=7,Label='hit'})
 local fields={Flag=true,Count=2.5,Amount=1,Text='text',Point2=B.Vec2.new(1,2),Point3=B.Vec3.new(1,2,3),Point4=B.Vec4.new(1,2,3,4),Rotation=B.Quaternion.new(0,0,0,1),Target=B.UUID.Root()}
 assert(not pcall(function()self.Typed:Fire(fields)end));fields.Count=2;self.Typed:Fire(fields)
 assert(not pcall(function()self.Signal:Fire({Amount='bad',Label='hit'})end))
end
function C:OnUpdate(dt)
 assert(self.LastEvent:GetValue("Amount")==self.Entity:GetWorldTransform().Position.X)
 assert(self.LastValues[1]==self.LastEvent:GetValue("Amount") and self.LastArguments[1].Name=="Amount")
end
return C)lua");
    assert(engine.RefreshAssets());auto luaAsset=AssetManager::GetAsset(script);assert(luaAsset&&luaAsset->State==AssetState::Ready);auto entity=engine.GetScene().CreateEntity();LuaRuntime lua;std::string error;
    ScriptBinding binding;binding.Module=luaAsset->CachePath;binding.TypeName="EventConsumer";binding.Entity=entity.GetUUID().ToString();binding.Properties["Signal"]=asset->Id.ToString();binding.Properties["Typed"]=AssetManager::GetAsset(typedSource)->Id.ToString();assert(lua.Configure({binding},error));assert(lua.Start(error));assert(entity.GetWorldTransform().Position.X==7);fire(9);assert(entity.GetWorldTransform().Position.X==9);lua.Update(.016f);lua.Stop();fire(11);assert(entity.GetWorldTransform().Position.X==9);
    bool nativeFired=false;auto nativeToken=event.Subscribe([&](const EventInvocation& e){nativeFired=e.Get<double>("Amount")==5;});NativeScriptRuntime native;ScriptBinding nativeBinding;nativeBinding.Module=std::filesystem::u8path(argv[1]);nativeBinding.TypeName="LifecycleProbe";nativeBinding.Entity=entity.GetUUID().ToString();nativeBinding.Properties["Signal"]=asset->Id.ToString();assert(native.Configure({nativeBinding},error));assert(native.Start(error));assert(nativeFired);native.Stop();event.Unsubscribe(nativeToken);
    auto cooked=folder/"Cooked";const auto exported=engine.ExportCookedProject(cooked,{});if(!exported)std::fprintf(stderr,"Cook failed: %s\n",engine.GetLastError().c_str());assert(exported);{Engine standalone;assert(standalone.LoadProject(cooked/"Game.bproject",false));auto loaded=GlobalEvent::Load(asset->Id);assert(loaded.GetDefinition().Name=="Damage");loaded.Fire({2.0,std::string("standalone")});}
    EventDefinition definition;write(source,"Version: 1\nName: Bad\nArguments:\n - Name: Repeated\n   Type: Number\n - Name: Repeated\n   Type: Number\n");assert(!GlobalEventsAccess::Read(source,definition,error));
}
