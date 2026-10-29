#include "Bazzalt/GlobalEvent.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include "Runtime/GlobalEventsAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/ExtensionAccess.h"
#include <ryml.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <deque>
#include <thread>
#include <unordered_set>
#include <stdexcept>
namespace Bazzalt {
namespace {
struct Entry {
    std::shared_ptr<void> Module;GlobalEvent::Callback Callback;UUID Event,OwnerEntity,OwnerScene;
    const void* Owner=nullptr;int Level=-1;bool AssetAccess=false;std::string Extension;
};
struct Pending {EventInvocation Invocation;unsigned Depth=0;std::size_t Bytes=0;};
struct Registry {
    std::thread::id Thread=std::this_thread::get_id();
    std::map<UUID,EventDefinition> Definitions;
    std::map<std::uint64_t,std::shared_ptr<Entry>> Listeners;
    std::deque<Pending> Queue;std::uint64_t Next=1,Frame=~std::uint64_t(0);
    unsigned Fires=0,Deliveries=0,Depth=0;std::size_t PendingBytes=0;bool Dispatching=false;
};
thread_local const void* ClosingOwner=nullptr;
Registry& State(){static Registry value;return value;}
void Report(const std::string& error){Console::Error(error,"Global Event");if(!Console::IsAvailable())std::cerr<<"[Global Event] "<<error<<'\n';}
void Check(){if(State().Thread!=std::this_thread::get_id())throw std::logic_error("GlobalEvent must be used on the engine thread");}
bool Identifier(const std::string& text){if(text.empty()||text.size()>128||!((text[0]>='A'&&text[0]<='Z')||(text[0]>='a'&&text[0]<='z')||text[0]=='_'))return false;for(auto c:text)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return false;return true;}
bool Valid(const EventValue& value){return std::visit([](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,double>)return std::isfinite(v);else if constexpr(std::is_same_v<T,std::string>)return v.size()<=65536;else if constexpr(std::is_same_v<T,Vec2>)return std::isfinite(v.X)&&std::isfinite(v.Y);else if constexpr(std::is_same_v<T,Vec3>)return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);else if constexpr(std::is_same_v<T,Vec4>||std::is_same_v<T,Quaternion>)return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z)&&std::isfinite(v.W);else return true;},value);}
struct CallbackScope {
    std::shared_ptr<void> Module;const void* Owner;int Level;bool Access;std::string Extension;
    CallbackScope(const Entry& e):Module(Detail::SetCurrentGameplayModule(e.Module)),Owner(Runtime::ApplicationEventsAccess::SetOwner(e.Owner)),Level(Runtime::ApplicationEventsAccess::SetSubsystemLevel(e.Level)),Access(Runtime::ApplicationEventsAccess::SetAssetAccess(e.AssetAccess)),Extension(Runtime::ExtensionAccess::SetCaller(e.Extension)){}
    ~CallbackScope(){Runtime::ExtensionAccess::SetCaller(std::move(Extension));Runtime::ApplicationEventsAccess::SetAssetAccess(Access);Runtime::ApplicationEventsAccess::SetSubsystemLevel(Level);Runtime::ApplicationEventsAccess::SetOwner(Owner);Detail::SetCurrentGameplayModule(std::move(Module));}
};
}
const EventValue& EventInvocation::GetValue(const std::string& name)const{for(std::size_t i=0;i<Arguments.size();++i)if(Arguments[i].Name==name)return Values.at(i);throw std::out_of_range("Unknown event argument: "+name);}
bool Runtime::GlobalEventsAccess::Read(const std::filesystem::path& path,EventDefinition& definition,std::string& error){
    try{
        std::error_code ec;auto bytes=std::filesystem::file_size(path,ec);if(ec||bytes>1024*1024)throw std::invalid_argument("Global Event is missing or exceeds 1 MiB");
        std::ifstream stream(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(stream)),{});if(!stream.eof()&&stream.fail())throw std::runtime_error("Could not read Global Event");
        auto tree=ryml::parse_in_arena(ryml::csubstr(text.data(),text.size()));auto root=tree.crootref();
        auto scalar=[](ryml::ConstNodeRef n){if(!n.has_val()||n.has_val_tag())throw std::invalid_argument("Expected untagged scalar");auto s=n.val();return std::string(s.str,s.len);};
        if(!root.is_map()||root.num_children()!=3||!root.has_child("Version")||scalar(root["Version"])!="1"||!root.has_child("Name")||!root.has_child("Arguments")||!root["Arguments"].is_seq())throw std::invalid_argument("Expected Version: 1, Name and Arguments");
        EventDefinition value;value.Name=scalar(root["Name"]);if(!Identifier(value.Name)||root["Arguments"].num_children()>32)throw std::invalid_argument("Invalid event name or argument count (maximum 32)");
        static const char* types[]={"Boolean","Integer","Number","String","Vec2","Vec3","Vec4","Quaternion","UUID"};std::unordered_set<std::string> seen;
        for(auto item:root["Arguments"].children()){
            if(!item.is_map()||item.num_children()!=2||!item.has_child("Name")||!item.has_child("Type"))throw std::invalid_argument("Expected argument Name and Type");
            EventArgument argument;argument.Name=scalar(item["Name"]);if(!Identifier(argument.Name)||!seen.insert(argument.Name).second)throw std::invalid_argument("Invalid or duplicate event argument name");auto type=scalar(item["Type"]);unsigned index=0;for(;index<9&&type!=types[index];++index){}if(index==9)throw std::invalid_argument("Unknown event argument type");argument.Type=EventArgumentType(index);value.Arguments.push_back(std::move(argument));
        }
        definition=std::move(value);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void Runtime::GlobalEventsAccess::BindThread(){Check();}
const void* Runtime::GlobalEventsAccess::SetClosingOwner(const void* owner)noexcept{const auto previous=ClosingOwner;ClosingOwner=owner;return previous;}
void Runtime::GlobalEventsAccess::DisconnectOwner(const void* owner)noexcept{
    if(!owner)return;auto& s=State();std::vector<std::uint64_t> ids;for(auto& [id,e]:s.Listeners)if(e->Owner==owner)ids.push_back(id);for(auto id:ids)s.Listeners.erase(id);
}
GlobalEvent GlobalEvent::Load(UUID asset){
    Check();GlobalEvent result;if(!asset)return result;const auto record=AssetManager::GetAsset(asset);
    if(!record||record->State!=AssetState::Ready||record->Importer!="Bazzalt.GlobalEvent")throw std::invalid_argument("Not a ready Global Event asset: "+asset.ToString());
    EventDefinition definition;std::string error;if(!Runtime::GlobalEventsAccess::Read(record->CachePath,definition,error))throw std::invalid_argument(error);
    auto& definitions=State().Definitions;if(!definitions.contains(asset)&&definitions.size()>=1024)throw std::length_error("Loaded Global Event limit exceeded");definitions[asset]=std::move(definition);result.m_asset=asset;return result;
}
bool GlobalEvent::IsValid()const{Check();return m_asset&&State().Definitions.contains(m_asset);}
EventDefinition GlobalEvent::GetDefinition()const{Check();if(!IsValid())throw std::logic_error("Invalid Global Event");return State().Definitions.at(m_asset);}
GlobalEvent::Subscription GlobalEvent::Subscribe(Callback callback,Entity owner)const{
    Check();if(!IsValid()||!callback)throw std::invalid_argument("Invalid event or callback");auto& s=State();if(s.Listeners.size()>=8192)throw std::length_error("Global Event subscription limit exceeded");
    const auto context=Runtime::ApplicationEventsAccess::SetOwner(nullptr);Runtime::ApplicationEventsAccess::SetOwner(context);if(context&&context==ClosingOwner)throw std::logic_error("Cannot subscribe while a script VM is closing");auto extension=Runtime::ExtensionAccess::SetCaller("");Runtime::ExtensionAccess::SetCaller(extension);
    auto e=std::make_shared<Entry>();e->Module=Detail::GetCurrentGameplayModule();e->Callback=std::move(callback);e->Event=m_asset;e->Owner=context;e->Level=Runtime::ApplicationEventsAccess::GetSubsystemLevel();e->AssetAccess=Runtime::ApplicationEventsAccess::GetAssetAccess();e->Extension=std::move(extension);
    if(owner){e->OwnerEntity=owner.GetUUID();e->OwnerScene=owner.GetScene()->GetUUID();}const auto id=s.Next++;s.Listeners.emplace(id,std::move(e));return id;
}
bool GlobalEvent::Unsubscribe(Subscription id)const{Check();auto& entries=State().Listeners;auto it=entries.find(id);if(it==entries.end()||it->second->Event!=m_asset)return false;auto retained=it->second;entries.erase(it);return true;}
void GlobalEvent::Fire(std::vector<EventValue> arguments)const{
    Check();auto definition=GetDefinition();if(arguments.size()!=definition.Arguments.size())throw std::invalid_argument("Global Event argument count mismatch");
    for(std::size_t i=0;i<arguments.size();++i)if(arguments[i].index()!=std::size_t(definition.Arguments[i].Type)||!Valid(arguments[i]))throw std::invalid_argument("Invalid event argument: "+definition.Arguments[i].Name);
    auto& s=State();const auto frame=Time::GetFrameCount();if(!s.Dispatching&&s.Frame!=frame){s.Frame=frame;s.Fires=s.Deliveries=0;}
    if(s.Depth>=32||s.Fires>=4096||s.Queue.size()>=1024)throw std::length_error("Global Event fire/recursion budget exceeded");++s.Fires;
    std::size_t bytes=sizeof(Pending)+definition.Name.size();for(const auto& argument:definition.Arguments)bytes+=sizeof(EventArgument)+argument.Name.size();for(const auto& value:arguments)bytes+=sizeof(EventValue)+(std::holds_alternative<std::string>(value)?std::get<std::string>(value).size():0);
    if(bytes>256*1024||s.PendingBytes>8*1024*1024-bytes)throw std::length_error("Global Event payload/queue memory budget exceeded");
    s.Queue.push_back({{m_asset,definition.Name,definition.Arguments,std::move(arguments)},s.Dispatching?s.Depth+1:0,bytes});s.PendingBytes+=bytes;if(s.Dispatching)return;
    struct Drain {Registry& S;~Drain(){S.Dispatching=false;S.Depth=0;S.Queue.clear();S.PendingBytes=0;}} drain{s};s.Dispatching=true;
    while(!s.Queue.empty()){
        auto pending=std::move(s.Queue.front());s.Queue.pop_front();s.PendingBytes-=pending.Bytes;s.Depth=pending.Depth;
        std::vector<std::uint64_t> ids;for(auto& [id,e]:s.Listeners)if(e->Event==pending.Invocation.Event)ids.push_back(id);
        for(auto id:ids){auto it=s.Listeners.find(id);if(it==s.Listeners.end())continue;auto entry=it->second;
            if(++s.Deliveries>65536)throw std::length_error("Global Event delivery budget exceeded");
            if(entry->OwnerEntity){auto* scene=SceneManager::GetActiveScene();if(!scene||scene->GetUUID()!=entry->OwnerScene||!scene->GetEntity(entry->OwnerEntity)){s.Listeners.erase(id);continue;}}
            try{CallbackScope scope(*entry);entry->Callback(pending.Invocation);}
            catch(const std::exception& error){s.Listeners.erase(id);if(entry->Level>=0)Runtime::ApplicationEventsAccess::Fail(error.what());else Report(std::string("Global Event listener disabled: ")+error.what());}
            catch(...){s.Listeners.erase(id);if(entry->Level>=0)Runtime::ApplicationEventsAccess::Fail("Global Event callback failed");else Report("Global Event listener disabled after unknown exception");}
        }
    }
}
}
