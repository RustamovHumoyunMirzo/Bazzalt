#include "Script/LuaBindings.h"
#include "Physics/PhysicsFields.h"
namespace Bazzalt::Runtime {
namespace {
template<class T> void VehicleOperations(sol::table api,const char* name) {
    sol::table operations=api["_ComponentOperations"];auto type=operations.create_named(name);
    type["Has"]=[](Entity e){return e.HasComponent<T>();};type["Get"]=[](Entity e){return T(e.GetComponent<T>());};
    type["Add"]=[](Entity e){return T(e.AddComponent<T>());};
    type["Set"]=[](Entity e,T value){if(!value.IsValid())throw std::invalid_argument("Invalid vehicle component");e.GetComponent<T>()=value;};
    type["Remove"]=[](Entity e){e.RemoveComponent<T>();};type["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<T>();};type["SetEnabled"]=[](Entity e,bool enabled){e.SetComponentEnabled<T>(enabled);};
    type["Inherited"]=[](Entity e)->std::optional<T>{auto* v=e.GetScene()->TryGetInheritedComponent<T>(e);return v?std::optional<T>(*v):std::nullopt;};
}
}
void BindLuaVehicles(sol::table api) {
    api.new_enum("VehicleCollisionMode","Ray",VehicleCollisionMode::Ray,"SphereCast",VehicleCollisionMode::SphereCast,"CylinderCast",VehicleCollisionMode::CylinderCast);
    {auto type=api.new_usertype<Vehicle>("Vehicle",sol::constructors<Vehicle()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&Vehicle::Name;
    BAZZALT_VEHICLE_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&Vehicle::IsValid;VehicleOperations<Vehicle>(api,"Vehicle");}
    {auto type=api.new_usertype<VehicleWheel>("VehicleWheel",sol::constructors<VehicleWheel()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&VehicleWheel::Name;
    BAZZALT_VEHICLE_WHEEL_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&VehicleWheel::IsValid;VehicleOperations<VehicleWheel>(api,"VehicleWheel");}
    {auto type=api.new_usertype<WheelSuspension>("WheelSuspension",sol::constructors<WheelSuspension()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&WheelSuspension::Name;
    BAZZALT_WHEEL_SUSPENSION_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&WheelSuspension::IsValid;VehicleOperations<WheelSuspension>(api,"WheelSuspension");}
    {auto type=api.new_usertype<WheelTire>("WheelTire",sol::constructors<WheelTire()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&WheelTire::Name;
    BAZZALT_WHEEL_TIRE_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&WheelTire::IsValid;VehicleOperations<WheelTire>(api,"WheelTire");}
    {auto type=api.new_usertype<WheelBrake>("WheelBrake",sol::constructors<WheelBrake()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&WheelBrake::Name;
    BAZZALT_WHEEL_BRAKE_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&WheelBrake::IsValid;VehicleOperations<WheelBrake>(api,"WheelBrake");}
    {auto type=api.new_usertype<WheelSteering>("WheelSteering",sol::constructors<WheelSteering()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&WheelSteering::Name;
    BAZZALT_WHEEL_STEERING_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&WheelSteering::IsValid;VehicleOperations<WheelSteering>(api,"WheelSteering");}
    {auto type=api.new_usertype<VehicleEngine>("VehicleEngine",sol::constructors<VehicleEngine()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&VehicleEngine::Name;
    BAZZALT_VEHICLE_ENGINE_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&VehicleEngine::IsValid;VehicleOperations<VehicleEngine>(api,"VehicleEngine");}
    {auto type=api.new_usertype<VehicleTransmission>("VehicleTransmission",sol::constructors<VehicleTransmission()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&VehicleTransmission::Name;
    BAZZALT_VEHICLE_TRANSMISSION_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&VehicleTransmission::IsValid;VehicleOperations<VehicleTransmission>(api,"VehicleTransmission");}
    {auto type=api.new_usertype<VehicleDifferential>("VehicleDifferential",sol::constructors<VehicleDifferential()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&VehicleDifferential::Name;
    BAZZALT_VEHICLE_DIFFERENTIAL_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&VehicleDifferential::IsValid;VehicleOperations<VehicleDifferential>(api,"VehicleDifferential");}
    {auto type=api.new_usertype<VehicleAntiRollBar>("VehicleAntiRollBar",sol::constructors<VehicleAntiRollBar()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&VehicleAntiRollBar::Name;
    BAZZALT_VEHICLE_ANTI_ROLL_BAR_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&VehicleAntiRollBar::IsValid;VehicleOperations<VehicleAntiRollBar>(api,"VehicleAntiRollBar");}
    {auto type=api.new_usertype<VehicleAerodynamics>("VehicleAerodynamics",sol::constructors<VehicleAerodynamics()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) type[#Name]=&VehicleAerodynamics::Name;
    BAZZALT_VEHICLE_AERODYNAMICS_FIELDS(FIELD)
#undef FIELD
    type["IsValid"]=&VehicleAerodynamics::IsValid;VehicleOperations<VehicleAerodynamics>(api,"VehicleAerodynamics");}
    auto state=api.new_usertype<VehicleState>("VehicleState");
    state["Speed"]=sol::readonly(&VehicleState::Speed);
    state["EngineRPM"]=sol::readonly(&VehicleState::EngineRPM);
    state["Clutch"]=sol::readonly(&VehicleState::Clutch);
    state["Gear"]=sol::readonly(&VehicleState::Gear);
    state["GroundedWheels"]=sol::readonly(&VehicleState::GroundedWheels);
    state["WheelCount"]=sol::readonly(&VehicleState::WheelCount);
    auto wheel=api.new_usertype<VehicleWheelState>("VehicleWheelState");
    wheel["Grounded"]=sol::readonly(&VehicleWheelState::Grounded);
    wheel["Position"]=sol::readonly(&VehicleWheelState::Position);
    wheel["ContactPoint"]=sol::readonly(&VehicleWheelState::ContactPoint);
    wheel["ContactNormal"]=sol::readonly(&VehicleWheelState::ContactNormal);
    wheel["AngularVelocity"]=sol::readonly(&VehicleWheelState::AngularVelocity);
    wheel["SteerAngle"]=sol::readonly(&VehicleWheelState::SteerAngle);
    wheel["SuspensionLength"]=sol::readonly(&VehicleWheelState::SuspensionLength);
    wheel["SuspensionForce"]=sol::readonly(&VehicleWheelState::SuspensionForce);
    wheel["LongitudinalSlip"]=sol::readonly(&VehicleWheelState::LongitudinalSlip);
    wheel["LateralSlip"]=sol::readonly(&VehicleWheelState::LateralSlip);
    sol::usertype<PhysicsScene> physics=api["PhysicsScene"];
    physics["SetVehicleInput"]=[](PhysicsScene& p,Entity e,float throttle,float steering,sol::optional<float> brake,sol::optional<float> handbrake){return p.SetVehicleInput(e,throttle,steering,brake.value_or(0),handbrake.value_or(0));};
    physics["SetVehicleGear"]=[](PhysicsScene& p,Entity e,int gear,sol::optional<float> clutch){return p.SetVehicleGear(e,gear,clutch.value_or(1));};
    physics["GetVehicleState"]=&PhysicsScene::GetVehicleState;
    physics["GetVehicleWheelState"]=&PhysicsScene::GetVehicleWheelState;
}
}
