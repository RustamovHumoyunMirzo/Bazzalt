#include "Script/LuaBindings.h"
#include "Bazzalt/Console.h"
namespace Bazzalt::Runtime {
void BindLuaConsole(sol::table api){
    api.new_enum("ConsoleLevel","Info",ConsoleLevel::Info,"Warning",ConsoleLevel::Warning,"Error",ConsoleLevel::Error);
    auto filter=api.new_usertype<ConsoleFilter>("ConsoleFilter",sol::constructors<ConsoleFilter()>());
    filter["Levels"]=&ConsoleFilter::Levels;filter["Source"]=&ConsoleFilter::Source;filter["Contains"]=&ConsoleFilter::Contains;
    auto message=api.new_usertype<ConsoleMessage>("ConsoleMessage",sol::no_constructor);
    message["Id"]=&ConsoleMessage::Id;message["Text"]=&ConsoleMessage::Text;message["Level"]=&ConsoleMessage::Level;message["Source"]=&ConsoleMessage::Source;message["ShowIcon"]=&ConsoleMessage::ShowIcon;message["Timestamp"]=&ConsoleMessage::Timestamp;
    auto console=api.create_named("Console");console["IsAvailable"]=&Console::IsAvailable;
    console["Log"]=[](const std::string& text,sol::optional<ConsoleLevel> level,sol::optional<std::string> source,sol::optional<bool> icon){Console::Log(text,level.value_or(ConsoleLevel::Info),source.value_or("Script"),icon.value_or(true));};
    console["Info"]=[](const std::string& text,sol::optional<std::string> source,sol::optional<bool> icon){Console::Info(text,source.value_or("Script"),icon.value_or(true));};
    console["Warning"]=[](const std::string& text,sol::optional<std::string> source,sol::optional<bool> icon){Console::Warning(text,source.value_or("Script"),icon.value_or(true));};
    console["Error"]=[](const std::string& text,sol::optional<std::string> source,sol::optional<bool> icon){Console::Error(text,source.value_or("Script"),icon.value_or(true));};
    console["Clear"]=[](sol::optional<ConsoleFilter> f){Console::Clear(f.value_or(ConsoleFilter{}));};
    console["ClearAt"]=[](std::int64_t index){if(index>0)Console::ClearAt(static_cast<std::size_t>(index-1));};
    console["ClearMessage"]=&Console::ClearMessage;console["ClearMessages"]=&Console::ClearMessages;
    console["GetCount"]=[](sol::optional<ConsoleFilter> f){return Console::GetCount(f.value_or(ConsoleFilter{}));};
    console["GetMessageAt"]=[](std::int64_t index){return index>0?Console::GetMessageAt(static_cast<std::size_t>(index-1)):std::optional<ConsoleMessage>{};};
    console["FindMessage"]=&Console::FindMessage;
    console["GetMessages"]=[](sol::this_state state,sol::optional<ConsoleFilter> f)->sol::object{
        auto result=Console::GetMessages(f.value_or(ConsoleFilter{}));if(!result)return sol::make_object(state,sol::nil);
        sol::state_view lua(state);auto list=lua.create_table();int index=1;for(const auto& message:*result)list[index++]=message;return sol::make_object(state,list);
    };
}
}
