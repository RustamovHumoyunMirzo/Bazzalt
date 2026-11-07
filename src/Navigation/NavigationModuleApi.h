#pragma once
#include "Bazzalt/Components/NavMeshSurface.h"
#include "Bazzalt/Navigation.h"
#include <array>
namespace Bazzalt::Runtime {
struct NavigationTriangle {
    Vec3 A, B, C;
    unsigned Area = 1;
};
struct NavigationModuleApi {
    unsigned Version, Size;
    void (*Release)(Scene &);
    void (*Bake)(Entity, const std::vector<NavigationTriangle> &);
    void (*Step)(Scene &, float);
    bool (*Ready)(Entity);
    NavigationPath (*Path)(Entity, Vec3, Vec3, unsigned);
    NavigationHit (*Sample)(Entity, Vec3, Vec3, unsigned);
    NavigationHit (*Ray)(Entity, Vec3, Vec3, unsigned);
    void (*Cost)(Entity, unsigned, float);
    bool (*Destination)(Entity, Vec3);
    void (*Reset)(Entity);
    bool (*Warp)(Entity, Vec3);
    NavigationAgentState (*Agent)(Entity);
    NavigationStatistics (*Statistics)(Scene &);
    std::vector<std::uint8_t> (*Capture)(Entity);
    void (*Restore)(Entity, const std::vector<std::uint8_t> &);
    std::vector<std::array<float, 10>> (*Guides)(Scene &, UUID);
};
const NavigationModuleApi &NavigationModule();
void ReleaseNavigationScene(Scene &) noexcept;
void UpdateNavigationScene(Scene &, float);
std::vector<NavigationTriangle> CollectNavigationGeometry(Entity);
std::vector<std::array<float, 10>> NavigationGuides(Scene &, UUID);
} // namespace Bazzalt::Runtime
