#include "Rendering/LODVisibility.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/LOD.h"
#include "Bazzalt/Components/HLOD.h"
#include "Bazzalt/Components/LODTransition.h"
#include "Bazzalt/Console.h"
#include "Physics/PhysicsModuleApi.h"
#include <chrono>
#include <bit>
namespace Bazzalt::Runtime {
void LODViewState::RejectUnsupported(UUID owner,bool supported){if(supported||HardHidden.contains(owner))return;auto found=Contributions.find(owner);if(found!=Contributions.end()){for(const auto& row:found->second)if(!row.Fading&&row.Mask.Minimum>=row.Mask.Maximum&&!row.Selected)return;for(const auto& contribution:found->second)if(contribution.Fading)UnsupportedGroups.insert(contribution.Group);}}
void LODViewState::Resolve(){
    Hidden=HardHidden;Masks.clear();
    for(const auto& [owner,rows]:Contributions){if(HardHidden.contains(owner))continue;LODMask mask;
        for(const auto& row:rows){auto next=UnsupportedGroups.contains(row.Group)?LODMask{0,row.Selected?1.f:0.f}:row.Mask;mask.Minimum=std::max(mask.Minimum,next.Minimum);mask.Maximum=std::min(mask.Maximum,next.Maximum);}
        if(mask.Minimum>=mask.Maximum)Hidden.insert(owner);else if(mask.Minimum>0||mask.Maximum<1)Masks[owner]=mask;
    }
}
void EvaluateLOD(Scene& scene,Vec3 eye,float projectionYScale,bool orthographic,LODViewState& state,double now){
    if(now<0)now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    state.Contributions.clear();state.HardHidden.clear();state.UnsupportedGroups.clear();state.HasFades=false;
    std::unordered_set<UUID> alive;std::vector<Entity> groups;
    for(auto h:scene.GetRegistry().view<LOD>())groups.push_back(scene.GetEntity(static_cast<Entity::Id>(h)));
    for(auto h:scene.GetRegistry().view<HLOD>()){auto e=scene.GetEntity(static_cast<Entity::Id>(h));if(!e.HasComponent<LOD>())groups.push_back(e);}
    auto depth=[&](Entity e){std::size_t d=0,limit=scene.GetEntityCount()+1;while(e&&limit--){++d;e=e.GetParent();}return d;};
    std::sort(groups.begin(),groups.end(),[&](Entity a,Entity b){auto x=depth(a),y=depth(b);return x==y?a.GetUUID()<b.GetUUID():x<y;});
    const auto descendant=[&](Entity child,Entity ancestor){std::size_t remaining=scene.GetEntityCount()+1;for(auto e=child;e&&remaining--;e=e.GetParent())if(e==ancestor)return true;return false;};
    for(auto group:groups){
        const auto id=group.GetUUID();alive.insert(id);
        bool hidden=state.HardHidden.contains(id);if(auto inherited=state.Contributions.find(id);inherited!=state.Contributions.end())for(const auto& row:inherited->second)if(row.Mask.Minimum>=row.Mask.Maximum&&!row.Selected)hidden=true;
        if(hidden){state.Levels.erase(id);state.Blends.erase(id);continue;}
        auto* cluster=group.TryGetComponent<HLOD>();auto lod=cluster?cluster->GetLOD():group.GetComponent<LOD>();
        bool valid=lod.IsValid()&&(!cluster||cluster->IsConfigured())&&!(cluster&&group.HasComponent<LOD>());std::vector<std::pair<Entity,unsigned>> roots;
        if(valid)for(unsigned level=0;level<lod.Levels.size();++level)for(auto root:lod.Levels[level].Roots){auto entity=scene.GetEntity(root);if(!entity||entity==group||!descendant(entity,group)){valid=false;break;}roots.emplace_back(entity,level);}
        if(valid)for(std::size_t a=0;a<roots.size();++a)for(std::size_t b=a+1;b<roots.size();++b)if(descendant(roots[a].first,roots[b].first)||descendant(roots[b].first,roots[a].first))valid=false;
        if(!valid){state.Levels.erase(id);state.Blends.erase(id);if(state.Warnings.insert(id).second)Console::Warning("Invalid LOD/HLOD group "+id.ToString()+": assign non-overlapping child subtrees and valid descending thresholds; use one group component per entity.","Rendering");continue;}
        state.Warnings.erase(id);const auto* transition=group.TryGetComponent<LODTransition>();
        const bool smooth=transition&&transition->Enabled&&transition->IsValid()&&lod.Enabled&&lod.ForcedLevel==-1;
        std::uint64_t signature=14695981039346656037ULL;auto mix=[&](std::uint64_t v){signature=(signature^v)*1099511628211ULL;};
        mix(lod.Enabled);mix(std::uint64_t(std::int64_t(lod.ForcedLevel)));mix(lod.Cull);mix(std::bit_cast<std::uint32_t>(lod.Bias));mix(std::bit_cast<std::uint32_t>(lod.Hysteresis));
        if(transition){mix(transition->Enabled);mix(std::bit_cast<std::uint32_t>(transition->Duration));mix(unsigned(transition->Easing));mix(transition->FadeCulling);}
        for(const auto& level:lod.Levels){mix(std::bit_cast<std::uint32_t>(level.ScreenHeight));for(auto root:level.Roots){mix(root.GetHigh());mix(root.GetLow());}}
        auto old=state.Levels.find(id);auto& blend=state.Blends[id];if(blend.Signature!=signature){old=state.Levels.end();blend={};blend.Signature=signature;}
        const auto height=lod.GetScreenHeight(PhysicsRenderMatrix(scene,group),eye,projectionYScale,orthographic);
        const bool first=old==state.Levels.end();const int selected=lod.SelectLevel(height,first?-2:old->second);state.Levels[id]=selected;
        if(first||!smooth||(!transition->FadeCulling&&(selected<0||blend.From<0||blend.To<0))){blend.From=blend.To=selected;blend.Progress=1;}
        else{
            const float delta=float(std::max(0.0,now-blend.LastTime))/transition->Duration;
            if(selected==blend.From&&blend.From!=blend.To){blend.Progress=std::max(0.f,blend.Progress-delta);if(blend.Progress==0){blend.To=blend.From;blend.Progress=1;}}
            else{
                if(selected!=blend.To){blend.From=blend.Progress<.5f?blend.From:blend.To;blend.To=selected;blend.Progress=0;}
                else blend.Progress=std::min(1.f,blend.Progress+delta);
                if(blend.Progress>=1)blend.From=blend.To;
            }
        }
        blend.LastTime=now;const bool active=blend.From!=blend.To;const float p=active?transition->Evaluate(blend.Progress):1;
        state.HasFades|=active;
        for(const auto& [root,level]:roots){const bool current=int(level)==selected;LODMask mask{0,current?1.f:0.f};
            if(active){mask={0,0};if(int(level)==blend.From)mask={p,1};else if(int(level)==blend.To)mask={0,p};}
            const bool participating=active&&(int(level)==blend.From||int(level)==blend.To);
            if(!active&&current)continue;
            std::vector<Entity> pending{root};while(!pending.empty()){auto entity=pending.back();pending.pop_back();if(participating)state.Contributions[entity.GetUUID()].push_back({id,mask,current,true});else state.HardHidden.insert(entity.GetUUID());for(auto child:scene.GetChildren(entity))pending.push_back(child);}
        }
    }
    std::erase_if(state.Levels,[&](const auto& item){return !alive.contains(item.first);});std::erase_if(state.Blends,[&](const auto& item){return !alive.contains(item.first);});std::erase_if(state.Warnings,[&](UUID id){return !alive.contains(id);});std::erase_if(state.FallbackWarnings,[&](UUID id){return !alive.contains(id);});state.Resolve();
}
}
