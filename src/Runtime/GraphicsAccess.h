#pragma once
#include "Bazzalt/Graphics.h"
#include "Bazzalt/Scene.h"

namespace Bazzalt::Runtime {
struct GraphicsGeometry {
    std::uint64_t Id=0,Revision=0;
    GraphicsTopology Topology{};
    std::vector<GraphicsVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};
struct GraphicsCommand {
    std::shared_ptr<const GraphicsGeometry> Geometry;
    UUID Material{};
    Mat4 Transform{};
    GraphicsDrawOptions Options;
    bool Scoped=false;
    UUID Camera{};
    UUID Owner{};
    std::uint64_t Viewport=0;
};
struct GraphicsAccess {
    static void DrawEntity(GraphicsMesh mesh,UUID material,UUID owner,GraphicsDrawOptions options);
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
