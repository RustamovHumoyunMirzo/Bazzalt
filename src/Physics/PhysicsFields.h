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
#define BAZZALT_JOINT_FIELDS(X) X(Type,"Type") X(ConnectedBody,"Connected Body") X(Anchor,"Anchor") X(ConnectedAnchor,"Connected Anchor") X(Axis,"Axis") X(AutoConfigureConnectedAnchor,"Auto Configure Connected Anchor") X(EnableCollision,"Enable Collision") X(BreakForce,"Break Force") X(BreakTorque,"Break Torque") X(MinDistance,"Min Distance") X(MaxDistance,"Max Distance") X(MinLimit,"Min Limit") X(MaxLimit,"Max Limit") X(SpringFrequency,"Spring Frequency") X(SpringDamping,"Spring Damping") X(Friction,"Friction") X(Motor,"Motor") X(MotorVelocity,"Motor Velocity") X(MotorTarget,"Motor Target") X(MotorForce,"Motor Force") X(MotorFrequency,"Motor Frequency") X(MotorDamping,"Motor Damping")
#define BAZZALT_SOFT_FIELDS(X) X(Mass,"Mass") X(Compliance,"Compliance") X(BendCompliance,"Bend Compliance") X(Damping,"Damping") X(GravityFactor,"Gravity Factor") X(Friction,"Friction") X(Restitution,"Restitution") X(MaxVelocity,"Max Velocity") X(Wind,"Wind") X(Iterations,"Iterations") X(Layer,"Layer") X(MaterialAsset,"Material Asset") X(AllowSleeping,"Allow Sleeping") X(CastShadows,"Cast Shadows")
#define BAZZALT_CLOTH_FIELDS(X) X(BendMode,"Bend Mode") X(SubdivisionLevel,"Subdivision Level") X(Pressure,"Pressure") X(CollisionRadius,"Collision Radius") X(Width,"Width") X(Height,"Height") X(SegmentsX,"Segments X") X(SegmentsY,"Segments Y") X(PinEdges,"Pin Edges") X(ShearCompliance,"Shear Compliance") BAZZALT_SOFT_FIELDS(X)
#define BAZZALT_ROPE_FIELDS(X) X(Length,"Length") X(Radius,"Radius") X(Segments,"Segments") X(Sides,"Sides") X(PinStart,"Pin Start") X(PinEnd,"Pin End") BAZZALT_SOFT_FIELDS(X)
#define PHYSICS_VISIT(Name,Label) function(#Name,Label,value.Name);
template<class T,class F> void VisitBody(T& value,F function){BAZZALT_BODY_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitShapeCollider(T& value,F function){BAZZALT_SHAPE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitMeshCollider(T& value,F function){BAZZALT_MESH_COLLIDER_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitPhysicsSettings(T& value,F function){BAZZALT_PHYSICS_SETTINGS_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitJoint(T& value,F function){BAZZALT_JOINT_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitCloth(T& value,F function){BAZZALT_CLOTH_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitRope(T& value,F function){BAZZALT_ROPE_FIELDS(PHYSICS_VISIT)}
#undef PHYSICS_VISIT
}
