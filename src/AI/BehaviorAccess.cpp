#include "AI/BehaviorModuleApi.h"
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
namespace {
const BehaviorModuleApi* LoadModule(){
    using Entry=const BehaviorModuleApi*(*)(unsigned,unsigned);
#ifdef _WIN32
    HMODULE core=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&BehaviorModule),&core))throw std::runtime_error("Cannot locate core for AI behaviors");
    std::wstring name(32768,L'\0');auto size=GetModuleFileNameW(core,name.data(),DWORD(name.size()));
    if(!size||size>=name.size())throw std::runtime_error("Cannot resolve core path");name.resize(size);
    auto path=std::filesystem::path(name).parent_path()/L"bazzalt_ai.dll";
    auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto get=module?reinterpret_cast<Entry>(GetProcAddress(module,"BazzaltGetBehaviorModuleApi")):nullptr;
#else
    Dl_info info{};if(!dladdr(reinterpret_cast<void*>(&BehaviorModule),&info)||!info.dli_fname)throw std::runtime_error("Cannot resolve core path");
#ifdef __APPLE__
    auto path=std::filesystem::path(info.dli_fname).parent_path()/"libbazzalt_ai.dylib";
#else
    auto path=std::filesystem::path(info.dli_fname).parent_path()/"libbazzalt_ai.so";
#endif
    auto module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    auto get=module?reinterpret_cast<Entry>(dlsym(module,"BazzaltGetBehaviorModuleApi")):nullptr;
#endif
    auto api=get?get(1,sizeof(BehaviorModuleApi)):nullptr;
    if(!api||api->Version!=1||api->Size!=sizeof(BehaviorModuleApi)||!api->CreateTree||!api->LoadTree||!api->DestroyTree||!api->TickTree||!api->ResetTree||!api->InitializeTree||!api->CreateAgents||!api->DestroyAgents||!api->GetBlackboard||!api->SetHandlers||!api->ClearHandlers||!api->ResetAgent||!api->StepAgents||!api->ReleaseAgents||!api->References||!api->ResetDefaults)throw std::runtime_error("Missing or incompatible sibling bazzalt_ai behavior runtime");
    // Match navigation: retain the sibling module for all outstanding handles.
    return api;
}
}
const BehaviorModuleApi& BehaviorModule(){static const auto* api=LoadModule();return *api;}
}
