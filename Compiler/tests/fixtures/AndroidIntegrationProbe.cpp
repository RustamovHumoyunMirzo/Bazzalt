#include <Bazzalt/Script.h>
#include <Bazzalt/Application.h>
#include <Bazzalt/Input.h>
#include <Bazzalt/Material.h>
#include <Bazzalt/SaveGame.h>
#include <Bazzalt/Components/PrimitiveObject.h>
#include <Bazzalt/Components/Rigidbody.h>
#include <Bazzalt/Components/ShapeCollider.h>
#include <fstream>

COMPONENT(AndroidIntegrationProbe) {
public:
    PROPERTY(int, ExpectedRuns, 1)
    Bazzalt::Entity Cube;
    int Frames = 0;
    int Runs = 0;
    std::filesystem::path ResultDirectory;

    void OnCreate() override {
        using namespace Bazzalt;
        Application::SetTargetFrameRate(30);
        auto previous = SaveGame::Load("progress");
        if (previous.Success) Runs = static_cast<int>(std::get<std::int64_t>(previous.Document->Get("runs")->Data));
        ++Runs;
        if (Runs != ExpectedRuns) throw std::runtime_error("Save persistence across APK updates failed");
        SaveDocument document;
        document.Set("runs", Runs);
        auto result = SaveGame::Save("progress", document);
        if (!result.Success) throw std::runtime_error(result.Error);
        auto queued = SaveGame::SaveAsync("queued", document);
        SaveGame::Flush();
        if (!queued.GetResult().Success || SaveGame::ListSlots().size() != 2)
            throw std::runtime_error("Android asynchronous saves or slot enumeration failed");
        ResultDirectory = SaveGame::GetDirectory();

        auto* scene = GetEntity().GetScene();
        Cube = scene->CreateEntity("Cube");
        Cube.GetComponent<Transform>().Position = {0, 0, -4};
        auto material = Material::Create(ShaderPreset::Unlit);
        material.SetColor("baseColor", {.1f, .8f, .2f, 1});
        Cube.AddComponent<PrimitiveObject>().SetMaterial(material);
        Cube.AddComponent<ShapeCollider>();
        Cube.AddComponent<Rigidbody>();
        auto floor = scene->CreateEntity("Floor");
        floor.GetComponent<Transform>().Position = {0, -1, -4};
        floor.GetComponent<Transform>().Scale = {10, .2f, 10};
        floor.AddComponent<ShapeCollider>();
    }

    void OnUpdate(float) override {
        if (Bazzalt::Input::GetKeyDown(Bazzalt::KeyCode::Q)) Bazzalt::Application::Quit();
        if (++Frames != 45) return;
        auto* scene = GetEntity().GetScene();
        const float height = Cube.GetWorldTransform().Position.Y;
        if (height >= -.1f || height < -2 || !scene->Raycast({0, 4, -4}, {0, -1, 0}, 10))
            throw std::runtime_error("Android physics simulation or raycast failed");
        std::ofstream result(ResultDirectory / "smoke.txt");
        result << "PASS " << Runs << ' ' << Bazzalt::Application::GetPlatform();
        if (!result) throw std::runtime_error("Could not write Android integration result");
    }
    void OnDestroy() override {
        if (!ResultDirectory.empty()) std::ofstream(ResultDirectory / "shutdown.txt") << "SHUTDOWN " << Runs;
    }
};
