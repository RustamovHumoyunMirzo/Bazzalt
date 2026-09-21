#include <cassert>
#include <filesystem>

#include "Runtime/Engine.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/SceneQueryBounds.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"

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
    auto& sourceCamera = child.AddComponent<Camera>();
    sourceCamera.PostProcessing.Bloom = false;
    sourceCamera.Viewport = CameraViewport::RightHalf();
    sourceCamera.AspectMode = CameraAspectMode::Automatic;
    sourceCamera.PostProcessing.DepthOfField.Enabled = true;
    sourceCamera.PostProcessing.DepthOfField.FocusDistance = 4.5f;
    auto& effect = sourceCamera.PostProcessing.CustomEffects.AddEffect(UUID::Generate(), "Test effect");
    effect.Order = 7;
    effect.SetParameter(PostProcessParameter::Float("amount", 0.75f));
    auto& queryBounds = child.AddComponent<SceneQueryBounds>();
    queryBounds.Shape = SceneQueryShape::Sphere;
    queryBounds.Radius = 2.5f;
    queryBounds.LayerMask = 0x10;
    child.AddComponent<GaussianBlur>().Size = 3.0f;
    child.AddComponent<Vignette>().Roundness = 0.6f;
    child.AddComponent<Light>().Type = LightType::Spot;
    child.AddComponent<Mesh>().MeshAsset = UUID{9, 9};

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
    assert(!restoredChild.GetComponent<Camera>().PostProcessing.Bloom);
    assert(restoredChild.GetComponent<Camera>().Viewport.X == 0.5f);
    assert(restoredChild.GetComponent<Camera>().Viewport.Width == 0.5f);
    assert(restoredChild.GetComponent<Camera>().PostProcessing.DepthOfField.Enabled);
    assert(restoredChild.GetComponent<Camera>().PostProcessing.DepthOfField.FocusDistance == 4.5f);
    const auto& effects = restoredChild.GetComponent<Camera>().PostProcessing.CustomEffects.GetEffects();
    assert(effects.size() == 1 && effects[0].Order == 7);
    assert(effects[0].Parameters.size() == 1);
    assert(restoredChild.GetComponent<SceneQueryBounds>().Shape == SceneQueryShape::Sphere);
    assert(restoredChild.GetComponent<SceneQueryBounds>().Radius == 2.5f);
    assert(restoredChild.GetComponent<SceneQueryBounds>().LayerMask == 0x10);
    assert(restoredChild.GetComponent<GaussianBlur>().Size == 3.0f);
    assert(restoredChild.GetComponent<Vignette>().Roundness == 0.6f);
    assert(restoredChild.GetComponent<Light>().Type == LightType::Spot);
    assert(restoredChild.GetComponent<Mesh>().MeshAsset == UUID(9, 9));

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

    assert(SceneManager::LoadScene(sceneAsset->Id));
    assert(SceneManager::IsLoadPending());

    // A second scan consumes the existing YAML .meta and keeps its UUID.
    Runtime::Engine secondScan;
    assert(secondScan.LoadProject(projectPath, false));
    const auto rescannedAsset = AssetManager::GetAsset(assetScenePath);
    assert(rescannedAsset && rescannedAsset->Id == sceneAsset->Id);

    std::filesystem::remove_all(directory);
    return 0;
}
