#pragma once
#include "Bazzalt/Component.h"
#include <cmath>
namespace Bazzalt {
/// Attach to players/cameras. All enabled sources contribute; any source can retain a cell.
struct StreamingSource : Component {
    float DistanceScale=1;
    bool IsValid()const{return std::isfinite(DistanceScale)&&DistanceScale>0&&DistanceScale<=1000;}
};
}
