#pragma once
#include "Bazzalt/Physics.h"
namespace Bazzalt { class ComponentSerializationRegistry; }
namespace Bazzalt::Runtime {
inline constexpr std::uint32_t PhysicsModuleVersion=2;
enum class PhysicsCommand { Force,ForceAtPosition,Impulse,ImpulseAtPosition,Torque,AngularImpulse,LinearVelocity,AngularVelocity,MoveKinematic,Teleport,Wake,Sleep };
struct PhysicsModuleApi {
    std::uint32_t Version,Size;
    void (*Release)(Scene&);
    void (*Start)(Scene&);
    void (*Stop)(Scene&);
    void (*Step)(Scene&,float,bool);
    void (*Synchronize)(Scene&);
    bool (*IsRunning)(Scene&);
    std::size_t (*BodyCount)(Scene&);
    std::vector<PhysicsHit> (*Query)(Scene&,int,const PhysicsShape&,const Transform&,Vec3,float,const PhysicsQueryOptions&);
    bool (*Command)(Scene&,Entity,PhysicsCommand,Vec3,Vec3,Quaternion,float,bool);
    Vec3 (*Velocity)(Scene&,Entity,bool);
    bool (*Awake)(Scene&,Entity);
    PhysicsScene::Subscription (*Subscribe)(Scene&,PhysicsEventType,PhysicsScene::Callback,Entity);
    bool (*Unsubscribe)(Scene&,PhysicsScene::Subscription);
    void (*DisconnectOwner)(const void*);
    void (*SetInterpolationAlpha)(Scene&,float);
    Mat4 (*RenderMatrix)(Scene&,Entity);
    bool (*JointControl)(Scene&,Entity,JointType,int);
    std::vector<Vec3> (*SoftVertices)(Scene&,Entity);
    bool (*SoftControl)(Scene&,Entity,Vec3,int,int);
    void (*Render)(Scene&);
    std::vector<std::array<float,10>> (*Guides)(Scene&,UUID);
};
using GetPhysicsModuleApi=const PhysicsModuleApi* (*)(std::uint32_t,std::uint32_t);
const PhysicsModuleApi& PhysicsModule();
void RenderPhysicsScene(Scene&);
void ReleasePhysicsScene(Scene&) noexcept;
void DisconnectPhysicsOwner(const void*) noexcept;
Mat4 PhysicsRenderMatrix(Scene&,Entity);
void RegisterPhysicsComponents(ComponentSerializationRegistry&);
}
