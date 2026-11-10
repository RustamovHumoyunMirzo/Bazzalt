#pragma once
#include "Bazzalt/Scene.h"
#include "Bazzalt/GUI.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/Terrain.h"
#include <set>

namespace Bazzalt::EditorBridge {
// Inspect references directly. Editor polling must not serialize geometry arrays.
inline void CollectMaterialUsage(Scene& scene,std::set<UUID>& materials){
    const auto add=[&](UUID id){if(id)materials.insert(id);};
    const auto& environment=scene.GetEnvironment();
    if(environment.Mode==SceneEnvironmentMode::Material)add(environment.MaterialAsset);
    auto& registry=scene.GetRegistry();
    for(auto handle:registry.view<GuiElement>()){
        const auto& value=registry.get<GuiElement>(handle);
        if(value.Enabled)add(value.MaterialAsset);
    }
    for(auto handle:registry.view<Camera>()){
        const auto& value=registry.get<Camera>(handle);
        if(value.Enabled)for(const auto& pass:value.PostProcessing.GetMaterials())if(pass.Enabled)add(pass.MaterialAsset);
    }
    for(auto handle:registry.view<Terrain>()){
        const auto& value=registry.get<Terrain>(handle);
        if(!value.Enabled)continue;
        for(const auto& layer:value.Layers)if(layer.Enabled)add(layer.Material);
        for(const auto& prototype:value.Vegetation)if(prototype.Enabled)add(prototype.Material);
        for(const auto& spline:value.Splines)add(spline.Material);
    }
    for(auto handle:registry.view<PrimitiveObject>()){
        const auto& value=registry.get<PrimitiveObject>(handle);
        if(value.Enabled)add(value.MaterialAsset);
    }
    for(auto handle:registry.view<Mesh>()){
        const auto& value=registry.get<Mesh>(handle);
        if(!value.Enabled||!value.MeshAsset)continue;
        add(value.MaterialAsset);
        for(auto id:value.Materials)add(id);
    }
}
}
