#include "Physics/PhysicsModuleApi.h"
#include "Bazzalt/Scene.h"
#include <filesystem>
#include <stdexcept>
#include "Runtime/ModuleLoader.h"
namespace Bazzalt::Runtime {
namespace {
const PhysicsModuleApi* Loaded=nullptr;
const PhysicsModuleApi* Load(){
    const auto* api = LoadSiblingModuleApi<PhysicsModuleApi>("bazzalt_physics",
        "BazzaltGetPhysicsModuleApi", PhysicsModuleVersion);
    if(!api||api->Version!=PhysicsModuleVersion||api->Size!=sizeof(PhysicsModuleApi)||!api->NavigationGeometry||!api->Release||!api->Start||!api->Stop||!api->Step||!api->Synchronize||!api->IsRunning||!api->BodyCount||!api->Query||!api->Command||!api->Velocity||!api->Awake||!api->Subscribe||!api->Unsubscribe||!api->DisconnectOwner||!api->SetInterpolationAlpha||!api->RenderMatrix||!api->Guides||!api->JointControl||!api->SoftVertices||!api->SoftControl||!api->Render||!api->VehicleInput||!api->VehicleGear||!api->ReadVehicle||!api->ReadWheel||!api->Replay.GetDeterminism||!api->Replay.SetDeterminism||!api->Replay.CrossPlatform||!api->Replay.Capture||!api->Replay.Restore||!api->Replay.Control||!api->Replay.Status||!api->Replay.Frame)throw std::runtime_error("Incompatible physics runtime; reinstall matching bazzalt_physics");
    return api;
}
}
const PhysicsModuleApi& PhysicsModule(){static const auto* api=Load();Loaded=api;return *api;}
Mat4 PhysicsRenderMatrix(Scene& scene,Entity entity){return Loaded?Loaded->RenderMatrix(scene,entity):entity.GetWorldMatrix();}
void DisconnectPhysicsOwner(const void* owner) noexcept {if(Loaded)try{Loaded->DisconnectOwner(owner);}catch(...) {}}
void RenderPhysicsScene(Scene& scene){if(scene.GetRegistry().view<Cloth>().size()||scene.GetRegistry().view<Rope>().size())PhysicsModule().Render(scene);}
void ReleasePhysicsScene(Scene& scene) noexcept {if(Loaded)try{Loaded->Release(scene);}catch(...) {}}
}
namespace Bazzalt {
bool Scene::SetPhysicsSettings(const PhysicsSettings& settings){
    if(!settings.IsValid())return false;
    const bool capacity=settings.MaxBodies!=m_physicsSettings.MaxBodies||settings.MaxBodyPairs!=m_physicsSettings.MaxBodyPairs||settings.MaxContactConstraints!=m_physicsSettings.MaxContactConstraints;
    if(capacity&&Runtime::Loaded){if(Runtime::Loaded->IsRunning(*this))return false;Runtime::Loaded->Release(*this);}
    m_physicsSettings=settings;return true;
}
PhysicsScene::PhysicsScene(Scene& scene):m_scene(&scene),m_lifetime(scene.m_lifetime){}
Scene& PhysicsScene::GetScene() const {if(m_lifetime.expired())throw std::logic_error("Physics scene has been unloaded");return *m_scene;}
PhysicsSettings PhysicsScene::GetSettings() const{return GetScene().GetPhysicsSettings();}
bool PhysicsScene::SetSettings(const PhysicsSettings& settings){return GetScene().SetPhysicsSettings(settings);}
void PhysicsScene::SetGravity(Vec3 gravity){auto settings=GetSettings();settings.Gravity=gravity;if(!SetSettings(settings))throw std::invalid_argument("Invalid scene gravity");}
Vec3 PhysicsScene::GetGravity()const{return GetSettings().Gravity;}
void PhysicsScene::SetLayerCollision(std::uint32_t a,std::uint32_t b,bool enabled){if(a>=32||b>=32)throw std::out_of_range("Physics layers range from 0 to 31");auto s=GetSettings();if(enabled){s.CollisionMasks[a]|=1u<<b;s.CollisionMasks[b]|=1u<<a;}else{s.CollisionMasks[a]&=~(1u<<b);s.CollisionMasks[b]&=~(1u<<a);}SetSettings(s);}
bool PhysicsScene::GetLayerCollision(std::uint32_t a,std::uint32_t b)const{if(a>=32||b>=32)return false;auto s=GetSettings();return (s.CollisionMasks[a]&(1u<<b))&&(s.CollisionMasks[b]&(1u<<a));}
bool PhysicsScene::IsRunning()const{return Runtime::PhysicsModule().IsRunning(GetScene());}
void PhysicsScene::Synchronize(){Runtime::PhysicsModule().Synchronize(GetScene());}
void PhysicsScene::Simulate(float dt){if(GetSettings().AutoSimulation)throw std::logic_error("Disable AutoSimulation before calling Simulate");Runtime::PhysicsModule().Step(GetScene(),dt,true);}
std::size_t PhysicsScene::GetBodyCount()const{return Runtime::PhysicsModule().BodyCount(GetScene());}
std::vector<PhysicsHit> PhysicsScene::RaycastAll(Vec3 p,Vec3 d,float distance,const PhysicsQueryOptions& o)const {Transform t;t.Position=p;return Runtime::PhysicsModule().Query(GetScene(),0,{},t,d,distance,o);}
PhysicsHit PhysicsScene::Raycast(Vec3 p,Vec3 d,float distance,const PhysicsQueryOptions& o)const {auto options=o;options.MaxResults=1;auto hits=RaycastAll(p,d,distance,options);return hits.empty()?PhysicsHit{}:hits.front();}
std::vector<PhysicsHit> PhysicsScene::ShapeCastAll(const PhysicsShape& s,const Transform& t,Vec3 d,float distance,const PhysicsQueryOptions& o)const{return Runtime::PhysicsModule().Query(GetScene(),1,s,t,d,distance,o);}
PhysicsHit PhysicsScene::ShapeCast(const PhysicsShape& s,const Transform& t,Vec3 d,float distance,const PhysicsQueryOptions& o)const {auto options=o;options.MaxResults=1;auto hits=ShapeCastAll(s,t,d,distance,options);return hits.empty()?PhysicsHit{}:hits.front();}
std::vector<PhysicsHit> PhysicsScene::OverlapShape(const PhysicsShape& s,const Transform& t,const PhysicsQueryOptions& o)const{return Runtime::PhysicsModule().Query(GetScene(),2,s,t,{},0,o);}
std::vector<PhysicsHit> PhysicsScene::OverlapSphere(Vec3 p,float r,const PhysicsQueryOptions& o)const{PhysicsShape s;s.Radius=r;Transform t;t.Position=p;return OverlapShape(s,t,o);}
std::vector<PhysicsHit> PhysicsScene::OverlapBox(Vec3 p,Vec3 extents,Quaternion rotation,const PhysicsQueryOptions& o)const{PhysicsShape s;s.Shape=ColliderShape::Box;s.Size=extents*2;Transform t;t.Position=p;t.Rotation=rotation;return OverlapShape(s,t,o);}
#define COMMAND(Method,Op) bool PhysicsScene::Method(Entity e,Vec3 v){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::Op,v,{},{},0,true);}
COMMAND(ApplyForce,Force) COMMAND(ApplyImpulse,Impulse) COMMAND(ApplyTorque,Torque) COMMAND(ApplyAngularImpulse,AngularImpulse) COMMAND(SetLinearVelocity,LinearVelocity) COMMAND(SetAngularVelocity,AngularVelocity)
#undef COMMAND
bool PhysicsScene::ApplyForceAtPosition(Entity e,Vec3 v,Vec3 p){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::ForceAtPosition,v,p,{},0,true);}
bool PhysicsScene::ApplyImpulseAtPosition(Entity e,Vec3 v,Vec3 p){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::ImpulseAtPosition,v,p,{},0,true);}
bool PhysicsScene::MoveKinematic(Entity e,Vec3 p,Quaternion q,float dt){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::MoveKinematic,p,{},q,dt,true);}
bool PhysicsScene::Teleport(Entity e,Vec3 p,Quaternion q,bool activate){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::Teleport,p,{},q,0,activate);}
bool PhysicsScene::WakeUp(Entity e){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::Wake,{},{},{},0,true);}
bool PhysicsScene::Sleep(Entity e){return Runtime::PhysicsModule().Command(GetScene(),e,Runtime::PhysicsCommand::Sleep,{},{},{},0,true);}
Vec3 PhysicsScene::GetLinearVelocity(Entity e)const{return Runtime::PhysicsModule().Velocity(GetScene(),e,false);}
Vec3 PhysicsScene::GetAngularVelocity(Entity e)const{return Runtime::PhysicsModule().Velocity(GetScene(),e,true);}
bool PhysicsScene::IsAwake(Entity e)const{return Runtime::PhysicsModule().Awake(GetScene(),e);}
bool PhysicsScene::SetGravity(Entity e,std::optional<Vec3> gravity){if(e.GetScene()!=&GetScene()||!e||!e.HasComponent<Rigidbody>())return false;auto value=e.GetComponent<Rigidbody>();value.Gravity=gravity;if(!value.IsValid())return false;e.GetComponent<Rigidbody>()=value;return true;}
std::optional<Vec3> PhysicsScene::GetGravity(Entity e)const{if(e.GetScene()!=&GetScene()||!e||!e.HasComponent<Rigidbody>())return {};return e.GetComponent<Rigidbody>().Gravity;}
PhysicsScene::Subscription PhysicsScene::Subscribe(PhysicsEventType type,Callback cb,Entity filter){return Runtime::PhysicsModule().Subscribe(GetScene(),type,std::move(cb),filter);}
bool PhysicsScene::Unsubscribe(Subscription id){return Runtime::PhysicsModule().Unsubscribe(GetScene(),id);}
bool PhysicsScene::BreakJoint(Entity e,JointType t){return Runtime::PhysicsModule().JointControl(GetScene(),e,t,0);}
bool PhysicsScene::RepairJoint(Entity e,JointType t){return Runtime::PhysicsModule().JointControl(GetScene(),e,t,1);}
bool PhysicsScene::IsJointBroken(Entity e,JointType t)const{return Runtime::PhysicsModule().JointControl(GetScene(),e,t,2);}
std::vector<Vec3> PhysicsScene::GetSoftBodyVertices(Entity e)const{return Runtime::PhysicsModule().SoftVertices(GetScene(),e);}
bool PhysicsScene::ApplySoftBodyImpulse(Entity e,Vec3 v,int index){return Runtime::PhysicsModule().SoftControl(GetScene(),e,v,index,0);}
bool PhysicsScene::SetSoftBodyVertexPinned(Entity e,unsigned index,bool pinned){if(index>16384)return false;return Runtime::PhysicsModule().SoftControl(GetScene(),e,{},int(index),pinned?1:2);}
bool PhysicsScene::SetVehicleInput(Entity e,float throttle,float steering,float brake,float handbrake){return Runtime::PhysicsModule().VehicleInput(GetScene(),e,throttle,steering,brake,handbrake);}
bool PhysicsScene::SetVehicleGear(Entity e,int gear,float clutch){return Runtime::PhysicsModule().VehicleGear(GetScene(),e,gear,clutch);}
std::optional<VehicleState> PhysicsScene::GetVehicleState(Entity e)const{return Runtime::PhysicsModule().ReadVehicle(GetScene(),e);}
std::optional<VehicleWheelState> PhysicsScene::GetVehicleWheelState(Entity e)const{return Runtime::PhysicsModule().ReadWheel(GetScene(),e);}

}
