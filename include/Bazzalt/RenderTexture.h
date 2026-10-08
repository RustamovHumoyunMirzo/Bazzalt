#pragma once
#include <algorithm>
#include <stdexcept>
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/CameraRenderTarget.h"

namespace Bazzalt {
// Logical, non-owning camera output. Resolve explicitly in a scene; no GPU
// pointers survive resizing, component removal, or scene unloading.
class RenderTexture final {
public:
    RenderTexture() = default;
    [[nodiscard]] static RenderTexture FromCamera(Entity camera) {
        if (!camera || !camera.HasComponent<Camera>() || (!camera.GetComponent<Camera>().RenderTarget&&!camera.HasComponent<CameraRenderTarget>()))
            throw std::invalid_argument("RenderTexture requires Camera and CameraRenderTarget");
        return FromCameraUUID(camera.GetUUID());
    }
    [[nodiscard]] static RenderTexture FromCameraUUID(UUID id) { RenderTexture value; value.m_camera=id; return value; }
    [[nodiscard]] UUID GetCameraUUID() const { return m_camera; }
    [[nodiscard]] Entity Resolve(Scene& scene) const { return m_camera ? scene.GetEntity(m_camera) : Entity{}; }
    [[nodiscard]] bool IsValid(Scene& scene) const {
        auto entity=Resolve(scene);const auto* camera=entity.TryGetComponent<Camera>();
        const auto* target=entity.TryGetComponent<CameraRenderTarget>();
        return camera && camera->Enabled && camera->Active && (camera->RenderTarget?camera->GetRenderTarget().IsRenderTarget():target && target->Enabled);
    }
    [[nodiscard]] Vec2 GetSize(Scene& scene) const {
        auto entity=Resolve(scene);if(const auto* camera=entity.TryGetComponent<Camera>();camera&&camera->RenderTarget){auto texture=camera->GetRenderTarget();if(!texture.IsRenderTarget())return {};auto descriptor=texture.GetDescriptor();return {float(descriptor.Width),float(descriptor.Height)};}const auto* target=entity.TryGetComponent<CameraRenderTarget>();
        return target ? Vec2{float(std::clamp(target->Width,1u,4096u)),float(std::clamp(target->Height,1u,4096u))} : Vec2{};
    }
    bool Resize(Scene& scene,std::uint32_t width,std::uint32_t height) const {
        if (!width || !height || width>4096 || height>4096) throw std::out_of_range("RenderTexture size must be 1–4096 pixels");
        auto entity=Resolve(scene);if(const auto* camera=entity.TryGetComponent<Camera>();camera&&camera->RenderTarget){auto texture=camera->GetRenderTarget();if(!texture.IsRenderTarget())return false;texture.Resize(width,height);return true;}auto* target=entity.TryGetComponent<CameraRenderTarget>();
        if (!target) return false;target->Width=width;target->Height=height;return true;
    }
private:
    UUID m_camera{};
};
}
