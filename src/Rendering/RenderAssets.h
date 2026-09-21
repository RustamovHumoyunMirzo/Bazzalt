#pragma once

#include <cstdint>
#include <memory>

#include "Bazzalt/Components/Mesh.h"
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
    void UpdateMesh(Handle handle, const Mat4& transform, const Mesh& component);
    void DestroyMesh(Handle handle);
    [[nodiscard]] bool PreparePostProcessEffect(const CustomPostProcessEffect& effect);
    void Update();
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Bazzalt::Runtime
