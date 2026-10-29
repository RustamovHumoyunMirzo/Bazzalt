#include "Rendering/LODVisibility.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/LOD.h"
#include "Bazzalt/Console.h"
#include "Physics/PhysicsModuleApi.h"
namespace Bazzalt::Runtime {
void EvaluateLOD(Scene& scene,Vec3 eye,float projectionYScale,bool orthographic,LODViewState& state){
    state.Hidden.clear();std::unordered_set<UUID> alive;
    const auto descendant=[&](Entity child,Entity ancestor){std::size_t remaining=scene.GetEntityCount()+1;for(auto e=child;e&&remaining--;e=e.GetParent())if(e==ancestor)return true;return false;};
    for(auto handle:scene.GetRegistry().view<LOD>()){
        auto group=scene.GetEntity(static_cast<Entity::Id>(handle));const auto id=group.GetUUID();alive.insert(id);const auto& lod=group.GetComponent<LOD>();
        bool valid=lod.IsValid();std::vector<std::pair<Entity,unsigned>> roots;
        if(valid)for(unsigned level=0;level<lod.Levels.size();++level)for(auto root:lod.Levels[level].Roots){auto entity=scene.GetEntity(root);if(!entity||entity==group||!descendant(entity,group)){valid=false;break;}roots.emplace_back(entity,level);}
        if(valid)for(std::size_t a=0;a<roots.size();++a)for(std::size_t b=a+1;b<roots.size();++b)if(roots[a].second!=roots[b].second&&(descendant(roots[a].first,roots[b].first)||descendant(roots[b].first,roots[a].first)))valid=false;
        if(!valid){state.Levels.erase(id);if(state.Warnings.insert(id).second)Console::Warning("Invalid LOD group "+id.ToString()+": assign distinct child subtrees and descending screen thresholds.","Rendering");continue;}
        state.Warnings.erase(id);const auto old=state.Levels.find(id);const auto height=lod.GetScreenHeight(PhysicsRenderMatrix(scene,group),eye,projectionYScale,orthographic);
        const int selected=lod.SelectLevel(height,old==state.Levels.end()?-2:old->second);state.Levels[id]=selected;
        for(const auto& [root,level]:roots)if(int(level)!=selected){std::vector<Entity> pending{root};while(!pending.empty()){auto entity=pending.back();pending.pop_back();if(!state.Hidden.insert(entity.GetUUID()).second)continue;for(auto child:scene.GetChildren(entity))pending.push_back(child);}}
    }
    std::erase_if(state.Levels,[&](const auto& item){return !alive.contains(item.first);});std::erase_if(state.Warnings,[&](UUID id){return !alive.contains(id);});
}
}
