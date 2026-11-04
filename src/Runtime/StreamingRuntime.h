#pragma once
#include "Bazzalt/Streaming.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Entity.h"
#include <filesystem>
#include <memory>
#include <unordered_set>
namespace Bazzalt::Runtime {
class Engine;
class LuaRuntime;
class StreamingRuntime final {
public:
    explicit StreamingRuntime(Engine& engine);
    ~StreamingRuntime();
    ResourceLoadHandle RequestAsset(UUID id);
    SceneLoadHandle RequestScene(const std::filesystem::path& path,bool additive,bool activation,const Transform& offset={},UUID asset={});
    SceneLoadHandle RequestScene(UUID asset,bool additive,bool activation,const Transform& offset={});
    bool UnloadCell(UUID asset);
    bool RegisterCell(const StreamingCell& cell);
    bool UnregisterCell(UUID asset);
    void SetPosition(Vec3 position);
    void ClearPosition();
    void SetSource(UUID source,Vec3 position,float scale);
    bool RemoveSource(UUID source);
    bool RegisterGrid(const StreamingGrid&);
    std::vector<StreamingCellInfo> GetCells()const;
    std::vector<StreamingEvent> GetEvents()const;
    StreamingStatistics GetStatistics()const;
    bool RetryCell(UUID asset);
    StreamingSettings GetSettings() const;
    void SetSettings(const StreamingSettings& settings);
    void Pump();
    void Reset();
    bool StartScripts();
    std::unordered_set<UUID> GetScriptOwnedEntities()const;
    void StopScripts();
    void CollectReloadBindings(std::vector<ScriptBinding>& bindings)const;
    bool PrepareReload(const std::vector<ScriptBinding>& bindings,std::string& error);
    void CommitReload();
    void NotifyReload();
    void CancelReload();
    void AdoptHierarchy(Entity root);
    void Update(float dt);
    void FixedUpdate(float dt);
    void LateUpdate(float dt);
    bool IsReplacingScene() const;
    bool HasPendingScene() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
