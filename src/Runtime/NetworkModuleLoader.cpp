#include "Network/NetworkModuleApi.h"
#include <filesystem>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
namespace {
const NetworkModuleApi* Load(){
    // Resolve relative to the core library, never the project or working directory.
    // Keep the module loaded for process lifetime: endpoint handles can outlive individual scenes.
#if defined(_WIN32)
    HMODULE core=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&NetworkModule),&core))throw std::runtime_error("Cannot locate Bazzalt core for network loading");
    std::wstring filename(32768,L'\0');const auto length=GetModuleFileNameW(core,filename.data(),static_cast<DWORD>(filename.size()));
    if(!length||length>=filename.size())throw std::runtime_error("Cannot resolve Bazzalt core path");
    filename.resize(length);const auto path=std::filesystem::path(filename).parent_path()/L"bazzalt_network.dll";
    auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)throw std::runtime_error("Could not load sibling bazzalt_network.dll (Windows error "+std::to_string(GetLastError())+"). Reinstall the matching runtime modules.");
    auto get=reinterpret_cast<GetNetworkModuleApi>(GetProcAddress(module,"BazzaltGetNetworkModuleApi"));
#else
    Dl_info info{};if(!dladdr(reinterpret_cast<void*>(&NetworkModule),&info)||!info.dli_fname)throw std::runtime_error("Cannot resolve Bazzalt core path");
#if defined(__APPLE__)
    const auto path=std::filesystem::path(info.dli_fname).parent_path()/"libbazzalt_network.dylib";
#else
    const auto path=std::filesystem::path(info.dli_fname).parent_path()/"libbazzalt_network.so";
#endif
    auto module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!module){const auto* error=dlerror();throw std::runtime_error(std::string("Could not load sibling network runtime: ")+(error?error:"unknown error"));}
    auto get=reinterpret_cast<GetNetworkModuleApi>(dlsym(module,"BazzaltGetNetworkModuleApi"));
#endif
    const auto* api=get?get(NetworkModuleVersion,sizeof(NetworkModuleApi)):nullptr;
    if(!api||api->Version!=NetworkModuleVersion||api->Size!=sizeof(NetworkModuleApi)||!api->Create||!api->Destroy||!api->Update||!api->State||!api->Send||!api->Poll||!api->Token||!api->Address||!api->Error||!api->Peers||!api->Statistics||!api->Connect||!api->Disconnect)throw std::runtime_error("Incompatible networking module");
    return api;
}
}
const NetworkModuleApi& NetworkModule(){static const auto* api=Load();return *api;}
}
