#include "Runtime/Engine.h"
#include "Runtime/LuaRuntime.h"
#include "Bazzalt/AI.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/BehaviorTreeAgent.h"
#include "Bazzalt/Systems/BehaviorTreeSystem.h"
#include "Physics/PhysicsModuleApi.h"
#include <cassert>
#include <fstream>
using namespace Bazzalt;
namespace {struct Access:System {static void Tick(System& system,Scene& scene){constexpr auto method=&Access::OnFixedUpdate;(system.*method)(scene,.1f);}};}
int main(){
    const auto directory=std::filesystem::temp_directory_path()/("bazzalt-lua-ai-"+UUID::Generate().ToString());std::filesystem::create_directories(directory/"Assets");
    {
        std::ofstream project(directory/"AI.bproject");project<<"FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000001\"\nName: AI\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties:\n";
        std::ofstream asset(directory/"Assets/NPC.bbtree");asset<<"<BehaviorTree version='1'><Tree><Sequence><Condition name='ready'/><Action name='watch'/></Sequence></Tree></BehaviorTree>";
    }
    auto source=directory/"Assets/Guard.lua",output=directory/"Guard.blua";
    {
        std::ofstream file(source);file<<R"lua(
local B = Bazzalt
local Guard = COMPONENT('Guard')
function Guard:OnCreate()
  local memory = B.AI.GetBlackboard(self.Entity)
  memory:Set('ready', true); memory:Set('label', 'Guard'); memory:Set('target', B.Vec3.new(1,2,3))
  assert(memory:Contains('ready') and memory:Get('ready') == true and memory:Get('target').Y == 2)
  assert(memory:Number('missing', 8) == 8 and memory:Get('missing') == nil)
  memory:Erase('label'); assert(not memory:Contains('label'))
  assert(not pcall(function() memory:Set('bad', {}) end))
  local scratch = B.Blackboard.new()
  local tree = B.BehaviorTree.FromSource([[<BehaviorTree version='1'><Blackboard><Number key='health' value='10'/></Blackboard><Tree><Action name='standalone'/></Tree></BehaviorTree>]])
  tree:Initialize(scratch); assert(scratch:Number('health') == 10)
  local function action(name, entity, board, dt, cancelled)
    if cancelled then board:Set('cancelled', true); return end
    assert(name == 'standalone' and dt > 0); board:Set('tick', true); return B.BehaviorStatus.Running
  end
  assert(tree:Tick(self.Entity, scratch, .1, action, nil) == B.BehaviorStatus.Running)
  tree:Reset(self.Entity, scratch, action); assert(scratch:Get('cancelled') and scratch:Get('tick'))
  assert(not pcall(function() tree:Tick(self.Entity, scratch, -.1, action, nil) end))
  local movement = B.BehaviorTree.FromSource([[<BehaviorTree version='1'><Tree><SetVelocity x='2'/></Tree></BehaviorTree>]])
  assert(movement:Tick(self.Entity, scratch, .1, nil, nil) == B.BehaviorStatus.Success)
  assert(self.Entity:GetScene():GetPhysics():GetLinearVelocity(self.Entity).X == 2)
  local loaded = B.BehaviorTree.Load(self.Entity:GetComponent('BehaviorTreeAgent').Tree)
  loaded:Initialize(scratch)
  local component = self.Entity:GetComponent('BehaviorTreeAgent');component.Loop = false;self.Entity:SetComponent('BehaviorTreeAgent', component)
  assert(self.Entity:HasComponent('BehaviorTreeAgent') and self.Entity:GetComponent('BehaviorTreeAgent').Loop == false)
  B.AI.SetHandlers(self.Entity,
    function(name, entity, board, dt, cancelled)
      if cancelled then
        assert(not pcall(function() B.AI.SetHandlers(entity, nil, nil) end))
        board:Set('cancels', board:Number('cancels') + 1); return
      end
      assert(name == 'watch');board:Set('ticks', board:Number('ticks') + 1);return B.BehaviorStatus.Running
    end,
    function(name, entity, board, dt) return name == 'ready' and board:Get('ready') end)
end
return Guard
)lua";
    }
    {
        Runtime::Engine engine;assert(engine.LoadProject(directory/"AI.bproject"));auto& scene=engine.GetScene();auto entity=scene.CreateEntity("Guard");
        auto asset=AssetManager::GetAsset(directory/"Assets/NPC.bbtree");assert(asset&&asset->State==AssetState::Ready);entity.AddComponent<BehaviorTreeAgent>().Tree=asset->Id;entity.AddComponent<Rigidbody>();entity.AddComponent<ShapeCollider>();
        Runtime::PhysicsModule().Start(scene);
        Runtime::LuaRuntime runtime;std::string error;assert(Runtime::LuaRuntime::Import(source,output,error));Runtime::ScriptBinding binding;binding.Module=output;binding.Entity=entity.GetUUID().ToString();binding.TypeName="Guard";
        assert(runtime.Configure({binding},error));if(!runtime.Start(error)){std::fprintf(stderr,"%s\n",error.c_str());assert(false);}
        auto& system=scene.GetSystem<BehaviorTreeSystem>();Access::Tick(system,scene);Access::Tick(system,scene);auto& memory=system.GetBlackboard(entity);assert(memory.Number("ticks")==2);
        runtime.Stop();assert(memory.Number("cancels")==1);Access::Tick(system,scene);assert(memory.Number("ticks")==2);
        assert(runtime.Start(error));Access::Tick(system,scene);runtime.DiscardForReload();assert(memory.Number("cancels")==2);Access::Tick(system,scene);assert(memory.Number("ticks")==3);
        Runtime::ReleasePhysicsScene(scene);
    }
    std::filesystem::remove_all(directory);
}
