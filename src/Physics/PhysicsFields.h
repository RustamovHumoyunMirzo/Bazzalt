#pragma once
#include "Bazzalt/Physics.h"
namespace Bazzalt::Runtime {
#define BAZZALT_BODY_FIELDS(X) \
 X(MotionType,"Motion Type") X(CollisionDetection,"Collision Detection") X(Interpolation,"Interpolation") X(Constraints,"Constraints") \
 X(Mass,"Mass") X(InertiaMultiplier,"Inertia Multiplier") X(CenterOfMassOffset,"Center Of Mass Offset") X(LinearDamping,"Linear Damping") X(AngularDamping,"Angular Damping") \
 X(MaxLinearVelocity,"Max Linear Velocity") X(MaxAngularVelocity,"Max Angular Velocity") X(GravityFactor,"Gravity Factor") X(Gravity,"Gravity") \
 X(LinearVelocity,"Linear Velocity") X(AngularVelocity,"Angular Velocity") X(AllowSleeping,"Allow Sleeping") X(StartAwake,"Start Awake") \
 X(UseManifoldReduction,"Use Manifold Reduction") X(ApplyGyroscopicForce,"Apply Gyroscopic Force") X(CollideKinematicVsNonDynamic,"Collide Kinematic Vs Non Dynamic") \
 X(EnhancedInternalEdgeRemoval,"Enhanced Internal Edge Removal") X(VelocityIterations,"Velocity Iterations") X(PositionIterations,"Position Iterations")
#define BAZZALT_COLLIDER_FIELDS(X) \
 X(Offset,"Offset") X(Rotation,"Rotation") X(Scale,"Scale") X(Friction,"Friction") X(Restitution,"Restitution") X(ConvexRadius,"Convex Radius") X(Layer,"Layer") X(CollisionGroup,"Collision Group") X(IsTrigger,"Is Trigger")
#define BAZZALT_SHAPE_FIELDS(X) X(Shape,"Shape") X(Size,"Size") X(Radius,"Radius") X(Height,"Height") BAZZALT_COLLIDER_FIELDS(X)
#define BAZZALT_MESH_COLLIDER_FIELDS(X) X(MeshAsset,"Mesh Asset") X(Convex,"Convex") X(DoubleSided,"Double Sided") X(Sanitize,"Sanitize") BAZZALT_COLLIDER_FIELDS(X)
#define BAZZALT_PHYSICS_SETTINGS_FIELDS(X) \
 X(Gravity,"Gravity") X(AutoSimulation,"Auto Simulation") X(AllowSleeping,"Allow Sleeping") X(MaxBodies,"Max Bodies") X(MaxBodyPairs,"Max Body Pairs") X(MaxContactConstraints,"Max Contact Constraints") \
 X(CollisionSteps,"Collision Steps") X(VelocityIterations,"Velocity Iterations") X(PositionIterations,"Position Iterations") X(WorkerThreads,"Worker Threads") \
 X(SpeculativeContactDistance,"Speculative Contact Distance") X(PenetrationSlop,"Penetration Slop") X(Baumgarte,"Baumgarte") X(TimeBeforeSleep,"Time Before Sleep") X(PointVelocitySleepThreshold,"Point Velocity Sleep Threshold") X(CollisionMasks,"Collision Masks")
#define PHYSICS_VISIT(Name,Label) function(#Name,Label,value.Name);
template<class T,class F> void VisitBody(T& value,F function){BAZZALT_BODY_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitShapeCollider(T& value,F function){BAZZALT_SHAPE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitMeshCollider(T& value,F function){BAZZALT_MESH_COLLIDER_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitPhysicsSettings(T& value,F function){BAZZALT_PHYSICS_SETTINGS_FIELDS(PHYSICS_VISIT)}
#undef PHYSICS_VISIT
}
