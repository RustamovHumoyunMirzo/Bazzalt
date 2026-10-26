#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "Bazzalt/Material.h"

namespace Bazzalt {
namespace Detail { struct GraphicsMeshState; }
namespace Runtime { struct GraphicsAccess; }

enum class GraphicsTopology { Points, Lines, LineStrip, Triangles, TriangleStrip };
struct GraphicsVertex {
    Vec3 Position{};
    Vec3 Normal{0,0,1};
    Vec2 UV{};
    Vec4 Color{1,1,1,1};
};
// Shared, reusable CPU geometry. GPU buffers are private and allocated on demand.
class BAZZALT_API GraphicsMesh final {
public:
    GraphicsMesh()=default;
    bool IsValid() const;
    std::uint64_t GetId() const;
    std::uint64_t GetRevision() const;
    GraphicsTopology GetTopology() const;
    std::size_t GetVertexCount() const;
    std::size_t GetIndexCount() const;
    std::vector<GraphicsVertex> GetVertices() const;
    std::vector<std::uint32_t> GetIndices() const;
    void SetGeometry(std::vector<GraphicsVertex> vertices,std::vector<std::uint32_t> indices={},GraphicsTopology topology=GraphicsTopology::Triangles);
    void Destroy(); // Invalidates every copy; already-submitted draws retain their snapshot.
private:
    std::shared_ptr<Detail::GraphicsMeshState> m_state;
    friend class Graphics;
    friend struct Runtime::GraphicsAccess;
};
struct GraphicsDrawOptions {
    UUID Camera{}; // Zero: all cameras; camera callbacks automatically scope their draws.
    std::uint8_t LayerMask=0x7f;
    std::uint8_t Priority=4; // 0..7; transparent ordering still follows the material.
    bool CastShadows=false;
    bool ReceiveShadows=true;
};
enum class GraphicsEventType {
    OnBeginFrame, OnCameraResize, OnBeforeCamera, OnBeforeDraw,
    OnAfterDraw, OnAfterCamera, OnEndFrame
};
struct GraphicsEvent {
    GraphicsEventType Type{};
    UUID Scene{};
    UUID Camera{};
    std::uint64_t Viewport=0;
    std::uint64_t Frame=0;
    std::uint32_t Width=0,Height=0;
    bool IsEditorCamera=false;
    Mat4 View{},Projection{},ViewProjection{};
    std::size_t DrawIndex=0,DrawCount=0;
};
// Main-thread drawing API. Submissions last one rendered frame, including captures.
// Before-camera callbacks run during preparation, before GPU render passes begin.
class BAZZALT_API Graphics final {
public:
    Graphics()=delete;
    using Subscription=std::uint64_t;
    using Callback=std::function<void(const GraphicsEvent&)>;
    static constexpr std::size_t MaxVertices=262144;
    static constexpr std::size_t MaxIndices=1048576;
    static constexpr std::size_t MaxDraws=4096;
    static GraphicsMesh CreateMesh(std::vector<GraphicsVertex> vertices,std::vector<std::uint32_t> indices={},GraphicsTopology topology=GraphicsTopology::Triangles);
    static void DrawMesh(GraphicsMesh mesh,Material material={},Mat4 transform={},GraphicsDrawOptions options={});
    static void DrawVertices(std::vector<GraphicsVertex> vertices,std::vector<std::uint32_t> indices={},GraphicsTopology topology=GraphicsTopology::Triangles,Material material={},Mat4 transform={},GraphicsDrawOptions options={});
    static void DrawLine(Vec3 from,Vec3 to,Vec4 color={1,1,1,1},GraphicsDrawOptions options={});
    static void DrawTriangle(Vec3 a,Vec3 b,Vec3 c,Vec4 color={1,1,1,1},GraphicsDrawOptions options={});
    static void ClearDraws();
    static bool IsAvailable();
    // Pass the attached entity as owner to expire callbacks when it is destroyed.
    // Scene transitions and Lua VM teardown also remove subscriptions safely.
    static Subscription Subscribe(GraphicsEventType type,Callback callback,Entity owner={});
    static bool Unsubscribe(Subscription subscription);
    static Subscription OnBeginFrame(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnBeginFrame,std::move(callback),owner);}
    static Subscription OnCameraResize(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnCameraResize,std::move(callback),owner);}
    static Subscription OnBeforeCamera(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnBeforeCamera,std::move(callback),owner);}
    static Subscription OnBeforeDraw(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnBeforeDraw,std::move(callback),owner);}
    static Subscription OnAfterDraw(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnAfterDraw,std::move(callback),owner);}
    static Subscription OnAfterCamera(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnAfterCamera,std::move(callback),owner);}
    static Subscription OnEndFrame(Callback callback,Entity owner={}){return Subscribe(GraphicsEventType::OnEndFrame,std::move(callback),owner);}
};
}
