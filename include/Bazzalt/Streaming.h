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
enum class AsyncLoadStatus : std::uint8_t { Queued, Loading, Ready, Activating, Complete, Cancelled, Failed, Unloaded, Unloading };
struct StreamingSettings {
    double MainThreadBudgetMilliseconds = 2.0;
    std::uint32_t EntitiesPerTick = 128;
    std::uint32_t MaxPendingLoads = 64;
    std::uint32_t MaxConcurrentLoads = 4;
    std::size_t ResourceBudgetBytes = 256u * 1024u * 1024u;
    std::uint32_t MaxResidentCells = 128;
    float RetryDelaySeconds = 2;
};
struct StreamingCell {
    UUID SceneAsset;
    Transform Offset;
    float LoadDistance = 100, UnloadDistance = 150;
    Vec3 HalfExtents{};
    int Priority = 0;
    bool AlwaysLoaded = false;
};
struct StreamingGridCell { UUID SceneAsset;int X=0,Y=0,Z=0; };
struct BAZZALT_API StreamingGrid {
    Vec3 Origin{},CellSize{256};
    float LoadDistance=100,UnloadDistance=150;
    std::vector<StreamingGridCell> Cells;
    bool IsValid()const;
};
struct StreamingCellInfo {
    UUID SceneAsset,SceneUUID,RootUUID;
    AsyncLoadStatus Status=AsyncLoadStatus::Unloaded;
    float Progress=0,Distance=0;
    bool Automatic=false;
    std::string Error;
};
struct StreamingEvent { UUID SceneAsset,RootUUID;AsyncLoadStatus Status=AsyncLoadStatus::Queued;std::string Error; };
struct StreamingStatistics {
    std::uint32_t RegisteredCells=0,ResidentCells=0,PendingLoads=0,UnloadingCells=0,Sources=0;
    std::size_t ResourceBytes=0;
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
