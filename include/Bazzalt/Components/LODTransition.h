#pragma once
#include "Bazzalt/Component.h"
#include <cmath>
#include <algorithm>
namespace Bazzalt {
enum class LODEasing { Linear, SmoothStep };
// Attach alongside LOD or HLOD. Unsupported material groups use an atomic hard cut.
struct LODTransition : Component {
    float Duration=.25f;
    LODEasing Easing=LODEasing::SmoothStep;
    bool FadeCulling=true;
    [[nodiscard]] bool IsValid() const{return std::isfinite(Duration)&&Duration>=.01f&&Duration<=10&&unsigned(Easing)<=1;}
    [[nodiscard]] float Evaluate(float progress) const{auto p=std::clamp(progress,0.f,1.f);return Easing==LODEasing::SmoothStep?p*p*(3-2*p):p;}
};
}
