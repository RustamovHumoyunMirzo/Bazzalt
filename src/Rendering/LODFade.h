#pragma once
#include "Rendering/LODVisibility.h"
#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <map>
#include <utils/Entity.h>
#include <string_view>
#include <tuple>
namespace Bazzalt::Runtime {
inline bool SupportsLODFade(const filament::MaterialInstance* instance){
    if(!instance)return false;auto* material=instance->getMaterial();if(!material->hasParameter("bazzaltLodFade"))return false;
    std::vector<filament::Material::ParameterInfo> parameters(material->getParameterCount());material->getParameters(parameters.data(),parameters.size());
    for(const auto& p:parameters)if(std::string_view(p.name)=="bazzaltLodFade")return !p.isSampler&&!p.isSubpass&&p.count==1&&p.type==filament::Material::ParameterType::FLOAT4;return false;
}
struct LODFadeSwaps {
    struct Original {utils::Entity Entity;std::size_t Primitive;const filament::MaterialInstance* Material;};
    std::vector<Original> Originals;
    std::map<std::tuple<const filament::MaterialInstance*,float,float>,filament::MaterialInstance*> Copies;
    void Apply(filament::Engine& engine,utils::Entity entity,LODMask mask){
        auto& manager=engine.getRenderableManager();auto instance=manager.getInstance(entity);if(!instance)return;
        for(std::size_t i=0;i<manager.getPrimitiveCount(instance);++i){auto* original=manager.getMaterialInstanceAt(instance,i);if(!SupportsLODFade(original))continue;
            auto key=std::make_tuple(original,mask.Minimum,mask.Maximum);auto& copy=Copies[key];if(!copy){copy=filament::MaterialInstance::duplicate(original);copy->setParameter("bazzaltLodFade",filament::math::float4{mask.Minimum,mask.Maximum,1,0});}
            Originals.push_back({entity,i,original});manager.setMaterialInstanceAt(instance,i,copy);
        }
    }
    void Restore(filament::Engine& engine){auto& manager=engine.getRenderableManager();for(const auto& original:Originals){auto instance=manager.getInstance(original.Entity);if(instance&&original.Primitive<manager.getPrimitiveCount(instance))manager.setMaterialInstanceAt(instance,original.Primitive,original.Material);}Originals.clear();}
    void Clear(filament::Engine& engine){Restore(engine);for(auto& [_,copy]:Copies)engine.destroy(copy);Copies.clear();}
};
}
