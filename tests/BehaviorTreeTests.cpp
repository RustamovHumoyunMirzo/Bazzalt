#include "Bazzalt/AI.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Physics.h"
#include "Physics/PhysicsModuleApi.h"
#include "Runtime/Engine.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Components/BehaviorTreeAgent.h"
#include "Bazzalt/Systems/BehaviorTreeSystem.h"
#include "AI/BehaviorModuleApi.h"
#include "AI/BehaviorLifecycle.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Navigation/NavigationModuleApi.h"
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <cassert>
using namespace Bazzalt;
using namespace Bazzalt::AI;
namespace {
void* ModuleOf(void* address){
#ifdef _WIN32
    HMODULE module=nullptr;
    assert(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module));
    return module;
#else
    Dl_info info{};assert(dladdr(address,&info));return info.dli_fbase;
#endif
}
void CheckModuleBoundary(){
    const auto& api=Runtime::BehaviorModule();const auto& nav=Runtime::NavigationModule();
    auto module=ModuleOf(reinterpret_cast<void*>(nav.Step));
    assert(ModuleOf(reinterpret_cast<void*>(api.CreateTree))==module);
    assert(ModuleOf(reinterpret_cast<void*>(api.LoadTree))==module);
    assert(ModuleOf(reinterpret_cast<void*>(api.TickTree))==module);
    assert(ModuleOf(reinterpret_cast<void*>(api.InitializeTree))==module);
    assert(ModuleOf(reinterpret_cast<void*>(api.CreateAgents))==module);
    assert(ModuleOf(reinterpret_cast<void*>(api.StepAgents))==module);
    assert(ModuleOf(reinterpret_cast<void*>(&AI::BehaviorTree::FromSource))!=module);
#ifdef _WIN32
    wchar_t path[32768]{};assert(GetModuleFileNameW(static_cast<HMODULE>(module),path,32768));
    assert(std::filesystem::path(path).filename()==L"bazzalt_ai.dll");
    auto get=reinterpret_cast<const Runtime::BehaviorModuleApi*(*)(unsigned,unsigned)>(GetProcAddress(static_cast<HMODULE>(module),"BazzaltGetBehaviorModuleApi"));
    assert(get&&get(1,sizeof(Runtime::BehaviorModuleApi))==&api);
    assert(!get(2,sizeof(Runtime::BehaviorModuleApi))&&!get(1,0));
#endif
}
struct Access:System {static void Tick(System& system,Scene& scene,float dt){constexpr auto method=&Access::OnFixedUpdate;(system.*method)(scene,dt);}};
BehaviorTree Tree(const std::string& nodes){return BehaviorTree::FromSource("<BehaviorTree version=\"1\"><Tree>"+nodes+"</Tree></BehaviorTree>");}
void Invalid(const std::string& source){bool rejected=false;try{BehaviorTree::FromSource(source);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
}
int main(int argc,char** argv){
    assert(argc==2);
    CheckModuleBoundary();
    Blackboard memory;Context context{{},memory,.25f};
    auto sequence=Tree("<Sequence><AddNumber key='runs' value='1'/><Wait seconds='.5'/><Succeed/></Sequence>");
    assert(sequence.Tick(context)==Status::Running);assert(memory.Number("runs")==1);
    assert(sequence.Tick(context)==Status::Success);assert(memory.Number("runs")==1);
    assert(sequence.Tick(context)==Status::Running);assert(memory.Number("runs")==2);
    Blackboard other;Context second{{},other,.25f};auto independent=Tree("<Wait seconds='.5'/>");assert(independent.Tick(second)==Status::Running);
    int cancelled=0;context.Action=[&](const std::string&,Context&,bool cancel){if(cancel)++cancelled;return Status::Running;};
    auto reactive=Tree("<ReactiveSelector><IsTrue key='alarm'/><Action name='patrol'/></ReactiveSelector>");
    assert(reactive.Tick(context)==Status::Running);memory.Set("alarm",true);assert(reactive.Tick(context)==Status::Success);assert(cancelled==1);
    auto utility=Tree("<Utility><Option key='eat'><SetString key='chosen' value='eat'/></Option><Option key='sleep'><SetString key='chosen' value='sleep'/></Option></Utility>");
    memory.Set("eat",2.0);memory.Set("sleep",3.0);assert(utility.Tick(context)==Status::Success);assert(*memory.Get<std::string>("chosen")=="sleep");
    auto parallel=Tree("<Parallel><AddNumber key='once'/><Wait seconds='.5'/></Parallel>");assert(parallel.Tick(context)==Status::Running);assert(parallel.Tick(context)==Status::Success);assert(memory.Number("once")==1);
    auto repeat=Tree("<Repeat count='3'><AddNumber key='repeated'/></Repeat>");assert(repeat.Tick(context)==Status::Running);assert(repeat.Tick(context)==Status::Running);assert(repeat.Tick(context)==Status::Success);assert(memory.Number("repeated")==3);
    auto cooldown=Tree("<Selector><Cooldown seconds='1'><AddNumber key='cooled'/></Cooldown><Succeed/></Selector>");
    assert(cooldown.Tick(context)==Status::Success);for(int i=0;i<3;++i)cooldown.Tick(context);assert(memory.Number("cooled")==1);cooldown.Tick(context);assert(memory.Number("cooled")==2);
    auto timeout=Tree("<Timeout seconds='.5'><Action name='idle'/></Timeout>");assert(timeout.Tick(context)==Status::Running);assert(timeout.Tick(context)==Status::Failure);assert(cancelled==2);
    auto defaults=BehaviorTree::FromSource("<?xml version='1.0'?><!-- npc --><BehaviorTree version='1'><Blackboard><Number key='health' value='100'/><String key='name' value='A &amp; B'/><Vector key='target' x='2'/></Blackboard><Tree><Succeed/></Tree></BehaviorTree>");
    defaults.Initialize(memory);assert(memory.Number("health")==100);assert(*memory.Get<std::string>("name")=="A & B");memory.Set("health",20.0);defaults.Initialize(memory);assert(memory.Number("health")==20);
    Invalid("<!DOCTYPE BehaviorTree><BehaviorTree/>");Invalid("<BehaviorTree version='1'><Tree><Wait seconds='nan'/></Tree></BehaviorTree>");Invalid("<BehaviorTree version='1'><Tree><Invert/></Tree></BehaviorTree>");Invalid("<BehaviorTree version='1'><Tree><Unknown/></Tree></BehaviorTree>");
    Scene scene;auto body=scene.CreateEntity("NPC");body.AddComponent<Rigidbody>();body.AddComponent<ShapeCollider>();
    Runtime::PhysicsModule().Start(scene);context.Self=body;
    auto velocity=Tree("<SetVelocity x='3'/>");assert(velocity.Tick(context)==Status::Success);assert(scene.GetPhysics().GetLinearVelocity(body).X==3);
    auto stop=Tree("<Stop/>");assert(stop.Tick(context)==Status::Success);assert(scene.GetPhysics().GetLinearVelocity(body).Length()<.001f);
    auto overlap=Tree("<OverlapSphere key='nearby' radius='2'/>");assert(overlap.Tick(context)==Status::Failure);assert(memory.Number("nearby")==0);
    memory.Set("target",Vec3{10,0,0});auto move=Tree("<MoveTo key='target' speed='2'/>");assert(move.Tick(context)==Status::Running);assert(scene.GetPhysics().GetLinearVelocity(body).X>1.9f);move.Reset(context);assert(scene.GetPhysics().GetLinearVelocity(body).Length()<.001f);
    memory.Set("target",Vec3{10,0,0});auto lostTarget=Tree("<MoveTo/>");assert(lostTarget.Tick(context)==Status::Running);memory.Erase("target");assert(lostTarget.Tick(context)==Status::Failure);assert(scene.GetPhysics().GetLinearVelocity(body).Length()<.001f);
    Runtime::ReleasePhysicsScene(scene);
    const auto directory=std::filesystem::temp_directory_path()/("bazzalt-ai-"+UUID::Generate().ToString());
    std::filesystem::create_directories(directory/"Assets");
    {
        std::ofstream project(directory/"AI.bproject");project<<"FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000001\"\nName: AI\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties:\n";
        std::ofstream source(directory/"Assets/NPC.bbtree");source<<"<BehaviorTree version='1'><Tree><AddNumber key='ticks'/></Tree></BehaviorTree>";
    }
    {
        Runtime::Engine engine;assert(engine.LoadProject(directory/"AI.bproject"));
        auto asset=AssetManager::GetAsset(directory/"Assets/NPC.bbtree");assert(asset&&asset->State==AssetState::Ready&&asset->Importer=="Bazzalt.BehaviorTree");
        auto& world=engine.GetScene();auto npc=world.CreateEntity("NPC");auto& config=npc.AddComponent<BehaviorTreeAgent>();config.Tree=asset->Id;config.Loop=false;
        Access::Tick(world.GetSystem<BehaviorTreeSystem>(),world,.1f);Access::Tick(world.GetSystem<BehaviorTreeSystem>(),world,.1f);auto& board=world.GetSystem<BehaviorTreeSystem>().GetBlackboard(npc);assert(board.Number("ticks")==1);
        auto npc2=world.CreateEntity("Second NPC");npc2.AddComponent<BehaviorTreeAgent>().Tree=asset->Id;Access::Tick(world.GetSystem<BehaviorTreeSystem>(),world,.1f);Access::Tick(world.GetSystem<BehaviorTreeSystem>(),world,.1f);assert(world.GetSystem<BehaviorTreeSystem>().GetBlackboard(npc2).Number("ticks")==2);assert(board.Number("ticks")==1);
        auto replacementId=npc.GetUUID();board.Set("old_state",true);
        world.DestroyEntity(npc);npc=world.CreateEntity(replacementId,"Replacement");auto& replacementConfig=npc.AddComponent<BehaviorTreeAgent>();replacementConfig.Tree=asset->Id;replacementConfig.Loop=false;
        auto& behavior=world.GetSystem<BehaviorTreeSystem>();assert(!behavior.GetBlackboard(npc).Contains("old_state"));Access::Tick(behavior,world,.1f);assert(behavior.GetBlackboard(npc).Number("ticks")==1);
        // Default callbacks and explicit owner tokens both retain native code and detach by script owner.
        std::ofstream actionFile(directory/"Assets/Action.bbtree");actionFile<<"<BehaviorTree version='1'><Tree><Action name='run'/></Tree></BehaviorTree>";actionFile.close();assert(engine.RefreshAssets());auto actionAsset=AssetManager::GetAsset(directory/"Assets/Action.bbtree");assert(actionAsset&&actionAsset->State==AssetState::Ready);npc.GetComponent<BehaviorTreeAgent>().Tree=actionAsset->Id;
        int nativeOwner=0,explicitOwner=0,cancellations=0;auto module=std::make_shared<int>(7);std::weak_ptr<int> retained=module;
        auto previousModule=Detail::SetCurrentGameplayModule(module);auto previousOwner=Runtime::ApplicationEventsAccess::SetOwner(&nativeOwner);
        behavior.SetHandlers(npc,[&](const std::string&,Context&,bool cancel){if(cancel)++cancellations;return Status::Running;},{},&explicitOwner);
        behavior.Action=[&](const std::string&,Context&,bool cancel){if(cancel)++cancellations;return Status::Running;};
        Runtime::ApplicationEventsAccess::SetOwner(previousOwner);Detail::SetCurrentGameplayModule(previousModule);module.reset();assert(!retained.expired());
        Access::Tick(behavior,world,.1f);Runtime::DisconnectAIHandlers(&nativeOwner);assert(cancellations==1);assert(retained.expired());assert(!behavior.Action);
        npc.GetComponent<BehaviorTreeAgent>().Tree=asset->Id;
        PropertyMap values;auto descriptor=engine.GetSceneSerializer().GetComponents().Find("Bazzalt.BehaviorTreeAgent");assert(descriptor&&descriptor->Serialize(npc,values));
        auto restored=world.CreateEntity("Restored");assert(descriptor->Deserialize(restored,values,1));assert(restored.GetComponent<BehaviorTreeAgent>().Tree==asset->Id);assert(!restored.GetComponent<BehaviorTreeAgent>().Loop);
        // String UUIDs remain visible to editor dependency tools; custom actions keep coverage partial.
        std::ofstream references(directory/"Assets/References.bbtree");references<<"<BehaviorTree version='1'><Blackboard><String key='asset' value='"<<asset->Id.ToString()<<"'/></Blackboard><Tree><SetString key='other' value='"<<actionAsset->Id.ToString()<<"'/></Tree></BehaviorTree>";references.close();assert(engine.RefreshAssets());auto referenceAsset=AssetManager::GetAsset(directory/"Assets/References.bbtree");auto report=AssetManager::GetDependencyReport(referenceAsset->Id);assert(report.State==AssetDependencyScanState::Partial);assert(report.References.size()==2);
        auto remembered=npc.GetUUID();behavior.GetBlackboard(npc).Set("old_state",true);world.Clear();auto fresh=world.CreateEntity(remembered,"Fresh Scene Agent");auto& freshConfig=fresh.AddComponent<BehaviorTreeAgent>();freshConfig.Tree=asset->Id;freshConfig.Loop=false;assert(!behavior.GetBlackboard(fresh).Contains("old_state"));Access::Tick(behavior,world,.1f);assert(behavior.GetBlackboard(fresh).Number("ticks")==1);
        auto nativeAgent=world.CreateEntity("Native AI");nativeAgent.AddComponent<BehaviorTreeAgent>().Tree=actionAsset->Id;
        auto defaultAgent=world.CreateEntity("Default AI");defaultAgent.AddComponent<BehaviorTreeAgent>().Tree=actionAsset->Id;
        Runtime::ScriptBinding binding;binding.Module=std::filesystem::absolute(argv[1]);binding.Entity=nativeAgent.GetUUID().ToString();binding.TypeName="LifecycleProbe";binding.Properties["LogPath"]=(directory/"ai-lifecycle.log").string();
        Runtime::NativeScriptRuntime oldRuntime,newRuntime;std::string error;assert(oldRuntime.Configure({binding},error));assert(oldRuntime.Start(error));Access::Tick(behavior,world,.1f);
        assert(newRuntime.Configure({binding},error));assert(newRuntime.Start(error,false,true));assert(newRuntime.PreserveFrom(oldRuntime,error));oldRuntime.DiscardForReload();Access::Tick(behavior,world,.1f);
        newRuntime.ActivateReload();Access::Tick(behavior,world,.1f);newRuntime.Stop();Access::Tick(behavior,world,.1f);
        std::ifstream aiLog(directory/"ai-lifecycle.log");std::string text((std::istreambuf_iterator<char>(aiLog)),{});
        for(const auto* token:{"ai_tick:","ai_cancel:","default_tick:","default_cancel:"}){std::size_t count=0,at=0;while((at=text.find(token,at))!=std::string::npos){++count;at+=std::char_traits<char>::length(token);}assert(count==2);}
        std::ofstream invalid(directory/"Assets/Invalid.bbtree");invalid<<"<BehaviorTree version='1'><Tree><Unknown/></Tree></BehaviorTree>";invalid.close();engine.RefreshAssets();
        auto rejected=AssetManager::GetAsset(directory/"Assets/Invalid.bbtree");assert(rejected&&rejected->State!=AssetState::Ready);
    }
    std::filesystem::remove_all(directory);
}
