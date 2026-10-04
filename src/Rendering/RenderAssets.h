#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include "Bazzalt/UUID.h"

#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/PostProcessing.h"
#include "Bazzalt/Math.h"

namespace filament { class Engine; class Scene; }

namespace Bazzalt::Runtime {

// Private UUID-to-GPU bridge. Renderer ownership never crosses the public API.
class RenderAssets final {
public:
    using Handle = std::uint64_t;
    static constexpr Handle InvalidHandle = 0;

    RenderAssets(filament::Engine& engine, filament::Scene& scene);
    ~RenderAssets();
    RenderAssets(const RenderAssets&) = delete;
    RenderAssets& operator=(const RenderAssets&) = delete;

    [[nodiscard]] Handle CreateMesh(const Mesh& component);
    [[nodiscard]] Handle CreateMesh(const Mesh& component, UUID modelInstance);
    void UpdateMesh(Handle handle, const Mat4& transform, const Mesh& component);
    void DestroyMesh(Handle handle);
    [[nodiscard]] Handle CreatePrimitive(const PrimitiveObject& component);
    void UpdatePrimitive(Handle handle, const Mat4& transform, const PrimitiveObject& component);
    void DestroyPrimitive(Handle handle) { DestroyMesh(handle); }
    [[nodiscard]] bool PreparePostProcessEffect(const CustomPostProcessEffect& effect);
    void Update();
    void SetEditorOwner(Handle handle,UUID owner);
    void BeginEditorView(const std::unordered_set<UUID>& hidden);
    void EndEditorView();
    bool SetDebugMode(const std::string& mode);
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Bazzalt::Runtime
