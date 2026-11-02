#include "Bazzalt/PhysicsReplay.h"
#include "Physics/PhysicsModuleApi.h"
#include "Bazzalt/Scene.h"
namespace Bazzalt {
PhysicsReplay::PhysicsReplay(Scene& scene):m_scene(&scene),m_physics(scene){}
Scene& PhysicsReplay::GetScene()const{(void)m_physics.IsRunning();return *m_scene;}
PhysicsDeterminismSettings PhysicsReplay::GetDeterminismSettings()const{return Runtime::PhysicsModule().Replay.GetDeterminism(GetScene());}
void PhysicsReplay::SetDeterminismSettings(const PhysicsDeterminismSettings& settings){if(!settings.IsValid())throw std::invalid_argument("Invalid deterministic stepping settings");Runtime::PhysicsModule().Replay.SetDeterminism(GetScene(),settings);}
bool PhysicsReplay::IsSolverCrossPlatformDeterministic()const{return Runtime::PhysicsModule().Replay.CrossPlatform();}
PhysicsState PhysicsReplay::CaptureState()const{return Runtime::PhysicsModule().Replay.Capture(GetScene());}
void PhysicsReplay::RestoreState(const PhysicsState& state){Runtime::PhysicsModule().Replay.Restore(GetScene(),state);}
void PhysicsReplay::BeginRecording(const PhysicsReplaySettings& settings){if(!settings.IsValid())throw std::invalid_argument("Invalid replay limits");Runtime::PhysicsModule().Replay.Control(GetScene(),0,settings,0,false);}
void PhysicsReplay::EndRecording(){Runtime::PhysicsModule().Replay.Control(GetScene(),1,{},0,false);}
void PhysicsReplay::BeginPlayback(bool loop){Runtime::PhysicsModule().Replay.Control(GetScene(),2,{},0,loop);}
void PhysicsReplay::StopPlayback(){Runtime::PhysicsModule().Replay.Control(GetScene(),3,{},0,false);}
void PhysicsReplay::SeekFrame(std::uint64_t index){Runtime::PhysicsModule().Replay.Control(GetScene(),4,{},index,false);}
bool PhysicsReplay::AdvancePlayback(){auto before=GetStatus();Runtime::PhysicsModule().Replay.Control(GetScene(),5,{},0,false);auto after=GetStatus();return before.Cursor!=after.Cursor;}
void PhysicsReplay::Clear(){Runtime::PhysicsModule().Replay.Control(GetScene(),6,{},0,false);}
PhysicsReplayStatus PhysicsReplay::GetStatus()const{return Runtime::PhysicsModule().Replay.Status(GetScene());}
PhysicsState PhysicsReplay::GetFrame(std::uint64_t index)const{return Runtime::PhysicsModule().Replay.Frame(GetScene(),index);}
}
