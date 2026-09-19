#include <cassert>
#include <filesystem>

#include "Runtime/Engine.h"

struct Health : Bazzalt::Component {
    float Value = 100.0f;
};

void RegisterHealth(Bazzalt::SceneSerializer& serializer) {
    serializer.GetComponents().Register<Health>("Game.Health", 1,
        [](const Health& health, Bazzalt::PropertyMap& properties) {
            properties["Value"] = std::to_string(health.Value);
        },
        [](Health& health, const Bazzalt::PropertyMap& properties, std::uint32_t version) {
            if (version != 1 || properties.find("Value") == properties.end()) return false;
            health.Value = std::stof(properties.at("Value"));
            return true;
        });
}

int main() {
    using namespace Bazzalt;
    const auto directory = std::filesystem::temp_directory_path() / "bazzalt_serialization_tests";
    std::filesystem::create_directories(directory);
    const auto firstScenePath = directory / "first.bscene";
    const auto preservedScenePath = directory / "preserved.bscene";
    const auto projectPath = directory / "test.bproject";

    Runtime::Engine sourceEngine;
    Scene& source = sourceEngine.GetScene();
    assert(source.GetRootEntity().GetUUID().IsRoot());
    Entity parent = source.CreateEntity(UUID{1, 1}, "Parent entity");
    Entity child = source.CreateEntity(UUID{2, 2}, "Child entity");
    child.SetParent(parent);
    child.GetComponent<Transform>().Position = {1.25f, 2.5f, 5.0f};
    child.AddComponent<Health>().Value = 42.5f;

    RegisterHealth(sourceEngine.GetSceneSerializer());
    assert(sourceEngine.SaveScene(firstScenePath));

    // A runtime without the game component can still load and re-save it.
    Runtime::Engine limitedEngine;
    assert(limitedEngine.LoadScene(firstScenePath));
    assert(limitedEngine.GetScene().GetEntity(UUID{2, 2}).HasComponent<UnresolvedComponents>());
    assert(limitedEngine.SaveScene(preservedScenePath));

    Runtime::Engine restoredEngine;
    RegisterHealth(restoredEngine.GetSceneSerializer());
    assert(restoredEngine.LoadScene(preservedScenePath));
    Entity restoredChild = restoredEngine.GetScene().GetEntity(UUID{2, 2});
    assert(restoredChild.GetParent().GetUUID() == UUID(1, 1));
    assert(restoredChild.GetComponent<Health>().Value == 42.5f);
    assert(restoredChild.GetComponent<Transform>().Position.X == 1.25f);

    ProjectMetadata& project = sourceEngine.GetProject();
    project.Name = "Serialization Test";
    project.AssetDirectory = "Content";
    project.StartupScene = "Scenes/Main.bscene";
    project.Properties["assets.databaseVersion"] = "1";
    assert(sourceEngine.SaveProject(projectPath));
    Runtime::Engine projectEngine;
    assert(projectEngine.LoadProject(projectPath, false));
    assert(projectEngine.GetProject().ProjectUUID == project.ProjectUUID);
    assert(projectEngine.GetProject().AssetDirectory == project.AssetDirectory);
    assert(projectEngine.GetProject().Properties == project.Properties);

    assert(projectEngine.Init());
    assert(SceneManager::LoadScene(preservedScenePath));
    assert(SceneManager::IsLoadPending());
    projectEngine.Update();
    assert(!SceneManager::IsLoadPending());
    assert(SceneManager::GetActiveScene()->GetEntity(UUID{2, 2}));
    projectEngine.Shutdown();

    std::filesystem::remove_all(directory);
    return 0;
}
