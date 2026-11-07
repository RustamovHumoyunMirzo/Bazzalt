#pragma once
#include "Bazzalt/Entity.h"
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include <filesystem>
#include <vector>

namespace Bazzalt {
class Scene;
enum class NavigationPathStatus { Invalid, Partial, Complete };
enum class NavigationAgentStatus { Idle, Moving, Arrived, Blocked, Invalid };
enum class NavigationObstacleShape { Box, Cylinder };
struct NavigationPath {
    NavigationPathStatus Status = NavigationPathStatus::Invalid;
    std::vector<Vec3> Corners;
    float Length = 0;
};
struct NavigationHit {
    bool Found = false;
    Vec3 Position{}, Normal{};
    float Distance = 0;
};
struct NavigationAgentState {
    NavigationAgentStatus Status = NavigationAgentStatus::Invalid;
    Vec3 Position{}, Velocity{}, Destination{};
    float RemainingDistance = 0;
};
struct NavigationStatistics {
    unsigned Surfaces = 0, Tiles = 0, Agents = 0, Obstacles = 0;
    bool ObstacleUpdatesPending = false;
};
// Scene-owned navigation. Methods run on the engine thread. Coordinates are world-space.
class BAZZALT_API Navigation final {
  public:
    static void Bake(Entity surface);
    static void Clear(Scene &scene);
    static bool IsReady(Entity surface);
    static void Save(Entity surface, const std::filesystem::path &destination);
    static void Load(Entity surface, const std::filesystem::path &source);
    static void LoadAsset(Entity surface, UUID asset);
    static NavigationPath CalculatePath(Entity surface, Vec3 start, Vec3 end,
                                        unsigned areaMask = 0xffff);
    static NavigationHit SamplePosition(Entity surface, Vec3 position, Vec3 extents = {2, 4, 2},
                                        unsigned areaMask = 0xffff);
    static NavigationHit Raycast(Entity surface, Vec3 start, Vec3 end, unsigned areaMask = 0xffff);
    static void SetAreaCost(Entity surface, unsigned area, float cost);
    static bool SetDestination(Entity agent, Vec3 destination);
    static void ResetPath(Entity agent);
    static bool Warp(Entity agent, Vec3 position);
    static NavigationAgentState GetAgentState(Entity agent);
    static NavigationStatistics GetStatistics(Scene &scene);
};
} // namespace Bazzalt
