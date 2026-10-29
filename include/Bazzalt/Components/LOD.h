#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <vector>
#include <stdexcept>
#include <unordered_set>
namespace Bazzalt {
struct LODLevel {
    float ScreenHeight=.5f;
    std::vector<UUID> Roots;
};
// Render-only groups. Roots name non-overlapping child subtrees, never the owner.
struct LOD : Component {
    Vec3 Center{};
    float Size=1,Bias=1,Hysteresis=.1f;
    bool Cull=true;
    int ForcedLevel=-1; // -1 automatic, -2 culled, otherwise a zero-based level.
    std::vector<LODLevel> Levels{{.5f,{}},{.15f,{}},{.02f,{}}};
    [[nodiscard]] bool IsValid() const {
        if(Levels.empty()||Levels.size()>8||!std::isfinite(Size)||Size<=0||!std::isfinite(Bias)||Bias<=0||
           !std::isfinite(Center.X)||!std::isfinite(Center.Y)||!std::isfinite(Center.Z)||
           !std::isfinite(Hysteresis)||Hysteresis<0||Hysteresis>.5f||ForcedLevel<-2||ForcedLevel>=int(Levels.size()))return false;
        float previous=2;std::size_t rootCount=0;
        for(const auto& level:Levels){if(!std::isfinite(level.ScreenHeight)||level.ScreenHeight<0||level.ScreenHeight>1||level.ScreenHeight>=previous||level.Roots.size()>256)return false;previous=level.ScreenHeight;rootCount+=level.Roots.size();}
        // Ordinary model groups use a few subtree roots; avoid per-camera heap churn.
        if(rootCount<=32){std::array<UUID,32> roots;std::size_t count=0;for(const auto& level:Levels)for(auto id:level.Roots){if(!id||std::find(roots.begin(),roots.begin()+count,id)!=roots.begin()+count)return false;roots[count++]=id;}}
        else{std::unordered_set<UUID> roots;roots.reserve(rootCount);for(const auto& level:Levels)for(auto id:level.Roots)if(!id||!roots.insert(id).second)return false;}
        return true;
    }
    void ForceLevel(int level){if(level<-2||level>=int(Levels.size()))throw std::out_of_range("Invalid forced LOD level");ForcedLevel=level;}
    // Returns -1 for culled. previous=-2 disables hysteresis on first evaluation.
    [[nodiscard]] int SelectLevel(float screenHeight,int previous=-2) const {
        if(!IsValid())return -1;
        if(!Enabled)return 0;
        if(ForcedLevel!=-1)return ForcedLevel==-2?-1:ForcedLevel;
        if(!std::isfinite(screenHeight)||screenHeight<0)return -1;
        const int count=int(Levels.size());const float height=screenHeight*Bias;
        int candidate=count;for(int i=0;i<count;++i)if(height>=Levels[i].ScreenHeight){candidate=i;break;}
        if(!Cull&&candidate==count)candidate=count-1;
        if(previous>=-1&&previous<count){const int old=previous<0?count:previous;
            if(candidate<old){candidate=old;for(int i=0;i<old;++i)if(height>=Levels[i].ScreenHeight*(1+Hysteresis)){candidate=i;break;}}
            else if(candidate>old){candidate=old;while(candidate<count&&height<Levels[candidate].ScreenHeight*(1-Hysteresis))++candidate;}
        }
        if(!Cull&&candidate==count)candidate=count-1;
        return candidate==count?-1:candidate;
    }
    [[nodiscard]] float GetScreenHeight(const Mat4& world,Vec3 cameraPosition,float projectionYScale,bool orthographic=false) const {
        const float scale=std::max({world.TransformDirection({1,0,0}).Length(),world.TransformDirection({0,1,0}).Length(),world.TransformDirection({0,0,1}).Length()});
        const float diameter=Size*scale;const float distance=(world.TransformPoint(Center)-cameraPosition).Length();
        if(!std::isfinite(diameter)||!std::isfinite(distance)||!std::isfinite(projectionYScale))return 0;
        if(!orthographic&&distance<=Epsilon)return std::numeric_limits<float>::max();
        const double height=double(diameter)*std::abs(double(projectionYScale))*.5/(orthographic?1:distance);
        return float(std::min(height,double(std::numeric_limits<float>::max())));
    }
};
}
