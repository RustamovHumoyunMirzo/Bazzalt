#include "Bazzalt/PhysicsReplay.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Physics/PhysicsModuleApi.h"
#include <cassert>
#include <functional>
using namespace Bazzalt;
static bool Rejects(const std::function<void()>& action){try{action();return false;}catch(const std::exception&){return true;}}
int main(){
 Scene scene;auto settings=scene.GetPhysicsSettings();settings.AutoSimulation=false;settings.WorkerThreads=2;assert(scene.SetPhysicsSettings(settings));
 auto floor=scene.CreateEntity("Floor");floor.GetComponent<Transform>().Position={0,-.5f,0};floor.AddComponent<ShapeCollider>().Size={20,1,20};
 auto body=scene.CreateEntity("Ball");body.GetComponent<Transform>().Position={0,3,0};body.AddComponent<ShapeCollider>().Shape=ColliderShape::Sphere;body.AddComponent<Rigidbody>();
 auto cloth=scene.CreateEntity("Cloth");cloth.GetComponent<Transform>().Position={5,3,0};cloth.AddComponent<PrimitiveObject>();auto& c=cloth.AddComponent<Cloth>();c.SubdivisionLevel=0;c.PinEdges=0;
 auto car=scene.CreateEntity("Car");car.GetComponent<Transform>().Position={-5,1,0};car.AddComponent<ShapeCollider>().Size={1.6f,.4f,3};car.AddComponent<Rigidbody>().Mass=1200;car.AddComponent<Vehicle>();car.AddComponent<VehicleEngine>();car.AddComponent<VehicleTransmission>();
 std::vector<Entity> wheels;for(int i=0;i<4;++i){auto wheel=scene.CreateEntity("Wheel");wheel.SetParent(car,false);wheel.AddComponent<VehicleWheel>().Position={i%2?.9f:-.9f,0,i<2?1.2f:-1.2f};wheel.AddComponent<WheelSuspension>();wheel.AddComponent<WheelTire>();wheel.AddComponent<WheelBrake>();if(i<2)wheel.AddComponent<WheelSteering>();wheels.push_back(wheel);}
 auto& diff=car.AddComponent<VehicleDifferential>();diff.LeftWheel=wheels[2].GetUUID();diff.RightWheel=wheels[3].GetUUID();
 PhysicsReplay replay(scene);PhysicsDeterminismSettings deterministic;deterministic.Enabled=true;replay.SetDeterminismSettings(deterministic);Runtime::PhysicsModule().Start(scene);auto physics=scene.GetPhysics();
 assert(Rejects([&]{physics.Simulate(1.f/30.f);}));assert(Rejects([&]{replay.RestoreState({});}));assert(physics.ApplyForce(body,{1,0,0}));
 auto wheelInitial=*physics.GetVehicleWheelState(wheels[0]);auto initial=replay.CaptureState();assert(initial.IsValid()&&initial.GetByteSize()>0&&initial.GetTick()==0);
 auto ui=scene.CreateEntity("Unrelated UI");ui.GetComponent<Transform>().Position={99,0,0};
 auto simulate=[&]{for(int i=0;i<90;++i){assert(physics.SetVehicleInput(car,.3f,float(i%3-1)*.1f));if(i==3)physics.ApplyImpulse(body,{1,2,0});physics.ApplyForce(body,{.1f,0,0});physics.Simulate(deterministic.FixedDeltaTime);}};
 simulate();auto end=replay.CaptureState();auto position=body.GetWorldTransform().Position;auto velocity=physics.GetLinearVelocity(body);auto vertices=physics.GetSoftBodyVertices(cloth);
 replay.RestoreState(initial);assert(body.GetWorldTransform().Position.Y==3);auto wheelRestored=*physics.GetVehicleWheelState(wheels[0]);assert(wheelRestored.SteerAngle==wheelInitial.SteerAngle&&wheelRestored.LongitudinalSlip==wheelInitial.LongitudinalSlip&&wheelRestored.Grounded==wheelInitial.Grounded);assert(ui.GetWorldTransform().Position.X==99);simulate();assert(body.GetWorldTransform().Position==position);assert(physics.GetLinearVelocity(body)==velocity);assert(physics.GetSoftBodyVertices(cloth)==vertices);assert(replay.CaptureState().GetHash()==end.GetHash());
 auto pinned=replay.CaptureState();assert(physics.SetSoftBodyVertexPinned(cloth,0,true));replay.RestoreState(pinned);assert(replay.CaptureState().GetHash()==pinned.GetHash());
 PhysicsReplaySettings limits;limits.MaxFrames=5;replay.BeginRecording(limits);for(int i=0;i<7;++i)physics.Simulate(deterministic.FixedDeltaTime);auto status=replay.GetStatus();assert(status.FrameCount==5&&status.LimitReached&&status.Mode==PhysicsReplayMode::Idle&&status.MemoryBytes<=limits.MaxMemoryBytes);
 int callbacks=0;auto subscription=physics.Subscribe(PhysicsEventType::CollisionStay,[&](const CollisionEvent&){++callbacks;});
 auto last=replay.GetFrame(4);replay.BeginPlayback();assert(replay.GetStatus().Cursor==0);assert(!physics.ApplyImpulse(body,{10,0,0}));assert(Rejects([&]{physics.Simulate(deterministic.FixedDeltaTime);}));for(int i=0;i<4;++i)assert(replay.AdvancePlayback());assert(!replay.AdvancePlayback());assert(replay.GetStatus().AtEnd);assert(replay.CaptureState().GetHash()==last.GetHash());assert(callbacks==0);assert(physics.Unsubscribe(subscription));
 replay.SeekFrame(1);assert(replay.GetStatus().Cursor==1);assert(Rejects([&]{replay.SeekFrame(100);}));assert(replay.GetStatus().Cursor==1);replay.StopPlayback();replay.BeginPlayback(true);replay.SeekFrame(4);assert(replay.AdvancePlayback()&&replay.GetStatus().Cursor==0);replay.StopPlayback();
 auto checkpoint=replay.GetFrame(0);replay.Clear();assert(replay.GetStatus().FrameCount==0);replay.RestoreState(checkpoint);
 limits.MaxFrames=100;limits.MaxMemoryBytes=replay.CaptureState().GetByteSize()+1;replay.BeginRecording(limits);physics.Simulate(deterministic.FixedDeltaTime);assert(replay.GetStatus().LimitReached&&replay.GetStatus().FrameCount==1&&replay.GetStatus().MemoryBytes<=limits.MaxMemoryBytes);replay.Clear();
 limits.MaxMemoryBytes=1024;assert(Rejects([&]{replay.BeginRecording(limits);}));assert(replay.GetStatus().Mode==PhysicsReplayMode::Idle);
 body.GetComponent<ShapeCollider>().Radius=.7f;assert(Rejects([&]{replay.RestoreState(checkpoint);}));Runtime::PhysicsModule().Stop(scene);Runtime::PhysicsModule().Start(scene);assert(Rejects([&]{replay.RestoreState(checkpoint);}));Runtime::PhysicsModule().Stop(scene);
 Scene another;auto otherSettings=another.GetPhysicsSettings();otherSettings.AutoSimulation=false;another.SetPhysicsSettings(otherSettings);Runtime::PhysicsModule().Start(another);PhysicsReplay other(another);assert(Rejects([&]{other.RestoreState(initial);}));Runtime::PhysicsModule().Stop(another);
 return 0;
}
