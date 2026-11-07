#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/Rigidbody.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Scene.h"
#include "Navigation/NavigationFields.h"
#include "Navigation/NavigationModuleApi.h"
#include "Physics/PhysicsModuleApi.h"
#include "Rendering/ModelGeometry.h"
#include "Rendering/PrimitiveGeometry.h"
#include <fstream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
namespace {
const NavigationModuleApi *Loaded = nullptr;
struct PreviewAttempt {
    Entity Object;
    UUID Asset;
    std::filesystem::path Cache;
    float Radius = 0, Height = 0;
};
std::map<Scene *, std::map<UUID, PreviewAttempt>> PreviewAttempts;
const NavigationModuleApi *LoadModule() {
#ifdef _WIN32
    HMODULE core = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&NavigationModule), &core))
        throw std::runtime_error("Cannot locate core for navigation");
    std::wstring name(32768, L'\0');
    auto size = GetModuleFileNameW(core, name.data(), DWORD(name.size()));
    if (!size || size >= name.size())
        throw std::runtime_error("Cannot resolve core path");
    name.resize(size);
    auto path = std::filesystem::path(name).parent_path() / L"bazzalt_ai.dll";
    auto module = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto get = module ? reinterpret_cast<const NavigationModuleApi *(*)(unsigned, unsigned)>(
                            GetProcAddress(module, "BazzaltGetNavigationModuleApi"))
                      : nullptr;
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void *>(&NavigationModule), &info) || !info.dli_fname)
        throw std::runtime_error("Cannot resolve core path");
#ifdef __APPLE__
    auto path = std::filesystem::path(info.dli_fname).parent_path() / "libbazzalt_ai.dylib";
#else
    auto path = std::filesystem::path(info.dli_fname).parent_path() / "libbazzalt_ai.so";
#endif
    auto module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto get = module ? reinterpret_cast<const NavigationModuleApi *(*)(unsigned, unsigned)>(
                            dlsym(module, "BazzaltGetNavigationModuleApi"))
                      : nullptr;
#endif
    auto api = get ? get(1, sizeof(NavigationModuleApi)) : nullptr;
    if (!api || api->Version != 1 || api->Size != sizeof(NavigationModuleApi) || !api->Bake ||
        !api->Release || !api->Step || !api->Restore)
        throw std::runtime_error("Missing or incompatible sibling bazzalt_ai navigation runtime");
    return api;
}
} // namespace
const NavigationModuleApi &NavigationModule() {
    static const auto *api = LoadModule();
    Loaded = api;
    return *api;
}
void ReleaseNavigationScene(Scene &scene) noexcept {
    PreviewAttempts.erase(&scene);
    if (Loaded)
        try {
            Loaded->Release(scene);
        } catch (...) {
        }
}
void UpdateNavigationScene(Scene &scene, float dt) {
    for (auto handle : scene.GetRegistry().view<NavMeshSurface>()) {
        auto surface = scene.GetEntity(static_cast<Entity::Id>(handle));
        auto &settings = surface.GetComponent<NavMeshSurface>();
        if (settings.Enabled && !Navigation::IsReady(surface)) {
            if (settings.Data)
                Navigation::LoadAsset(surface, settings.Data);
            else if (settings.AutoBake)
                Navigation::Bake(surface);
        }
    }
    if (Loaded)
        Loaded->Step(scene, dt);
}
std::vector<std::array<float, 10>> NavigationGuides(Scene &scene, UUID selected) {
    std::vector<std::array<float, 10>> result;
    auto e = scene.GetEntity(selected);
    if (!e)
        return result;
    if (auto surface = e.TryGetComponent<NavMeshSurface>(); surface && surface->Enabled &&
                                                            surface->IsValid() && surface->Data &&
                                                            !Navigation::IsReady(e)) {
        auto asset = AssetManager::GetAsset(surface->Data);
        if (asset && asset->State == AssetState::Ready) {
            auto &previous = PreviewAttempts[&scene][selected];
            if (previous.Object != e || previous.Asset != surface->Data ||
                previous.Cache != asset->CachePath || previous.Radius != surface->AgentRadius ||
                previous.Height != surface->AgentHeight) {
                previous = {e, surface->Data, asset->CachePath, surface->AgentRadius,
                            surface->AgentHeight};
                try {
                    Navigation::LoadAsset(e, surface->Data);
                } catch (const std::exception &error) {
                    Console::Warning(error.what(), "Navigation");
                }
            }
        }
    }
    if (Loaded)
        result = Loaded->Guides(scene, selected);
    auto line = [&](Vec3 a, Vec3 b) {
        result.push_back({a.X, a.Y, a.Z, b.X, b.Y, b.Z, .2f, .75f, 1, .85f});
    };
    if (auto s = e.TryGetComponent<NavMeshSurface>(); s && s->Enabled && s->IsValid()) {
        auto matrix = e.GetWorldMatrix();
        Vec3 points[8];
        for (int i = 0; i < 8; ++i)
            points[i] = matrix.TransformPoint(s->Center + Vec3{s->Size.X * ((i & 1) ? .5f : -.5f),
                                                               s->Size.Y * ((i & 2) ? .5f : -.5f),
                                                               s->Size.Z * ((i & 4) ? .5f : -.5f)});
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit))
                    line(points[i], points[i | bit]);
    }
    if (auto a = e.TryGetComponent<NavigationAgent>(); a && a->Enabled && a->IsValid()) {
        auto center = e.GetWorldTransform().Position - Vec3{0, a->BaseOffset, 0};
        for (int i = 0; i < 16; ++i) {
            float x = 2 * Pi * i / 16, y = 2 * Pi * (i + 1) / 16;
            Vec3 p{std::cos(x) * a->Radius, 0, std::sin(x) * a->Radius},
                q{std::cos(y) * a->Radius, 0, std::sin(y) * a->Radius};
            line(center + p, center + q);
            line(center + p + Vec3{0, a->Height, 0}, center + q + Vec3{0, a->Height, 0});
            if (i % 4 == 0)
                line(center + p, center + p + Vec3{0, a->Height, 0});
        }
    }
    return result;
}
std::vector<NavigationTriangle> CollectNavigationGeometry(Entity surface) {
    auto &settings = surface.GetComponent<NavMeshSurface>();
    auto &scene = *surface.GetScene();
    std::vector<NavigationTriangle> out;
    if (settings.UsePhysicsGeometry) {
        for (auto &triangle : PhysicsModule().NavigationGeometry(scene, settings.LayerMask))
            out.push_back({triangle[0], triangle[1], triangle[2], 1});
        return out;
    }
    std::map<UUID, std::shared_ptr<ModelGeometryAsset>> models;
    for (auto handle : scene.GetRegistry().view<Transform>()) {
        auto e = scene.GetEntity(static_cast<Entity::Id>(handle));
        if (e.HasComponent<NavigationAgent>() || e.HasComponent<NavigationObstacle>())
            continue;
        bool dynamic = false;
        for (auto p = e; p; p = p.GetParent()) {
            if (p.HasComponent<NavigationAgent>() || p.HasComponent<NavigationObstacle>()) {
                dynamic = true;
                break;
            }
            if (auto b = p.TryGetComponent<Rigidbody>();
                b && b->Enabled && b->MotionType != BodyMotionType::Static) {
                dynamic = true;
                break;
            }
        }
        if (dynamic)
            continue;
        auto modifier = e.TryGetComponent<NavMeshModifier>();
        if (modifier && modifier->Enabled && (!modifier->IsValid() || modifier->Ignore))
            continue;
        unsigned area = modifier && modifier->Enabled ? modifier->Area : 1;
        auto world = e.GetWorldMatrix();
        auto append = [&](const ModelGeometry &geometry, const Mat4 &local = Mat4{}) {
            auto transform = world * local;
            for (auto &t : geometry.Triangles) {
                if (out.size() >= 2000000)
                    throw std::length_error("Navigation geometry exceeds two million triangles");
                auto a = transform.TransformPoint(t.A), b = transform.TransformPoint(t.B),
                     c = transform.TransformPoint(t.C);
                if (Vec3::Dot(Vec3::Cross(transform.TransformDirection({1, 0, 0}),
                                          transform.TransformDirection({0, 1, 0})),
                              transform.TransformDirection({0, 0, 1})) < 0)
                    std::swap(b, c);
                out.push_back({a, b, c, area});
            }
        };
        if (auto p = e.TryGetComponent<PrimitiveObject>();
            p && p->Enabled && (p->LayerMask & settings.LayerMask)) {
            auto geometry = GetCachedPrimitiveGeometry(*p);
            append(*geometry->Picking);
        } else if (auto m = e.TryGetComponent<Mesh>();
                   m && m->Enabled && (m->LayerMask & settings.LayerMask) && m->MeshAsset) {
            auto &model = models[m->MeshAsset];
            if (!model) {
                auto asset = AssetManager::GetAsset(m->MeshAsset);
                if (!asset || asset->State != AssetState::Ready)
                    throw std::runtime_error("Navigation mesh source is not ready");
                std::ifstream file(asset->CachePath, std::ios::binary | std::ios::ate);
                auto size = file.tellg();
                if (size <= 0 || size > 128 * 1024 * 1024)
                    throw std::runtime_error("Invalid navigation mesh source");
                std::vector<std::uint8_t> bytes(std::size_t(size), 0);
                file.seekg(0);
                if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
                    throw std::runtime_error("Cannot read navigation mesh source");
                model = ReadModelGeometry(bytes, asset->CachePath);
                if (!model)
                    throw std::runtime_error("Cannot decode navigation mesh source");
            }
            if (m->ModelNodeIndex == Mesh::EntireAsset) {
                auto metadata = AssetManager::LoadModel(m->MeshAsset);
                if (!metadata)
                    throw std::runtime_error("Navigation model metadata is missing");
                auto visit = [&](auto &&self, unsigned index, const Mat4 &parent,
                                 unsigned depth) -> void {
                    if (index >= metadata->Nodes.size() || depth > 256)
                        throw std::runtime_error("Invalid navigation model hierarchy");
                    auto &node = metadata->Nodes[index];
                    auto local = parent * Mat4::Transform(node.Position, node.Rotation, node.Scale);
                    if (node.HasMesh() && node.SourceIndex < model->SourceNodes.size() &&
                        model->SourceNodes[node.SourceIndex])
                        append(*model->SourceNodes[node.SourceIndex], local);
                    for (auto child : node.Children)
                        self(self, child, local, depth + 1);
                };
                for (auto root : metadata->Roots)
                    visit(visit, root, Mat4{}, 0);
            } else if (m->ModelNodeIndex < model->SourceNodes.size() &&
                       model->SourceNodes[m->ModelNodeIndex])
                append(*model->SourceNodes[m->ModelNodeIndex]);
        }
    }
    return out;
}
} // namespace Bazzalt::Runtime
namespace Bazzalt {
void Navigation::Bake(Entity e) {
    if (!e || !e.HasComponent<NavMeshSurface>() || !e.GetComponent<NavMeshSurface>().IsValid())
        throw std::invalid_argument("A valid NavMesh Surface is required");
    Runtime::NavigationModule().Bake(e, Runtime::CollectNavigationGeometry(e));
}
void Navigation::Clear(Scene &s) { Runtime::ReleaseNavigationScene(s); }
bool Navigation::IsReady(Entity e) { return Runtime::Loaded && Runtime::Loaded->Ready(e); }
void Navigation::Save(Entity e, const std::filesystem::path &path) {
    auto bytes = Runtime::NavigationModule().Capture(e);
    auto temp = path;
    temp += std::filesystem::path("." + UUID::Generate().ToString() + ".tmp");
    try {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file.write(reinterpret_cast<const char *>(bytes.data()),
                        std::streamsize(bytes.size())))
            throw std::runtime_error("Cannot write navigation bake");
        file.close();
        if (!file)
            throw std::runtime_error("Cannot finish navigation bake");
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace navigation bake");
#else
        std::filesystem::rename(temp, path);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
void Navigation::Load(Entity e, const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    if (size <= 0 || size > 128 * 1024 * 1024)
        throw std::runtime_error("Invalid navigation bake size");
    std::vector<std::uint8_t> bytes(std::size_t(size), 0);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
        throw std::runtime_error("Cannot read navigation bake");
    Runtime::NavigationModule().Restore(e, bytes);
}
void Navigation::LoadAsset(Entity e, UUID id) {
    auto asset = AssetManager::GetAsset(id);
    if (!asset || asset->State != AssetState::Ready || asset->Importer != "Bazzalt.Navigation")
        throw std::runtime_error("Navigation bake asset is not ready");
    Load(e, asset->CachePath);
}
NavigationPath Navigation::CalculatePath(Entity e, Vec3 a, Vec3 b, unsigned mask) {
    return Runtime::NavigationModule().Path(e, a, b, mask);
}
NavigationHit Navigation::SamplePosition(Entity e, Vec3 p, Vec3 ext, unsigned mask) {
    return Runtime::NavigationModule().Sample(e, p, ext, mask);
}
NavigationHit Navigation::Raycast(Entity e, Vec3 a, Vec3 b, unsigned mask) {
    return Runtime::NavigationModule().Ray(e, a, b, mask);
}
void Navigation::SetAreaCost(Entity e, unsigned a, float c) {
    Runtime::NavigationModule().Cost(e, a, c);
}
bool Navigation::SetDestination(Entity e, Vec3 p) {
    return Runtime::NavigationModule().Destination(e, p);
}
void Navigation::ResetPath(Entity e) { Runtime::NavigationModule().Reset(e); }
bool Navigation::Warp(Entity e, Vec3 p) { return Runtime::NavigationModule().Warp(e, p); }
NavigationAgentState Navigation::GetAgentState(Entity e) {
    return Runtime::NavigationModule().Agent(e);
}
NavigationStatistics Navigation::GetStatistics(Scene &s) {
    return Runtime::NavigationModule().Statistics(s);
}
} // namespace Bazzalt
