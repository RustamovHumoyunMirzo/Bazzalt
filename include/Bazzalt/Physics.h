#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/Entity.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Components/Rigidbody.h"
#include "Bazzalt/Components/ShapeCollider.h"
#include "Bazzalt/Components/MeshCollider.h"
#include <array>
#include <memory>
#include <functional>
#include <vector>
namespace Bazzalt {
class Scene;
struct PhysicsSettings {
    Vec3 Gravity{0,-9.81f,0};
    bool AutoSimulation=true,AllowSleeping=true;
    std::uint32_t MaxBodies=65536,MaxBodyPairs=65536,MaxContactConstraints=16384;
    std::uint32_t CollisionSteps=1,VelocityIterations=10,PositionIterations=2,WorkerThreads=2;
    float SpeculativeContactDistance=.02f,PenetrationSlop=.02f,Baumgarte=.2f;
    float TimeBeforeSleep=.5f,PointVelocitySleepThreshold=.03f;
    std::array<std::uint32_t,32> CollisionMasks=[](){std::array<std::uint32_t,32> v{};v.fill(0xffffffffu);return v;}();
    [[nodiscard]] bool IsValid() const {
        return std::isfinite(Gravity.X)&&std::isfinite(Gravity.Y)&&std::isfinite(Gravity.Z)&&MaxBodies>=1&&MaxBodies<=1048576&&MaxBodyPairs>=1&&MaxBodyPairs<=4194304&&MaxContactConstraints>=1&&MaxContactConstraints<=1048576&&
            CollisionSteps>=1&&CollisionSteps<=16&&VelocityIterations>=1&&VelocityIterations<=255&&PositionIterations>=1&&PositionIterations<=255&&WorkerThreads<=32&&
            std::isfinite(SpeculativeContactDistance)&&SpeculativeContactDistance>=0&&std::isfinite(PenetrationSlop)&&PenetrationSlop>=0&&std::isfinite(Baumgarte)&&Baumgarte>=0&&Baumgarte<=1&&
            std::isfinite(TimeBeforeSleep)&&TimeBeforeSleep>=0&&std::isfinite(PointVelocitySleepThreshold)&&PointVelocitySleepThreshold>=0;
    }
};
struct PhysicsQueryOptions {
    std::uint32_t LayerMask=0xffffffffu,MaxResults=1024;
    bool IncludeTriggers=false,IncludeBackFaces=false;
    std::vector<UUID> ExcludedEntities;
};
struct PhysicsHit {
    Entity Object{},Collider{};
    Vec3 Point{},Normal{};
    float Distance=0,Fraction=0;
    std::uint32_t SubShape=0;
    explicit operator bool() const { return bool(Object); }
    [[nodiscard]] bool IsValid() const { return bool(Object); }
};
struct PhysicsShape {
    ColliderShape Shape=ColliderShape::Sphere;
    Vec3 Size{1};float Radius=.5f,Height=2;
};
enum class PhysicsEventType : std::uint8_t { CollisionEnter,CollisionStay,CollisionExit,TriggerEnter,TriggerStay,TriggerExit };
struct ContactPoint { Vec3 PointA{},PointB{};float Penetration=0; };
struct CollisionEvent {
    PhysicsEventType Type{};
    float DeltaTime=0;
    Entity BodyA{},BodyB{},ColliderA{},ColliderB{};
    UUID BodyAUUID{},BodyBUUID{},ColliderAUUID{},ColliderBUUID{};
    Vec3 Normal{},RelativeVelocity{};
    std::vector<ContactPoint> Contacts;
};
// Scene-scoped facade; all solver state and third-party types are private.
class BAZZALT_API PhysicsScene final {
public:
    explicit PhysicsScene(Scene& scene);
    [[nodiscard]] PhysicsSettings GetSettings() const;
    bool SetSettings(const PhysicsSettings& settings);
    void SetGravity(Vec3 gravity);
    [[nodiscard]] Vec3 GetGravity() const;
    void SetLayerCollision(std::uint32_t first,std::uint32_t second,bool enabled);
    [[nodiscard]] bool GetLayerCollision(std::uint32_t first,std::uint32_t second) const;
    [[nodiscard]] bool IsRunning() const;
    void Simulate(float fixedDeltaTime);
    void Synchronize();
    [[nodiscard]] std::size_t GetBodyCount() const;
    [[nodiscard]] PhysicsHit Raycast(Vec3 origin,Vec3 direction,float distance=10000,const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] std::vector<PhysicsHit> RaycastAll(Vec3 origin,Vec3 direction,float distance=10000,const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] PhysicsHit ShapeCast(const PhysicsShape& shape,const Transform& transform,Vec3 direction,float distance,const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] std::vector<PhysicsHit> ShapeCastAll(const PhysicsShape& shape,const Transform& transform,Vec3 direction,float distance,const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] std::vector<PhysicsHit> OverlapShape(const PhysicsShape& shape,const Transform& transform,const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] std::vector<PhysicsHit> OverlapSphere(Vec3 center,float radius,const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] std::vector<PhysicsHit> OverlapBox(Vec3 center,Vec3 halfExtents,Quaternion rotation={},const PhysicsQueryOptions& options={}) const;
    [[nodiscard]] PhysicsHit SphereCast(Vec3 origin,float radius,Vec3 direction,float distance,const PhysicsQueryOptions& options={}) const { PhysicsShape shape;shape.Radius=radius;Transform transform;transform.Position=origin;return ShapeCast(shape,transform,direction,distance,options); }
    [[nodiscard]] std::vector<PhysicsHit> SphereCastAll(Vec3 origin,float radius,Vec3 direction,float distance,const PhysicsQueryOptions& options={}) const { PhysicsShape shape;shape.Radius=radius;Transform transform;transform.Position=origin;return ShapeCastAll(shape,transform,direction,distance,options); }
    [[nodiscard]] bool CheckSphere(Vec3 center,float radius,const PhysicsQueryOptions& options={}) const { auto query=options;query.MaxResults=1;return !OverlapSphere(center,radius,query).empty(); }
    [[nodiscard]] bool CheckBox(Vec3 center,Vec3 halfExtents,Quaternion rotation={},const PhysicsQueryOptions& options={}) const { auto query=options;query.MaxResults=1;return !OverlapBox(center,halfExtents,rotation,query).empty(); }
    bool ApplyForce(Entity body,Vec3 force);
    bool ApplyForceAtPosition(Entity body,Vec3 force,Vec3 position);
    bool ApplyImpulse(Entity body,Vec3 impulse);
    bool ApplyImpulseAtPosition(Entity body,Vec3 impulse,Vec3 position);
    bool ApplyTorque(Entity body,Vec3 torque);
    bool ApplyAngularImpulse(Entity body,Vec3 impulse);
    bool SetLinearVelocity(Entity body,Vec3 velocity);
    bool SetAngularVelocity(Entity body,Vec3 velocity);
    [[nodiscard]] Vec3 GetLinearVelocity(Entity body) const;
    [[nodiscard]] Vec3 GetAngularVelocity(Entity body) const;
    bool MoveKinematic(Entity body,Vec3 position,Quaternion rotation,float deltaTime);
    bool Teleport(Entity body,Vec3 position,Quaternion rotation,bool activate=true);
    bool WakeUp(Entity body);
    bool Sleep(Entity body);
    [[nodiscard]] bool IsAwake(Entity body) const;
    bool SetGravity(Entity body,std::optional<Vec3> gravity);
    [[nodiscard]] std::optional<Vec3> GetGravity(Entity body) const;
    using Subscription=std::uint64_t;
    using Callback=std::function<void(const CollisionEvent&)>;
    Subscription Subscribe(PhysicsEventType type,Callback callback,Entity filter={});
    bool Unsubscribe(Subscription subscription);
private:
    [[nodiscard]] Scene& GetScene() const;
    Scene* m_scene;
    std::weak_ptr<void> m_lifetime;
};
}
