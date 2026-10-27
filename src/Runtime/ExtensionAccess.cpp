#include "Runtime/ExtensionAccess.h"
#include <ryml.hpp>
#include "Bazzalt/Detail/ModuleLifetime.h"
#include <fstream>
#include <set>
#include <memory>
#include <regex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt {
namespace {
struct Function {std::shared_ptr<void> Library;ExtensionFunction Call;};
std::map<std::string,std::map<std::string,Function>> NativeFunctions;
std::map<std::string,std::filesystem::path> Lua;
std::map<std::string,std::pair<std::string,bool>> Metadata;
std::map<std::string,UUID> Assets;
std::map<std::string,std::string> Components;
thread_local std::string Caller;
std::shared_ptr<void> CurrentLibrary;
std::set<std::string> Allowed;
std::vector<std::shared_ptr<void>> Libraries;
std::vector<std::shared_ptr<void>> NativeDirectories;
std::filesystem::path ConfiguredRoot;
std::string Text(ryml::ConstNodeRef n){auto v=n.val();return {v.str,v.len};}
std::filesystem::path Path(const std::filesystem::path& root,const std::string& text){auto p=std::filesystem::u8path(text);if(p.empty()||p.is_absolute())throw std::runtime_error("Invalid extension runtime path");for(auto& part:p)if(part=="..")throw std::runtime_error("Extension path traversal");auto result=std::filesystem::weakly_canonical(root/p);auto relative=result.lexically_relative(std::filesystem::weakly_canonical(root));if(relative.empty()||*relative.begin()==".."||!std::filesystem::is_regular_file(result))throw std::runtime_error("Missing extension runtime file");return result;}
}
void ExtensionRegistry::Export(const std::string& module,const std::string& name,ExtensionFunction function){
    static const std::regex identifier("[A-Za-z_][A-Za-z0-9_]*");
    if(!CurrentLibrary||!Allowed.contains(module)||!std::regex_match(name,identifier)||!function)throw std::invalid_argument("Invalid extension export or undeclared module");
    if(NativeFunctions[module].contains(name))throw std::invalid_argument("Duplicate extension function: "+module+"."+name);
    NativeFunctions[module].emplace(name,Function{CurrentLibrary,std::move(function)});
}
UUID Extensions::GetAsset(const std::string& extension,const std::string& asset){auto found=Assets.find(extension+":"+asset);return found==Assets.end()?UUID{}:found->second;}
bool Extensions::HasModule(const std::string& name){return (NativeFunctions.contains(name)||Lua.contains(name))&&Runtime::ExtensionAccess::Visible(name,Caller);}
std::vector<std::string> Extensions::GetModules(){std::set<std::string> names;for(const auto& [name,f]:NativeFunctions)names.insert(name);for(const auto& [name,path]:Lua)names.insert(name);std::vector<std::string> result;for(const auto& name:names)if(Runtime::ExtensionAccess::Visible(name,Caller))result.push_back(name);return result;}
ExtensionValue Extensions::Call(const std::string& module,const std::string& name,const ExtensionArguments& args){if(!Runtime::ExtensionAccess::Visible(module,Caller))throw std::runtime_error("Private or unavailable extension module: "+module);if(args.size()>128)throw std::length_error("Too many extension arguments");if(Lua.contains(module))return Runtime::InvokeLuaExtension(module,name,args);auto m=NativeFunctions.find(module);if(m==NativeFunctions.end()||!m->second.contains(name))throw std::out_of_range("Native extension function is unavailable: "+module+"."+name);auto function=m->second.at(name);struct Scope{std::shared_ptr<void> Previous;std::string PreviousCaller;~Scope(){Detail::SetCurrentGameplayModule(std::move(Previous));Caller=std::move(PreviousCaller);}} scope{Detail::SetCurrentGameplayModule(function.Library),Caller};Caller=Runtime::ExtensionAccess::Owner(module);return function.Call(args);}
namespace Runtime {
bool ExtensionAccess::Visible(const std::string& name,const std::string& caller){auto item=Metadata.find(name);return item!=Metadata.end()&&(item->second.second||item->second.first==caller);}
std::string ExtensionAccess::ComponentOwner(const std::string& type){auto item=Components.find(type);return item==Components.end()?"":item->second;}
std::string ExtensionAccess::Owner(const std::string& name){auto item=Metadata.find(name);return item==Metadata.end()?"":item->second.first;}
std::string ExtensionAccess::SetCaller(std::string caller){auto previous=Caller;Caller=std::move(caller);return previous;}
std::filesystem::path ExtensionAccess::Root(){return ConfiguredRoot;}
void ExtensionAccess::Clear(){ConfiguredRoot.clear();ClearLuaExtensions();Metadata.clear();Assets.clear();Components.clear();NativeFunctions.clear();Lua.clear();Allowed.clear();CurrentLibrary.reset();Libraries.clear();NativeDirectories.clear();}
std::filesystem::path ExtensionAccess::LuaModule(const std::string& name){auto item=Lua.find(name);return item==Lua.end()?std::filesystem::path{}:item->second;}
std::vector<std::string> ExtensionAccess::Functions(const std::string& name){std::vector<std::string> result;auto item=::Bazzalt::NativeFunctions.find(name);if(item!=::Bazzalt::NativeFunctions.end())for(const auto& [key,value]:item->second)result.push_back(key);return result;}
void ExtensionAccess::Configure(const std::filesystem::path& projectRoot){
    Clear();auto root=projectRoot/"ext";if(!std::filesystem::exists(root/"runtime.json"))root=projectRoot/".bazzalt"/"Extensions";
    if(!std::filesystem::exists(root/"runtime.json"))return;
    try{
        if(std::filesystem::file_size(root/"runtime.json")>1024*1024)throw std::runtime_error("Extension runtime index exceeds limit");
        std::ifstream file(root/"runtime.json",std::ios::binary);std::string data((std::istreambuf_iterator<char>(file)),{});if(data.size()>1024*1024)throw std::runtime_error("Extension runtime index exceeds limit");
        auto tree=ryml::parse_in_arena(ryml::csubstr(data.data(),data.size()));auto node=tree.rootref();
        if(Text(node["FormatVersion"])!="1")throw std::runtime_error("Unsupported extension runtime index");
#ifdef _WIN32
        if(node.has_child("NativeDirectories"))for(auto directory:node["NativeDirectories"].children()){
            auto relative=std::filesystem::u8path(Text(directory));if(relative.empty()||relative.is_absolute())throw std::runtime_error("Invalid extension native directory");for(const auto& part:relative)if(part=="..")throw std::runtime_error("Extension directory traversal");auto path=std::filesystem::weakly_canonical(root/relative);auto within=path.lexically_relative(std::filesystem::weakly_canonical(root));if(within.empty()||*within.begin()==".."||!std::filesystem::is_directory(path))throw std::runtime_error("Missing extension native directory");auto cookie=AddDllDirectory(path.c_str());if(!cookie)throw std::runtime_error("Could not register extension native directory");NativeDirectories.emplace_back(cookie,[](void* value){RemoveDllDirectory(value);});
        }
#endif
        if(node.has_child("NativeLibraries")){
            std::vector<std::filesystem::path> pending;for(auto library:node["NativeLibraries"].children())pending.push_back(Path(root,Text(library)));
            while(!pending.empty()){
                bool progress=false;
                for(auto it=pending.begin();it!=pending.end();){
#ifdef _WIN32
                    auto library=LoadLibraryExW(it->c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                    if(library){Libraries.emplace_back(library,[directories=NativeDirectories](void* value){FreeLibrary(static_cast<HMODULE>(value));});}
#else
                    auto library=dlopen(it->c_str(),RTLD_NOW|RTLD_GLOBAL);if(library)Libraries.emplace_back(library,[](void* value){dlclose(value);});
#endif
                    if(library){it=pending.erase(it);progress=true;}else ++it;
                }
                if(!progress)throw std::runtime_error("Could not load extension native dependencies; check architecture, companion libraries and Linux SONAME/RPATH");
            }
        }
        if(node.has_child("Assets"))for(auto asset:node["Assets"].children()){auto key=asset.key();UUID id;if(!UUID::TryParse(Text(asset),id)||!id)throw std::runtime_error("Invalid extension asset UUID");Assets[{key.str,key.len}]=id;}
        if(node.has_child("Components"))for(auto component:node["Components"].children()){auto key=component.key();Components[{key.str,key.len}]=Text(component);}
        for(auto module:node["Modules"].children()){
            auto name=Text(module["Name"]);if(Metadata.contains(name)||name.empty())throw std::runtime_error("Extension module collision: "+name);
            Metadata[name]={module.has_child("Owner")?Text(module["Owner"]):name,!module.has_child("Public")||Text(module["Public"])=="true"};auto path=Path(root,Text(module["Path"]));auto language=Text(module["Language"]);
            if(language=="lua"){Lua.emplace(name,path);continue;}
            if(language!="cpp")throw std::runtime_error("Unknown extension module language");
            std::shared_ptr<void> lease;
#ifdef _WIN32
            auto library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!library)throw std::runtime_error("Could not load native extension library");
            lease={library,[directories=NativeDirectories](void* p){FreeLibrary(static_cast<HMODULE>(p));}};auto get=reinterpret_cast<const ExtensionModuleApi*(*)()>(GetProcAddress(library,"BazzaltGetExtensionModuleV1"));
#else
            auto library=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(dlerror());lease={library,[](void* p){dlclose(p);}};auto get=reinterpret_cast<const ExtensionModuleApi*(*)()>(dlsym(library,"BazzaltGetExtensionModuleV1"));
#endif
            if(!get)throw std::runtime_error("Native extension entry point missing");const auto* api=get();if(!api||api->AbiVersion!=ExtensionAbiVersion||!api->Register)throw std::runtime_error("Native extension ABI mismatch");
            Allowed={name};CurrentLibrary=lease;ExtensionRegistry registry;api->Register(registry);CurrentLibrary.reset();Allowed.clear();Libraries.push_back(std::move(lease));
        }
        ConfiguredRoot=std::filesystem::weakly_canonical(projectRoot);
    }catch(...){Clear();throw;}
}
}
}
