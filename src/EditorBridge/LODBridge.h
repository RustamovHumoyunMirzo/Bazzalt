#pragma once
#include "EditorBridge/PhysicsBridge.h"
#include "Bazzalt/Components/LOD.h"
namespace Bazzalt::EditorBridge {
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
        if(!value.IsValid())return false;
        for(std::size_t a=0;a<value.Levels.size();++a)for(std::size_t b=a+1;b<value.Levels.size();++b)for(auto left:value.Levels[a].Roots)for(auto right:value.Levels[b].Roots){auto l=entity.GetScene()->GetEntity(left),r=entity.GetScene()->GetEntity(right);if(l&&r&&(entity.GetScene()->IsAncestor(r,l)||entity.GetScene()->IsAncestor(l,r)))return false;}
        entity.GetComponent<LOD>()=std::move(value);return true;
    }catch(const py::cast_error&){return false;}
}
}
