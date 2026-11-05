#include "Bazzalt/DeveloperConsole.h"
#include "Bazzalt/Application.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include "Runtime/DeveloperConsoleAccess.h"
#include "Runtime/GlobalEventsAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <thread>
namespace Bazzalt {
namespace {
bool Flag(CVarFlags value,CVarFlags flag){return (unsigned(value)&unsigned(flag))!=0;}
struct Context {
    std::shared_ptr<void> Module=Detail::GetCurrentGameplayModule();
    const void* Owner=Runtime::ApplicationEventsAccess::GetOwner();
    int Level=Runtime::ApplicationEventsAccess::GetSubsystemLevel();
    bool AssetAccess=Runtime::ApplicationEventsAccess::GetAssetAccess();
};
struct Scope {
    std::shared_ptr<void> Module;const void* Owner;int Level;bool Assets;
    explicit Scope(const Context& c):Module(Detail::SetCurrentGameplayModule(c.Module)),Owner(Runtime::ApplicationEventsAccess::SetOwner(c.Owner)),Level(Runtime::ApplicationEventsAccess::SetSubsystemLevel(c.Level)),Assets(Runtime::ApplicationEventsAccess::SetAssetAccess(c.AssetAccess)){}
    ~Scope(){Runtime::ApplicationEventsAccess::SetAssetAccess(Assets);Runtime::ApplicationEventsAccess::SetSubsystemLevel(Level);Runtime::ApplicationEventsAccess::SetOwner(Owner);Detail::SetCurrentGameplayModule(std::move(Module));}
};
struct Variable {Context Caller;CVarInfo Info;CVar::Callback Changed;};
struct Command {Context Caller;ConsoleCommand Info;DeveloperConsole::CommandCallback Callback;std::uint64_t Id=0;};
struct Listener {Context Caller;CVar::Callback Callback;};
struct State {
    std::thread::id Thread=std::this_thread::get_id();
    std::map<std::string,std::shared_ptr<Variable>> Variables;
    std::map<std::string,std::shared_ptr<Command>> Commands;
    std::map<std::uint64_t,std::shared_ptr<Listener>> Listeners;
    std::map<std::string,CVarValue> Orphaned;
    std::deque<std::string> History;
    bool Enabled=false,Visible=false,Cheats=false;
    unsigned Depth=0;std::uint64_t Next=1;
    std::size_t HistoryLimit=256,MessageLimit=2048,MessageBytes=0;
    std::mutex Mutex;std::deque<ConsoleMessage> Messages;std::uint64_t NextMessage=1;
};
State& Data(){static State state;return state;}
void Thread(){if(Data().Thread!=std::this_thread::get_id())throw std::logic_error("Developer console operation requires the engine thread");}
std::string CanonicalName(std::string name,bool prefix=false){
    if(name.size()>128||(!prefix&&name.empty()))throw std::invalid_argument("Invalid console name");
    for(char& c:name){if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))throw std::invalid_argument("Console names use ASCII letters, numbers, dot, dash and underscore");}
    if(!prefix&&name[0]>='0'&&name[0]<='9')throw std::invalid_argument("Console name cannot start with a number");return name;
}
void Valid(const CVarDefinition& def,const CVarValue& value){
    if(def.DefaultValue.index()!=value.index())throw std::invalid_argument("CVar value type mismatch");
    if(auto* text=std::get_if<std::string>(&value)){if(text->size()>4096||text->find('\0')!=std::string::npos)throw std::invalid_argument("CVar string exceeds 4096 bytes or contains NUL");}
    if(auto* f=std::get_if<double>(&value);f&&!std::isfinite(*f))throw std::invalid_argument("CVar float must be finite");
    if(value.index()==1||value.index()==2){const long double n=value.index()==1?static_cast<long double>(std::get<std::int64_t>(value)):std::get<double>(value);if((def.Minimum&&n<*def.Minimum)||(def.Maximum&&n>*def.Maximum))throw std::out_of_range("CVar value is outside its bounds");}
}
void Registration(){if(Runtime::GlobalEventsAccess::IsOwnerClosing(Runtime::ApplicationEventsAccess::GetOwner()))throw std::logic_error("Cannot register console callbacks while a script VM is closing");}
std::shared_ptr<Variable> Find(const std::string& name){auto i=Data().Variables.find(CanonicalName(name));if(i==Data().Variables.end())throw std::out_of_range("Unknown CVar: "+name);return i->second;}
CVarValue Parse(const Variable& v,const std::string& text){
    if(text.size()>4096)throw std::length_error("CVar text exceeds 4096 bytes");
    CVarValue value;switch(v.Info.Type){
    case CVarType::Boolean:if(text=="true"||text=="1"||text=="on")value=true;else if(text=="false"||text=="0"||text=="off")value=false;else throw std::invalid_argument("Expected true/false, on/off or 1/0");break;
    case CVarType::Integer:{std::int64_t number;auto result=std::from_chars(text.data(),text.data()+text.size(),number);if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw std::invalid_argument("Expected a signed 64-bit integer");value=number;break;}
    case CVarType::Float:{std::istringstream in(text);in.imbue(std::locale::classic());in>>std::noskipws;double number;if(!(in>>number)||in.peek()!=std::char_traits<char>::eof()||!std::isfinite(number))throw std::invalid_argument("Expected a finite decimal number");value=number;break;}
    case CVarType::String:value=text;break;
    }Valid(v.Info.Definition,value);return value;
}
void Writable(const Variable& v){if(Flag(v.Info.Definition.Flags,CVarFlags::ReadOnly))throw std::logic_error("CVar is read-only");if(Flag(v.Info.Definition.Flags,CVarFlags::Cheat)&&!Data().Cheats)throw std::logic_error("Cheat CVars are disabled by the host");}
void Notify(const std::shared_ptr<Variable>& v,const CVarChange& change){
    const auto call=[&](const Context& context,const CVar::Callback& callback){try{Scope scope(context);callback(change);}catch(const std::exception& e){DeveloperConsole::Log(std::string("CVar callback failed: ")+e.what(),ConsoleLevel::Error,"CVar");}catch(...){DeveloperConsole::Log("CVar callback failed",ConsoleLevel::Error,"CVar");}};
    if(v->Changed)call(v->Caller,v->Changed);
    std::vector<std::pair<std::uint64_t,std::shared_ptr<Listener>>> listeners(Data().Listeners.begin(),Data().Listeners.end());
    for(auto& [id,listener]:listeners)if(Data().Listeners.contains(id))call(listener->Caller,listener->Callback);
}
struct Depth {Depth(){if(Data().Depth>=16)throw std::logic_error("Console callback recursion limit exceeded");++Data().Depth;}~Depth(){--Data().Depth;}};
void Assign(const std::shared_ptr<Variable>& v,CVarValue value,bool pending=true){
    Depth depth;Valid(v->Info.Definition,value);bool latch=pending&&Flag(v->Info.Definition.Flags,CVarFlags::RestartRequired);auto before=latch?v->Info.PendingValue.value_or(v->Info.Value):v->Info.Value;if(value==before)return;
    if(latch){if(value==v->Info.Value)v->Info.PendingValue.reset();else v->Info.PendingValue=value;}else{v->Info.Value=value;v->Info.PendingValue.reset();}
    Notify(v,{v->Info.Definition.Name,std::move(before),std::move(value),latch});
}
std::vector<std::string> Tokens(const std::string& text){
    if(text.size()>4096||text.find('\0')!=std::string::npos||text.find_first_of("\r\n")!=std::string::npos)throw std::invalid_argument("Console command must be one line, at most 4096 bytes");
    std::vector<std::string> result;std::string token;char quote=0;bool started=false;
    for(std::size_t i=0;i<text.size();++i){char c=text[i];if(c=='\\'&&i+1<text.size()&&(text[i+1]=='\\'||text[i+1]=='"'||text[i+1]=='\'')){token+=text[++i];started=true;continue;}if(quote){if(c==quote)quote=0;else token+=c;started=true;}else if(c=='"'||c=='\''){quote=c;started=true;}else if(c==' '||c=='\t'){if(started){result.push_back(std::move(token));token.clear();started=false;}}else{token+=c;started=true;}if(result.size()>65)throw std::length_error("Too many console arguments");}
    if(quote)throw std::invalid_argument("Unterminated console quote");if(started)result.push_back(std::move(token));if(result.size()>65)throw std::length_error("Too many console arguments");return result;
}
bool Match(const ConsoleMessage& m,const ConsoleFilter& f){return (f.Levels&unsigned(m.Level))&&(f.Source.empty()||f.Source==m.Source)&&(f.Contains.empty()||m.Text.find(f.Contains)!=std::string::npos);}
std::string Describe(const CVarInfo& v){return v.Definition.Name+" = "+CVar::ToString(v.Value)+(v.PendingValue?" (pending restart: "+CVar::ToString(*v.PendingValue)+")":"");}
const std::vector<std::string> Builtins={"help","commands","cvars","get","set","reset","toggle","echo","clear","history"};
std::string Join(const std::vector<std::string>& args,std::size_t start=0){std::string s;for(auto i=start;i<args.size();++i){if(i>start)s+=' ';s+=args[i];}return s;}
void Arity(const std::vector<std::string>& a,std::size_t low,std::size_t high){if(a.size()<low||a.size()>high)throw std::invalid_argument("Wrong number of arguments; use help");}
}
std::string CVar::ToString(const CVarValue& value){return std::visit([](const auto& v)->std::string{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::string>)return v;else if constexpr(std::is_same_v<T,bool>)return v?"true":"false";else{std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<v;return s.str();}},value);}
void CVar::Register(CVarDefinition definition,Callback changed){
    Thread();Registration();definition.Name=CanonicalName(definition.Name);if(definition.Description.size()>4096||unsigned(definition.Flags)>15||(definition.Minimum&&!std::isfinite(*definition.Minimum))||(definition.Maximum&&!std::isfinite(*definition.Maximum))||(definition.Minimum&&definition.Maximum&&*definition.Minimum>*definition.Maximum))throw std::invalid_argument("Invalid CVar definition");
    if((definition.Minimum||definition.Maximum)&&definition.DefaultValue.index()!=1&&definition.DefaultValue.index()!=2)throw std::invalid_argument("Bounds require a numeric CVar");
    auto& data=Data();if(data.Variables.contains(definition.Name)||data.Commands.contains(definition.Name)||std::find(Builtins.begin(),Builtins.end(),definition.Name)!=Builtins.end())throw std::logic_error("Console name already registered: "+definition.Name);if(data.Variables.size()>=4096)throw std::length_error("CVar registration limit exceeded");Valid(definition,definition.DefaultValue);
    auto variable=std::make_shared<Variable>();variable->Info.Definition=definition;variable->Info.Type=CVarType(definition.DefaultValue.index());variable->Info.Value=definition.DefaultValue;variable->Changed=std::move(changed);
    if(auto old=data.Orphaned.find(definition.Name);old!=data.Orphaned.end()){if(!Flag(definition.Flags,CVarFlags::ReadOnly))try{Valid(definition,old->second);variable->Info.Value=old->second;}catch(const std::exception&){}data.Orphaned.erase(old);}data.Variables.emplace(definition.Name,std::move(variable));
}
bool CVar::Unregister(const std::string& name){Thread();auto i=Data().Variables.find(CanonicalName(name));if(i==Data().Variables.end())return false;auto entry=i->second;if(Runtime::ApplicationEventsAccess::GetOwner()&&entry->Caller.Owner!=Runtime::ApplicationEventsAccess::GetOwner())throw std::logic_error("CVar belongs to a different owner");Data().Variables.erase(i);return true;}
bool CVar::Exists(const std::string& name){Thread();return Data().Variables.contains(CanonicalName(name));}
CVarInfo CVar::GetInfo(const std::string& name){Thread();return Find(name)->Info;}
CVarValue CVar::GetValue(const std::string& name){return GetInfo(name).Value;}
std::vector<CVarInfo> CVar::GetAll(const std::string& prefix){Thread();auto p=CanonicalName(prefix,true);std::vector<CVarInfo> result;for(auto& [name,v]:Data().Variables)if(name.starts_with(p))result.push_back(v->Info);return result;}
void CVar::SetValue(const std::string& name,CVarValue value){Thread();auto v=Find(name);Writable(*v);Assign(v,std::move(value));}
void CVar::SetFromString(const std::string& name,const std::string& value){Thread();auto v=Find(name);Writable(*v);Assign(v,Parse(*v,value));}
void CVar::Reset(const std::string& name){Thread();auto v=Find(name);Writable(*v);Assign(v,v->Info.Definition.DefaultValue);}
void CVar::Toggle(const std::string& name){Thread();auto v=Find(name);if(v->Info.Type!=CVarType::Boolean)throw std::logic_error("Toggle requires a Boolean CVar");Writable(*v);Assign(v,!std::get<bool>(v->Info.PendingValue.value_or(v->Info.Value)));}
CVar::Subscription CVar::Subscribe(Callback changed){Thread();Registration();if(!changed)throw std::invalid_argument("Empty CVar callback");if(Data().Listeners.size()>=4096)throw std::length_error("CVar listener limit exceeded");auto id=Data().Next++;Data().Listeners.emplace(id,std::make_shared<Listener>(Listener{Context{},std::move(changed)}));return id;}
bool CVar::Unsubscribe(Subscription id){Thread();auto i=Data().Listeners.find(id);if(i==Data().Listeners.end())return false;auto retained=i->second;Data().Listeners.erase(i);return true;}
void CVar::ApplyPendingChanges(){Thread();std::vector<std::shared_ptr<Variable>> values;for(auto& [_,v]:Data().Variables)if(v->Info.PendingValue)values.push_back(v);for(auto& v:values)if(v->Info.PendingValue)Assign(v,*v->Info.PendingValue,false);}
std::map<std::string,std::string> CVar::ExportArchive(){Thread();std::map<std::string,std::string> result;for(auto& [name,v]:Data().Variables)if(Flag(v->Info.Definition.Flags,CVarFlags::Archive))result.emplace(name,ToString(v->Info.PendingValue.value_or(v->Info.Value)));return result;}
void CVar::ImportArchive(const std::map<std::string,std::string>& values){
    Thread();Depth depth;if(values.size()>4096)throw std::length_error("CVar archive exceeds registration limit");
    struct Update {std::shared_ptr<Variable> Target;CVarValue Value;CVarChange Change;};std::vector<Update> updates;std::set<std::string> unique;
    for(auto& [name,text]:values){auto v=Find(name);if(!unique.insert(v->Info.Definition.Name).second)throw std::invalid_argument("Duplicate case-insensitive CVar archive name");if(!Flag(v->Info.Definition.Flags,CVarFlags::Archive))throw std::logic_error("CVar is not archived: "+name);Writable(*v);auto value=Parse(*v,text);bool latch=Flag(v->Info.Definition.Flags,CVarFlags::RestartRequired);auto before=latch?v->Info.PendingValue.value_or(v->Info.Value):v->Info.Value;if(value!=before)updates.push_back({v,value,{v->Info.Definition.Name,before,value,latch}});}
    // Allocate all value copies before committing. Notify only after every value is published.
    for(auto& u:updates){if(u.Change.Pending){if(u.Value==u.Target->Info.Value)u.Target->Info.PendingValue.reset();else u.Target->Info.PendingValue=std::move(u.Value);}else{u.Target->Info.Value=std::move(u.Value);u.Target->Info.PendingValue.reset();}}
    for(auto& u:updates)Notify(u.Target,u.Change);
}
void DeveloperConsole::SetEnabled(bool enabled){Thread();Data().Enabled=enabled;if(!enabled)Data().Visible=false;}
bool DeveloperConsole::IsEnabled(){Thread();return Data().Enabled;}
void DeveloperConsole::SetVisible(bool visible){Thread();Data().Visible=visible&&Data().Enabled;}
bool DeveloperConsole::IsVisible(){Thread();return Data().Enabled&&Data().Visible;}
void DeveloperConsole::SetCheatsAllowed(bool allowed){
    Thread();Data().Cheats=allowed;if(allowed)return;
    std::vector<std::shared_ptr<Variable>> variables;for(auto& [_,v]:Data().Variables)if(Flag(v->Info.Definition.Flags,CVarFlags::Cheat))variables.push_back(v);
    for(auto& v:variables){v->Info.PendingValue.reset();Assign(v,v->Info.Definition.DefaultValue,false);}
}
bool DeveloperConsole::AreCheatsAllowed(){Thread();return Data().Cheats;}
DeveloperConsole::CommandHandle DeveloperConsole::RegisterCommand(ConsoleCommand command,CommandCallback callback){Thread();Registration();command.Name=CanonicalName(command.Name);if(!callback||command.Description.size()>4096||command.Usage.size()>4096)throw std::invalid_argument("Invalid console command");if(Data().Commands.contains(command.Name)||Data().Variables.contains(command.Name)||std::find(Builtins.begin(),Builtins.end(),command.Name)!=Builtins.end())throw std::logic_error("Console name already registered: "+command.Name);if(Data().Commands.size()>=4096)throw std::length_error("Console command limit exceeded");auto id=Data().Next++;auto key=command.Name;Data().Commands.emplace(key,std::make_shared<Command>(Command{Context{},std::move(command),std::move(callback),id}));return id;}
bool DeveloperConsole::UnregisterCommand(CommandHandle id){Thread();for(auto i=Data().Commands.begin();i!=Data().Commands.end();++i)if(i->second->Id==id){auto retained=i->second;if(Runtime::ApplicationEventsAccess::GetOwner()&&retained->Caller.Owner!=Runtime::ApplicationEventsAccess::GetOwner())throw std::logic_error("Command belongs to a different owner");Data().Commands.erase(i);return true;}return false;}
std::vector<ConsoleCommand> DeveloperConsole::GetCommands(const std::string& prefix){Thread();auto p=CanonicalName(prefix,true);std::vector<ConsoleCommand> result;for(auto& name:Builtins)if(name.starts_with(p))result.push_back({name,"Built-in console command",name,false});for(auto& [name,cmd]:Data().Commands)if(name.starts_with(p))result.push_back(cmd->Info);std::sort(result.begin(),result.end(),[](auto& a,auto& b){return a.Name<b.Name;});return result;}
std::vector<std::string> DeveloperConsole::Complete(const std::string& prefix){
    Thread();if(prefix.size()>4096)return {};auto split=prefix.find_first_of(" \t");std::string lead,p;bool commands=true,variables=true;
    if(split==std::string::npos)p=CanonicalName(prefix,true);
    else{auto verb=CanonicalName(prefix.substr(0,split));auto start=prefix.find_first_not_of(" \t",split);if(start==std::string::npos)start=prefix.size();auto tail=prefix.substr(start);if(tail.find_first_of(" \t\"'")!=std::string::npos)return {};if(verb=="get"||verb=="set"||verb=="reset"||verb=="toggle"||verb=="cvars")commands=false;else if(verb=="commands")variables=false;else if(verb!="help")return {};lead=prefix.substr(0,start);p=CanonicalName(tail,true);}
    std::vector<std::string> result;if(commands)for(auto& cmd:GetCommands(p))result.push_back(lead+cmd.Name);if(variables)for(auto& v:CVar::GetAll(p))result.push_back(lead+v.Definition.Name);std::sort(result.begin(),result.end());return result;
}
ConsoleResult DeveloperConsole::Execute(const std::string& line){
    try{Thread();if(!Data().Enabled)return {false,"Developer console is disabled"};Depth depth;auto tokens=Tokens(line);if(tokens.empty())return {true,{}};auto name=CanonicalName(tokens.front());tokens.erase(tokens.begin());if(Data().HistoryLimit&&(Data().History.empty()||Data().History.back()!=line)){Data().History.push_back(line);while(Data().History.size()>Data().HistoryLimit)Data().History.pop_front();}Log("> "+line,ConsoleLevel::Info,"Command");std::string output;
        if(name=="help"){Arity(tokens,0,1);if(tokens.empty())output="help [name], commands [prefix], cvars [prefix], get name, set name value, reset name, toggle name, echo text, clear, history. A CVar name reads it; name value writes it. Quotes preserve spaces. No shell execution.";else if(CVar::Exists(tokens[0])){auto info=CVar::GetInfo(tokens[0]);output=Describe(info)+"\n"+info.Definition.Description;}else{auto commands=GetCommands(tokens[0]);for(auto& command:commands)output+=command.Usage+" - "+command.Description+"\n";if(output.empty())throw std::out_of_range("Unknown command");}}
        else if(name=="commands"){Arity(tokens,0,1);for(auto& c:GetCommands(tokens.empty()?"":tokens[0]))output+=c.Name+" - "+c.Description+"\n";}
        else if(name=="cvars"){Arity(tokens,0,1);for(auto& v:CVar::GetAll(tokens.empty()?"":tokens[0]))output+=Describe(v)+"\n";}
        else if(name=="get"){Arity(tokens,1,1);output=Describe(CVar::GetInfo(tokens[0]));}
        else if(name=="set"){Arity(tokens,2,2);CVar::SetFromString(tokens[0],tokens[1]);output=Describe(CVar::GetInfo(tokens[0]));}
        else if(name=="reset"||name=="toggle"){Arity(tokens,1,1);if(name=="reset")CVar::Reset(tokens[0]);else CVar::Toggle(tokens[0]);output=Describe(CVar::GetInfo(tokens[0]));}
        else if(name=="echo")output=Join(tokens);
        else if(name=="clear"){Arity(tokens,0,0);Clear();}
        else if(name=="history"){Arity(tokens,0,0);output=Join(GetHistory());}
        else if(auto cmd=Data().Commands.find(name);cmd!=Data().Commands.end()){auto retained=cmd->second;if(retained->Info.Cheat&&!Data().Cheats)throw std::logic_error("Cheat commands are disabled by the host");Scope scope(retained->Caller);output=retained->Callback(tokens);}
        else if(CVar::Exists(name)){Arity(tokens,0,1);if(!tokens.empty())CVar::SetFromString(name,tokens[0]);output=Describe(CVar::GetInfo(name));}
        else throw std::out_of_range("Unknown command or CVar: "+name);
        if(output.size()>65536)output.resize(65536);if(!output.empty())Log(output,ConsoleLevel::Info,"Console");return {true,std::move(output)};
    }catch(const std::exception& error){Log(error.what(),ConsoleLevel::Error,"Console");return {false,error.what()};}catch(...){Log("Console command failed",ConsoleLevel::Error,"Console");return {false,"Console command failed"};}
}
std::vector<std::string> DeveloperConsole::GetHistory(){Thread();return {Data().History.begin(),Data().History.end()};}
void DeveloperConsole::ClearHistory(){Thread();Data().History.clear();}
void DeveloperConsole::SetHistoryLimit(std::size_t limit){Thread();if(limit>4096)throw std::out_of_range("History limit is 0..4096");Data().HistoryLimit=limit;while(Data().History.size()>limit)Data().History.pop_front();}
void DeveloperConsole::SetMessageLimit(std::size_t limit){Thread();if(limit<1||limit>65536)throw std::out_of_range("Message limit is 1..65536");std::lock_guard lock(Data().Mutex);Data().MessageLimit=limit;while(Data().Messages.size()>limit){Data().MessageBytes-=Data().Messages.front().Text.size()+Data().Messages.front().Source.size();Data().Messages.pop_front();}}
void DeveloperConsole::Log(const std::string& text,ConsoleLevel level,const std::string& source){if(level!=ConsoleLevel::Info&&level!=ConsoleLevel::Warning&&level!=ConsoleLevel::Error)throw std::invalid_argument("Invalid console level");auto& data=Data();{std::lock_guard lock(data.Mutex);data.Messages.push_back({data.NextMessage++,text.substr(0,65536),level,source.substr(0,128),true,std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()});data.MessageBytes+=data.Messages.back().Text.size()+data.Messages.back().Source.size();while(data.Messages.size()>data.MessageLimit||data.MessageBytes>8u*1024u*1024u){data.MessageBytes-=data.Messages.front().Text.size()+data.Messages.front().Source.size();data.Messages.pop_front();}}Console::Log(text.substr(0,65536),level,source.substr(0,128));}
std::vector<ConsoleMessage> DeveloperConsole::GetMessages(const ConsoleFilter& filter){std::lock_guard lock(Data().Mutex);std::vector<ConsoleMessage> result;for(auto& m:Data().Messages)if(Match(m,filter))result.push_back(m);return result;}
void DeveloperConsole::Clear(const ConsoleFilter& filter){{std::lock_guard lock(Data().Mutex);std::erase_if(Data().Messages,[&](auto& m){if(!Match(m,filter))return false;Data().MessageBytes-=m.Text.size()+m.Source.size();return true;});}Console::Clear(filter);}
void Runtime::DeveloperConsoleAccess::DisconnectOwner(const void* owner) noexcept{
    if(!owner)return;try{
        Thread();std::vector<std::string> variables,commands;std::vector<std::uint64_t> listeners;
        for(auto& [name,v]:Data().Variables)if(v->Caller.Owner==owner)variables.push_back(name);
        for(auto& [name,c]:Data().Commands)if(c->Caller.Owner==owner)commands.push_back(name);
        for(auto& [id,l]:Data().Listeners)if(l->Caller.Owner==owner)listeners.push_back(id);
        for(auto& name:variables){auto it=Data().Variables.find(name);if(it==Data().Variables.end()||it->second->Caller.Owner!=owner)continue;auto retained=it->second;if(Data().Orphaned.size()<4096)Data().Orphaned.insert_or_assign(name,retained->Info.PendingValue.value_or(retained->Info.Value));Data().Variables.erase(it);}
        for(auto& name:commands){auto it=Data().Commands.find(name);if(it==Data().Commands.end()||it->second->Caller.Owner!=owner)continue;auto retained=it->second;Data().Commands.erase(it);}
        for(auto id:listeners){auto it=Data().Listeners.find(id);if(it==Data().Listeners.end()||it->second->Caller.Owner!=owner)continue;auto retained=it->second;Data().Listeners.erase(it);}
    }catch(...){}
}
void Runtime::DeveloperConsoleAccess::BindThread(){Data().Thread=std::this_thread::get_id();}
void Runtime::DeveloperConsoleAccess::Reset(){Thread();auto variables=std::move(Data().Variables);auto commands=std::move(Data().Commands);auto listeners=std::move(Data().Listeners);Data().Orphaned.clear();Data().History.clear();Data().Enabled=false;Data().Visible=false;Data().Cheats=false;{std::lock_guard lock(Data().Mutex);Data().Messages.clear();Data().MessageBytes=0;}}
void Runtime::DeveloperConsoleAccess::Initialize(bool editor){
    Reset();DeveloperConsole::SetEnabled(editor);
    CVar::Register({"app.target_fps","Target FPS; zero is uncapped",std::int64_t(Application::GetTargetFrameRate()),CVarFlags::Archive,0,1000},[](auto& e){if(!e.Pending)Application::SetTargetFrameRate(int(std::get<std::int64_t>(e.Value)));});
    CVar::Register({"app.background_fps","Unfocused target FPS",std::int64_t(Application::GetBackgroundFrameRate()),CVarFlags::Archive,0,1000},[](auto& e){if(!e.Pending)Application::SetBackgroundFrameRate(int(std::get<std::int64_t>(e.Value)));});
    CVar::Register({"app.run_in_background","Continue updating when unfocused",Application::GetRunInBackground(),CVarFlags::Archive},[](auto& e){if(!e.Pending)Application::SetRunInBackground(std::get<bool>(e.Value));});
    CVar::Register({"time.scale","Gameplay time multiplier",1.0,CVarFlags::Cheat,0,100},[](auto& e){if(!e.Pending)Time::SetTimeScale(float(std::get<double>(e.Value)));});
    CVar::Register({"debug.stats","Show runtime FPS and frame counter in developer console",false,CVarFlags::None});
    DeveloperConsole::RegisterCommand({"app.quit","Request a normal application shutdown","app.quit [exitCode]",false},[](auto& args){Arity(args,0,1);int code=0;if(!args.empty()){auto result=std::from_chars(args[0].data(),args[0].data()+args[0].size(),code);if(result.ec!=std::errc{}||result.ptr!=args[0].data()+args[0].size())throw std::invalid_argument("Invalid exit code");}Application::Quit(code);return "Shutdown requested";});
}
}
