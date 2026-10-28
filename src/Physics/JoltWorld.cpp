#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include "Physics/PhysicsModuleApi.h"
#include "Physics/PhysicsExtras.h"
#include "Physics/PhysicsFields.h"
#include "Physics/PhysicsGeometry.h"
#include "Physics/PhysicsKeys.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include "Bazzalt/Detail/SubsystemAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include <algorithm>
#include <bit>
#include <unordered_set>
#include <chrono>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>
#include <stdexcept>

namespace Bazzalt::Runtime {
namespace {
JPH::Vec3 V(Vec3 v){return {v.X,v.Y,v.Z};}
Vec3 V(JPH::Vec3Arg v){return {v.GetX(),v.GetY(),v.GetZ()};}
JPH::Quat Q(Quaternion q){if(!std::isfinite(q.X)||!std::isfinite(q.Y)||!std::isfinite(q.Z)||!std::isfinite(q.W)||q.X*q.X+q.Y*q.Y+q.Z*q.Z+q.W*q.W<1e-12f)throw std::invalid_argument("Physics requires a finite nonzero rotation");q=q.Normalized();return {q.X,q.Y,q.Z,q.W};}
Quaternion Q(JPH::QuatArg q){return {q.GetX(),q.GetY(),q.GetZ(),q.GetW()};}
bool Finite(Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);}
void RequireVector(Vec3 v){if(!Finite(v))throw std::invalid_argument("Physics requires finite vectors");}
bool Same(const Transform& a,const Transform& b){return (a.Position-b.Position).LengthSquared()<1e-10f&&std::abs(Quaternion::Dot(a.Rotation,b.Rotation))>1-1e-6f&&(a.Scale-b.Scale).LengthSquared()<1e-10f;}
JPH::RefConst<JPH::Shape> ShapeResult(JPH::ShapeSettings::ShapeResult result){if(result.HasError())throw std::runtime_error("Collider shape: "+std::string(result.GetError().c_str()));return result.Get();}
JPH::RefConst<JPH::Shape> Shape(const PhysicsShape& s,Vec3 scale,float convexRadius=.02f){
    RequireVector(scale);if(scale.X<=0||scale.Y<=0||scale.Z<=0)throw std::invalid_argument("Collider scale must be positive");
    ShapeCollider check;check.Shape=s.Shape;check.Size=s.Size;check.Radius=s.Radius;check.Height=s.Height;if(!check.IsValid())throw std::invalid_argument("Invalid collider dimensions");
    RequireVector(s.Size*scale);RequireVector({s.Radius*std::max({scale.X,scale.Y,scale.Z}),s.Height*scale.Y,0});
    switch(s.Shape){
    case ColliderShape::Box:{auto half=s.Size*scale*.5f;return ShapeResult(JPH::BoxShapeSettings(V(half),std::min(convexRadius,std::min({half.X,half.Y,half.Z})*.49f)).Create());}
    case ColliderShape::Sphere:return ShapeResult(JPH::SphereShapeSettings(s.Radius*std::max({scale.X,scale.Y,scale.Z})).Create());
    case ColliderShape::Capsule:{float radius=s.Radius*std::max(scale.X,scale.Z),half=std::max(0.f,s.Height*scale.Y*.5f-radius);if(half<1e-6f)return ShapeResult(JPH::SphereShapeSettings(radius).Create());return ShapeResult(JPH::CapsuleShapeSettings(half,radius).Create());}
    case ColliderShape::Cylinder:{float radius=s.Radius*std::max(scale.X,scale.Z),half=s.Height*scale.Y*.5f;return ShapeResult(JPH::CylinderShapeSettings(half,radius,std::min(convexRadius,std::min(half,radius)*.49f)).Create());}
    case ColliderShape::Plane:return ShapeResult(JPH::PlaneShapeSettings(JPH::Plane(JPH::Vec3::sAxisY(),0),nullptr,10000000.f).Create());
    }throw std::invalid_argument("Unknown collider shape");
}
struct BroadLayers final:JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers()const override{return 2;}
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer)const override{return JPH::BroadPhaseLayer((layer&32)?0:1);}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer)const override{return layer.GetValue()==0?"Static":"Moving";}
#endif
};
struct PairFilter final:JPH::ObjectLayerPairFilter {const PhysicsSettings* Settings=nullptr;bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b)const override{if((a&32)&&(b&32))return false;if((a&64)||(b&64)||!Settings)return true;const auto first=a&31,second=b&31;return (Settings->CollisionMasks[first]&(1u<<second))&&(Settings->CollisionMasks[second]&(1u<<first));}};
struct BroadFilter final:JPH::ObjectVsBroadPhaseLayerFilter {bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b)const override{return !(a&32)||b.GetValue()==1;}};
using GuideLines=std::vector<std::pair<Vec3,Vec3>>;
struct ColliderData {std::string GeometryKey;std::shared_ptr<const GuideLines> GuideLocal;std::vector<std::array<float,10>> GuideWorld;Vec3 GuidePosition;Quaternion GuideRotation;bool GuideReady=false;UUID Body,Collider;std::uint32_t Layer=0,Group=0;float Friction=.5f,Restitution=0;bool Trigger=false,DoubleSided=true;JPH::RefConst<JPH::Shape> NativeShape;Transform Local;};
struct PendingForce {UUID Owner;Entity::Id Generation;PhysicsCommand Command;Vec3 Value,Point;};
struct BodyEntry {JPH::BodyID Id;UUID Owner;std::string Signature;Transform LastScene;Transform PreviousScene;Rigidbody Authored;bool HasRigidbody=false;std::vector<std::uint64_t> Shapes;};
struct Subscription {std::shared_ptr<void> Lease;PhysicsScene::Callback Callback;PhysicsEventType Type;UUID Filter;const void* Owner=nullptr;int Level=-1;};
using Pair=std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t>;
using ContactPair=std::tuple<UUID,UUID,UUID,UUID,bool>;
ContactPair ContactKey(const CollisionEvent& e){auto a=std::pair{e.BodyAUUID,e.ColliderAUUID},b=std::pair{e.BodyBUUID,e.ColliderBUUID};if(b<a)std::swap(a,b);return {a.first,a.second,b.first,b.second,int(e.Type)>=int(PhysicsEventType::TriggerEnter)};}
bool IsExit(PhysicsEventType type){return type==PhysicsEventType::CollisionExit||type==PhysicsEventType::TriggerExit;}

struct World final:JPH::ContactListener {
    bool Preview=false;
    std::map<UUID,std::string> SoftDiagnostics;
    Scene* ScenePtr;bool Running=false,Dead=false,Stepping=false,Dispatching=false,Updating=false;float Alpha=1,LastDelta=0;
    PhysicsSettings Settings;
    bool TopologyDirty=true;std::vector<entt::scoped_connection> Connections;std::vector<PendingForce> Forces;
    BroadLayers Broad;PairFilter Pairs;BroadFilter BroadPairs;
    JPH::PhysicsSystem System;
    JPH::TempAllocatorImplWithMallocFallback Allocator{16*1024*1024};
    std::unique_ptr<JPH::JobSystemThreadPool> Jobs;
    std::map<JointKey,JointEntry> Joints;
    std::map<UUID,SoftEntry> SoftBodies;
    std::map<std::pair<unsigned,unsigned>,unsigned> Suppressed;
    std::map<UUID,BodyEntry> Bodies;
    std::map<std::uint32_t,UUID> BodyOwners;
    std::map<std::uint64_t,ColliderData> Colliders;
    std::map<unsigned,ColliderData> SoftColliders;
    std::uint64_t NextShape=1,NextSubscription=1;
    std::map<std::uint64_t,std::shared_ptr<Subscription>> Subscriptions;
    std::mutex EventMutex;
    std::map<Pair,CollisionEvent> Contacts;
    std::map<ContactPair,std::size_t> ContactCounts,Queued;
    std::vector<CollisionEvent> Events;
    bool Overflow=false;
    explicit World(Scene& scene):ScenePtr(&scene),Settings(scene.GetPhysicsSettings()){
        if(!Settings.IsValid())throw std::invalid_argument("Invalid scene physics settings");
        Pairs.Settings=&Settings;
        System.Init(Settings.MaxBodies,0,Settings.MaxBodyPairs,Settings.MaxContactConstraints,Broad,BroadPairs,Pairs);
        System.SetContactListener(this);Configure();
#define TRACK(Type) Connections.emplace_back(scene.GetRegistry().on_construct<Type>().connect<&World::Changed>(*this));Connections.emplace_back(scene.GetRegistry().on_destroy<Type>().connect<&World::Changed>(*this));Connections.emplace_back(scene.GetRegistry().on_update<Type>().connect<&World::Changed>(*this));
        TRACK(Rigidbody) TRACK(ShapeCollider) TRACK(MeshCollider) TRACK(Joint) TRACK(DistanceJoint) TRACK(HingeJoint) TRACK(SliderJoint) TRACK(BallAndSocketJoint) TRACK(FixedJoint) TRACK(Cloth) TRACK(Rope)
#undef TRACK
    }
    void Changed(entt::registry&,entt::entity){TopologyDirty=true;}
    ~World(){ClearExtras();Connections.clear();System.SetContactListener(nullptr);for(auto& [id,b]:Bodies){System.GetBodyInterface().RemoveBody(b.Id);System.GetBodyInterface().DestroyBody(b.Id);}}
    void Configure(){
        const auto next=ScenePtr->GetPhysicsSettings();
        if(next.MaxBodies!=Settings.MaxBodies||next.MaxBodyPairs!=Settings.MaxBodyPairs||next.MaxContactConstraints!=Settings.MaxContactConstraints)throw std::logic_error("Physics capacity changes require restarting the scene simulation");
        const bool changedLayers=Settings.CollisionMasks!=next.CollisionMasks;const bool changedGravity=Settings.Gravity!=next.Gravity;const bool changedSleep=Settings.AllowSleeping!=next.AllowSleeping;Settings=next;System.SetGravity(V(Settings.Gravity));if(changedLayers)for(auto& [uuid,entry]:Bodies){System.GetBodyInterface().InvalidateContactCache(entry.Id);if(entry.Authored.MotionType!=BodyMotionType::Static)System.GetBodyInterface().ActivateBody(entry.Id);}
        if(changedSleep||changedGravity)for(auto& [uuid,entry]:Bodies)if(entry.Authored.MotionType!=BodyMotionType::Static){JPH::BodyLockWrite lock(System.GetBodyLockInterface(),entry.Id);if(lock.Succeeded()&&changedSleep)lock.GetBody().SetAllowSleeping(entry.Authored.AllowSleeping&&Settings.AllowSleeping);}
        if(changedSleep)for(auto& [uuid,entry]:SoftBodies){auto entity=ScenePtr->GetEntity(uuid);bool allow=true;if(auto* c=entity.TryGetComponent<Cloth>())allow=c->AllowSleeping;else if(auto* r=entity.TryGetComponent<Rope>())allow=r->AllowSleeping;JPH::BodyLockWrite lock(System.GetBodyLockInterface(),entry.Id);if(lock.Succeeded())lock.GetBody().SetAllowSleeping(allow&&Settings.AllowSleeping);}
        if(changedGravity||changedSleep)for(auto& [uuid,entry]:Bodies)if(entry.Authored.MotionType==BodyMotionType::Dynamic)System.GetBodyInterface().ActivateBody(entry.Id);
        JPH::PhysicsSettings settings=System.GetPhysicsSettings();settings.mNumVelocitySteps=Settings.VelocityIterations;settings.mNumPositionSteps=Settings.PositionIterations;settings.mSpeculativeContactDistance=Settings.SpeculativeContactDistance;settings.mPenetrationSlop=Settings.PenetrationSlop;settings.mBaumgarte=Settings.Baumgarte;settings.mTimeBeforeSleep=Settings.TimeBeforeSleep;settings.mPointVelocitySleepThreshold=Settings.PointVelocitySleepThreshold;
        System.SetPhysicsSettings(settings);
        if(!Jobs||Jobs->GetMaxConcurrency()!=int(Settings.WorkerThreads+1))Jobs=std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs,JPH::cMaxPhysicsBarriers,int(Settings.WorkerThreads));
    }
    const ColliderData* Data(const JPH::Body& body,const JPH::SubShapeID& sub)const{if(body.IsSoftBody()){auto s=SoftColliders.find(body.GetID().GetIndexAndSequenceNumber());return s==SoftColliders.end()?nullptr:&s->second;}auto it=Colliders.find(body.GetShape()->GetSubShapeUserData(sub));return it==Colliders.end()?nullptr:&it->second;}
    JPH::ValidateResult OnContactValidate(const JPH::Body& a,const JPH::Body& b,JPH::RVec3Arg,const JPH::CollideShapeResult& hit)override{
        std::pair<unsigned,unsigned> ids=std::minmax(a.GetID().GetIndexAndSequenceNumber(),b.GetID().GetIndexAndSequenceNumber());if(Suppressed.contains({ids.first,ids.second}))return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
        const auto* x=Data(a,hit.mSubShapeID1);const auto* y=Data(b,hit.mSubShapeID2);
        if(x&&y&&((x->Group&&x->Group==y->Group)||!(Settings.CollisionMasks[x->Layer]&(1u<<y->Layer))||!(Settings.CollisionMasks[y->Layer]&(1u<<x->Layer))))return JPH::ValidateResult::RejectContact;
        return JPH::ValidateResult::AcceptContact;
    }
    void Queue(CollisionEvent event){
        const auto key=ContactKey(event);auto found=Queued.find(key);
        if(found!=Queued.end()&&!IsExit(event.Type)&&!IsExit(Events[found->second].Type)){
            auto& previous=Events[found->second];
            for(const auto& point:event.Contacts){auto same=std::find_if(previous.Contacts.begin(),previous.Contacts.end(),[&](const auto& p){return (p.PointA-point.PointA).LengthSquared()<1e-8f&&(p.PointB-point.PointB).LengthSquared()<1e-8f;});if(same!=previous.Contacts.end())same->Penetration=std::max(same->Penetration,point.Penetration);else if(previous.Contacts.size()<64)previous.Contacts.push_back(point);}
            return;
        }
        if(Events.size()>=65536){Overflow=true;return;}Queued[key]=Events.size();Events.push_back(std::move(event));
    }
    void Contact(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings,bool persisted){
        const auto* x=Data(a,manifold.mSubShapeID1);const auto* y=Data(b,manifold.mSubShapeID2);if(!x||!y)return;
        settings.mIsSensor=x->Trigger||y->Trigger;settings.mCombinedFriction=std::sqrt(x->Friction*y->Friction);settings.mCombinedRestitution=std::max(x->Restitution,y->Restitution);
        CollisionEvent event;event.Type=settings.mIsSensor?(persisted?PhysicsEventType::TriggerStay:PhysicsEventType::TriggerEnter):(persisted?PhysicsEventType::CollisionStay:PhysicsEventType::CollisionEnter);
        event.BodyAUUID=x->Body;event.BodyBUUID=y->Body;event.ColliderAUUID=x->Collider;event.ColliderBUUID=y->Collider;event.Normal=V(manifold.mWorldSpaceNormal);event.RelativeVelocity=V(b.GetLinearVelocity()-a.GetLinearVelocity());
        const auto count=std::min<unsigned>(manifold.mRelativeContactPointsOn1.size(),64);for(unsigned i=0;i<count;++i)event.Contacts.push_back({V(manifold.GetWorldSpaceContactPointOn1(i)),V(manifold.GetWorldSpaceContactPointOn2(i)),manifold.mPenetrationDepth});
        Pair pair{a.GetID().GetIndexAndSequenceNumber(),b.GetID().GetIndexAndSequenceNumber(),manifold.mSubShapeID1.GetValue(),manifold.mSubShapeID2.GetValue()};
        std::lock_guard lock(EventMutex);const auto key=ContactKey(event);auto& pairCount=ContactCounts[key];const bool entering=pairCount==0;if(!Contacts.contains(pair))++pairCount;if(!entering)event.Type=settings.mIsSensor?PhysicsEventType::TriggerStay:PhysicsEventType::CollisionStay;Contacts[pair]=event;Queue(std::move(event));
    }
    void OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,JPH::ContactSettings& s)override{Contact(a,b,m,s,false);}
    void OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,JPH::ContactSettings& s)override{Contact(a,b,m,s,true);}
    void OnContactRemoved(const JPH::SubShapeIDPair& p)override{
        Pair rawKey{p.GetBody1ID().GetIndexAndSequenceNumber(),p.GetBody2ID().GetIndexAndSequenceNumber(),p.GetSubShapeID1().GetValue(),p.GetSubShapeID2().GetValue()};
        std::lock_guard lock(EventMutex);auto found=Contacts.find(rawKey);if(found==Contacts.end())return;auto event=found->second;event.Type=event.Type==PhysicsEventType::TriggerEnter||event.Type==PhysicsEventType::TriggerStay?PhysicsEventType::TriggerExit:PhysicsEventType::CollisionExit;event.Contacts.clear();const auto key=ContactKey(event);Contacts.erase(found);auto count=ContactCounts.find(key);if(count!=ContactCounts.end()&&--count->second>0)return;ContactCounts.erase(key);Queue(std::move(event));
    }
    void Dispatch(){
        Dispatching=true;struct Reset{bool& Flag;~Reset(){Flag=false;}} reset{Dispatching};
        std::vector<CollisionEvent> events;{std::lock_guard lock(EventMutex);events.swap(Events);Queued.clear();if(Overflow){Overflow=false;throw std::runtime_error("Physics contact queue exceeded 65536 events");}}
        for(auto& event:events){if(Dead)return;event.DeltaTime=LastDelta;event.BodyA=ScenePtr->GetEntity(event.BodyAUUID);event.BodyB=ScenePtr->GetEntity(event.BodyBUUID);event.ColliderA=ScenePtr->GetEntity(event.ColliderAUUID);event.ColliderB=ScenePtr->GetEntity(event.ColliderBUUID);
            std::vector<std::uint64_t> ids;for(auto& [id,s]:Subscriptions)if(s->Type==event.Type)ids.push_back(id);
            for(auto id:ids){if(Dead)return;auto it=Subscriptions.find(id);if(it==Subscriptions.end())continue;auto s=it->second;if(s->Filter&&s->Filter!=event.BodyAUUID&&s->Filter!=event.BodyBUUID&&s->Filter!=event.ColliderAUUID&&s->Filter!=event.ColliderBUUID)continue;
                if(s->Filter&&!ScenePtr->GetEntity(s->Filter))continue;
                auto previous=Detail::SetCurrentGameplayModule(s->Lease);auto owner=ApplicationEventsAccess::SetOwner(s->Owner);auto level=ApplicationEventsAccess::SetSubsystemLevel(s->Level);try{s->Callback(event);}catch(...){ApplicationEventsAccess::SetOwner(owner);ApplicationEventsAccess::SetSubsystemLevel(level);Detail::SetCurrentGameplayModule(std::move(previous));throw;}ApplicationEventsAccess::SetOwner(owner);ApplicationEventsAccess::SetSubsystemLevel(level);Detail::SetCurrentGameplayModule(std::move(previous));
            }
        }
    }
    void ClearExtras();
    void DetachJoints(JPH::BodyID);
    void SyncExtras();
    void UpdateExtras(float);
    void DrawSoftBodies();
    void Synchronize();
    void Step(float dt,bool manual);
};
struct Service {
    std::map<Scene*,std::shared_ptr<World>> Worlds;
    std::thread::id MainThread=std::this_thread::get_id();
    std::uint64_t NextSubscription=1;
    Service(){JPH::RegisterDefaultAllocator();JPH::Factory::sInstance=new JPH::Factory();JPH::RegisterTypes();}
    ~Service(){Worlds.clear();JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}
};
Service& State(){static Service service;if(service.MainThread!=std::this_thread::get_id())throw std::logic_error("Physics APIs must be called on the main thread");return service;}
std::shared_ptr<World> Get(Scene& scene){Detail::RequireSubsystemService(Detail::SubsystemService::Physics,"Physics");auto& worlds=State().Worlds;auto it=worlds.find(&scene);if(it!=worlds.end())return it->second;auto world=std::make_shared<World>(scene);worlds.emplace(&scene,world);return world;}
std::string Stamp(const auto& value){return PhysicsKey(value);}
Vec3 PoseKey(Vec3 v){return {std::round(v.X*100000.f)/100000.f,std::round(v.Y*100000.f)/100000.f,std::round(v.Z*100000.f)/100000.f};}
Quaternion PoseKey(Quaternion q){q=q.Normalized();if(q.W<0)q={-q.X,-q.Y,-q.Z,-q.W};return {std::round(q.X*1000000.f)/1000000.f,std::round(q.Y*1000000.f)/1000000.f,std::round(q.Z*1000000.f)/1000000.f,std::round(q.W*1000000.f)/1000000.f};}
struct ColliderBuild {Entity EntityValue;UUID Root;bool Mesh=false;Transform Local;std::string Signature,MeshKey;};

std::string MeshCookKey(const ColliderBuild& part,const MeshCollider& c){auto key=std::string("mesh")+part.MeshKey;AppendPhysicsKey(key,part.Local.Scale);AppendPhysicsKey(key,c.Convex);AppendPhysicsKey(key,c.ConvexRadius);AppendPhysicsKey(key,c.DoubleSided);AppendPhysicsKey(key,c.Sanitize);return key;}
JPH::RefConst<JPH::Shape> CookMesh(const ColliderBuild& part,const MeshCollider& c,BodyMotionType motion){
    if(!c.Convex&&motion==BodyMotionType::Dynamic)throw std::invalid_argument("Dynamic MeshCollider must be convex");
    struct Cached {std::string Data;JPH::PhysicsMaterialList Materials;std::size_t Bytes;};static std::map<std::string,Cached> cache;static std::size_t bytes=0;constexpr std::size_t Limit=64u*1024u*1024u;
    const auto key=MeshCookKey(part,c);
    if(auto found=cache.find(key);found!=cache.end()){
        // Restore the cooked tree/hull, never share mutable per-collider user data.
        std::istringstream stream(found->second.Data,std::ios::binary);JPH::StreamInWrapper input(stream);
        auto result=JPH::Shape::sRestoreFromBinaryState(input);if(result.HasError())throw std::runtime_error("Could not restore cached collision geometry");
        auto restored=result.Get();restored->RestoreMaterialState(found->second.Materials.data(),JPH::uint(found->second.Materials.size()));return restored;
    }
    std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
    if(!ReadPhysicsMesh(part.EntityValue,c.MeshAsset,part.MeshKey,vertices,indices)||vertices.empty())throw std::runtime_error("MeshCollider requires valid attached geometry or a mesh asset");
    JPH::Array<JPH::Vec3> points;points.reserve(vertices.size());for(auto p:vertices){auto point=p*part.Local.Scale;RequireVector(point);points.push_back(V(point));}
    JPH::RefConst<JPH::Shape> shape;
    if(c.Convex){
        struct PointHash {std::size_t operator()(const std::array<std::uint32_t,3>& value)const{std::size_t result=2166136261u;for(auto coordinate:value)result=(result^coordinate)*16777619u;return result;}};
        std::unordered_set<std::array<std::uint32_t,3>,PointHash> seen;seen.reserve(points.size());std::size_t count=0;
        for(auto point:points){const auto bits=[](float v){return std::bit_cast<std::uint32_t>(v==0?0.f:v);};if(seen.insert({bits(point.GetX()),bits(point.GetY()),bits(point.GetZ())}).second)points[count++]=point;}
        points.resize(count);
        shape=ShapeResult(JPH::ConvexHullShapeSettings(points,c.ConvexRadius).Create());
    }else{
        JPH::TriangleList triangles;triangles.reserve(indices.size()/3*(c.DoubleSided?2:1));
        for(std::size_t i=0;i+2<indices.size();i+=3){if(indices[i]>=points.size()||indices[i+1]>=points.size()||indices[i+2]>=points.size())throw std::invalid_argument("Invalid mesh collider indices");triangles.emplace_back(points[indices[i]],points[indices[i+1]],points[indices[i+2]]);if(c.DoubleSided)triangles.emplace_back(points[indices[i]],points[indices[i+2]],points[indices[i+1]]);}
        JPH::MeshShapeSettings settings(triangles);if(c.Sanitize)settings.Sanitize();shape=ShapeResult(settings.Create());
    }
    std::ostringstream stream(std::ios::binary);JPH::StreamOutWrapper output(stream);shape->SaveBinaryState(output);
    auto data=stream.str();const auto used=data.size()+key.size();
    if(used<=Limit&&!output.IsFailed()){while(!cache.empty()&&(bytes+used>Limit||cache.size()>=128)){bytes-=cache.begin()->second.Bytes;cache.erase(cache.begin());}JPH::PhysicsMaterialList materials;shape->SaveMaterialState(materials);cache.emplace(key,Cached{std::move(data),std::move(materials),used});bytes+=used;}
    return shape;
}

void World::Synchronize(){
    if(Dead)return;if(Stepping)throw std::logic_error("Cannot synchronize physics during simulation");Configure();
    std::map<UUID,std::vector<ColliderBuild>> groups;PhysicsMeshRevisions revisions;
    const auto collect=[&](Entity entity,bool mesh){
        Entity owner=entity;for(auto parent=entity;parent;parent=parent.GetParent())if(parent.HasComponent<Rigidbody>()){if(!parent.GetComponent<Rigidbody>().Enabled)return;owner=parent;break;}
        if(!entity.GetComponent<Transform>().Enabled||owner.GetUUID().IsRoot())return;
        auto rootPose=owner.GetWorldTransform();RequireVector(rootPose.Position);(void)Q(rootPose.Rotation);Mat4 relative;for(auto current=entity;current&&current!=owner;current=current.GetParent())relative=current.GetComponent<Transform>().GetMatrix()*relative;relative=Mat4::Scaling(rootPose.Scale)*relative;Mat4 matrix;
        std::string signature;
        if(mesh){const auto& c=entity.GetComponent<MeshCollider>();if(!c.Enabled)return;if(!c.IsValid())throw std::invalid_argument("Invalid MeshCollider on "+entity.GetUUID().ToString());matrix=relative*Mat4::Transform(c.Offset,c.Rotation,c.Scale);VisitMeshCollider(c,[&](const char*,const char*,const auto& field){signature+=Stamp(field)+";";});}
        else{const auto& c=entity.GetComponent<ShapeCollider>();if(!c.Enabled)return;if(!c.IsValid())throw std::invalid_argument("Invalid ShapeCollider on "+entity.GetUUID().ToString());matrix=relative*Mat4::Transform(c.Offset,c.Rotation,c.Scale);VisitShapeCollider(c,[&](const char*,const char*,const auto& field){signature+=Stamp(field)+";";});}
        Transform local;if(!matrix.Decompose(local.Position,local.Rotation,local.Scale)||local.Scale.X<=0||local.Scale.Y<=0||local.Scale.Z<=0)throw std::invalid_argument("Physics colliders require non-singular positive world scale");
        RequireVector(local.Position);RequireVector(local.Scale);(void)Q(local.Rotation);
        std::string meshKey;if(mesh){const auto& collider=entity.GetComponent<MeshCollider>();meshKey=PhysicsMeshSignature(entity,collider.MeshAsset,revisions);if(meshKey.empty()&&!collider.MeshAsset)return;signature+=meshKey;}
        signature+=Stamp(PoseKey(local.Position))+";"+Stamp(PoseKey(local.Rotation))+";"+Stamp(PoseKey(local.Scale));
        groups[owner.GetUUID()].push_back({entity,owner.GetUUID(),mesh,local,signature,meshKey});
    };
    for(auto handle:ScenePtr->GetRegistry().view<ShapeCollider,Transform>())collect(ScenePtr->GetEntity(static_cast<Entity::Id>(handle)),false);
    for(auto handle:ScenePtr->GetRegistry().view<MeshCollider,Transform>())collect(ScenePtr->GetEntity(static_cast<Entity::Id>(handle)),true);
    for(auto handle:ScenePtr->GetRegistry().view<Rigidbody,Transform>()){auto entity=ScenePtr->GetEntity(static_cast<Entity::Id>(handle));if(!entity.GetUUID().IsRoot()&&entity.GetComponent<Rigidbody>().Enabled&&entity.GetComponent<Transform>().Enabled)groups.try_emplace(entity.GetUUID());}
    auto& interface=System.GetBodyInterface();std::set<UUID> retained;
    for(auto it=Bodies.begin();it!=Bodies.end();)if(!groups.contains(it->first)){DetachJoints(it->second.Id);interface.RemoveBody(it->second.Id);interface.DestroyBody(it->second.Id);BodyOwners.erase(it->second.Id.GetIndexAndSequenceNumber());for(auto key:it->second.Shapes)Colliders.erase(key);it=Bodies.erase(it);}else ++it;
    for(auto& [uuid,parts]:groups){
        auto entity=ScenePtr->GetEntity(uuid);Rigidbody body;const auto* authored=entity.TryGetComponent<Rigidbody>();if(authored){body=*authored;if(!body.IsValid())throw std::invalid_argument("Invalid Rigidbody on "+uuid.ToString());}else body.MotionType=BodyMotionType::Static;
        std::string signature;VisitBody(body,[&](const char*,const char*,const auto& field){signature+=Stamp(field)+";";});for(const auto& c:parts)signature+=Stamp(c.EntityValue.GetUUID())+c.Signature;
        auto pose=entity.GetWorldTransform();auto old=Bodies.find(uuid);retained.insert(uuid);
        if(old!=Bodies.end()&&old->second.Signature==signature){
            if(!Same(pose,old->second.LastScene)){interface.SetPositionAndRotation(old->second.Id,V(pose.Position),Q(pose.Rotation),body.StartAwake?JPH::EActivation::Activate:JPH::EActivation::DontActivate);old->second.LastScene=pose;old->second.PreviousScene=pose;}
            continue;
        }
        JPH::StaticCompoundShapeSettings compound;std::vector<std::uint64_t> shapes;bool anySolid=false,anyTrigger=false,hasConcave=false;
        struct ShapeGuard {std::map<std::uint64_t,ColliderData>& Data;std::vector<std::uint64_t>& Keys;bool Committed=false;~ShapeGuard(){if(!Committed)for(auto key:Keys)Data.erase(key);}} shapeGuard{Colliders,shapes};
        for(const auto& part:parts){
            ColliderData data;data.Body=uuid;data.Collider=part.EntityValue.GetUUID();JPH::RefConst<JPH::Shape> shape;
            if(part.Mesh){const auto& c=part.EntityValue.GetComponent<MeshCollider>();shape=CookMesh(part,c,body.MotionType);data.GeometryKey=MeshCookKey(part,c);
                hasConcave|=!c.Convex;
                data.Layer=c.Layer;data.Group=c.CollisionGroup;data.Trigger=c.IsTrigger;data.Friction=c.Friction;data.Restitution=c.Restitution;data.DoubleSided=c.DoubleSided;
            }else{const auto& c=part.EntityValue.GetComponent<ShapeCollider>();PhysicsShape s;s.Shape=c.Shape;s.Size=c.Size;s.Radius=c.Radius;s.Height=c.Height;if(c.Shape==ColliderShape::Plane&&body.MotionType!=BodyMotionType::Static)throw std::invalid_argument("Plane Collider requires a static body");shape=Shape(s,part.Local.Scale,c.ConvexRadius);if(c.Shape==ColliderShape::Plane)hasConcave=true;data.Layer=c.Layer;data.Group=c.CollisionGroup;data.Trigger=c.IsTrigger;data.Friction=c.Friction;data.Restitution=c.Restitution;}
            if(data.GeometryKey.empty())data.GeometryKey="shape"+part.Signature+Stamp(part.Local.Scale);
            auto key=NextShape++;const_cast<JPH::Shape*>(shape.GetPtr())->SetUserData(key);Colliders.emplace(key,data);Colliders.at(key).NativeShape=shape;Colliders.at(key).Local=part.Local;shapes.push_back(key);anyTrigger|=data.Trigger;anySolid|=!data.Trigger;
            compound.AddShape(V(part.Local.Position),Q(part.Local.Rotation),shape);
        }
        auto shape=parts.empty()?ShapeResult(JPH::EmptyShapeSettings().Create()):ShapeResult(compound.Create());if(body.CenterOfMassOffset.LengthSquared()>0)shape=ShapeResult(JPH::OffsetCenterOfMassShapeSettings(V(body.CenterOfMassOffset*pose.Scale),shape).Create());
        std::uint16_t objectLayer=parts.empty()?64:std::uint16_t(Colliders.at(shapes.front()).Layer);for(auto key:shapes)if(Colliders.at(key).Layer!=(objectLayer&31)){objectLayer=64;break;}if(body.MotionType==BodyMotionType::Static)objectLayer|=32;
        JPH::BodyCreationSettings settings(shape,V(pose.Position),Q(pose.Rotation),JPH::EMotionType(int(body.MotionType)),objectLayer);
        settings.mAllowDynamicOrKinematic=!hasConcave;settings.mAllowedDOFs=JPH::EAllowedDOFs(63^int(body.Constraints));settings.mIsSensor=anyTrigger&&!anySolid;settings.mCollideKinematicVsNonDynamic=body.CollideKinematicVsNonDynamic||anyTrigger;
        settings.mUseManifoldReduction=body.UseManifoldReduction&&parts.size()==1;settings.mApplyGyroscopicForce=body.ApplyGyroscopicForce;settings.mMotionQuality=body.CollisionDetection==CollisionDetectionMode::Continuous?JPH::EMotionQuality::LinearCast:JPH::EMotionQuality::Discrete;
        settings.mEnhancedInternalEdgeRemoval=body.EnhancedInternalEdgeRemoval;settings.mAllowSleeping=body.AllowSleeping&&Settings.AllowSleeping;
        settings.mLinearDamping=body.LinearDamping;settings.mAngularDamping=body.AngularDamping;settings.mMaxLinearVelocity=body.MaxLinearVelocity;settings.mMaxAngularVelocity=body.MaxAngularVelocity;settings.mGravityFactor=body.Gravity?0:body.GravityFactor;
        settings.mNumVelocityStepsOverride=body.VelocityIterations;settings.mNumPositionStepsOverride=body.PositionIterations;
        if(body.MotionType==BodyMotionType::Dynamic){settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;settings.mMassPropertiesOverride.mMass=body.Mass;settings.mInertiaMultiplier=body.InertiaMultiplier;}
        if(hasConcave&&body.MotionType==BodyMotionType::Kinematic){settings.mOverrideMassProperties=JPH::EOverrideMassProperties::MassAndInertiaProvided;settings.mMassPropertiesOverride.mMass=body.Mass;settings.mMassPropertiesOverride.mInertia=JPH::Mat44::sIdentity();}
        settings.mLinearVelocity=V(body.LinearVelocity);settings.mAngularVelocity=V(body.AngularVelocity);
        if(settings.HasMassProperties()){auto properties=settings.GetMassProperties();if(!std::isfinite(properties.mMass)||properties.mMass<=0)throw std::invalid_argument("Collider mass properties are outside the supported numeric range");for(int row=0;row<3;++row)for(int column=0;column<3;++column)if(!std::isfinite(properties.mInertia(row,column))||(row==column&&properties.mInertia(row,column)<=0))throw std::invalid_argument("Collider inertia is outside the supported numeric range");}
        bool wasAwake=body.StartAwake;
        if(old!=Bodies.end()){
            if(old->second.Authored.LinearVelocity==body.LinearVelocity)settings.mLinearVelocity=interface.GetLinearVelocity(old->second.Id);
            if(old->second.Authored.AngularVelocity==body.AngularVelocity)settings.mAngularVelocity=interface.GetAngularVelocity(old->second.Id);
            wasAwake=interface.IsActive(old->second.Id)||old->second.Authored.Gravity!=body.Gravity||old->second.Authored.GravityFactor!=body.GravityFactor||old->second.Authored.MotionType!=body.MotionType;DetachJoints(old->second.Id);interface.RemoveBody(old->second.Id);interface.DestroyBody(old->second.Id);BodyOwners.erase(old->second.Id.GetIndexAndSequenceNumber());for(auto key:old->second.Shapes)Colliders.erase(key);Bodies.erase(old);
        }
        auto* native=interface.CreateBody(settings);if(!native)throw std::runtime_error("Scene exceeded Max Bodies physics capacity");auto id=native->GetID();interface.AddBody(id,wasAwake&&body.MotionType!=BodyMotionType::Static?JPH::EActivation::Activate:JPH::EActivation::DontActivate);
        Bodies.emplace(uuid,BodyEntry{id,uuid,std::move(signature),pose,pose,body,bool(authored),shapes});shapeGuard.Committed=true;BodyOwners[id.GetIndexAndSequenceNumber()]=uuid;
    }
    for(auto it=Bodies.begin();it!=Bodies.end();)if(!retained.contains(it->first)){DetachJoints(it->second.Id);interface.RemoveBody(it->second.Id);interface.DestroyBody(it->second.Id);BodyOwners.erase(it->second.Id.GetIndexAndSequenceNumber());for(auto key:it->second.Shapes)Colliders.erase(key);it=Bodies.erase(it);}else ++it;
    SyncExtras();TopologyDirty=false;
}
void World::Step(float dt,bool manual){
    if(!std::isfinite(dt)||dt<=0||dt>1)throw std::invalid_argument("Physics step must be finite, greater than zero and at most one second");
    if(Stepping||Dispatching||Updating)throw std::logic_error("Recursive physics simulation is not allowed");
    if(!manual&&!ScenePtr->GetPhysicsSettings().AutoSimulation)return;
    Updating=true;struct UpdateReset{bool& Flag;~UpdateReset(){Flag=false;}} updating{Updating};
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBeforePhysicsStep,dt});ApplicationEventsAccess::ThrowIfFailed();
    Synchronize();LastDelta=dt;Stepping=true;struct Reset{bool& Flag;~Reset(){Flag=false;}} reset{Stepping};
    auto& interface=System.GetBodyInterface();
    for(auto& [id,entry]:SoftBodies){auto* body=System.GetBodyLockInterfaceNoLock().TryGetBody(entry.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());if(entry.Wind.LengthSquared()>0){for(auto& v:motion->GetVertices())if(v.mInvMass>0)v.mVelocity+=(body->GetRotation().Conjugated()*V(entry.Wind))*dt;interface.ActivateBody(entry.Id);}}
    auto forces=std::move(Forces);Forces.clear();for(const auto& force:forces){auto found=Bodies.find(force.Owner);auto entity=ScenePtr->GetEntity(force.Owner);if(found==Bodies.end()||!entity||entity.GetId()!=force.Generation||interface.GetMotionType(found->second.Id)!=JPH::EMotionType::Dynamic)continue;switch(force.Command){case PhysicsCommand::Force:interface.AddForce(found->second.Id,V(force.Value));break;case PhysicsCommand::ForceAtPosition:interface.AddForce(found->second.Id,V(force.Value),V(force.Point));break;case PhysicsCommand::Torque:interface.AddTorque(found->second.Id,V(force.Value));break;default:break;}}
    for(auto& [uuid,entry]:Bodies){auto entity=ScenePtr->GetEntity(uuid);auto* body=entity.TryGetComponent<Rigidbody>();if(body&&body->MotionType==BodyMotionType::Dynamic&&body->Gravity&&interface.IsActive(entry.Id)){const auto acceleration=*body->Gravity*body->GravityFactor;if(acceleration.LengthSquared()>1e-12f)interface.AddForce(entry.Id,V(acceleration*body->Mass),JPH::EActivation::DontActivate);}}
    for(auto& [id,entry]:Bodies)entry.PreviousScene=entry.LastScene;
    const auto error=System.Update(dt,int(Settings.CollisionSteps),&Allocator,Jobs.get());if(error!=JPH::EPhysicsUpdateError::None)throw std::runtime_error("Jolt update exhausted physics capacity; increase scene physics limits");
    std::vector<UUID> order;for(auto& [id,entry]:Bodies)order.push_back(id);
    const auto depth=[&](UUID id){int count=0;for(auto e=ScenePtr->GetEntity(id);e&&count<1024;e=e.GetParent())++count;return count;};
    std::stable_sort(order.begin(),order.end(),[&](UUID a,UUID b){return depth(a)<depth(b);});
    for(auto id:order){auto& entry=Bodies.at(id);auto entity=ScenePtr->GetEntity(id);if(!entity||entry.Authored.MotionType==BodyMotionType::Static)continue;auto transform=entity.GetWorldTransform();transform.Position=V(interface.GetPosition(entry.Id));transform.Rotation=Q(interface.GetRotation(entry.Id));if(!ScenePtr->SetWorldTransform(entity,transform))throw std::runtime_error("Cannot synchronize physics under a singular parent transform");entry.LastScene=transform;}
    UpdateExtras(dt);Stepping=false;Dispatch();ApplicationEventsAccess::Emit({ApplicationEventType::OnAfterPhysicsStep,dt});ApplicationEventsAccess::ThrowIfFailed();
}
struct QueryFilter final:JPH::ShapeFilter {
    World& W;const PhysicsQueryOptions& O;
    QueryFilter(World& w,const PhysicsQueryOptions& o):W(w),O(o){}
    bool Accept(const ColliderData& data)const{return (O.LayerMask&(1u<<data.Layer))&&(O.IncludeTriggers||!data.Trigger)&&std::find(O.ExcludedEntities.begin(),O.ExcludedEntities.end(),data.Body)==O.ExcludedEntities.end()&&std::find(O.ExcludedEntities.begin(),O.ExcludedEntities.end(),data.Collider)==O.ExcludedEntities.end();}
    bool ShouldCollide(const JPH::Shape* shape,const JPH::SubShapeID& sub)const override{auto it=W.Colliders.find(shape->GetUserData());return it==W.Colliders.end()||Accept(it->second);}
    bool ShouldCollide(const JPH::Shape*,const JPH::SubShapeID&,const JPH::Shape* shape,const JPH::SubShapeID& sub)const override{return ShouldCollide(shape,sub);}
};
template<class Base> struct Hits final:Base {
    using Result=typename Base::ResultType;
    World& W;const QueryFilter& Filter;std::vector<Result> Values;std::vector<UUID> Keys;std::uint32_t Limit;
    Hits(World& world,const QueryFilter& filter,std::uint32_t limit):W(world),Filter(filter),Limit(limit){}
    void AddHit(const Result& hit)override{
        const auto body=[&](){if constexpr(requires{hit.mBodyID;})return hit.mBodyID;else return hit.mBodyID2;}();
        // Queries run on the main thread outside Update; no body can mutate here.
        auto shape=W.System.GetBodyInterfaceNoLock().GetShape(body);auto found=W.Colliders.find(shape->GetSubShapeUserData(hit.mSubShapeID2));
        const ColliderData* data=found==W.Colliders.end()?nullptr:&found->second;if(auto soft=W.SoftColliders.find(body.GetIndexAndSequenceNumber());soft!=W.SoftColliders.end())data=&soft->second;if(!data||!Filter.Accept(*data))return;auto key=data->Collider;
        auto existing=std::find(Keys.begin(),Keys.end(),key);
        if(existing!=Keys.end()){auto index=std::size_t(existing-Keys.begin());if(hit.GetEarlyOutFraction()<Values[index].GetEarlyOutFraction())Values[index]=hit;}
        else if(Values.size()<Limit){Values.push_back(hit);Keys.push_back(key);}
        else{auto worst=std::max_element(Values.begin(),Values.end(),[](const auto& a,const auto& b){return a.GetEarlyOutFraction()<b.GetEarlyOutFraction();});if(hit.GetEarlyOutFraction()<worst->GetEarlyOutFraction()){Keys[std::size_t(worst-Values.begin())]=key;*worst=hit;}}
        if(Values.size()==Limit){float furthest=Values.front().GetEarlyOutFraction();for(auto& v:Values)furthest=std::max(furthest,v.GetEarlyOutFraction());this->UpdateEarlyOutFraction(furthest);}
    }
};
PhysicsHit Hit(World& w,const JPH::BodyID& id,const JPH::SubShapeID& sub,Vec3 point,Vec3 normal,float distance,float fraction,const QueryFilter& filter){
    JPH::BodyLockRead lock(w.System.GetBodyLockInterface(),id);if(!lock.Succeeded())return {};const auto& body=lock.GetBody();auto* data=w.Data(body,sub);if(!data||!filter.Accept(*data))return {};
    if(normal.LengthSquared()<1e-12f)normal=V(body.GetWorldSpaceSurfaceNormal(sub,V(point)));
    return {w.ScenePtr->GetEntity(data->Body),w.ScenePtr->GetEntity(data->Collider),point,normal.Normalized(),distance,fraction,sub.GetValue()};
}
std::vector<PhysicsHit> Query(Scene& scene,int mode,const PhysicsShape& shape,const Transform& transform,Vec3 direction,float distance,const PhysicsQueryOptions& options){
    RequireVector(transform.Position);RequireVector(direction);if(options.MaxResults<1||options.MaxResults>65536||!std::isfinite(distance)||distance<0)throw std::invalid_argument("Invalid physics query limits");
    if(mode!=0&&shape.Shape==ColliderShape::Plane)throw std::invalid_argument("Shape queries require a finite box, sphere, capsule or cylinder");
    if(mode!=2&&(distance<=0||direction.LengthSquared()<1e-12f))return {};
    auto w=Get(scene);if(!w->Running||w->TopologyDirty)w->Synchronize();QueryFilter filter(*w,options);std::vector<PhysicsHit> result;const auto movement=direction.Normalized()*distance;
    const auto backfaces=options.IncludeBackFaces?JPH::EBackFaceMode::CollideWithBackFaces:JPH::EBackFaceMode::IgnoreBackFaces;
    if(mode==0){Hits<JPH::CastRayCollector> hits(*w,filter,options.MaxResults);JPH::RayCastSettings settings;settings.SetBackFaceMode(backfaces);w->System.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(V(transform.Position),V(movement)),settings,hits,{}, {},{},filter);for(const auto& h:hits.Values){auto hit=Hit(*w,h.mBodyID,h.mSubShapeID2,transform.Position+movement*h.mFraction,{},distance*h.mFraction,h.mFraction,filter);if(hit)result.push_back(hit);}}
    else {auto native=Shape(shape,transform.Scale);auto matrix=JPH::RMat44::sRotationTranslation(Q(transform.Rotation),V(transform.Position));
        if(mode==1){Hits<JPH::CastShapeCollector> hits(*w,filter,options.MaxResults);JPH::ShapeCastSettings settings;settings.SetBackFaceMode(backfaces);auto cast=JPH::RShapeCast::sFromWorldTransform(native,JPH::Vec3::sReplicate(1),matrix,V(movement));w->System.GetNarrowPhaseQuery().CastShape(cast,settings,JPH::RVec3::sZero(),hits,{}, {},{},filter);for(const auto& h:hits.Values){auto hit=Hit(*w,h.mBodyID2,h.mSubShapeID2,V(h.mContactPointOn2),-V(h.mPenetrationAxis),distance*h.mFraction,h.mFraction,filter);if(hit)result.push_back(hit);}}
        else{Hits<JPH::CollideShapeCollector> hits(*w,filter,options.MaxResults);JPH::CollideShapeSettings settings;settings.mBackFaceMode=backfaces;matrix=matrix.PreTranslated(native->GetCenterOfMass());w->System.GetNarrowPhaseQuery().CollideShape(native,JPH::Vec3::sReplicate(1),matrix,settings,JPH::RVec3::sZero(),hits,{}, {},{},filter);for(const auto& h:hits.Values){auto hit=Hit(*w,h.mBodyID2,h.mSubShapeID2,V(h.mContactPointOn2),-V(h.mPenetrationAxis),0,0,filter);if(hit)result.push_back(hit);}}
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.Distance!=b.Distance?a.Distance<b.Distance:a.Object.GetUUID()!=b.Object.GetUUID()?a.Object.GetUUID()<b.Object.GetUUID():a.Collider.GetUUID()<b.Collider.GetUUID();});return result;
}
BodyEntry* BodyFor(World& w,Entity e){if(e.GetScene()!=w.ScenePtr||!e)return nullptr;for(auto current=e;current;current=current.GetParent()){if(auto* body=current.TryGetComponent<Rigidbody>();body&&!body->Enabled)return nullptr;auto found=w.Bodies.find(current.GetUUID());if(found!=w.Bodies.end())return &found->second;if(current.HasComponent<Rigidbody>())return nullptr;}return nullptr;}
bool Command(Scene& scene,Entity e,PhysicsCommand command,Vec3 value,Vec3 point,Quaternion rotation,float dt,bool activate){
    RequireVector(value);RequireVector(point);auto w=Get(scene);if(!w->Running)throw std::logic_error("Physics body control requires a running simulation");if(w->TopologyDirty)w->Synchronize();auto* entry=BodyFor(*w,e);if(!entry)return false;auto& body=w->System.GetBodyInterface();const auto id=entry->Id;const auto motion=body.GetMotionType(id);
    const bool dynamic=motion==JPH::EMotionType::Dynamic;
    switch(command){
    case PhysicsCommand::Force:case PhysicsCommand::ForceAtPosition:case PhysicsCommand::Torque:if(!dynamic)return false;if(w->Forces.size()>=65536)throw std::length_error("Physics force queue exceeded limit; advance simulation before submitting more forces");w->Forces.push_back({entry->Owner,scene.GetEntity(entry->Owner).GetId(),command,value,point});break;
    case PhysicsCommand::Impulse:if(!dynamic)return false;body.AddImpulse(id,V(value));break;
    case PhysicsCommand::ImpulseAtPosition:if(!dynamic)return false;body.AddImpulse(id,V(value),V(point));break;
    case PhysicsCommand::AngularImpulse:if(!dynamic)return false;body.AddAngularImpulse(id,V(value));break;
    case PhysicsCommand::LinearVelocity:if(motion==JPH::EMotionType::Static)return false;body.SetLinearVelocity(id,V(value));break;
    case PhysicsCommand::AngularVelocity:if(motion==JPH::EMotionType::Static)return false;body.SetAngularVelocity(id,V(value));break;
    case PhysicsCommand::MoveKinematic:if(!std::isfinite(rotation.LengthSquared())||rotation.LengthSquared()<1e-12f)return false;if(motion!=JPH::EMotionType::Kinematic||!std::isfinite(dt)||dt<=0)return false;body.MoveKinematic(id,V(value),Q(rotation),dt);break;
    case PhysicsCommand::Teleport:{if(!std::isfinite(rotation.LengthSquared())||rotation.LengthSquared()<1e-12f)return false;body.SetPositionAndRotation(id,V(value),Q(rotation),activate?JPH::EActivation::Activate:JPH::EActivation::DontActivate);auto entity=scene.GetEntity(entry->Owner);auto transform=entity.GetWorldTransform();transform.Position=value;transform.Rotation=rotation.Normalized();if(!scene.SetWorldTransform(entity,transform))return false;entry->LastScene=transform;entry->PreviousScene=transform;break;}
    case PhysicsCommand::Wake:if(motion==JPH::EMotionType::Static)return false;body.ActivateBody(id);break;
    case PhysicsCommand::Sleep:if(!dynamic)return false;body.DeactivateBody(id);break;
    }return true;
}
}
static Mat4 RenderMatrix(Scene& scene,Entity entity){
    auto world=State().Worlds.find(&scene);if(world==State().Worlds.end()||!world->second->Running||!world->second->Settings.AutoSimulation)return entity.GetWorldMatrix();auto& w=*world->second;auto* entry=BodyFor(w,entity);
    if(!entry||!entry->HasRigidbody||entry->Authored.Interpolation==BodyInterpolation::None)return entity.GetWorldMatrix();
    auto root=scene.GetEntity(entry->Owner);if(!root||!Same(root.GetWorldTransform(),entry->LastScene))return entity.GetWorldMatrix();
    auto interpolated=entry->LastScene;interpolated.Position=entry->PreviousScene.Position*(1-w.Alpha)+entry->LastScene.Position*w.Alpha;interpolated.Rotation=Quaternion::Slerp(entry->PreviousScene.Rotation,entry->LastScene.Rotation,w.Alpha);
    Mat4 inverse;if(!root.GetWorldMatrix().TryInverse(inverse))return entity.GetWorldMatrix();return interpolated.GetMatrix()*inverse*entity.GetWorldMatrix();
}
static std::shared_ptr<const GuideLines> GuideGeometry(const JPH::Shape& shape,const std::string& key){
    static std::map<std::string,std::shared_ptr<const GuideLines>> cache;static std::size_t bytes=0;constexpr std::size_t Limit=32u*1024u*1024u;
    if(auto found=cache.find(key);found!=cache.end())return found->second;
    auto lines=std::make_shared<GuideLines>();JPH::Shape::GetTrianglesContext context;const auto com=shape.GetCenterOfMass();
    const auto box=shape.GetSubType()==JPH::EShapeSubType::Plane?JPH::AABox(com-JPH::Vec3::sReplicate(10),com+JPH::Vec3::sReplicate(10)):JPH::AABox::sBiggest();
    shape.GetTrianglesStart(context,box,com,JPH::Quat::sIdentity(),JPH::Vec3::sReplicate(1));
    const auto stride=std::max(1u,(shape.GetStats().mNumTriangles+4095u)/4096u);std::uint64_t index=0;
    JPH::Float3 points[96];for(;;){const int triangles=shape.GetTrianglesNext(context,32,points);if(!triangles)break;for(int i=0;i<triangles;++i,++index){if(index%stride||lines->size()>=12288)continue;for(int edge=0;edge<3;++edge){const auto& a=points[i*3+edge];const auto& b=points[i*3+(edge+1)%3];lines->push_back({{a.x,a.y,a.z},{b.x,b.y,b.z}});}}}
    const auto used=lines->size()*sizeof(GuideLines::value_type)+key.size();if(used<=Limit){while(!cache.empty()&&(bytes+used>Limit||cache.size()>=256)){bytes-=cache.begin()->second->size()*sizeof(GuideLines::value_type)+cache.begin()->first.size();cache.erase(cache.begin());}cache.emplace(key,lines);bytes+=used;}return lines;
}
#include "Physics/PhysicsExtras.inl"
static std::vector<std::array<float,10>> Guides(Scene& scene,UUID selected){

    auto w=Get(scene);if(w->TopologyDirty)w->Synchronize();std::vector<std::array<float,10>> lines;
    const auto count=std::count_if(w->Colliders.begin(),w->Colliders.end(),[&](const auto& item){return item.second.Body==selected||item.second.Collider==selected;});
    const auto budget=std::max<std::size_t>(3,12288/std::max<std::size_t>(1,count));
    for(auto& [key,data]:w->Colliders){if(data.Body!=selected&&data.Collider!=selected)continue;auto found=w->Bodies.find(data.Body);if(found==w->Bodies.end())continue;
        auto& body=w->System.GetBodyInterface();const auto rotation=Q(body.GetRotation(found->second.Id)*Q(data.Local.Rotation));const auto position=V(body.GetPosition(found->second.Id)+body.GetRotation(found->second.Id)*V(data.Local.Position));
        if(!data.GuideReady||data.GuidePosition!=position||(data.GuideRotation.X!=rotation.X||data.GuideRotation.Y!=rotation.Y||data.GuideRotation.Z!=rotation.Z||data.GuideRotation.W!=rotation.W)){
            if(!data.GuideLocal)data.GuideLocal=GuideGeometry(*data.NativeShape,data.GeometryKey);
            data.GuideWorld.clear();data.GuideWorld.reserve(data.GuideLocal->size());
            for(auto& [a,b]:*data.GuideLocal){auto first=position+rotation.Rotate(a),second=position+rotation.Rotate(b);data.GuideWorld.push_back({first.X,first.Y,first.Z,second.X,second.Y,second.Z,data.Trigger?.2f:.3f,data.Trigger?.8f:1.f,data.Trigger?1.f:.35f,.85f});}
            data.GuidePosition=position;data.GuideRotation=rotation;data.GuideReady=true;
        }
        const auto wanted=std::min({budget,data.GuideWorld.size(),std::size_t(12288)-lines.size()});
        for(std::size_t i=0;i<wanted;++i)lines.push_back(data.GuideWorld[i*data.GuideWorld.size()/wanted]);
        if(lines.size()==12288)break;
    }
    for(auto& [key,j]:w->Joints)if(key.first==selected&&j.Native&&!j.Broken){auto a=V(j.Native->GetBody1()->GetCenterOfMassTransform()*j.Native->GetConstraintToBody1Matrix().GetTranslation()),b=V(j.Native->GetBody2()->GetCenterOfMassTransform()*j.Native->GetConstraintToBody2Matrix().GetTranslation());lines.push_back({a.X,a.Y,a.Z,b.X,b.Y,b.Z,1,.7f,.2f,1});for(auto axis:{Vec3{.1f,0,0},Vec3{0,.1f,0},Vec3{0,0,.1f}}){auto x=a-axis,y=a+axis;lines.push_back({x.X,x.Y,x.Z,y.X,y.Y,y.Z,1,.7f,.2f,1});}}
    if(auto it=w->SoftBodies.find(selected);it!=w->SoftBodies.end()){auto points=SoftVertices(*w,scene.GetEntity(selected));for(std::size_t i=0;i+2<it->second.Indices.size()&&lines.size()<12288;i+=3)for(int e=0;e<3;++e){auto a=points[it->second.Indices[i+e]],b=points[it->second.Indices[i+(e+1)%3]];lines.push_back({a.X,a.Y,a.Z,b.X,b.Y,b.Z,.3f,1,.35f,.8f});}}
    return lines;
}
static const PhysicsModuleApi Api{
    PhysicsModuleVersion,sizeof(PhysicsModuleApi),
    [](Scene& scene){auto& worlds=State().Worlds;auto it=worlds.find(&scene);if(it!=worlds.end()){it->second->Dead=true;worlds.erase(it);}},
    [](Scene& scene){auto w=Get(scene);w->Synchronize();w->Running=true;},
    [](Scene& scene){auto& worlds=State().Worlds;auto it=worlds.find(&scene);if(it!=worlds.end()){it->second->Dead=true;worlds.erase(it);}},
    [](Scene& scene,float dt,bool manual){auto w=Get(scene);if(!w->Running)throw std::logic_error("Physics simulation has not started");w->Step(dt,manual);},
    [](Scene& scene){Get(scene)->Synchronize();},
    [](Scene& scene){auto& worlds=State().Worlds;auto it=worlds.find(&scene);return it!=worlds.end()&&it->second->Running;},
    [](Scene& scene){auto w=Get(scene);if(!w->Running||w->TopologyDirty)w->Synchronize();return w->Bodies.size()+w->SoftBodies.size();},
    Query,Command,
    [](Scene& scene,Entity e,bool angular){auto w=Get(scene);if(w->TopologyDirty)w->Synchronize();auto* entry=BodyFor(*w,e);return entry?V(angular?w->System.GetBodyInterface().GetAngularVelocity(entry->Id):w->System.GetBodyInterface().GetLinearVelocity(entry->Id)):Vec3{};},
    [](Scene& scene,Entity e){auto w=Get(scene);if(w->TopologyDirty)w->Synchronize();auto* entry=BodyFor(*w,e);return entry&&w->System.GetBodyInterface().IsActive(entry->Id);},
    [](Scene& scene,PhysicsEventType type,PhysicsScene::Callback callback,Entity filter){if(int(type)>5||!callback||(filter&&filter.GetScene()!=&scene))throw std::invalid_argument("Invalid physics event subscription");auto w=Get(scene);if(w->Subscriptions.size()>=8192)throw std::length_error("Physics subscriptions exceed limit");auto id=State().NextSubscription++;auto owner=ApplicationEventsAccess::SetOwner(nullptr);ApplicationEventsAccess::SetOwner(owner);w->Subscriptions.emplace(id,std::make_shared<Subscription>(Subscription{Detail::GetCurrentGameplayModule(),std::move(callback),type,filter?filter.GetUUID():UUID{},owner,ApplicationEventsAccess::GetSubsystemLevel()}));return id;},
    [](Scene& scene,PhysicsScene::Subscription id){auto& worlds=State().Worlds;auto it=worlds.find(&scene);return it!=worlds.end()&&it->second->Subscriptions.erase(id)!=0;},
    [](const void* owner){for(auto& [scene,w]:State().Worlds){std::vector<std::uint64_t> ids;for(auto& [id,s]:w->Subscriptions)if(s->Owner==owner)ids.push_back(id);for(auto id:ids)w->Subscriptions.erase(id);}},
    [](Scene& scene,float alpha){auto it=State().Worlds.find(&scene);if(it!=State().Worlds.end())it->second->Alpha=std::clamp(alpha,0.f,1.f);},
    RenderMatrix,
    [](Scene& scene,Entity e,JointType t,int operation){auto w=Get(scene);if(!e||e.GetScene()!=&scene||int(t)>4)return false;w->Synchronize();bool result=false;for(auto& [key,j]:w->Joints)if(key.first==e.GetUUID()&&j.Settings.Type==t){if(operation==0){j.Broken=true;if(j.Native){j.Native->SetEnabled(false);for(auto* body:{j.Native->GetBody1(),j.Native->GetBody2()})if(body->IsDynamic())w->System.GetBodyInterface().ActivateBody(body->GetID());}result=true;}else if(operation==1){j.Broken=false;if(j.Native)j.Native->SetEnabled(true);if(j.Native)for(auto* body:{j.Native->GetBody1(),j.Native->GetBody2()})if(body->IsDynamic())w->System.GetBodyInterface().ActivateBody(body->GetID());result=true;}else result|=j.Broken;}return result;},
    [](Scene& scene,Entity e){return SoftVertices(*Get(scene),e);},
    [](Scene& scene,Entity e,Vec3 value,int index,int operation){return SoftControl(*Get(scene),e,value,index,operation);},
    [](Scene& scene){auto w=Get(scene);const bool old=w->Preview;w->Preview=!w->Running;struct Restore{World& W;bool Old;~Restore(){W.Preview=Old;}} restore{*w,old};if(!w->Running||w->TopologyDirty)w->Synchronize();w->DrawSoftBodies();},Guides
};
}
#ifdef _WIN32
#define PHYSICS_EXPORT __declspec(dllexport)
#else
#define PHYSICS_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PHYSICS_EXPORT const Bazzalt::Runtime::PhysicsModuleApi* BazzaltGetPhysicsModuleApi(std::uint32_t version,std::uint32_t size){return version==Bazzalt::Runtime::PhysicsModuleVersion&&size==sizeof(Bazzalt::Runtime::PhysicsModuleApi)?&Bazzalt::Runtime::Api:nullptr;}
