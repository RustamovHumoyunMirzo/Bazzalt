#include "Runtime/ConsoleStore.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_set>
namespace Bazzalt {
namespace {
struct Store {std::mutex Mutex;unsigned Hosts=0;std::size_t Bytes=0;std::uint64_t Revision=1,NextId=1;std::deque<ConsoleMessage> Messages;};
Store& Data(){static Store value;return value;}
bool Matches(const ConsoleMessage& message,const ConsoleFilter& filter){return (filter.Levels&static_cast<unsigned>(message.Level))&& (filter.Source.empty()||filter.Source==message.Source)&&(filter.Contains.empty()||message.Text.find(filter.Contains)!=std::string::npos);}
template<class Predicate> void Remove(Predicate predicate){auto& data=Data();std::lock_guard lock(data.Mutex);if(!data.Hosts)return;if(std::erase_if(data.Messages,[&](const auto& m){if(!predicate(m))return false;data.Bytes-=m.Text.size()+m.Source.size();return true;}))++data.Revision;}
}
bool Console::IsAvailable() noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);return d.Hosts!=0;}catch(...){return false;}}
void Console::Log(const std::string& text,ConsoleLevel level,const std::string& source,bool icon) noexcept{
    try{auto& d=Data();std::lock_guard lock(d.Mutex);if(!d.Hosts)return;
        if(level!=ConsoleLevel::Info&&level!=ConsoleLevel::Warning&&level!=ConsoleLevel::Error)return;
        // Bound retained data and per-message payloads; discarded messages never print.
        if(text.size()>65536||source.size()>256)return;
        const double timestamp=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        d.Messages.push_back({d.NextId++,text,level,source.empty()?"Script":source,icon,timestamp});
        d.Bytes+=d.Messages.back().Text.size()+d.Messages.back().Source.size();
        while(d.Messages.size()>10000||d.Bytes>16*1024*1024){d.Bytes-=d.Messages.front().Text.size()+d.Messages.front().Source.size();d.Messages.pop_front();}++d.Revision;
    }catch(...){}
}
void Console::Info(const std::string& text,const std::string& source,bool icon) noexcept{Log(text,ConsoleLevel::Info,source,icon);}
void Console::Warning(const std::string& text,const std::string& source,bool icon) noexcept{Log(text,ConsoleLevel::Warning,source,icon);}
void Console::Error(const std::string& text,const std::string& source,bool icon) noexcept{Log(text,ConsoleLevel::Error,source,icon);}
void Console::Clear(const ConsoleFilter& filter) noexcept{try{Remove([&](const auto& message){return Matches(message,filter);});}catch(...){} }
void Console::ClearAt(std::size_t index) noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);if(d.Hosts&&index<d.Messages.size()){d.Bytes-=d.Messages[index].Text.size()+d.Messages[index].Source.size();d.Messages.erase(d.Messages.begin()+index);++d.Revision;}}catch(...){} }
void Console::ClearMessage(std::uint64_t id) noexcept{try{Remove([&](const auto& message){return message.Id==id;});}catch(...){} }
void Console::ClearMessages(const std::vector<std::uint64_t>& ids) noexcept{if(!IsAvailable())return;try{std::unordered_set<std::uint64_t> selected(ids.begin(),ids.end());Remove([&](const auto& message){return selected.contains(message.Id);});}catch(...){} }
std::optional<std::vector<ConsoleMessage>> Console::GetMessages(const ConsoleFilter& filter) noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);if(!d.Hosts)return {};std::vector<ConsoleMessage> result;for(const auto& message:d.Messages)if(Matches(message,filter))result.push_back(message);return result;}catch(...){return {};}}
std::optional<std::size_t> Console::GetCount(const ConsoleFilter& filter) noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);if(!d.Hosts)return {};return std::count_if(d.Messages.begin(),d.Messages.end(),[&](const auto& message){return Matches(message,filter);});}catch(...){return {};}}
std::optional<ConsoleMessage> Console::GetMessageAt(std::size_t index) noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);if(!d.Hosts||index>=d.Messages.size())return {};return d.Messages[index];}catch(...){return {};}}
std::optional<ConsoleMessage> Console::FindMessage(std::uint64_t id) noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);if(!d.Hosts)return {};for(const auto& m:d.Messages)if(m.Id==id)return m;return {};}catch(...){return {};}}
namespace Runtime {
void ConsoleAccess::Acquire(){auto& d=Data();std::lock_guard lock(d.Mutex);++d.Hosts;}
void ConsoleAccess::Release() noexcept{try{auto& d=Data();std::lock_guard lock(d.Mutex);if(d.Hosts&&!--d.Hosts){d.Messages.clear();d.Bytes=0;++d.Revision;}}catch(...){} }
std::optional<ConsoleSnapshot> ConsoleAccess::Snapshot(std::uint64_t afterRevision){auto& d=Data();std::lock_guard lock(d.Mutex);if(!d.Hosts||afterRevision==d.Revision)return {};return ConsoleSnapshot{d.Revision,{d.Messages.begin(),d.Messages.end()}};}
}
}
