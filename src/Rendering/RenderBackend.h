#pragma once

#include <memory>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utils/Entity.h>

#include "Bazzalt/PostProcessing.h"
#include "Bazzalt/SceneEnvironment.h"

namespace filament {
class Engine;
class Renderer;
class Scene;
class View;
class Texture;
class Skybox;
}
namespace Bazzalt { class Scene; }

namespace Bazzalt::Runtime {

class RenderAssets;
class GuiRenderer;

// Owns Filament's low-level objects. No Filament type crosses the public API.
class RenderBackend final {
public:
    enum class ViewportKind { Scene, Game };
    struct EditorIcon { float X, Y, Z; bool Camera; };
    struct EditorGuide { float AX,AY,AZ,BX,BY,BZ,R,G,B,A; bool Outline=false; };
    RenderBackend();
    ~RenderBackend();
    RenderBackend(const RenderBackend&) = delete;
    RenderBackend& operator=(const RenderBackend&) = delete;

    bool Initialize();
    bool Initialize(bool headless);
    static std::vector<std::string> SupportedBackends();
    bool ConfigureBackend(const std::string& backend);
    void Shutdown();
    bool CreateViewport(std::uint64_t id, std::uintptr_t nativeWindow,
                        ViewportKind kind, std::uint32_t width, std::uint32_t height,
                        float pixelRatio = 1.0f);
    void ResizeViewport(std::uint64_t id, std::uint32_t width, std::uint32_t height,
                        float pixelRatio = 1.0f);
    void DestroyViewport(std::uint64_t id);
    void SetSceneCamera(std::uint64_t id, float eyeX, float eyeY, float eyeZ,
                        float targetX, float targetY, float targetZ);
    void SetEditorGizmo(bool visible, float x, float y, float z, int mode);
    void SetEditorGizmoHover(int axis);
    void SetEditorGrid(bool visible, int plane);
    void SetEditorIcons(const std::vector<EditorIcon>& icons);
    void SetEditorOrientationVisible(bool visible) { m_orientationVisible=visible; }
    void SetEditorGuides(const std::vector<EditorGuide>& guides);
    void SetEditorOutline(std::unordered_set<UUID> selected,std::unordered_set<UUID> hovered){m_outlineSelected=std::move(selected);m_outlineHovered=std::move(hovered);}
    void Render();
    void RenderCameraView(filament::View* view);
    void SetGuiScene(Bazzalt::Scene* scene);
    void ClearGuiResources();
    filament::Texture* GetCameraTexture(UUID id) const;
    void RegisterCameraView(UUID id,filament::View* view){m_cameraViews[id]=view;}
    void UnregisterCameraView(UUID id){m_cameraViews.erase(id);}
    static void ConfigureEditorFog(filament::View& view,float gridScale);
    void SetEditorHidden(std::unordered_set<UUID> hidden){m_editorHidden=std::move(hidden);}
    void RegisterEditorLight(UUID id,utils::Entity entity){m_editorLights[id]=entity;}
    void UnregisterEditorLight(UUID id){m_editorLights.erase(id);}
    void SetEnvironment(const SceneEnvironment& value);
    bool HasEnvironmentLighting() const;
    bool HasEnvironmentSkybox() const;
    [[nodiscard]] bool IsInitialized() const { return m_engine != nullptr; }

    [[nodiscard]] filament::Engine& GetEngine() const { return *m_engine; }
    [[nodiscard]] filament::Renderer& GetRenderer() const { return *m_renderer; }
    [[nodiscard]] filament::Scene& GetScene() const { return *m_scene; }
    [[nodiscard]] RenderAssets& GetAssets() const { return *m_assets; }
    void SetActiveViews(std::vector<filament::View*> views) { m_activeViews = std::move(views); }
    [[nodiscard]] const std::vector<filament::View*>& GetActiveViews() const { return m_activeViews; }
    void SetPresentationSize(std::uint32_t width, std::uint32_t height) {
        m_presentationWidth = width > 0 ? width : 1;
        m_presentationHeight = height > 0 ? height : 1;
    }
    [[nodiscard]] std::uint32_t GetPresentationWidth() const { return m_presentationWidth; }
    [[nodiscard]] std::uint32_t GetPresentationHeight() const { return m_presentationHeight; }
    void ClearPostProcessEffects() { m_postProcessEffects.clear(); }
    void SetPostProcessEffects(filament::View* view,
                               std::vector<CustomPostProcessEffect> effects) {
        m_postProcessEffects[view] = std::move(effects);
    }
    [[nodiscard]] const std::vector<CustomPostProcessEffect>* GetPostProcessEffects(
        filament::View* view) const {
        const auto found = m_postProcessEffects.find(view);
        return found == m_postProcessEffects.end() ? nullptr : &found->second;
    }

private:
    friend struct EditorOutlineTestAccess;
    struct ViewportResource;
    struct GizmoResource;
    struct EnvironmentResource;
    std::unique_ptr<EnvironmentResource> m_environment;
    Vec4 m_clearColor{0.055f,0.065f,0.085f,1};
    filament::Engine* m_engine = nullptr;
    std::string m_backend = "automatic";
    filament::Renderer* m_renderer = nullptr;
    filament::Scene* m_scene = nullptr;
    filament::Skybox* m_targetClearSky=nullptr;
    std::vector<filament::View*> m_activeViews;
    std::uint32_t m_presentationWidth = 1280;
    std::uint32_t m_presentationHeight = 720;
    std::unordered_map<filament::View*, std::vector<CustomPostProcessEffect>> m_postProcessEffects;
    std::unique_ptr<RenderAssets> m_assets;
    std::unique_ptr<GuiRenderer> m_guiRenderer;
    Bazzalt::Scene* m_guiScene=nullptr;
    std::unordered_map<UUID,filament::View*> m_cameraViews;
    std::unordered_map<std::uint64_t, std::unique_ptr<ViewportResource>> m_viewports;
    std::unique_ptr<GizmoResource> m_gizmo;
    bool m_gizmoVisible = false;
    float m_gizmoX = 0, m_gizmoY = 0, m_gizmoZ = 0;
    int m_gizmoMode = 0;
    float m_gizmoScale = 1;
    int m_gizmoHover = -1;
    bool m_gridVisible = true;
    bool m_orientationVisible = true;
    std::unordered_set<UUID> m_editorHidden;
    std::unordered_set<UUID> m_outlineSelected,m_outlineHovered;
    std::unordered_map<UUID,utils::Entity> m_editorLights;
    int m_gridPlane = 1;
    float m_gridCenterX = 0, m_gridCenterY = 0, m_gridCenterZ = 0;
    float m_gridScale = 1;
};

} // namespace Bazzalt::Runtime
