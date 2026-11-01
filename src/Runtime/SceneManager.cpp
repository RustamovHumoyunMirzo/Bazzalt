#include "Runtime/StreamingRuntime.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/Detail/SubsystemAccess.h"

#include <algorithm>
#include <vector>

#include "Runtime/Engine.h"

namespace Bazzalt {
namespace {
std::vector<Runtime::Engine*> Engines;
Runtime::Engine* ActiveEngine() { return Engines.empty() ? nullptr : Engines.back(); }
}

bool SceneManager::LoadScene(const std::filesystem::path& path) {
    Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.LoadScene");
    auto* engine = ActiveEngine();
    return engine != nullptr && engine->RequestSceneLoad(path);
}

bool SceneManager::LoadScene(UUID assetId) {
    Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.LoadScene");
    auto* engine = ActiveEngine();
    return engine != nullptr && engine->RequestSceneLoad(assetId);
}

SceneLoadHandle SceneManager::LoadSceneAsync(UUID id,bool activation){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.LoadSceneAsync");auto* engine=ActiveEngine();return engine?engine->m_streaming->RequestScene(id,false,activation):SceneLoadHandle{};}
SceneLoadHandle SceneManager::LoadSceneAsync(const std::filesystem::path& path,bool activation){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.LoadSceneAsync");auto* engine=ActiveEngine();return engine?engine->m_streaming->RequestScene(path,false,activation):SceneLoadHandle{};}
SceneLoadHandle SceneManager::LoadWorldCellAsync(UUID id,const Transform& offset){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.LoadWorldCellAsync");auto* engine=ActiveEngine();return engine?engine->m_streaming->RequestScene(id,true,true,offset):SceneLoadHandle{};}
bool SceneManager::UnloadWorldCell(UUID id){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.UnloadWorldCell");auto* engine=ActiveEngine();return engine&&engine->m_streaming->UnloadCell(id);}
bool SceneManager::RegisterStreamingCell(const StreamingCell& cell){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.RegisterStreamingCell");auto* engine=ActiveEngine();return engine&&engine->m_streaming->RegisterCell(cell);}
bool SceneManager::UnregisterStreamingCell(UUID id){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.UnregisterStreamingCell");auto* engine=ActiveEngine();return engine&&engine->m_streaming->UnregisterCell(id);}
void SceneManager::SetStreamingPosition(Vec3 position){Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.SetStreamingPosition");auto* engine=ActiveEngine();if(engine)engine->m_streaming->SetPosition(position);}
StreamingSettings SceneManager::GetStreamingSettings(){auto* engine=ActiveEngine();return engine?engine->m_streaming->GetSettings():StreamingSettings{};}
void SceneManager::SetStreamingSettings(const StreamingSettings& settings){auto* engine=ActiveEngine();if(engine)engine->m_streaming->SetSettings(settings);}

Scene* SceneManager::GetActiveScene() {
    Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"SceneManager.GetActiveScene");
    auto* engine = ActiveEngine();
    return engine != nullptr ? &engine->GetScene() : nullptr;
}

bool SceneManager::IsLoadPending() {
    auto* engine = ActiveEngine();
    return engine != nullptr && engine->IsSceneLoadPending();
}

std::string SceneManager::GetLastError() {
    auto* engine = ActiveEngine();
    return engine != nullptr ? engine->GetLastError() : "Runtime is not active";
}

void SceneManager::Bind(Runtime::Engine* engine) {
    if (engine && std::find(Engines.begin(), Engines.end(), engine) == Engines.end())
        Engines.push_back(engine);
}
void SceneManager::Unbind(Runtime::Engine* engine) {
    Engines.erase(std::remove(Engines.begin(), Engines.end(), engine), Engines.end());
}

} // namespace Bazzalt
