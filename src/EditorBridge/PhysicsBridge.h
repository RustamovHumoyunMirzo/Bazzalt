#pragma once
#include "Physics/PhysicsFields.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/AssetManager.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace Bazzalt::EditorBridge {
namespace py=pybind11;
template<class T> py::object PhysicsValue(const T& v){
    if constexpr(std::is_enum_v<T>)return py::int_(int(v));
    else if constexpr(std::is_same_v<T,Vec3>)return py::make_tuple(v.X,v.Y,v.Z);
    else if constexpr(std::is_same_v<T,Quaternion>){auto q=v.Normalized();return py::make_tuple(ToDegrees(std::atan2(2*(q.W*q.X+q.Y*q.Z),1-2*(q.X*q.X+q.Y*q.Y))),ToDegrees(std::asin(std::clamp(2*(q.W*q.Y-q.Z*q.X),-1.f,1.f))),ToDegrees(std::atan2(2*(q.W*q.Z+q.X*q.Y),1-2*(q.Y*q.Y+q.Z*q.Z))));}
    else if constexpr(std::is_same_v<T,UUID>)return py::str(v.ToString());
    else if constexpr(std::is_same_v<T,std::optional<Vec3>>)return v?PhysicsValue(*v):py::none();
    else return py::cast(v);
}
template<class T> bool SetPhysicsValue(T& field,py::handle input){
    if constexpr(std::is_enum_v<T>){const int n=py::cast<int>(input);if(n<0||n>255)return false;field=T(n);}
    else if constexpr(std::is_same_v<T,Vec3>){auto v=py::cast<std::array<float,3>>(input);field={v[0],v[1],v[2]};}
    else if constexpr(std::is_same_v<T,Quaternion>){auto v=py::cast<std::array<float,3>>(input);field=Quaternion::FromEuler({ToRadians(v[0]),ToRadians(v[1]),ToRadians(v[2])});}
    else if constexpr(std::is_same_v<T,UUID>){if(!UUID::TryParse(py::cast<std::string>(input),field))return false;}
    else if constexpr(std::is_same_v<T,std::optional<Vec3>>){if(input.is_none())field.reset();else{Vec3 v;if(!SetPhysicsValue(v,input))return false;field=v;}}
    else field=py::cast<T>(input);
    return true;
}
template<class F> bool PhysicsDispatch(Entity e,const std::string& name,F function){
    if(name=="Rigidbody")function.template operator()<Rigidbody>(e);
    else if(name=="Shape Collider")function.template operator()<ShapeCollider>(e);
    else if(name=="Mesh Collider")function.template operator()<MeshCollider>(e);
    else return false;return true;
}
inline void InspectPhysics(Entity e,py::list& components,py::dict& enabled,py::dict& data){
#define INSPECT(Type,Label,Visit) if(auto* body=e.TryGetComponent<Type>()){components.append(Label);enabled[Label]=body->Enabled;py::dict values;Runtime::Visit(*body,[&](const char*,const char* label,const auto& field){values[label]=PhysicsValue(field);});data[Label]=values;}
    INSPECT(Rigidbody,"Rigidbody",VisitBody) INSPECT(ShapeCollider,"Shape Collider",VisitShapeCollider) INSPECT(MeshCollider,"Mesh Collider",VisitMeshCollider)
#undef INSPECT
    if(auto* c=e.TryGetComponent<ShapeCollider>()){auto v=data["Shape Collider"].cast<py::dict>();if(c->Shape!=ColliderShape::Box)v.attr("pop")("Size",py::none());if(c->Shape==ColliderShape::Box||c->Shape==ColliderShape::Plane){v.attr("pop")("Radius",py::none());v.attr("pop")("Height",py::none());}else if(c->Shape==ColliderShape::Sphere)v.attr("pop")("Height",py::none());}
    if(auto* body=e.TryGetComponent<Rigidbody>()){auto values=data["Rigidbody"].cast<py::dict>();values["Use Scene Gravity"]=!body->Gravity.has_value();if(!body->Gravity)values.attr("pop")("Gravity",py::none());}
}
template<class T> bool ValidPhysicsAsset(const T&){return true;}
inline bool ValidPhysicsAsset(const MeshCollider& c){if(!c.MeshAsset)return true;auto asset=AssetManager::GetAsset(c.MeshAsset);return asset&&asset->State==AssetState::Ready&&asset->Importer=="Bazzalt.glTF";}
template<class T> void ApplyPhysicsSpecial(T& component,Entity e,const std::string& name,py::object value,bool& changed){if constexpr(std::is_same_v<T,Rigidbody>){if(name=="Use Scene Gravity"){component.Gravity=py::cast<bool>(value)?std::nullopt:std::optional<Vec3>(e.GetScene()->GetPhysicsSettings().Gravity);changed=true;}}}
inline bool SetPhysicsProperty(Entity e,const std::string& type,const std::string& name,py::object value){
    bool changed=false;
#define SET(Type,Label,Visit) if(type==Label&&e.HasComponent<Type>()){auto component=e.GetComponent<Type>();Runtime::Visit(component,[&](const char*,const char* label,auto& field){if(name==label)changed=SetPhysicsValue(field,value);});ApplyPhysicsSpecial(component,e,name,value,changed);if(changed&&component.IsValid()&&ValidPhysicsAsset(component)){e.GetComponent<Type>()=component;return true;}return false;}
    SET(Rigidbody,"Rigidbody",VisitBody) SET(ShapeCollider,"Shape Collider",VisitShapeCollider) SET(MeshCollider,"Mesh Collider",VisitMeshCollider)
#undef SET
    return false;
}
inline py::dict InspectPhysicsSettings(const PhysicsSettings& settings){py::dict data;Runtime::VisitPhysicsSettings(settings,[&](const char*,const char* label,const auto& field){data[label]=PhysicsValue(field);});return data;}
inline bool UpdatePhysicsSettings(Scene& scene,const py::dict& data){auto settings=scene.GetPhysicsSettings();bool valid=true;Runtime::VisitPhysicsSettings(settings,[&](const char*,const char* label,auto& field){if(data.contains(label)&&!SetPhysicsValue(field,data[label]))valid=false;});return valid&&scene.SetPhysicsSettings(settings);}
}
