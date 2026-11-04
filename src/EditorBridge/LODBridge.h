#pragma once
#include "EditorBridge/PhysicsBridge.h"
#include "Bazzalt/Components/LOD.h"
#include "Rendering/AdvancedLODFields.h"
#include "VFX/ParticleFields.h"
#include "Animation/AnimationFields.h"
namespace Bazzalt::EditorBridge {
inline bool ValidLODChildren(Entity owner,const LOD& lod){
    std::vector<Entity> roots;for(const auto& level:lod.Levels)for(auto id:level.Roots){auto target=owner.GetScene()->GetEntity(id);if(!target||target==owner||!owner.GetScene()->IsAncestor(owner,target))return false;roots.push_back(target);}
    for(std::size_t a=0;a<roots.size();++a)for(std::size_t b=a+1;b<roots.size();++b)if(owner.GetScene()->IsAncestor(roots[a],roots[b])||owner.GetScene()->IsAncestor(roots[b],roots[a]))return false;return true;
}
inline std::string AdvancedLODLabel(const char* name){static const std::unordered_map<std::string,std::string> labels{{"AnimatorEntity","Animator Entity"},{"PoleTarget","Pole Target"},{"RootBone","Root Bone"},{"TipBone","Tip Bone"},{"RootMotion","Root Motion"},{"DefaultClip","Default Clip"},{"AnimatePhysics","Animate Physics"},{"TargetOffset","Target Offset"},{"GroundRaycast","Ground Raycast"},{"RayDistance","Ray Distance"},{"GroundOffset","Ground Offset"},{"ScreenHeight","Screen Height"},{"CullHeight","Cull Height"},{"ForcedLevel","Forced Level"},{"FadeCulling","Fade Culling"},{"ViewCount","View Count"},{"AzimuthOffset","Azimuth Offset"},{"AlphaCutoff","Alpha Cutoff"},{"LayerMask","Layer Mask"},{"CastShadows","Cast Shadows"},{"MaterialOverride","Material Override"},{"MaxParticles","Max Particles"},{"SimulationSpeed","Simulation Speed"},{"RateMultiplier","Rate Multiplier"},{"SizeMultiplier","Size Multiplier"},{"PlayOnAwake","Play On Awake"},{"UseUnscaledTime","Use Unscaled Time"}};auto found=labels.find(name);return found==labels.end()?name:found->second;}
template<class T> py::object AdvancedLODValue(const T& value){
    if constexpr(std::is_same_v<T,Vec2>)return py::make_tuple(value.X,value.Y);
    else if constexpr(std::is_same_v<T,Vec4>)return py::make_tuple(value.X,value.Y,value.Z,value.W);
    else if constexpr(std::is_same_v<T,std::vector<UUID>>){py::list result;for(auto id:value)result.append(id.ToString());return result;}
    else return PhysicsValue(value);
}
template<class T> bool SetAdvancedLODValue(T& value,py::handle input){
    if constexpr(std::is_same_v<T,Vec2>){auto v=py::cast<std::array<float,2>>(input);value={v[0],v[1]};return true;}
    else if constexpr(std::is_same_v<T,Vec4>){auto v=py::cast<std::array<float,4>>(input);value={v[0],v[1],v[2],v[3]};return true;}
    else if constexpr(std::is_same_v<T,std::vector<UUID>>){auto rows=py::cast<std::vector<std::string>>(input);if(rows.size()>256)return false;value.clear();for(const auto& row:rows){UUID id;if(!UUID::TryParse(row,id)||!id)return false;value.push_back(id);}return true;}
    else if constexpr(std::is_same_v<T,std::uint8_t>){int n=py::cast<int>(input);if(n<0||n>127)return false;value=std::uint8_t(n);return true;}
    else return SetPhysicsValue(value,input);
}
template<class F> bool AdvancedLODDispatch(Entity e,const std::string& name,F function){
    if(name=="IK Constraint")function.template operator()<IKConstraint>(e);else if(name=="Animator")function.template operator()<Animator>(e);else if(name=="Particle Emitter")function.template operator()<ParticleEmitter>(e);else if(name=="HLOD")function.template operator()<HLOD>(e);else if(name=="LOD Transition")function.template operator()<LODTransition>(e);else if(name=="Impostor")function.template operator()<Impostor>(e);else return false;return true;
}
inline void InspectAdvancedLOD(Entity e,py::list& components,py::dict& enabled,py::dict& data){
#define INSPECT(Type,Label,Visit) if(auto* value=e.TryGetComponent<Type>()){components.append(Label);enabled[Label]=value->Enabled;py::dict fields;Runtime::Visit(*value,[&](const char* name,const auto& field){fields[py::str(AdvancedLODLabel(name))]=AdvancedLODValue(field);});data[Label]=fields;}
    INSPECT(IKConstraint,"IK Constraint",VisitIKConstraint) INSPECT(Animator,"Animator",VisitAnimator) INSPECT(ParticleEmitter,"Particle Emitter",VisitParticleEmitter) INSPECT(HLOD,"HLOD",VisitHLOD) INSPECT(LODTransition,"LOD Transition",VisitLODTransition) INSPECT(Impostor,"Impostor",VisitImpostor)
#undef INSPECT
}
inline bool SetAdvancedLODProperty(Entity e,const std::string& type,const std::string& name,py::object input){
    try{
#define SET(Type,Label,Visit) if(type==Label&&e.HasComponent<Type>()){auto value=e.GetComponent<Type>();bool changed=false;Runtime::Visit(value,[&](const char* key,auto& field){if(name==AdvancedLODLabel(key))changed=SetAdvancedLODValue(field,input);});if(!changed||!value.IsValid())return false;if constexpr(std::is_same_v<Type,HLOD>){if(!ValidLODChildren(e,value.GetLOD()))return false;}e.GetComponent<Type>()=std::move(value);return true;}
        SET(HLOD,"HLOD",VisitHLOD)
#undef SET
        if(type=="IK Constraint"&&e.HasComponent<IKConstraint>()){auto value=e.GetComponent<IKConstraint>();bool changed=false;Runtime::VisitIKConstraint(value,[&](const char* key,auto& field){if(name==AdvancedLODLabel(key))changed=SetAdvancedLODValue(field,input);});if(!changed||!value.IsValid())return false;e.GetComponent<IKConstraint>()=value;return true;}
        if(type=="Animator"&&e.HasComponent<Animator>()){auto value=e.GetComponent<Animator>();bool changed=false;Runtime::VisitAnimator(value,[&](const char* key,auto& field){if(name==AdvancedLODLabel(key))changed=SetAdvancedLODValue(field,input);});if(!changed||!value.IsValid())return false;e.GetComponent<Animator>()=value;return true;}
        if(type=="Particle Emitter"&&e.HasComponent<ParticleEmitter>()){auto value=e.GetComponent<ParticleEmitter>();bool changed=false;Runtime::VisitParticleEmitter(value,[&](const char* key,auto& field){if(name==AdvancedLODLabel(key))changed=SetAdvancedLODValue(field,input);});if(!changed||!value.IsValid())return false;e.GetComponent<ParticleEmitter>()=value;return true;}
        if(type=="LOD Transition"&&e.HasComponent<LODTransition>()){auto value=e.GetComponent<LODTransition>();bool changed=false;Runtime::VisitLODTransition(value,[&](const char* key,auto& field){if(name==AdvancedLODLabel(key))changed=SetAdvancedLODValue(field,input);});if(!changed||!value.IsValid())return false;e.GetComponent<LODTransition>()=value;return true;}
        if(type=="Impostor"&&e.HasComponent<Impostor>()){auto value=e.GetComponent<Impostor>();bool changed=false;Runtime::VisitImpostor(value,[&](const char* key,auto& field){if(name==AdvancedLODLabel(key))changed=SetAdvancedLODValue(field,input);});if(!changed||!value.IsValid())return false;e.GetComponent<Impostor>()=value;return true;}
    }catch(const py::cast_error&){}return false;
}
inline void InspectLOD(Entity entity,py::list& components,py::dict& enabled,py::dict& data){
    auto* lod=entity.TryGetComponent<LOD>();if(!lod)return;components.append("LOD");enabled["LOD"]=lod->Enabled;py::dict v;
    v["Center"]=PhysicsValue(lod->Center);v["Size"]=lod->Size;v["Bias"]=lod->Bias;v["Hysteresis"]=lod->Hysteresis;v["Cull"]=lod->Cull;v["Forced Level"]=lod->ForcedLevel;
    py::list levels;for(const auto& level:lod->Levels){py::dict row;row["ScreenHeight"]=level.ScreenHeight;py::list roots;for(auto id:level.Roots)roots.append(id.ToString());row["Roots"]=roots;levels.append(row);}v["Levels"]=levels;data["LOD"]=v;
}
inline bool SetLODProperty(Entity entity,const std::string& name,py::object input){
    if(!entity.HasComponent<LOD>())return false;auto value=entity.GetComponent<LOD>();
    try{
        if(name=="Center"){if(!SetPhysicsValue(value.Center,input))return false;}
        else if(name=="Size")value.Size=input.cast<float>();else if(name=="Bias")value.Bias=input.cast<float>();else if(name=="Hysteresis")value.Hysteresis=input.cast<float>();else if(name=="Cull")value.Cull=input.cast<bool>();else if(name=="Forced Level")value.ForcedLevel=input.cast<int>();
        else if(name=="Levels"){
            auto rows=input.cast<py::list>();if(rows.size()==0||rows.size()>8)return false;value.Levels.clear();
            for(auto row:rows){auto data=py::cast<py::dict>(row);if(!data.contains("ScreenHeight")||!data.contains("Roots"))return false;LODLevel level;level.ScreenHeight=data["ScreenHeight"].cast<float>();auto roots=data["Roots"].cast<py::list>();if(roots.size()>256)return false;
                for(auto root:roots){UUID id;if(!UUID::TryParse(py::cast<std::string>(root),id))return false;auto target=entity.GetScene()->GetEntity(id);if(!target||target==entity||!entity.GetScene()->IsAncestor(entity,target))return false;level.Roots.push_back(id);}value.Levels.push_back(std::move(level));}
            if(value.ForcedLevel>=int(value.Levels.size()))value.ForcedLevel=-1;
        }else return false;
        if(!value.IsValid()||!ValidLODChildren(entity,value))return false;
        for(std::size_t a=0;a<value.Levels.size();++a)for(std::size_t b=a+1;b<value.Levels.size();++b)for(auto left:value.Levels[a].Roots)for(auto right:value.Levels[b].Roots){auto l=entity.GetScene()->GetEntity(left),r=entity.GetScene()->GetEntity(right);if(l&&r&&(entity.GetScene()->IsAncestor(r,l)||entity.GetScene()->IsAncestor(l,r)))return false;}
        entity.GetComponent<LOD>()=std::move(value);return true;
    }catch(const py::cast_error&){return false;}
}
}
