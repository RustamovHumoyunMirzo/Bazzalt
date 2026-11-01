#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Components/Transform.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace Bazzalt {
namespace Runtime { class StreamingRuntime; struct ResourceLoadState; struct SceneLoadState; }
enum class AsyncLoadStatus : std::uint8_t { Queued, Loading, Ready, Activating, Complete, Cancelled, Failed, Unloaded };
struct StreamingSettings {
    double MainThreadBudgetMilliseconds = 2.0;
    std::uint32_t EntitiesPerTick = 128;
    std::uint32_t MaxPendingLoads = 64;
    std::uint32_t MaxConcurrentLoads = 4;
    std::size_t ResourceBudgetBytes = 256u * 1024u * 1024u;
};
struct StreamingCell {
    UUID SceneAsset;
    Transform Offset;
    float LoadDistance = 100, UnloadDistance = 150;
};
/// Immutable CPU asset data. Completion does not mean GPU residency.
class BAZZALT_API ResourceLoadHandle final {
public:
    ResourceLoadHandle() = default;
    AsyncLoadStatus GetStatus() const;
    bool IsDone() const;
    float GetProgress() const;
    std::string GetError() const;
    std::shared_ptr<const std::vector<std::uint8_t>> GetData() const;
    bool Cancel() const;
private:
    friend class Runtime::StreamingRuntime;
    explicit ResourceLoadHandle(std::shared_ptr<Runtime::ResourceLoadState> state):m_state(std::move(state)){}
    std::shared_ptr<Runtime::ResourceLoadState> m_state;
};
/// A scene replacement or additive world-cell request, activated only at a frame boundary.
class BAZZALT_API SceneLoadHandle final {
public:
    SceneLoadHandle() = default;
    AsyncLoadStatus GetStatus() const;
    bool IsDone() const;
    float GetProgress() const;
    std::string GetError() const;
    UUID GetSceneUUID() const;
    /// Additive cell's parent entity in the active scene. Root UUID means not yet created.
    UUID GetRootUUID() const;
    bool SetActivationAllowed(bool allowed) const;
    bool Cancel() const;
private:
    friend class Runtime::StreamingRuntime;
    explicit SceneLoadHandle(std::shared_ptr<Runtime::SceneLoadState> state):m_state(std::move(state)){}
    std::shared_ptr<Runtime::SceneLoadState> m_state;
};
}
