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
    project.StartupScene = "Content/Scenes/Main.bscene";
    project.Properties["assets.databaseVersion"] = "1";
    const auto assetScenePath = directory / "Content" / "Scenes" / "Main.bscene";
    std::filesystem::create_directories(assetScenePath.parent_path());
    std::filesystem::copy_file(preservedScenePath, assetScenePath,
                               std::filesystem::copy_options::overwrite_existing);
    assert(sourceEngine.SaveProject(projectPath));
    Runtime::Engine projectEngine;
    assert(projectEngine.LoadProject(projectPath, false));
    assert(projectEngine.GetProject().ProjectUUID == project.ProjectUUID);
    assert(projectEngine.GetProject().AssetDirectory == project.AssetDirectory);
    assert(projectEngine.GetProject().Properties == project.Properties);

    const auto sceneAsset = AssetManager::GetAsset(assetScenePath);
    assert(sceneAsset);
    assert(sceneAsset->Id);
    assert(sceneAsset->State == AssetState::Ready);
    assert(std::filesystem::exists(sceneAsset->MetaPath));
    assert(std::filesystem::exists(sceneAsset->CachePath));

    assert(projectEngine.Init());
    assert(SceneManager::LoadScene(sceneAsset->Id));
    assert(SceneManager::IsLoadPending());
    projectEngine.Update();
    assert(!SceneManager::IsLoadPending());
    assert(SceneManager::GetActiveScene()->GetEntity(UUID{2, 2}));
    projectEngine.Shutdown();

    // A second scan consumes the existing YAML .meta and keeps its UUID.
    Runtime::Engine secondScan;
    assert(secondScan.LoadProject(projectPath, false));
    const auto rescannedAsset = AssetManager::GetAsset(assetScenePath);
    assert(rescannedAsset && rescannedAsset->Id == sceneAsset->Id);

    std::filesystem::remove_all(directory);
    return 0;
}
