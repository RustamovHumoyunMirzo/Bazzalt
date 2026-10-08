#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <functional>
#include <filament/TextureSampler.h>
#include "Bazzalt/Texture.h"
#include "Bazzalt/UUID.h"

#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/PostProcessing.h"
#include "Bazzalt/Math.h"

namespace filament { class Engine; class Scene; class MaterialInstance; class Texture; class RenderTarget; }

namespace Bazzalt::Runtime {
struct ModelGeometry;

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
    struct EditorMeshGeometry { UUID Owner;Mat4 World;std::shared_ptr<const ModelGeometry> Geometry; };
    [[nodiscard]] std::vector<EditorMeshGeometry> GetEditorMeshes() const;
    [[nodiscard]] std::size_t GetMaterialSlotCount(Handle handle) const;
    bool BeginSelectionMask(filament::Scene& scene,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered,
                            filament::MaterialInstance* selectedMaterial,filament::MaterialInstance* hoverMaterial);
    void EndSelectionMask(filament::Scene& scene);
    void BeginEditorView(const std::unordered_set<UUID>& hidden);
    void EndEditorView();
    bool SetDebugMode(const std::string& mode);
    filament::Texture* GetGuiTexture(UUID id);
    void SetCameraTexture(UUID camera,filament::Texture* texture);
    std::vector<UUID> GetCameraDependencies() const;
    struct TextureTargets {filament::Texture* Color=nullptr;filament::Texture* History=nullptr;filament::Texture* Depth=nullptr;filament::RenderTarget* Target=nullptr;filament::RenderTarget* HistoryTarget=nullptr;};
    TextureTargets GetTextureTargets(UUID texture);
    void SetTextureProducer(UUID texture,UUID camera);
    UUID GetTextureProducer(UUID texture) const;
    void PublishTexture(UUID texture,filament::Texture* output);
    void GenerateTextureMipmaps(UUID texture,filament::Texture* output);
    void SetTextureCallbacks(std::function<void()> invalidate,std::function<filament::Texture*(UUID)> resolveCamera);
    void SynchronizeTextures();
    filament::TextureSampler GetTextureSampler(filament::Texture* texture) const;
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Bazzalt::Runtime
