#pragma once
#include "Bazzalt/Serialization.h"
#include "Bazzalt/SceneEnvironment.h"
#include "Bazzalt/Physics.h"
namespace Bazzalt::Runtime {
struct PreparedEntity {
    UUID Id, Parent;
    std::string Name;
    std::vector<SerializedComponent> Components;
};
struct PreparedScene {
    UUID Id;
    SceneEnvironment Environment;
    PhysicsSettings Physics;
    std::vector<PreparedEntity> Entities;
    PropertyMap ExtensionProperties;
};
// Worker-safe: parses data only. Never invokes component/user code or creates ECS entities.
bool PrepareSceneData(const std::string& text, PreparedScene& output, std::string& error);
bool PrepareScene(const std::filesystem::path& path, PreparedScene& output, std::string& error);
}
