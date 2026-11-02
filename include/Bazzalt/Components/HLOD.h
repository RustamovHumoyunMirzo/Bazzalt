#pragma once
#include "Bazzalt/Components/LOD.h"
namespace Bazzalt {
// Authored cluster proxy: Sources may themselves contain nested LOD/HLOD groups.
// Both representations must be distinct child subtrees of this entity.
struct HLOD : Component {
    Vec3 Center{};
    float Size=1,Bias=1,Hysteresis=.1f,ScreenHeight=.08f,CullHeight=0;
    int ForcedLevel=-1;
    std::vector<UUID> Sources,Proxies;
    [[nodiscard]] LOD GetLOD() const {
        LOD value;value.Enabled=Enabled;value.Center=Center;value.Size=Size;value.Bias=Bias;value.Hysteresis=Hysteresis;value.Cull=CullHeight>0;value.ForcedLevel=ForcedLevel;
        value.Levels={{ScreenHeight,Sources},{CullHeight,Proxies}};return value;
    }
    [[nodiscard]] bool IsValid() const{return GetLOD().IsValid();}
    [[nodiscard]] bool IsConfigured() const{return IsValid()&&!Sources.empty()&&!Proxies.empty();}
    void ForceLevel(int level){if(level<-2||level>1)throw std::out_of_range("Invalid HLOD level");ForcedLevel=level;}
};
}
