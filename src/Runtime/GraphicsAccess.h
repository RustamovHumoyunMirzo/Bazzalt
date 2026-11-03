#pragma once
#include "Bazzalt/Graphics.h"
#include <array>
#include "Bazzalt/Scene.h"

namespace Bazzalt::Runtime {
struct ParticleGpuUniforms;struct ParticleLight;
struct GraphicsGeometry {
    std::uint64_t Id=0,Revision=0;
    GraphicsTopology Topology{};
    std::vector<GraphicsVertex> Vertices;
    std::vector<std::uint32_t> Indices;
    std::vector<std::array<float,16>> ParticleData;
};
struct GraphicsCommand {
    std::shared_ptr<const GraphicsGeometry> Geometry;
    UUID Material{};
    Mat4 Transform{};
    GraphicsDrawOptions Options;
    bool Scoped=false,Particle=false;
    UUID ParticleTexture{};
    unsigned ParticleStyle=0;std::shared_ptr<const ParticleGpuUniforms> ParticleGPU;
    UUID Camera{};
    UUID Owner{};
    UUID Source{};
    std::uint32_t SourceNode=0xffffffffu,SourceSlot=0;
    std::uint64_t Viewport=0;
};
struct GraphicsAccess {
    static void DrawEntity(GraphicsMesh mesh,UUID material,UUID owner,GraphicsDrawOptions options,UUID source={},unsigned node=0xffffffffu,unsigned slot=0);
    static void DrawParticles(GraphicsMesh,UUID material,UUID texture,UUID owner,const GraphicsEvent&,unsigned style=0,std::shared_ptr<const ParticleGpuUniforms> gpu={},Mat4 transform={},std::vector<std::array<float,16>> data={});
    static std::vector<ParticleLight> GetParticleLights();
    static bool IsDeformed(UUID source);
    static void BindScene(Scene* scene);
    static void BeginFrame();
    static void BeforeCamera(GraphicsEvent event);
    static void BeforeDraws();
    static void AfterCamera(GraphicsEvent event);
    static void EndFrame();
    static void CancelFrame() noexcept;
    static const std::vector<GraphicsCommand>& Commands();
    static bool Matches(const GraphicsCommand&,const GraphicsEvent&);
    static Graphics::Subscription SubscribeScoped(GraphicsEventType,Graphics::Callback,Entity,const void* scope);
    static void DisconnectScope(const void* scope);
    static const void* SetScope(const void* scope);
};
}
