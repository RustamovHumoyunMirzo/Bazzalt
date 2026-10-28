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
#define BAZZALT_VEHICLE_FIELDS(X) X(Up,"Up") X(Forward,"Forward") X(MaxPitchRollAngle,"Max Pitch Roll Angle") X(CollisionRadius,"Collision Radius") X(CollisionTest,"Collision Test") X(Layer,"Layer")
#define BAZZALT_VEHICLE_WHEEL_FIELDS(X) X(Position,"Position") X(Up,"Up") X(Forward,"Forward") X(Radius,"Radius") X(Width,"Width") X(Inertia,"Inertia") X(AngularDamping,"Angular Damping") X(Animate,"Animate")
#define BAZZALT_WHEEL_SUSPENSION_FIELDS(X) X(Direction,"Direction") X(ForcePoint,"Force Point") X(MinLength,"Min Length") X(MaxLength,"Max Length") X(PreloadLength,"Preload Length") X(Frequency,"Frequency") X(Damping,"Damping") X(UseForcePoint,"Use Force Point")
#define BAZZALT_WHEEL_TIRE_FIELDS(X) X(LongitudinalPeakSlip,"Longitudinal Peak Slip") X(LongitudinalPeakGrip,"Longitudinal Peak Grip") X(LongitudinalTailSlip,"Longitudinal Tail Slip") X(LongitudinalTailGrip,"Longitudinal Tail Grip") X(LateralPeakAngle,"Lateral Peak Angle") X(LateralPeakGrip,"Lateral Peak Grip") X(LateralTailAngle,"Lateral Tail Angle") X(LateralTailGrip,"Lateral Tail Grip")
#define BAZZALT_WHEEL_BRAKE_FIELDS(X) X(MaxTorque,"Max Torque") X(HandbrakeTorque,"Handbrake Torque")
#define BAZZALT_WHEEL_STEERING_FIELDS(X) X(Axis,"Axis") X(MaxAngle,"Max Angle")
#define BAZZALT_VEHICLE_ENGINE_FIELDS(X) X(MaxTorque,"Max Torque") X(MinRPM,"Min RPM") X(MaxRPM,"Max RPM") X(Inertia,"Inertia") X(AngularDamping,"Angular Damping") X(LowTorque,"Low Torque") X(PeakRPMFraction,"Peak RPMFraction") X(HighTorque,"High Torque")
#define BAZZALT_VEHICLE_TRANSMISSION_FIELDS(X) X(Automatic,"Automatic") X(GearCount,"Gear Count") X(Gear1,"Gear 1") X(Gear2,"Gear 2") X(Gear3,"Gear 3") X(Gear4,"Gear 4") X(Gear5,"Gear 5") X(Gear6,"Gear 6") X(Gear7,"Gear 7") X(Gear8,"Gear 8") X(Gear9,"Gear 9") X(Gear10,"Gear 10") X(ReverseRatio,"Reverse Ratio") X(SwitchTime,"Switch Time") X(ClutchReleaseTime,"Clutch Release Time") X(SwitchLatency,"Switch Latency") X(ShiftUpRPM,"Shift Up RPM") X(ShiftDownRPM,"Shift Down RPM") X(ClutchStrength,"Clutch Strength")
#define BAZZALT_VEHICLE_DIFFERENTIAL_FIELDS(X) X(LeftWheel,"Left Wheel") X(RightWheel,"Right Wheel") X(Ratio,"Ratio") X(Split,"Split") X(LimitedSlipRatio,"Limited Slip Ratio") X(TorqueShare,"Torque Share")
#define BAZZALT_VEHICLE_ANTI_ROLL_BAR_FIELDS(X) X(LeftWheel,"Left Wheel") X(RightWheel,"Right Wheel") X(Stiffness,"Stiffness")
#define BAZZALT_VEHICLE_AERODYNAMICS_FIELDS(X) X(AirDensity,"Air Density") X(FrontalArea,"Frontal Area") X(DragCoefficient,"Drag Coefficient") X(DownforceCoefficient,"Downforce Coefficient") X(CenterOfPressure,"Center Of Pressure")
#define PHYSICS_VISIT(Name,Label) function(#Name,Label,value.Name);
template<class T,class F> void VisitBody(T& value,F function){BAZZALT_BODY_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitShapeCollider(T& value,F function){BAZZALT_SHAPE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitMeshCollider(T& value,F function){BAZZALT_MESH_COLLIDER_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitPhysicsSettings(T& value,F function){BAZZALT_PHYSICS_SETTINGS_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitJoint(T& value,F function){BAZZALT_JOINT_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitCloth(T& value,F function){BAZZALT_CLOTH_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitRope(T& value,F function){BAZZALT_ROPE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicle(T& value,F function){BAZZALT_VEHICLE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicleWheel(T& value,F function){BAZZALT_VEHICLE_WHEEL_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitWheelSuspension(T& value,F function){BAZZALT_WHEEL_SUSPENSION_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitWheelTire(T& value,F function){BAZZALT_WHEEL_TIRE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitWheelBrake(T& value,F function){BAZZALT_WHEEL_BRAKE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitWheelSteering(T& value,F function){BAZZALT_WHEEL_STEERING_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicleEngine(T& value,F function){BAZZALT_VEHICLE_ENGINE_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicleTransmission(T& value,F function){BAZZALT_VEHICLE_TRANSMISSION_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicleDifferential(T& value,F function){BAZZALT_VEHICLE_DIFFERENTIAL_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicleAntiRollBar(T& value,F function){BAZZALT_VEHICLE_ANTI_ROLL_BAR_FIELDS(PHYSICS_VISIT)}
template<class T,class F> void VisitVehicleAerodynamics(T& value,F function){BAZZALT_VEHICLE_AERODYNAMICS_FIELDS(PHYSICS_VISIT)}
#undef PHYSICS_VISIT
}
