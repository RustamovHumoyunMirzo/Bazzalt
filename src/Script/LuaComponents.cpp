#include "Script/LuaBindings.h"
namespace Bazzalt::Runtime {
template<class T> void ComponentOperations(sol::table operations,const char* name){
    sol::table value=operations.create_named(name);
    value["Has"]=[](Entity entity){return entity.HasComponent<T>();};
    value["Get"]=[](Entity entity){return T(entity.GetComponent<T>());};
    value["Inherited"]=[](Entity entity)->std::optional<T>{
        if(!entity.IsValid()||!entity.GetScene())return std::nullopt;
        const auto* value=entity.GetScene()->TryGetInheritedComponent<T>(entity);return value?std::optional<T>(*value):std::nullopt;
    };
    value["Add"]=[](Entity entity){entity.AddComponent<T>();return T(entity.GetComponent<T>());};
    value["Set"]=[](Entity entity,T value){
        if constexpr(std::is_same_v<T,Identity> || std::is_same_v<T,Hierarchy>)throw std::invalid_argument("Use UUID/hierarchy APIs rather than replacing identity or hierarchy");
        else {
            if constexpr(std::is_same_v<T,Name> || std::is_same_v<T,Transform>)value.Enabled=true;
            entity.GetComponent<T>()=std::move(value);
        }
    };
    if constexpr(!std::is_same_v<T,Identity> && !std::is_same_v<T,Hierarchy> && !std::is_same_v<T,Name> && !std::is_same_v<T,Transform>){
        value["Remove"]=[](Entity entity){entity.RemoveComponent<T>();};
        value["SetEnabled"]=[](Entity entity,bool enabled){entity.SetComponentEnabled<T>(enabled);};
        value["IsEnabled"]=[](Entity entity){return entity.IsComponentEnabled<T>();};
    }else{
        value["Remove"]=[](Entity){throw std::invalid_argument("Default components cannot be removed");};
        value["SetEnabled"]=[](Entity,bool){throw std::invalid_argument("Default components cannot be disabled");};
        value["IsEnabled"]=[](Entity){return true;};
    }
}
void BindLuaComponents(sol::table api){
    auto base=api.new_usertype<Component>("Component",sol::constructors<Component()>());
    base["IsEnabled"]=&Component::IsEnabled;base["SetEnabled"]=&Component::SetEnabled;
    base["Enabled"]=&Component::Enabled;
    auto operations=api.create_named("_ComponentOperations");
    {auto type=api.new_usertype<Transform>("Transform",sol::constructors<Transform()>(),sol::base_classes,sol::bases<Component>());
     type["Position"]=&Transform::Position;
     type["Rotation"]=&Transform::Rotation;
     type["Scale"]=&Transform::Scale;
    }
    ComponentOperations<Transform>(operations,"Transform");
    {auto type=api.new_usertype<Name>("Name",sol::constructors<Name()>(),sol::base_classes,sol::bases<Component>());
     type["Value"]=&Name::Value;
    }
    ComponentOperations<Name>(operations,"Name");
    {auto type=api.new_usertype<Identity>("Identity",sol::constructors<Identity()>(),sol::base_classes,sol::bases<Component>());
     type["Value"]=&Identity::Value;
    }
    ComponentOperations<Identity>(operations,"Identity");
    {auto type=api.new_usertype<Hierarchy>("Hierarchy",sol::constructors<Hierarchy()>(),sol::base_classes,sol::bases<Component>());
     type["Parent"]=&Hierarchy::Parent;
     type["Children"]=&Hierarchy::Children;
    }
    ComponentOperations<Hierarchy>(operations,"Hierarchy");
    {auto type=api.new_usertype<Camera>("Camera",sol::constructors<Camera()>(),sol::base_classes,sol::bases<Component>());
     type["Projection"]=&Camera::Projection;
     type["VerticalFieldOfView"]=&Camera::VerticalFieldOfView;
     type["OrthographicSize"]=&Camera::OrthographicSize;
     type["NearPlane"]=&Camera::NearPlane;
     type["FarPlane"]=&Camera::FarPlane;
     type["AspectRatio"]=&Camera::AspectRatio;
     type["AspectMode"]=&Camera::AspectMode;
     type["Viewport"]=&Camera::Viewport;
     type["Priority"]=&Camera::Priority;
     type["Active"]=&Camera::Active;
     type["ClearColor"]=&Camera::ClearColor;
     type["RenderTarget"]=&Camera::RenderTarget;type["SetRenderTarget"]=&Camera::SetRenderTarget;type["GetRenderTarget"]=&Camera::GetRenderTarget;
     type["PostProcessing"]=&Camera::PostProcessing;
    }
    ComponentOperations<Camera>(operations,"Camera");
    {auto type=api.new_usertype<CameraViewport>("CameraViewport",sol::constructors<CameraViewport()>());
     type["X"]=&CameraViewport::X;
     type["Y"]=&CameraViewport::Y;
     type["Width"]=&CameraViewport::Width;
     type["Height"]=&CameraViewport::Height;
    }
    {auto type=api.new_usertype<CameraPostProcessing>("CameraPostProcessing",sol::constructors<CameraPostProcessing()>());
     type["Enabled"]=&CameraPostProcessing::Enabled;
     type["Bloom"]=&CameraPostProcessing::Bloom;
     type["AmbientOcclusion"]=&CameraPostProcessing::AmbientOcclusion;
     type["AntiAliasingMode"]=&CameraPostProcessing::AntiAliasingMode;
     type["ToneMappingMode"]=&CameraPostProcessing::ToneMappingMode;
     type["Exposure"]=&CameraPostProcessing::Exposure;
     type["DepthOfField"]=&CameraPostProcessing::DepthOfField;
     type["CustomEffects"]=&CameraPostProcessing::CustomEffects;
    }
    {auto type=api.new_usertype<Light>("Light",sol::constructors<Light()>(),sol::base_classes,sol::bases<Component>());
     type["Type"]=&Light::Type;
     type["Color"]=&Light::Color;
     type["Intensity"]=&Light::Intensity;
     type["Range"]=&Light::Range;
     type["InnerConeAngle"]=&Light::InnerConeAngle;
     type["OuterConeAngle"]=&Light::OuterConeAngle;
     type["SunAngularRadius"]=&Light::SunAngularRadius;
     type["SunHaloSize"]=&Light::SunHaloSize;
     type["SunHaloFalloff"]=&Light::SunHaloFalloff;
     type["CastShadows"]=&Light::CastShadows;
    }
    ComponentOperations<Light>(operations,"Light");
    {auto type=api.new_usertype<Mesh>("Mesh",sol::constructors<Mesh()>(),sol::base_classes,sol::bases<Component>());
     type["MeshAsset"]=&Mesh::MeshAsset;
     type["MaterialAsset"]=&Mesh::MaterialAsset;
     type["ModelNodeIndex"]=&Mesh::ModelNodeIndex;
     type["Materials"]=&Mesh::Materials;
     type["LayerMask"]=&Mesh::LayerMask;
     type["Visible"]=&Mesh::Visible;
     type["CastShadows"]=&Mesh::CastShadows;
     type["ReceiveShadows"]=&Mesh::ReceiveShadows;
    }
    ComponentOperations<Mesh>(operations,"Mesh");
    {auto type=api.new_usertype<PrimitiveObject>("PrimitiveObject",sol::constructors<PrimitiveObject()>(),sol::base_classes,sol::bases<Component>());
     type["Shape"]=&PrimitiveObject::Shape;
     type["Size"]=&PrimitiveObject::Size;
     type["Radius"]=&PrimitiveObject::Radius;
     type["Height"]=&PrimitiveObject::Height;
     type["Width"]=&PrimitiveObject::Width;
     type["Depth"]=&PrimitiveObject::Depth;
     type["MajorRadius"]=&PrimitiveObject::MajorRadius;
     type["MinorRadius"]=&PrimitiveObject::MinorRadius;
     type["Segments"]=&PrimitiveObject::Segments;
     type["Rings"]=&PrimitiveObject::Rings;
     type["Color"]=&PrimitiveObject::Color;
     type["MaterialAsset"]=&PrimitiveObject::MaterialAsset;
     type["LayerMask"]=&PrimitiveObject::LayerMask;
     type["Visible"]=&PrimitiveObject::Visible;
     type["CastShadows"]=&PrimitiveObject::CastShadows;
     type["ReceiveShadows"]=&PrimitiveObject::ReceiveShadows;
    }
    ComponentOperations<PrimitiveObject>(operations,"PrimitiveObject");
    {auto type=api.new_usertype<GaussianBlur>("GaussianBlur",sol::constructors<GaussianBlur()>(),sol::base_classes,sol::bases<Component>());
     type["Size"]=&GaussianBlur::Size;
    }
    ComponentOperations<GaussianBlur>(operations,"GaussianBlur");
    {auto type=api.new_usertype<Vignette>("Vignette",sol::constructors<Vignette()>(),sol::base_classes,sol::bases<Component>());
     type["Color"]=&Vignette::Color;
     type["Intensity"]=&Vignette::Intensity;
     type["Roundness"]=&Vignette::Roundness;
     type["Smoothness"]=&Vignette::Smoothness;
    }
    ComponentOperations<Vignette>(operations,"Vignette");
    {auto type=api.new_usertype<SceneQueryBounds>("SceneQueryBounds",sol::constructors<SceneQueryBounds()>(),sol::base_classes,sol::bases<Component>());
     type["Shape"]=&SceneQueryBounds::Shape;
     type["Center"]=&SceneQueryBounds::Center;
     type["Extents"]=&SceneQueryBounds::Extents;
     type["Radius"]=&SceneQueryBounds::Radius;
     type["LayerMask"]=&SceneQueryBounds::LayerMask;
    }
    ComponentOperations<SceneQueryBounds>(operations,"SceneQueryBounds");
    {auto type=api.new_usertype<ModelInstance>("ModelInstance",sol::constructors<ModelInstance()>(),sol::base_classes,sol::bases<Component>());
     type["ModelAsset"]=&ModelInstance::ModelAsset;
    }
    ComponentOperations<ModelInstance>(operations,"ModelInstance");
    {auto type=api.new_usertype<ModelNode>("ModelNode",sol::constructors<ModelNode()>(),sol::base_classes,sol::bases<Component>());
     type["ModelAsset"]=&ModelNode::ModelAsset;
     type["SourceIndex"]=&ModelNode::SourceIndex;
     type["MeshIndex"]=&ModelNode::MeshIndex;
     type["StablePath"]=&ModelNode::StablePath;
     type["HasMesh"]=&ModelNode::HasMesh;
    }
    ComponentOperations<ModelNode>(operations,"ModelNode");
    {auto type=api.new_usertype<ScriptPropertyValue>("ScriptPropertyValue",sol::constructors<ScriptPropertyValue()>());
     type["Name"]=&ScriptPropertyValue::Name;
     type["Type"]=&ScriptPropertyValue::Type;
     type["Value"]=&ScriptPropertyValue::Value;
    }
    {auto type=api.new_usertype<ScriptAttachment>("ScriptAttachment",sol::constructors<ScriptAttachment()>());
     type["Source"]=&ScriptAttachment::Source;
     type["TypeName"]=&ScriptAttachment::TypeName;
     type["Enabled"]=&ScriptAttachment::Enabled;
     type["Lua"]=&ScriptAttachment::Lua;
     type["Properties"]=&ScriptAttachment::Properties;
    }
    {auto type=api.new_usertype<ScriptComponents>("ScriptComponents",sol::constructors<ScriptComponents()>(),sol::base_classes,sol::bases<Component>());
     type["Values"]=&ScriptComponents::Values;
    }
    ComponentOperations<ScriptComponents>(operations,"ScriptComponents");
    using DoF=CameraPostProcessing::DepthOfFieldSettings;
    auto dof=api.new_usertype<DoF>("DepthOfFieldSettings",sol::constructors<DoF()>());
    dof["Enabled"]=&DoF::Enabled;
    dof["FocusDistance"]=&DoF::FocusDistance;
    dof["Aperture"]=&DoF::Aperture;
    dof["ShutterSpeed"]=&DoF::ShutterSpeed;
    dof["Sensitivity"]=&DoF::Sensitivity;
    dof["CocScale"]=&DoF::CocScale;
    dof["CocAspectRatio"]=&DoF::CocAspectRatio;
    dof["MaxApertureDiameter"]=&DoF::MaxApertureDiameter;
    dof["Quality"]=&DoF::Quality;
    dof["NativeResolution"]=&DoF::NativeResolution;
    sol::usertype<Transform> transform=api["Transform"];
    transform["GetMatrix"]=&Transform::GetMatrix;transform["GetForward"]=&Transform::GetForward;transform["GetRight"]=&Transform::GetRight;transform["GetUp"]=&Transform::GetUp;transform["Translate"]=&Transform::Translate;transform["Rotate"]=&Transform::Rotate;
    sol::usertype<Mesh> mesh=api["Mesh"];
    mesh["SetMaterial"]=sol::overload([](Mesh& m,Material v){m.SetMaterial(v);},[](Mesh& m,std::size_t slot,Material v){m.SetMaterial(slot,v);});
    mesh["GetMaterial"]=sol::overload([](const Mesh& m){return m.GetMaterial();},[](const Mesh& m,std::size_t slot){return m.GetMaterial(slot);});
    mesh["ClearMaterial"]=sol::overload([](Mesh& m){m.ClearMaterial();},[](Mesh& m,std::size_t slot){m.ClearMaterial(slot);});
    mesh["GetMaterialCount"]=&Mesh::GetMaterialCount;mesh["EntireAsset"]=sol::var(Mesh::EntireAsset);
    sol::usertype<PrimitiveObject> primitive=api["PrimitiveObject"];primitive["SetMaterial"]=&PrimitiveObject::SetMaterial;primitive["GetMaterial"]=&PrimitiveObject::GetMaterial;primitive["ClearMaterial"]=&PrimitiveObject::ClearMaterial;
    sol::usertype<CameraViewport> viewport=api["CameraViewport"];
    viewport["FullScreen"]=&CameraViewport::FullScreen;viewport["LeftHalf"]=&CameraViewport::LeftHalf;viewport["RightHalf"]=&CameraViewport::RightHalf;viewport["TopHalf"]=&CameraViewport::TopHalf;viewport["BottomHalf"]=&CameraViewport::BottomHalf;viewport["Grid"]=&CameraViewport::Grid;
}
}
