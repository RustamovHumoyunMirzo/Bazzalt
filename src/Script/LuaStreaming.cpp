#include "Script/LuaBindings.h"
#include "Bazzalt/Streaming.h"
#include "Bazzalt/SceneManager.h"
namespace Bazzalt::Runtime {
void BindLuaStreaming(sol::table api){
    auto status=api.create_named("AsyncLoadStatus");
    status["Queued"]=AsyncLoadStatus::Queued;status["Loading"]=AsyncLoadStatus::Loading;status["Ready"]=AsyncLoadStatus::Ready;status["Activating"]=AsyncLoadStatus::Activating;status["Complete"]=AsyncLoadStatus::Complete;status["Cancelled"]=AsyncLoadStatus::Cancelled;status["Failed"]=AsyncLoadStatus::Failed;status["Unloaded"]=AsyncLoadStatus::Unloaded;
    auto resource=api.new_usertype<ResourceLoadHandle>("ResourceLoadHandle",sol::constructors<ResourceLoadHandle()>());
    resource["GetStatus"]=&ResourceLoadHandle::GetStatus;resource["IsDone"]=&ResourceLoadHandle::IsDone;resource["GetProgress"]=&ResourceLoadHandle::GetProgress;resource["GetError"]=&ResourceLoadHandle::GetError;resource["Cancel"]=&ResourceLoadHandle::Cancel;
    resource["GetData"]=[](const ResourceLoadHandle& handle)->sol::optional<std::string>{auto data=handle.GetData();if(!data)return sol::nullopt;return std::string(data->begin(),data->end());};
    auto scene=api.new_usertype<SceneLoadHandle>("SceneLoadHandle",sol::constructors<SceneLoadHandle()>());
    scene["GetStatus"]=&SceneLoadHandle::GetStatus;scene["IsDone"]=&SceneLoadHandle::IsDone;scene["GetProgress"]=&SceneLoadHandle::GetProgress;scene["GetError"]=&SceneLoadHandle::GetError;scene["Cancel"]=&SceneLoadHandle::Cancel;scene["GetSceneUUID"]=&SceneLoadHandle::GetSceneUUID;scene["GetRootUUID"]=&SceneLoadHandle::GetRootUUID;scene["SetActivationAllowed"]=&SceneLoadHandle::SetActivationAllowed;
    auto settings=api.new_usertype<StreamingSettings>("StreamingSettings",sol::constructors<StreamingSettings()>());
    settings["MainThreadBudgetMilliseconds"]=&StreamingSettings::MainThreadBudgetMilliseconds;settings["EntitiesPerTick"]=&StreamingSettings::EntitiesPerTick;settings["MaxPendingLoads"]=&StreamingSettings::MaxPendingLoads;settings["MaxConcurrentLoads"]=&StreamingSettings::MaxConcurrentLoads;settings["ResourceBudgetBytes"]=&StreamingSettings::ResourceBudgetBytes;
    auto cell=api.new_usertype<StreamingCell>("StreamingCell",sol::constructors<StreamingCell()>());
    cell["SceneAsset"]=&StreamingCell::SceneAsset;cell["Offset"]=&StreamingCell::Offset;cell["LoadDistance"]=&StreamingCell::LoadDistance;cell["UnloadDistance"]=&StreamingCell::UnloadDistance;
}
}
