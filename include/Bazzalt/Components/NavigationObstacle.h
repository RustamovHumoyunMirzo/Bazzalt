#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Navigation.h"
namespace Bazzalt {
struct NavigationObstacle : Component {
    NavigationObstacleShape Shape = NavigationObstacleShape::Box;
    Vec3 Center{}, Size{1, 2, 1};
    float Radius = .5f, Height = 2;
    bool Carve = true;
    bool IsValid() const {
        return (Shape == NavigationObstacleShape::Box ||
                Shape == NavigationObstacleShape::Cylinder) &&
               std::isfinite(Center.X) && std::isfinite(Center.Y) && std::isfinite(Center.Z) &&
               std::isfinite(Size.X) && Size.X > 0 && std::isfinite(Size.Y) && Size.Y > 0 &&
               std::isfinite(Size.Z) && Size.Z > 0 && std::isfinite(Radius) && Radius > 0 &&
               std::isfinite(Height) && Height > 0;
    }
};
} // namespace Bazzalt
