#include <filesystem>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "Runtime/Engine.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/ModelInstance.h"
#include "Bazzalt/Components/ModelNode.h"

namespace py = pybind11;

namespace Bazzalt::EditorBridge {
namespace {

UUID ParseUuid(const std::string& text) {
    UUID result;
    if (!UUID::TryParse(text, result)) throw std::invalid_argument("Invalid UUID");
    return result;
}

py::str PathText(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring value = path.wstring();
    return py::reinterpret_steal<py::str>(PyUnicode_FromWideChar(value.c_str(),
        static_cast<Py_ssize_t>(value.size())));
#else
    return py::str(path.string());
#endif
}

py::dict SnapshotEntity(Scene& scene, Entity entity) {
    py::dict result;
    result["uuid"] = entity.GetUUID().ToString();
    result["name"] = entity.GetComponent<Name>().Value;
    const Entity parent = entity.GetParent();
    result["parent"] = parent ? parent.GetUUID().ToString() : std::string{};
    const auto& transform = entity.GetComponent<Transform>();
    result["position"] = py::make_tuple(transform.Position.X, transform.Position.Y, transform.Position.Z);
    result["rotation"] = py::make_tuple(transform.Rotation.X, transform.Rotation.Y,
                                          transform.Rotation.Z, transform.Rotation.W);
    result["scale"] = py::make_tuple(transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
    py::list components;
    components.append("Transform");
    if (entity.HasComponent<Camera>()) components.append("Camera");
    if (entity.HasComponent<Light>()) components.append("Light");
    if (entity.HasComponent<Mesh>()) components.append("Mesh");
    if (entity.HasComponent<ModelInstance>()) components.append("Model Instance");
    if (entity.HasComponent<ModelNode>()) components.append("Model Node");
    result["components"] = components;
    return result;
}

class EditorHost final {
public:
    EditorHost() : m_engine(std::make_unique<Runtime::Engine>()) {}
    ~EditorHost() { Stop(); CleanupSnapshot(); }

    bool LoadProject(const std::string& path) {
        Stop();
        const bool result = m_engine->LoadProject(std::filesystem::u8path(path), true);
        if (result) m_projectPath = std::filesystem::u8path(path);
        return result;
    }
    bool LoadScene(const std::string& path) { Stop(); return m_engine->LoadScene(std::filesystem::u8path(path)); }
    bool SaveScene(const std::string& path) { return m_engine->SaveScene(std::filesystem::u8path(path)); }
    void NewScene() { Stop(); m_engine->CreateScene(); }
    std::string LastError() const { return m_engine->GetLastError(); }
    py::str ProjectDirectory() const { return m_projectPath.empty() ? py::str() : PathText(m_projectPath.parent_path()); }
    py::str AssetDirectory() const {
        if (m_projectPath.empty()) return py::str();
        return PathText(m_projectPath.parent_path() / m_engine->GetProject().AssetDirectory);
    }

    py::list Entities() {
        py::list result;
        auto& scene = m_engine->GetScene();
        std::function<void(Entity)> append = [&](Entity parent) {
            for (Entity child : parent.GetChildren()) {
                result.append(SnapshotEntity(scene, child));
                append(child);
            }
        };
        append(scene.GetRootEntity());
        return result;
    }
    py::dict EntityDetails(const std::string& id) {
        Entity entity = m_engine->GetScene().GetEntity(ParseUuid(id));
        return entity ? SnapshotEntity(m_engine->GetScene(), entity) : py::dict{};
    }
    std::string CreateEntity(const std::string& name, const std::string& parent) {
        Entity entity = m_engine->GetScene().CreateEntity(name);
        if (!parent.empty()) entity.SetParent(RequireEntity(parent));
        return entity.GetUUID().ToString();
    }
    bool DestroyEntity(const std::string& id) {
        Entity entity = m_engine->GetScene().GetEntity(ParseUuid(id));
        if (!entity) return false;
        m_engine->GetScene().DestroyEntity(entity); return true;
    }
    bool SetParent(const std::string& id, const std::string& parent) {
        Entity entity = RequireEntity(id);
        return entity.SetParent(parent.empty() ? m_engine->GetScene().GetRootEntity()
                                               : RequireEntity(parent));
    }
    bool Rename(const std::string& id, const std::string& name) {
        RequireEntity(id).GetComponent<Name>().Value = name; return true;
    }
    bool SetTransform(const std::string& id, const std::array<float, 3>& position,
                      const std::array<float, 4>& rotation,
                      const std::array<float, 3>& scale) {
        auto& value = RequireEntity(id).GetComponent<Transform>();
        value.Position = {position[0], position[1], position[2]};
        value.Rotation = Quaternion{rotation[0], rotation[1], rotation[2], rotation[3]}.Normalized();
        value.Scale = {scale[0], scale[1], scale[2]};
        return true;
    }
    bool Translate(const std::string& id, const std::array<float, 3>& delta) {
        RequireEntity(id).GetComponent<Transform>().Translate({delta[0], delta[1], delta[2]});
        return true;
    }
    std::string InstantiateModel(const std::string& asset, const std::string& parent) {
        Entity entity = m_engine->GetScene().InstantiateModel(
            ParseUuid(asset), parent.empty() ? Entity{} : RequireEntity(parent));
        return entity.GetUUID().ToString();
    }

    bool Play() {
        if (m_playing) return true;
        CleanupSnapshot();
        m_snapshot = std::filesystem::temp_directory_path() /
            ("bazzalt-editor-play-" + UUID::Generate().ToString() + ".bscene");
        if (!m_engine->SaveScene(m_snapshot)) return false;
        if (!m_engine->Init()) { CleanupSnapshot(); return false; }
        m_playing = true; m_paused = false; return true;
    }
    void Pause(bool paused) { if (m_playing) m_paused = paused; }
    void Step() { if (m_playing) m_engine->Update(); }
    void Tick() { if (m_playing && !m_paused) m_engine->Update(); }
    void Stop() {
        if (!m_playing) return;
        m_engine->Shutdown();
        m_playing = false; m_paused = false;
        if (!m_snapshot.empty()) m_engine->LoadScene(m_snapshot);
        CleanupSnapshot();
    }
    bool IsPlaying() const { return m_playing; }
    bool IsPaused() const { return m_paused; }

private:
    Entity RequireEntity(const std::string& id) {
        Entity entity = m_engine->GetScene().GetEntity(ParseUuid(id));
        if (!entity) throw std::invalid_argument("Entity does not exist");
        return entity;
    }
    void CleanupSnapshot() {
        if (m_snapshot.empty()) return;
        std::error_code error; std::filesystem::remove(m_snapshot, error); m_snapshot.clear();
    }

    std::unique_ptr<Runtime::Engine> m_engine;
    std::filesystem::path m_projectPath;
    std::filesystem::path m_snapshot;
    bool m_playing = false;
    bool m_paused = false;
};

} // namespace
} // namespace Bazzalt::EditorBridge

PYBIND11_MODULE(_bazzalt_runtime, module) {
    module.doc() = "Private BAZZALT editor-to-runtime bridge";
    py::class_<Bazzalt::EditorBridge::EditorHost>(module, "EditorHost")
        .def(py::init<>()).def("load_project", &Bazzalt::EditorBridge::EditorHost::LoadProject)
        .def("load_scene", &Bazzalt::EditorBridge::EditorHost::LoadScene)
        .def("save_scene", &Bazzalt::EditorBridge::EditorHost::SaveScene)
        .def("new_scene", &Bazzalt::EditorBridge::EditorHost::NewScene)
        .def("last_error", &Bazzalt::EditorBridge::EditorHost::LastError)
        .def("project_directory", &Bazzalt::EditorBridge::EditorHost::ProjectDirectory)
        .def("asset_directory", &Bazzalt::EditorBridge::EditorHost::AssetDirectory)
        .def("entities", &Bazzalt::EditorBridge::EditorHost::Entities)
        .def("entity_details", &Bazzalt::EditorBridge::EditorHost::EntityDetails)
        .def("create_entity", &Bazzalt::EditorBridge::EditorHost::CreateEntity,
             py::arg("name"), py::arg("parent") = "")
        .def("destroy_entity", &Bazzalt::EditorBridge::EditorHost::DestroyEntity)
        .def("set_parent", &Bazzalt::EditorBridge::EditorHost::SetParent)
        .def("rename", &Bazzalt::EditorBridge::EditorHost::Rename)
        .def("set_transform", &Bazzalt::EditorBridge::EditorHost::SetTransform)
        .def("translate", &Bazzalt::EditorBridge::EditorHost::Translate)
        .def("instantiate_model", &Bazzalt::EditorBridge::EditorHost::InstantiateModel,
             py::arg("asset"), py::arg("parent") = "")
        .def("play", &Bazzalt::EditorBridge::EditorHost::Play)
        .def("pause", &Bazzalt::EditorBridge::EditorHost::Pause)
        .def("step", &Bazzalt::EditorBridge::EditorHost::Step)
        .def("tick", &Bazzalt::EditorBridge::EditorHost::Tick)
        .def("stop", &Bazzalt::EditorBridge::EditorHost::Stop)
        .def("is_playing", &Bazzalt::EditorBridge::EditorHost::IsPlaying)
        .def("is_paused", &Bazzalt::EditorBridge::EditorHost::IsPaused);
}
