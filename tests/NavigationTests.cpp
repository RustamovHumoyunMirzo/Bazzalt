#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/NavMeshSurface.h"
#include "Bazzalt/Components/NavigationAgent.h"
#include "Bazzalt/Components/NavigationObstacle.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/Rigidbody.h"
#include "Bazzalt/Components/ShapeCollider.h"
#include "Bazzalt/Navigation.h"
#include "Bazzalt/Scene.h"
#include "Navigation/NavigationModuleApi.h"
#include "Physics/PhysicsModuleApi.h"
#include "Runtime/Engine.h"
#include <cassert>
#include <fstream>
using namespace Bazzalt;
int main() {
    Scene scene;
    auto ground = scene.CreateEntity("Floor");
    auto &plane = ground.AddComponent<PrimitiveObject>();
    plane.Shape = PrimitiveShape::Plane;
    plane.Width = 30;
    plane.Depth = 30;
    auto surface = scene.CreateEntity("Navigation");
    auto &settings = surface.AddComponent<NavMeshSurface>();
    settings.Size = {30, 10, 30};
    Navigation::Bake(surface);
    assert(Navigation::IsReady(surface));
    auto sample = Navigation::SamplePosition(surface, {0, 1, 0});
    assert(sample.Found);
    auto path = Navigation::CalculatePath(surface, {-8, 0, 0}, {8, 0, 0});
    assert(path.Status == NavigationPathStatus::Complete);
    assert(path.Length > 15);
    assert(!Navigation::Raycast(surface, {-8, 0, 0}, {8, 0, 0}).Found);
    assert(Navigation::Raycast(surface, {0, 0, 0}, {40, 0, 0}).Found);
    assert(Navigation::CalculatePath(surface, {-8, 0, 0}, {8, 0, 0}, 2).Status ==
           NavigationPathStatus::Invalid);
    auto agent = scene.CreateEntity("Agent");
    agent.AddComponent<NavigationAgent>().Surface = surface.GetUUID();
    auto pose = agent.GetWorldTransform();
    pose.Position = {-8, 0, 0};
    assert(agent.SetWorldTransform(pose));
    assert(Navigation::SetDestination(agent, {8, 0, 0}));
    for (int i = 0; i < 60; ++i)
        Runtime::UpdateNavigationScene(scene, 1.f / 60);
    assert(agent.GetWorldTransform().Position.X > -7);
    assert(Navigation::GetAgentState(agent).Status == NavigationAgentStatus::Moving);
    Navigation::ResetPath(agent);
    assert(Navigation::Warp(agent, {0, 0, -5}));
    assert(agent.GetWorldTransform().Position.Z < -4);
    auto obstacle = scene.CreateEntity("Obstacle");
    auto &wall = obstacle.AddComponent<NavigationObstacle>();
    wall.Size = {3, 4, 24};
    for (int i = 0; i < 80; ++i)
        Runtime::UpdateNavigationScene(scene, 1.f / 60);
    assert(Navigation::GetStatistics(scene).Obstacles == 1);
    auto blocked = Navigation::CalculatePath(surface, {-8, 0, 0}, {8, 0, 0});
    assert(blocked.Status != NavigationPathStatus::Complete || blocked.Length > path.Length + 3);
    scene.DestroyEntity(obstacle);
    for (int i = 0; i < 80; ++i)
        Runtime::UpdateNavigationScene(scene, 1.f / 60);
    assert(Navigation::GetStatistics(scene).Obstacles == 0);
    auto restored = Navigation::CalculatePath(surface, {-8, 0, 0}, {8, 0, 0});
    assert(restored.Status == NavigationPathStatus::Complete);
    auto file =
        std::filesystem::temp_directory_path() / (UUID::Generate().ToString() + ".bnavmesh");
    Navigation::Save(surface, file);
    Navigation::Clear(scene);
    assert(!Navigation::IsReady(surface));
    Navigation::Load(surface, file);
    assert(Navigation::CalculatePath(surface, {-8, 0, 0}, {8, 0, 0}).Status ==
           NavigationPathStatus::Complete);
    {
        std::ofstream corrupt(file, std::ios::binary | std::ios::trunc);
        corrupt << "broken";
    }
    bool rejected = false;
    try {
        Navigation::Load(surface, file);
    } catch (const std::exception &) {
        rejected = true;
    }
    assert(rejected && Navigation::IsReady(surface));
    std::filesystem::remove(file);
    std::vector<Entity> obstacles;
    for (int i = 0; i < 24; ++i) {
        auto e = scene.CreateEntity("Queued Obstacle");
        e.AddComponent<NavigationObstacle>().Size = {.5f, 1, .5f};
        e.GetComponent<Transform>().Position = {float(i % 6) * 2 - 6, .5f, float(i / 6) * 2 - 4};
        obstacles.push_back(e);
    }
    Runtime::UpdateNavigationScene(scene, 0);
    assert(Navigation::GetStatistics(scene).Obstacles == 24);
    assert(!Navigation::GetStatistics(scene).ObstacleUpdatesPending);
    for (auto e : obstacles)
        e.GetComponent<Transform>().Position.X += .5f;
    for (int i = 0; i < 100; ++i)
        Runtime::UpdateNavigationScene(scene, 1.f / 60);
    assert(Navigation::GetStatistics(scene).Obstacles == 24);
    assert(!Navigation::GetStatistics(scene).ObstacleUpdatesPending);
    for (auto e : obstacles)
        scene.DestroyEntity(e);
    Runtime::UpdateNavigationScene(scene, 0);
    assert(Navigation::GetStatistics(scene).Obstacles == 0);
    assert(!Navigation::GetStatistics(scene).ObstacleUpdatesPending);
    scene.Clear();
    assert(Navigation::GetStatistics(scene).Surfaces == 0);

    Scene physical;
    auto floor = physical.CreateEntity("Collider Floor");
    floor.AddComponent<ShapeCollider>().Size = {30, 1, 30};
    floor.GetComponent<Transform>().Position = {0, -.5f, 0};
    auto physicalSurface = physical.CreateEntity("Physics Navigation");
    auto &ps = physicalSurface.AddComponent<NavMeshSurface>();
    ps.Size = {30, 10, 30};
    ps.UsePhysicsGeometry = true;
    Navigation::Bake(physicalSurface);
    assert(Navigation::SamplePosition(physicalSurface, {0, 1, 0}).Found);
    auto walker = physical.CreateEntity("Kinematic Agent");
    walker.AddComponent<NavigationAgent>().Surface = physicalSurface.GetUUID();
    walker.AddComponent<Rigidbody>().MotionType = BodyMotionType::Kinematic;
    auto &capsule = walker.AddComponent<ShapeCollider>();
    capsule.Shape = ColliderShape::Capsule;
    capsule.Offset = {0, 1, 0};
    capsule.Radius = .5f;
    capsule.Height = 2;
    auto physicalWall = physical.CreateEntity("Physical Wall");
    physicalWall.AddComponent<ShapeCollider>().Size = {1, 4, 8};
    physicalWall.GetComponent<Transform>().Position = {3, 1, 0};
    Runtime::PhysicsModule().Start(physical);
    assert(Navigation::Warp(walker, {0, 0, 0}));
    assert(Navigation::SetDestination(walker, {7, 0, 0}));
    for (int i = 0; i < 180; ++i) {
        Runtime::UpdateNavigationScene(physical, 1.f / 60);
        Runtime::PhysicsModule().Step(physical, 1.f / 60, false);
    }
    assert(walker.GetWorldTransform().Position.X > .5f &&
           walker.GetWorldTransform().Position.X < 2.5f);
    walker.SetComponentEnabled<NavigationAgent>(false);
    Runtime::UpdateNavigationScene(physical, 1.f / 60);
    assert(Navigation::GetStatistics(physical).Agents == 0);

    auto folder = std::filesystem::temp_directory_path() / UUID::Generate().ToString();
    std::filesystem::create_directories(folder / "Assets");
    {
        std::ofstream project(folder / "Test.bproject");
        project
            << "FormatVersion: 1\nProjectUUID: \"" << UUID::Generate().ToString()
            << "\"\nName: Navigation\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties: {}\n";
    }
    {
        Runtime::Engine engine;
        assert(engine.LoadProject(folder / "Test.bproject", false));
        auto &startup = engine.GetScene();
        auto floor = startup.CreateEntity("Floor");
        auto &plane = floor.AddComponent<PrimitiveObject>();
        plane.Shape = PrimitiveShape::Plane;
        plane.Width = 20;
        plane.Depth = 20;
        auto surface = startup.CreateEntity("Navigation");
        auto &bake = surface.AddComponent<NavMeshSurface>();
        bake.Size = {20, 10, 20};
        bake.AutoBake = true;
        assert(engine.Init(true, false));
        assert(engine.StartScripts());
        assert(Navigation::IsReady(surface));
        auto bakedPath = folder / "Assets" / "World.bnavmesh";
        Navigation::Save(surface, bakedPath);
        assert(engine.RefreshAssets());
        auto asset = AssetManager::GetAsset(bakedPath);
        assert(asset && asset->State == AssetState::Ready);
        bake.Data = asset->Id;
        Navigation::Clear(startup);
        assert(!Navigation::IsReady(surface));
        assert(Runtime::NavigationGuides(startup, surface.GetUUID()).size() > 12);
        assert(Navigation::IsReady(surface));
        engine.StopScripts();
        assert(!Navigation::IsReady(surface));
        engine.Shutdown();
    }
    std::filesystem::remove_all(folder);
}
