#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
struct NavMeshSurface : Component {
    UUID Data{};
    Vec3 Center{}, Size{100, 40, 100};
    float CellSize = .25f, CellHeight = .15f;
    float AgentRadius = .5f, AgentHeight = 2, MaxSlope = 45, MaxClimb = .4f;
    float MaxSimplificationError = 1.3f;
    unsigned TileSize = 48, MaxAgents = 256, MaxObstacles = 256, LayerMask = 0xffffffffu;
    bool UsePhysicsGeometry = false, AutoBake = false;
    bool IsValid() const {
        auto positive = [](float v) { return std::isfinite(v) && v > 0; };
        return std::isfinite(Center.X) && std::isfinite(Center.Y) && std::isfinite(Center.Z) &&
               positive(Size.X) && positive(Size.Y) && positive(Size.Z) && Size.X <= 100000 &&
               Size.Y <= 10000 && Size.Z <= 100000 && CellSize >= .05f && CellSize <= 10 &&
               CellHeight >= .02f && CellHeight <= 5 && positive(AgentRadius) &&
               positive(AgentHeight) && AgentRadius <= 20 && AgentHeight <= 50 &&
               std::isfinite(MaxSlope) && MaxSlope >= 0 && MaxSlope < 90 &&
               std::isfinite(MaxClimb) && MaxClimb >= 0 && MaxClimb < AgentHeight &&
               positive(MaxSimplificationError) && MaxSimplificationError <= 10 && TileSize >= 16 &&
               TileSize <= 128 && MaxAgents > 0 && MaxAgents <= 4096 && MaxObstacles > 0 &&
               MaxObstacles <= 4096;
    }
};
} // namespace Bazzalt
