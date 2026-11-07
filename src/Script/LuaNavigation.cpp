#include "Bazzalt/Navigation.h"
#include "Bazzalt/Scene.h"
#include "Navigation/NavigationFields.h"
#include <sol/sol.hpp>
namespace Bazzalt::Runtime {
template <class T> void NavigationComponent(sol::table api, const char *name) {
    auto ops = api["_ComponentOperations"].get<sol::table>().create_named(name);
    ops["Has"] = [](Entity e) { return e.HasComponent<T>(); };
    ops["Get"] = [](Entity e) { return T(e.GetComponent<T>()); };
    ops["Add"] = [](Entity e) { return T(e.AddComponent<T>()); };
    ops["Set"] = [](Entity e, T value) {
        if (!value.IsValid())
            throw std::invalid_argument("Invalid navigation component");
        e.GetComponent<T>() = value;
    };
    ops["Remove"] = [](Entity e) { e.RemoveComponent<T>(); };
    ops["IsEnabled"] = [](Entity e) { return e.IsComponentEnabled<T>(); };
    ops["SetEnabled"] = [](Entity e, bool enabled) { e.SetComponentEnabled<T>(enabled); };
    ops["Inherited"] = [](Entity) -> std::optional<T> { return {}; };
}
void BindLuaNavigation(sol::table api) {
    api.new_enum("NavigationPathStatus", "Invalid", NavigationPathStatus::Invalid, "Partial",
                 NavigationPathStatus::Partial, "Complete", NavigationPathStatus::Complete);
    api.new_enum("NavigationAgentStatus", "Idle", NavigationAgentStatus::Idle, "Moving",
                 NavigationAgentStatus::Moving, "Arrived", NavigationAgentStatus::Arrived,
                 "Blocked", NavigationAgentStatus::Blocked, "Invalid",
                 NavigationAgentStatus::Invalid);
    api.new_enum("NavigationObstacleShape", "Box", NavigationObstacleShape::Box, "Cylinder",
                 NavigationObstacleShape::Cylinder);

    {
        auto t =
            api.new_usertype<NavMeshSurface>("NavMeshSurface", sol::constructors<NavMeshSurface>(),
                                             sol::base_classes, sol::bases<Component>());
        t["IsValid"] = &NavMeshSurface::IsValid;
        NavigationComponent<NavMeshSurface>(api, "NavMeshSurface");
        t["Data"] = &NavMeshSurface::Data;
        t["Center"] = &NavMeshSurface::Center;
        t["Size"] = &NavMeshSurface::Size;
        t["CellSize"] = &NavMeshSurface::CellSize;
        t["CellHeight"] = &NavMeshSurface::CellHeight;
        t["AgentRadius"] = &NavMeshSurface::AgentRadius;
        t["AgentHeight"] = &NavMeshSurface::AgentHeight;
        t["MaxSlope"] = &NavMeshSurface::MaxSlope;
        t["MaxClimb"] = &NavMeshSurface::MaxClimb;
        t["MaxSimplificationError"] = &NavMeshSurface::MaxSimplificationError;
        t["TileSize"] = &NavMeshSurface::TileSize;
        t["MaxAgents"] = &NavMeshSurface::MaxAgents;
        t["MaxObstacles"] = &NavMeshSurface::MaxObstacles;
        t["LayerMask"] = &NavMeshSurface::LayerMask;
        t["UsePhysicsGeometry"] = &NavMeshSurface::UsePhysicsGeometry;
        t["AutoBake"] = &NavMeshSurface::AutoBake;
    }
    {
        auto t = api.new_usertype<NavigationAgent>("NavigationAgent",
                                                   sol::constructors<NavigationAgent>(),
                                                   sol::base_classes, sol::bases<Component>());
        t["IsValid"] = &NavigationAgent::IsValid;
        NavigationComponent<NavigationAgent>(api, "NavigationAgent");
        t["Surface"] = &NavigationAgent::Surface;
        t["Radius"] = &NavigationAgent::Radius;
        t["Height"] = &NavigationAgent::Height;
        t["Speed"] = &NavigationAgent::Speed;
        t["Acceleration"] = &NavigationAgent::Acceleration;
        t["AngularSpeed"] = &NavigationAgent::AngularSpeed;
        t["StoppingDistance"] = &NavigationAgent::StoppingDistance;
        t["BaseOffset"] = &NavigationAgent::BaseOffset;
        t["SeparationWeight"] = &NavigationAgent::SeparationWeight;
        t["AreaMask"] = &NavigationAgent::AreaMask;
        t["UpdatePosition"] = &NavigationAgent::UpdatePosition;
        t["UpdateRotation"] = &NavigationAgent::UpdateRotation;
        t["Avoidance"] = &NavigationAgent::Avoidance;
        t["Stopped"] = &NavigationAgent::Stopped;
    }
    {
        auto t = api.new_usertype<NavigationObstacle>("NavigationObstacle",
                                                      sol::constructors<NavigationObstacle>(),
                                                      sol::base_classes, sol::bases<Component>());
        t["IsValid"] = &NavigationObstacle::IsValid;
        NavigationComponent<NavigationObstacle>(api, "NavigationObstacle");
        t["Shape"] = &NavigationObstacle::Shape;
        t["Center"] = &NavigationObstacle::Center;
        t["Size"] = &NavigationObstacle::Size;
        t["Radius"] = &NavigationObstacle::Radius;
        t["Height"] = &NavigationObstacle::Height;
        t["Carve"] = &NavigationObstacle::Carve;
    }
    {
        auto t = api.new_usertype<NavMeshModifier>("NavMeshModifier",
                                                   sol::constructors<NavMeshModifier>(),
                                                   sol::base_classes, sol::bases<Component>());
        t["IsValid"] = &NavMeshModifier::IsValid;
        NavigationComponent<NavMeshModifier>(api, "NavMeshModifier");
        t["Area"] = &NavMeshModifier::Area;
        t["Ignore"] = &NavMeshModifier::Ignore;
    }
    {
        auto t = api.new_usertype<NavigationPath>("NavigationPath", sol::no_constructor);
        t["Status"] = &NavigationPath::Status;
        t["Corners"] = &NavigationPath::Corners;
        t["Length"] = &NavigationPath::Length;
    }
    {
        auto t = api.new_usertype<NavigationHit>("NavigationHit", sol::no_constructor);
        t["Found"] = &NavigationHit::Found;
        t["Position"] = &NavigationHit::Position;
        t["Normal"] = &NavigationHit::Normal;
        t["Distance"] = &NavigationHit::Distance;
    }
    {
        auto t =
            api.new_usertype<NavigationAgentState>("NavigationAgentState", sol::no_constructor);
        t["Status"] = &NavigationAgentState::Status;
        t["Position"] = &NavigationAgentState::Position;
        t["Velocity"] = &NavigationAgentState::Velocity;
        t["Destination"] = &NavigationAgentState::Destination;
        t["RemainingDistance"] = &NavigationAgentState::RemainingDistance;
    }
    {
        auto t =
            api.new_usertype<NavigationStatistics>("NavigationStatistics", sol::no_constructor);
        t["Surfaces"] = &NavigationStatistics::Surfaces;
        t["Tiles"] = &NavigationStatistics::Tiles;
        t["Agents"] = &NavigationStatistics::Agents;
        t["Obstacles"] = &NavigationStatistics::Obstacles;
        t["ObstacleUpdatesPending"] = &NavigationStatistics::ObstacleUpdatesPending;
    }
    auto nav = api.create_named("Navigation");
    nav["Bake"] = &Navigation::Bake;
    nav["Clear"] = &Navigation::Clear;
    nav["IsReady"] = &Navigation::IsReady;
    nav["LoadAsset"] = &Navigation::LoadAsset;
    nav["CalculatePath"] = [](Entity e, Vec3 a, Vec3 b, sol::optional<unsigned> mask) {
        return Navigation::CalculatePath(e, a, b, mask.value_or(0xffff));
    };
    nav["SamplePosition"] = [](Entity e, Vec3 p, sol::optional<Vec3> ext,
                               sol::optional<unsigned> mask) {
        return Navigation::SamplePosition(e, p, ext.value_or(Vec3{2, 4, 2}), mask.value_or(0xffff));
    };
    nav["Raycast"] = [](Entity e, Vec3 a, Vec3 b, sol::optional<unsigned> mask) {
        return Navigation::Raycast(e, a, b, mask.value_or(0xffff));
    };
    nav["SetAreaCost"] = &Navigation::SetAreaCost;
    nav["SetDestination"] = &Navigation::SetDestination;
    nav["ResetPath"] = &Navigation::ResetPath;
    nav["Warp"] = &Navigation::Warp;
    nav["GetAgentState"] = &Navigation::GetAgentState;
    nav["GetStatistics"] = &Navigation::GetStatistics;
    nav["Save"] = [](Entity e, const std::string &path) {
        Navigation::Save(e, std::filesystem::u8path(path));
    };
    nav["Load"] = [](Entity e, const std::string &path) {
        Navigation::Load(e, std::filesystem::u8path(path));
    };
}
} // namespace Bazzalt::Runtime
