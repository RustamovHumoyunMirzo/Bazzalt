#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/StreamingRuntime.h"
#include "Runtime/PreparedScene.h"
#include "Runtime/Engine.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Runtime/LuaRuntime.h"
#include "Runtime/AssetDatabase.h"
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Profiler.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
namespace Bazzalt::Runtime {
namespace {
bool Terminal(AsyncLoadStatus status) {
    return status==AsyncLoadStatus::Complete||status==AsyncLoadStatus::Cancelled||status==AsyncLoadStatus::Failed||status==AsyncLoadStatus::Unloaded;
}
struct Operation {
    std::atomic<AsyncLoadStatus> Status{AsyncLoadStatus::Queued};
    std::atomic<bool> Cancelled{false};
    std::atomic<float> Progress{0};
    mutable std::mutex Mutex;
    std::string Error;
    void Fail(std::string error) {
        {std::lock_guard lock(Mutex);Error=std::move(error);}
        Status=Cancelled?AsyncLoadStatus::Cancelled:AsyncLoadStatus::Failed;
    }
};
struct Budget {
    std::atomic<std::size_t> Used{0}, Limit{256u*1024u*1024u};
};
struct Reservation {
    std::shared_ptr<Budget> Owner;
    std::size_t Bytes;
    Reservation(std::shared_ptr<Budget> owner,std::size_t bytes):Owner(std::move(owner)),Bytes(bytes) {
        auto used=Owner->Used.load();
        do {const auto limit=Owner->Limit.load();if(bytes>limit||used>limit-bytes)throw std::runtime_error("Streaming resource memory budget exceeded; release completed handles or increase the budget");}
        while(!Owner->Used.compare_exchange_weak(used,used+bytes));
    }
    ~Reservation(){Owner->Used.fetch_sub(Bytes);}
};
struct Bytes {
    Reservation Memory;
    std::vector<std::uint8_t> Data;
    Bytes(std::shared_ptr<Budget> budget,std::size_t size):Memory(std::move(budget),size),Data(size){}
};
std::uintmax_t FileSize(const std::filesystem::path& path,std::uintmax_t maximum) {
    std::error_code error;auto size=std::filesystem::file_size(path,error);
    if(error)throw std::runtime_error("Could not inspect streaming resource");
    if(size>maximum)throw std::runtime_error("Streaming resource exceeds the configured size limit");
    return size;
}
}
struct ResourceLoadState : Operation {
    std::filesystem::path Path;
    std::shared_ptr<const std::vector<std::uint8_t>> Data;
};
struct SceneLoadState : Operation {
    std::atomic<bool> Activation{true};
    bool Committing=false; // Protected by Mutex: cancellation cannot race the final swap/lifecycle.
    UUID SceneId,Root;
    std::shared_ptr<PreparedScene> Prepared;
    std::shared_ptr<Reservation> Memory;
};
struct StreamingRuntime::Impl {
    Engine& Host;
    StreamingSettings Settings;
    std::shared_ptr<Budget> Memory=std::make_shared<Budget>();
    std::vector<std::shared_ptr<ResourceLoadState>> Resources;
    struct Cell {
        std::filesystem::path Path;
        UUID Asset;
        Transform Offset;
        bool Additive=false,Unload=false;
        std::shared_ptr<SceneLoadState> State=std::make_shared<SceneLoadState>();
        std::unique_ptr<Scene> Staging;
        std::vector<UUID> Owned;
        std::size_t Created=0,Applied=0;
        std::unique_ptr<NativeScriptRuntime> Native;
        std::unique_ptr<LuaRuntime> Lua;
    };
    std::vector<std::shared_ptr<Cell>> Cells;
    std::unordered_map<UUID,StreamingCell> Registered;
    std::unordered_map<UUID,SceneLoadHandle> Automatic;
    Vec3 Position{};
    bool HasPosition=false,Replacing=false,Resetting=false;
    explicit Impl(Engine& engine):Host(engine){}
    std::size_t Pending() const {
        std::size_t count=0;
        for(auto& cell:Cells)if(!Terminal(cell->State->Status))++count;
        for(auto& resource:Resources)if(!Terminal(resource->Status))++count;
        return count;
    }
    void RemoveEntities(Cell& cell) {
        if(!cell.Additive)return;
        auto& scene=Host.GetScene();
        std::unordered_set<UUID> owned(cell.Owned.begin(),cell.Owned.end());
        if(cell.State->Root)owned.insert(cell.State->Root);
        // Preserve unrelated objects that were parented beneath the streamed cell.
        for(auto id:owned)if(auto entity=scene.GetEntity(id))for(auto child:entity.GetChildren())
            if(!owned.contains(child.GetUUID()))scene.RemoveParent(child,true);
        for(auto it=cell.Owned.rbegin();it!=cell.Owned.rend();++it)if(auto entity=scene.GetEntity(*it))scene.DestroyEntity(entity);
        if(auto root=scene.GetEntity(cell.State->Root))scene.DestroyEntity(root);
        cell.Owned.clear();
    }
    void Stop(Cell& cell) {if(cell.Lua)cell.Lua->Stop();if(cell.Native)cell.Native->Stop();cell.Lua.reset();cell.Native.reset();}
    void Release(Cell& cell) {Stop(cell);RemoveEntities(cell);cell.Staging.reset();{std::lock_guard lock(cell.State->Mutex);cell.State->Prepared.reset();cell.State->Memory.reset();}}
    void StartCellScripts(Cell& cell) {
        if(!Host.m_scriptsRunning)return;
        std::vector<ScriptBinding> native,lua;
        for(auto id:cell.Owned){
            auto entity=Host.GetScene().GetEntity(id);auto* components=entity.TryGetComponent<ScriptComponents>();
            if(!components||!components->IsEnabled())continue;
            for(const auto& script:components->Values){
                if(!script.Enabled)continue;
                UUID source;std::optional<AssetInfo> asset;
                if(UUID::TryParse(script.Source,source))asset=Host.FindAsset(source);
                else {auto path=std::filesystem::u8path(script.Source);asset=Host.FindAsset(path.is_absolute()?path:Host.m_projectPath.parent_path()/path);}
                if(!asset||asset->State!=AssetState::Ready)throw std::runtime_error("World cell script asset is not ready: "+script.TypeName);
                if(Host.m_subsystemAssets.contains(asset->Id))throw std::runtime_error("Global subsystem cannot be attached in a world cell");
                ScriptBinding binding;binding.Module=asset->CachePath;binding.Entity=id.ToString();binding.TypeName=script.TypeName;
                for(auto& property:script.Properties)binding.Properties[property.Name]=property.Value;
                const auto extension=asset->CachePath.extension();
                if(extension==".dll"||extension==".so"||extension==".dylib")native.push_back(std::move(binding));
                else if(extension==".blua"||extension==".lua")lua.push_back(std::move(binding));
                else throw std::runtime_error("World cell script has not been compiled/imported: "+script.TypeName);
            }
        }
        std::string error;
        if(!native.empty()) {cell.Native=std::make_unique<NativeScriptRuntime>();if(!cell.Native->Configure(std::move(native),error)||!cell.Native->Start(error))throw std::runtime_error(error);}
        if(!lua.empty()) {cell.Lua=std::make_unique<LuaRuntime>();if(!cell.Lua->Configure(std::move(lua),error)||!cell.Lua->Start(error))throw std::runtime_error(error);}
    }
};
StreamingRuntime::StreamingRuntime(Engine& host):m_impl(std::make_unique<Impl>(host)){}
StreamingRuntime::~StreamingRuntime(){Reset();}
ResourceLoadHandle StreamingRuntime::RequestAsset(UUID id) {
    auto state=std::make_shared<ResourceLoadState>();ResourceLoadHandle result(state);
    if(m_impl->Resetting){state->Fail("Scene is changing");return result;}
    auto asset=m_impl->Host.FindAsset(id);
    if(!asset||asset->State!=AssetState::Ready){state->Fail("Asset is not present or imported");return result;}
    if(m_impl->Pending()>=m_impl->Settings.MaxPendingLoads){state->Fail("Streaming request queue is full");return result;}
    state->Path=asset->CachePath;m_impl->Resources.push_back(state);return result;
}
SceneLoadHandle StreamingRuntime::RequestScene(UUID asset,bool additive,bool activation,const Transform& offset) {
    auto info=m_impl->Host.FindAsset(asset);
    if(!info||info->State!=AssetState::Ready||info->SourcePath.extension()!=".bscene") {
        auto state=std::make_shared<SceneLoadState>();state->Fail("Scene asset is not present or imported");return SceneLoadHandle(state);
    }
    return RequestScene(info->CachePath,additive,activation,offset,asset);
}
SceneLoadHandle StreamingRuntime::RequestScene(const std::filesystem::path& path,bool additive,bool activation,const Transform& offset,UUID asset) {
    auto& impl=*m_impl;
    if(impl.Resetting){auto state=std::make_shared<SceneLoadState>();state->Fail("Scene is changing");return SceneLoadHandle(state);}
    // One resident request per cell asset. Never duplicate entity UUIDs on repeated calls.
    if(additive&&asset)for(auto& old:impl.Cells)if(old->Additive&&old->Asset==asset&&!old->Unload&&!old->State->Cancelled&&(!Terminal(old->State->Status)||old->State->Status==AsyncLoadStatus::Complete))return SceneLoadHandle(old->State);
    auto cell=std::make_shared<Impl::Cell>();auto& state=*cell->State;
    if(path.empty()||path.extension()!=".bscene")state.Fail("Scene path must use .bscene");
    else if(impl.Pending()>=impl.Settings.MaxPendingLoads)state.Fail("Streaming request queue is full");
    else {
        cell->Path=path;cell->Asset=asset;cell->Additive=additive;cell->Offset=offset;state.Activation=activation;
        if(!std::isfinite(offset.Position.X)||!std::isfinite(offset.Position.Y)||!std::isfinite(offset.Position.Z)||!std::isfinite(offset.Scale.X)||!std::isfinite(offset.Scale.Y)||!std::isfinite(offset.Scale.Z)||offset.Scale.X==0||offset.Scale.Y==0||offset.Scale.Z==0||!std::isfinite(offset.Rotation.X)||!std::isfinite(offset.Rotation.Y)||!std::isfinite(offset.Rotation.Z)||!std::isfinite(offset.Rotation.W)||offset.Rotation.LengthSquared()<Epsilon)state.Fail("Invalid world cell offset");
        else impl.Cells.push_back(cell);
    }
    return SceneLoadHandle(cell->State);
}
bool StreamingRuntime::UnloadCell(UUID asset) {
    for(auto& cell:m_impl->Cells)if(cell->Additive&&cell->Asset==asset&&(!Terminal(cell->State->Status)||cell->State->Status==AsyncLoadStatus::Complete)){
        cell->Unload=true;return true;
    }
    return false;
}
bool StreamingRuntime::RegisterCell(const StreamingCell& cell) {
    if(m_impl->Resetting)return false;
    const auto asset=m_impl->Host.FindAsset(cell.SceneAsset);
    if(!asset||asset->State!=AssetState::Ready||asset->SourcePath.extension()!=".bscene"||!std::isfinite(cell.Offset.Position.X)||!std::isfinite(cell.Offset.Position.Y)||!std::isfinite(cell.Offset.Position.Z)||!cell.SceneAsset||!std::isfinite(cell.LoadDistance)||!std::isfinite(cell.UnloadDistance)||cell.LoadDistance<0||cell.UnloadDistance<=cell.LoadDistance||m_impl->Registered.size()>=1024)return false;
    if(m_impl->Registered.contains(cell.SceneAsset))return false;
    m_impl->Registered.emplace(cell.SceneAsset,cell);return true;
}
bool StreamingRuntime::UnregisterCell(UUID asset){UnloadCell(asset);m_impl->Automatic.erase(asset);return m_impl->Registered.erase(asset)!=0;}
void StreamingRuntime::SetPosition(Vec3 position){if(!std::isfinite(position.X)||!std::isfinite(position.Y)||!std::isfinite(position.Z))throw std::invalid_argument("Streaming position must be finite");m_impl->Position=position;m_impl->HasPosition=true;}
StreamingSettings StreamingRuntime::GetSettings() const{return m_impl->Settings;}
void StreamingRuntime::SetSettings(const StreamingSettings& settings) {
    if(!std::isfinite(settings.MainThreadBudgetMilliseconds)||settings.MainThreadBudgetMilliseconds<=0||settings.MainThreadBudgetMilliseconds>100||!settings.EntitiesPerTick||settings.EntitiesPerTick>100000||!settings.MaxConcurrentLoads||settings.MaxConcurrentLoads>32||!settings.MaxPendingLoads||settings.MaxPendingLoads>1024||settings.ResourceBudgetBytes<1024*1024)
        throw std::invalid_argument("Invalid streaming settings");
    m_impl->Settings=settings;m_impl->Memory->Limit=settings.ResourceBudgetBytes;
}
void StreamingRuntime::Reset() {
    auto& impl=*m_impl;
    if(impl.Resetting)return;
    impl.Resetting=true;
    struct Guard {bool& Flag;~Guard(){Flag=false;}} guard{impl.Resetting};
    auto cells=std::move(impl.Cells);impl.Cells.clear();
    for(auto& cell:cells){cell->State->Cancelled=true;impl.Release(*cell);if(cell->State->Status==AsyncLoadStatus::Complete)cell->State->Status=AsyncLoadStatus::Unloaded;else cell->State->Status=AsyncLoadStatus::Cancelled;}
    impl.Cells.clear();
    for(auto& resource:impl.Resources){if(!Terminal(resource->Status)){resource->Cancelled=true;resource->Status=AsyncLoadStatus::Cancelled;}}
    impl.Resources.clear();impl.Registered.clear();impl.Automatic.clear();impl.HasPosition=false;
}
void StreamingRuntime::StopScripts(){const auto cells=m_impl->Cells;for(auto& cell:cells)m_impl->Stop(*cell);}
bool StreamingRuntime::IsReplacingScene() const{return m_impl->Replacing;}
bool StreamingRuntime::HasPendingScene() const{for(auto& cell:m_impl->Cells)if(!cell->Additive&&!Terminal(cell->State->Status))return true;return false;}
void StreamingRuntime::Pump() {
    ProfileScope scope("Streaming.Pump");auto& impl=*m_impl;
    if(impl.HasPosition){
        std::vector<std::pair<float,UUID>> order;
        for(auto& [id,cell]:impl.Registered)order.emplace_back((impl.Position-cell.Offset.Position).Length(),id);
        std::sort(order.begin(),order.end());
        for(auto [distance,id]:order){auto& cell=impl.Registered.at(id);auto current=impl.Automatic.find(id);
            if(current!=impl.Automatic.end()&&distance>cell.UnloadDistance){UnloadCell(id);impl.Automatic.erase(current);}
            else if(current==impl.Automatic.end()&&distance<=cell.LoadDistance&&impl.Pending()<impl.Settings.MaxPendingLoads)
                impl.Automatic.emplace(id,RequestScene(id,true,true,cell.Offset));
        }
    }
    std::size_t running=0;
    for(auto& resource:impl.Resources)if(resource->Status==AsyncLoadStatus::Loading)++running;
    for(auto& cell:impl.Cells)if(cell->State->Status==AsyncLoadStatus::Loading)++running;
    for(auto& resource:impl.Resources) {
        if(resource->Cancelled){resource->Status=AsyncLoadStatus::Cancelled;continue;}
        if(resource->Status!=AsyncLoadStatus::Queued||running>=impl.Settings.MaxConcurrentLoads)continue;
        resource->Status=AsyncLoadStatus::Loading;++running;auto budget=impl.Memory;
        Jobs::Schedule([resource,budget]{
            try {
                ProfileScope job("Streaming.ReadAsset");
                const auto size=FileSize(resource->Path,budget->Limit.load());
                auto bytes=std::make_shared<Bytes>(budget,static_cast<std::size_t>(size));
                std::ifstream file(resource->Path,std::ios::binary);if(!file)throw std::runtime_error("Could not open streaming resource");
                std::size_t offset=0;
                while(offset<size&&!resource->Cancelled){const auto count=std::min<std::size_t>(1024*1024,bytes->Data.size()-offset);file.read(reinterpret_cast<char*>(bytes->Data.data()+offset),static_cast<std::streamsize>(count));if(!file)throw std::runtime_error("Streaming resource changed or could not be read");offset+=count;resource->Progress=size?float(double(offset)/double(size)):1;}
                if(resource->Cancelled){resource->Status=AsyncLoadStatus::Cancelled;return;}
                if(file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Streaming resource changed while reading");
                {std::lock_guard lock(resource->Mutex);if(resource->Cancelled){resource->Status=AsyncLoadStatus::Cancelled;return;}resource->Data={bytes,&bytes->Data};resource->Progress=1;resource->Status=AsyncLoadStatus::Complete;}
            }catch(const std::exception& error){resource->Fail(error.what());}catch(...){resource->Fail("Resource loading failed");}
        });
    }
    for(auto& cell:impl.Cells) {
        auto state=cell->State;
        if(state->Cancelled||cell->Unload)continue;
        if(state->Status!=AsyncLoadStatus::Queued||running>=impl.Settings.MaxConcurrentLoads)continue;
        state->Status=AsyncLoadStatus::Loading;++running;auto path=cell->Path;auto budget=impl.Memory;
        Jobs::Schedule([state,path,budget]{
            try{
                auto memory=std::make_shared<Reservation>(budget,static_cast<std::size_t>(FileSize(path,64u*1024u*1024u)));
                auto document=std::make_shared<PreparedScene>();std::string error;
                if(!PrepareScene(path,*document,error))throw std::runtime_error(error);
                if(state->Cancelled){state->Status=AsyncLoadStatus::Cancelled;return;}
                {std::lock_guard lock(state->Mutex);if(state->Cancelled){state->Status=AsyncLoadStatus::Cancelled;return;}state->SceneId=document->Id;state->Prepared=std::move(document);state->Memory=std::move(memory);state->Progress=.5f;state->Status=AsyncLoadStatus::Ready;}
            }catch(const std::exception& error){state->Fail(error.what());}catch(...){state->Fail("Scene loading failed");}
        });
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::duration<double,std::milli>(impl.Settings.MainThreadBudgetMilliseconds);
    std::size_t remaining=impl.Settings.EntitiesPerTick;
    // Keep the request list stable across scene replacement and script callbacks.
    const auto cells=impl.Cells;
    for(auto& cell:cells){auto& state=*cell->State;
        if(cell->Additive&&state.Status==AsyncLoadStatus::Complete&&state.Root&&!impl.Host.GetScene().GetEntity(state.Root))cell->Unload=true;
        if(cell->Unload||state.Cancelled){impl.Release(*cell);state.Status=cell->Unload?AsyncLoadStatus::Unloaded:AsyncLoadStatus::Cancelled;continue;}
        auto status=state.Status.load();if(status!=AsyncLoadStatus::Ready&&status!=AsyncLoadStatus::Activating)continue;
        if(!state.Activation||!remaining||std::chrono::steady_clock::now()>=deadline)continue;
        try {
            if(status==AsyncLoadStatus::Ready){
                state.Status=AsyncLoadStatus::Activating;
                if(cell->Additive){
                    auto& scene=impl.Host.GetScene();
                    for(auto& record:state.Prepared->Entities)if(scene.GetEntity(record.Id))throw std::runtime_error("World cell entity UUID conflicts with an existing entity");
                    auto root=scene.CreateEntity("Streamed world cell");
                    {std::lock_guard lock(state.Mutex);state.Root=root.GetUUID();}
                    root.AddOrReplaceComponent<Transform>(cell->Offset);
                }else{
                    ApplicationEventsAccess::Emit({ApplicationEventType::OnLoad});
                    ApplicationEvent loading{ApplicationEventType::OnSceneLoad};auto text=cell->Path.generic_u8string();loading.ScenePath.assign(reinterpret_cast<const char*>(text.data()),text.size());ApplicationEventsAccess::Emit(loading);
                    cell->Staging=std::make_unique<Scene>();cell->Staging->m_uuid=state.Prepared->Id;
                    cell->Staging->SetEnvironment(state.Prepared->Environment);cell->Staging->SetPhysicsSettings(state.Prepared->Physics);cell->Staging->m_extensionProperties=state.Prepared->ExtensionProperties;
                }
            }
            auto& scene=cell->Additive?impl.Host.GetScene():*cell->Staging;auto& records=state.Prepared->Entities;
            while(!state.Cancelled&&cell->Created<records.size()&&remaining&&std::chrono::steady_clock::now()<deadline){auto& record=records[cell->Created];scene.CreateEntity(record.Id,record.Name);cell->Owned.push_back(record.Id);++cell->Created;--remaining;}
            while(!state.Cancelled&&cell->Created==records.size()&&cell->Applied<records.size()&&remaining&&std::chrono::steady_clock::now()<deadline){
                auto& record=records[cell->Applied];auto entity=scene.GetEntity(record.Id);
                auto parent=record.Parent.IsRoot()?(cell->Additive?scene.GetEntity(state.Root):scene.GetRootEntity()):scene.GetEntity(record.Parent);
                if(!scene.SetParent(entity,parent,false))throw std::runtime_error("Invalid streamed hierarchy");
                for(auto& component:record.Components){
                    if(auto* descriptor=impl.Host.m_sceneSerializer.GetComponents().Find(component.Type)){
                        if(!descriptor->Deserialize(entity,component.Properties,component.Version))throw std::runtime_error("Invalid streamed component: "+component.Type);
                    }else {auto* unresolved=entity.TryGetComponent<UnresolvedComponents>();if(!unresolved)unresolved=&entity.AddComponent<UnresolvedComponents>();unresolved->Values.push_back(component);}
                }
                ++cell->Applied;--remaining;state.Progress=.5f+.5f*float(cell->Applied)/float(std::max<std::size_t>(1,records.size()));
            }
            if(state.Cancelled){impl.Release(*cell);state.Status=AsyncLoadStatus::Cancelled;continue;}
            if(cell->Applied!=records.size()||cell->Created!=records.size())continue;
            {std::lock_guard lock(state.Mutex);if(!state.Cancelled)state.Committing=true;}
            if(state.Cancelled){impl.Release(*cell);state.Status=AsyncLoadStatus::Cancelled;continue;}
            if(cell->Additive){impl.StartCellScripts(*cell);state.Prepared.reset();state.Memory.reset();state.Progress=1;state.Status=AsyncLoadStatus::Complete;}
            else {
                const bool play=impl.Host.m_scriptsRunning;
                // Stop old-cell behavior before replacing its owning scene.
                for(auto& other:cells)if(other!=cell){other->State->Cancelled=true;impl.Release(*other);other->State->Status=AsyncLoadStatus::Cancelled;}
                impl.Cells.clear();impl.Automatic.clear();impl.Registered.clear();impl.HasPosition=false;
                impl.Replacing=true;
                try{impl.Host.SetScene(std::move(cell->Staging));if(play&&!impl.Host.StartScripts(false))throw std::runtime_error(impl.Host.GetLastError());}
                catch(...){impl.Replacing=false;throw;}
                impl.Replacing=false;state.Prepared.reset();state.Memory.reset();state.Progress=1;state.Status=AsyncLoadStatus::Complete;return;
            }
        }catch(const std::exception& error){impl.Release(*cell);state.Fail(error.what());Console::Error(error.what(),"Streaming");}
    }
    std::erase_if(impl.Resources,[](auto& state){return Terminal(state->Status);});
    std::erase_if(impl.Cells,[](auto& cell){auto status=cell->State->Status.load();return Terminal(status)&&status!=AsyncLoadStatus::Complete;});
}
void StreamingRuntime::Update(float dt){auto cells=m_impl->Cells;for(auto& cell:cells)if(cell->State->Status==AsyncLoadStatus::Complete){if(cell->Native)cell->Native->Update(dt);if(cell->Lua)cell->Lua->Update(dt);}}
void StreamingRuntime::FixedUpdate(float dt){auto cells=m_impl->Cells;for(auto& cell:cells)if(cell->State->Status==AsyncLoadStatus::Complete){if(cell->Native)cell->Native->FixedUpdate(dt);if(cell->Lua)cell->Lua->FixedUpdate(dt);}}
void StreamingRuntime::LateUpdate(float dt){auto cells=m_impl->Cells;for(auto& cell:cells)if(cell->State->Status==AsyncLoadStatus::Complete){if(cell->Native)cell->Native->LateUpdate(dt);if(cell->Lua)cell->Lua->LateUpdate(dt);}}
} // namespace Bazzalt::Runtime
namespace Bazzalt {
namespace {
template<typename State> std::string Error(const std::shared_ptr<State>& state){if(!state)return "Invalid load handle";std::lock_guard lock(state->Mutex);return state->Error;}
template<typename State> bool Cancel(const std::shared_ptr<State>& state){if(!state)return false;std::lock_guard lock(state->Mutex);if(Runtime::Terminal(state->Status))return false;state->Cancelled=true;return true;}
}
AsyncLoadStatus ResourceLoadHandle::GetStatus()const{return m_state?m_state->Status.load():AsyncLoadStatus::Failed;}
bool ResourceLoadHandle::IsDone()const{return Runtime::Terminal(GetStatus());}
float ResourceLoadHandle::GetProgress()const{return m_state?m_state->Progress.load():0;}
std::string ResourceLoadHandle::GetError()const{return Error(m_state);}
std::shared_ptr<const std::vector<std::uint8_t>> ResourceLoadHandle::GetData()const{if(!m_state||m_state->Status!=AsyncLoadStatus::Complete)return {};std::lock_guard lock(m_state->Mutex);return m_state->Data;}
bool ResourceLoadHandle::Cancel()const{return Bazzalt::Cancel(m_state);}
AsyncLoadStatus SceneLoadHandle::GetStatus()const{return m_state?m_state->Status.load():AsyncLoadStatus::Failed;}
bool SceneLoadHandle::IsDone()const{return Runtime::Terminal(GetStatus());}
float SceneLoadHandle::GetProgress()const{return m_state?m_state->Progress.load():0;}
std::string SceneLoadHandle::GetError()const{return Error(m_state);}
UUID SceneLoadHandle::GetSceneUUID()const{if(!m_state)return {};std::lock_guard lock(m_state->Mutex);return m_state->SceneId;}
UUID SceneLoadHandle::GetRootUUID()const{if(!m_state)return {};std::lock_guard lock(m_state->Mutex);return m_state->Root;}
bool SceneLoadHandle::Cancel()const{if(!m_state)return false;std::lock_guard lock(m_state->Mutex);if(Runtime::Terminal(m_state->Status)||m_state->Committing)return false;m_state->Cancelled=true;return true;}
bool SceneLoadHandle::SetActivationAllowed(bool allowed)const{if(!m_state||IsDone()||GetStatus()==AsyncLoadStatus::Activating)return false;m_state->Activation=allowed;return true;}
}
