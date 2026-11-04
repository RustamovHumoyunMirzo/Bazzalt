#include "Script/LuaBindings.h"
#include "Bazzalt/Lighting.h"
#include "Rendering/LightingFields.h"
namespace Bazzalt::Runtime {
namespace {
template<class T>void LightingComponent(sol::table api,const char* name){sol::table operations=api["_ComponentOperations"];auto op=operations.create_named(name);op["Has"]=[](Entity e){return e.HasComponent<T>();};op["Get"]=[](Entity e){return T(e.GetComponent<T>());};op["Add"]=[](Entity e){return T(e.AddComponent<T>());};op["Set"]=[](Entity e,T value){if(!value.IsValid())throw std::invalid_argument("Invalid lighting component");e.GetComponent<T>()=std::move(value);Lighting::Invalidate(*e.GetScene());};op["Remove"]=[](Entity e){e.RemoveComponent<T>();Lighting::Invalidate(*e.GetScene());};op["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<T>();};op["SetEnabled"]=[](Entity e,bool value){e.SetComponentEnabled<T>(value);};op["Inherited"]=[](Entity)->std::optional<T>{return {};};}
}
void BindLuaLighting(sol::table api){
    auto mode=api.create_named("LightMode");mode["Realtime"]=LightMode::Realtime;mode["Baked"]=LightMode::Baked;mode["Mixed"]=LightMode::Mixed;
    auto status=api.create_named("LightBakeStatus");status["Baking"]=LightBakeStatus::Baking;status["Complete"]=LightBakeStatus::Complete;status["Applied"]=LightBakeStatus::Applied;status["Cancelled"]=LightBakeStatus::Cancelled;status["Failed"]=LightBakeStatus::Failed;
    auto settings=api.new_usertype<LightBakeSettings>("LightBakeSettings",sol::constructors<LightBakeSettings()>());
    settings["Samples"]=&LightBakeSettings::Samples;
    settings["Bounces"]=&LightBakeSettings::Bounces;
    settings["MaxDistance"]=&LightBakeSettings::MaxDistance;
    settings["RayBias"]=&LightBakeSettings::RayBias;
    settings["AmbientRadiance"]=&LightBakeSettings::AmbientRadiance;
    settings["DefaultAlbedo"]=&LightBakeSettings::DefaultAlbedo;
    settings["MaxTriangles"]=&LightBakeSettings::MaxTriangles;
    settings["LightmapSamples"]=&LightBakeSettings::LightmapSamples;
    settings["ReflectionSamples"]=&LightBakeSettings::ReflectionSamples;
    settings["MaxBakeTexels"]=&LightBakeSettings::MaxBakeTexels;
    settings["IsValid"]=&LightBakeSettings::IsValid;
    auto handle=api.new_usertype<LightBakeHandle>("LightBakeHandle",sol::constructors<LightBakeHandle()>());handle["GetStatus"]=&LightBakeHandle::GetStatus;handle["GetProgress"]=&LightBakeHandle::GetProgress;handle["GetError"]=&LightBakeHandle::GetError;handle["Cancel"]=&LightBakeHandle::Cancel;handle["Apply"]=&LightBakeHandle::Apply;
    auto probe=api.new_usertype<LightProbe>("LightProbe",sol::constructors<LightProbe()>(),sol::base_classes,sol::bases<Component>());probe["Radius"]=&LightProbe::Radius;probe["Intensity"]=&LightProbe::Intensity;probe["Baked"]=&LightProbe::Baked;probe["Coefficients"]=&LightProbe::Coefficients;probe["IsValid"]=&LightProbe::IsValid;
    sol::table operations=api["_ComponentOperations"];auto op=operations.create_named("LightProbe");op["Has"]=[](Entity e){return e.HasComponent<LightProbe>();};op["Get"]=[](Entity e){return LightProbe(e.GetComponent<LightProbe>());};op["Add"]=[](Entity e){return LightProbe(e.AddComponent<LightProbe>());};op["Set"]=[](Entity e,LightProbe value){if(!value.IsValid())throw std::invalid_argument("Invalid Light Probe");e.GetComponent<LightProbe>()=value;};op["Remove"]=[](Entity e){e.RemoveComponent<LightProbe>();};op["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<LightProbe>();};op["SetEnabled"]=[](Entity e,bool value){e.SetComponentEnabled<LightProbe>(value);};op["Inherited"]=[](Entity)->std::optional<LightProbe>{return {};};
    auto map=api.new_usertype<Lightmap>("Lightmap",sol::constructors<Lightmap()>(),sol::base_classes,sol::bases<Component>());
    map["Resolution"]=&Lightmap::Resolution;map["Padding"]=&Lightmap::Padding;map["Baked"]=&Lightmap::Baked;map["BakedResolution"]=&Lightmap::BakedResolution;map["BakedPadding"]=&Lightmap::BakedPadding;map["ChartGrid"]=&Lightmap::ChartGrid;map["Geometry"]=&Lightmap::Geometry;map["Texels"]=&Lightmap::Texels;map["TransformSignature"]=&Lightmap::TransformSignature;map["IsValid"]=&Lightmap::IsValid;
    auto reflection=api.new_usertype<ReflectionProbe>("ReflectionProbe",sol::constructors<ReflectionProbe()>(),sol::base_classes,sol::bases<Component>());
    reflection["Resolution"]=&ReflectionProbe::Resolution;reflection["Radius"]=&ReflectionProbe::Radius;reflection["Intensity"]=&ReflectionProbe::Intensity;reflection["BlendDistance"]=&ReflectionProbe::BlendDistance;reflection["BoxProjection"]=&ReflectionProbe::BoxProjection;reflection["BoxSize"]=&ReflectionProbe::BoxSize;reflection["Baked"]=&ReflectionProbe::Baked;reflection["CapturePosition"]=&ReflectionProbe::CapturePosition;reflection["BakedResolution"]=&ReflectionProbe::BakedResolution;reflection["Radiance"]=&ReflectionProbe::Radiance;reflection["IsValid"]=&ReflectionProbe::IsValid;
    LightingComponent<Lightmap>(api,"Lightmap");LightingComponent<ReflectionProbe>(api,"ReflectionProbe");
    auto lighting=api.create_named("Lighting");lighting["MaxProbes"]=Lighting::MaxProbes;lighting["BeginBake"]=sol::overload([](Scene& s){return Lighting::BeginBake(s);},[](Scene& s,const LightBakeSettings& settings){return Lighting::BeginBake(s,settings);});lighting["Clear"]=&Lighting::Clear;lighting["Invalidate"]=&Lighting::Invalidate;lighting["SampleReflection"]=sol::overload([](Scene& s,Vec3 p,Vec3 d){return Lighting::SampleReflection(s,p,d);},[](Scene& s,Vec3 p,Vec3 d,float r){return Lighting::SampleReflection(s,p,d,r);});lighting["Sample"]=&Lighting::Sample;lighting["CreateProbeGrid"]=sol::overload([](Scene& s,Vec3 a,Vec3 b){return Lighting::CreateProbeGrid(s,a,b);},[](Scene& s,Vec3 a,Vec3 b,unsigned x,unsigned y,unsigned z,float radius){return Lighting::CreateProbeGrid(s,a,b,x,y,z,radius);});
}
}
