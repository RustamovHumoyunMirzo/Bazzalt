#include <cassert>
#include <filesystem>

#include "Runtime/Engine.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/SceneQueryBounds.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/ModelInstance.h"
#include "Bazzalt/Components/ModelNode.h"
#include "Bazzalt/Components/PrimitiveObject.h"

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
    parent.AddComponent<ModelInstance>().ModelAsset = UUID{8, 8};
    auto& modelNode = child.AddComponent<ModelNode>();
    modelNode.ModelAsset = UUID{8, 8};
    modelNode.SourceIndex = 3;
    modelNode.MeshIndex = 2;
    modelNode.StablePath = "0/3";
    modelNode.HasMesh = true;
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
    child.GetComponent<Mesh>().MaterialAsset=UUID{7,8};
    auto& primitive=child.AddComponent<PrimitiveObject>();primitive.Shape=PrimitiveShape::Torus;primitive.MajorRadius=2.0f;primitive.MinorRadius=0.4f;primitive.Segments=48;primitive.Color={0.2f,0.4f,0.8f,1.0f};

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
    assert(restoredChild.GetParent().GetComponent<ModelInstance>().ModelAsset == UUID(8, 8));
    assert(restoredChild.GetComponent<ModelNode>().StablePath == "0/3");
    assert(restoredChild.GetComponent<ModelNode>().SourceIndex == 3);
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
    assert(restoredChild.GetComponent<Mesh>().MaterialAsset == UUID(7,8));
    const auto& restoredPrimitive=restoredChild.GetComponent<PrimitiveObject>();assert(restoredPrimitive.Shape==PrimitiveShape::Torus);assert(restoredPrimitive.MajorRadius==2.0f);assert(restoredPrimitive.MinorRadius==0.4f);assert(restoredPrimitive.Segments==48);assert(restoredPrimitive.Color.Z==0.8f);

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

    // Project metadata and native filesystem paths must never pass through the
    // Windows ANSI code page. This covers non-ASCII project, asset, and scene paths.
#ifdef _WIN32
    {
    const auto unicodeDirectory = directory / std::filesystem::path(L"Проекты");
    const auto unicodeAssets = std::filesystem::path(L"Ресурсы");
    const auto unicodeScene = unicodeAssets / std::filesystem::path(L"Сцены") /
                              std::filesystem::path(L"Главная.bscene");
    std::filesystem::create_directories(unicodeDirectory / unicodeScene.parent_path());
    std::filesystem::copy_file(preservedScenePath, unicodeDirectory / unicodeScene,
                               std::filesystem::copy_options::overwrite_existing);
    project.AssetDirectory = unicodeAssets;
    project.StartupScene = unicodeScene;
    const auto unicodeProjectPath = unicodeDirectory / std::filesystem::path(L"Игра.bproject");
    assert(sourceEngine.SaveProject(unicodeProjectPath));
    Runtime::Engine unicodeProjectEngine;
    assert(unicodeProjectEngine.LoadProject(unicodeProjectPath, true));
    assert(unicodeProjectEngine.GetProject().AssetDirectory == unicodeAssets);
    assert(unicodeProjectEngine.GetProject().StartupScene == unicodeScene);
    std::filesystem::remove(unicodeDirectory / unicodeScene);
    Runtime::Engine missingUnicodeSceneEngine;
    assert(missingUnicodeSceneEngine.LoadProject(unicodeProjectPath, true));
    assert(missingUnicodeSceneEngine.GetProject().StartupScene.empty());
    }
#endif

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
