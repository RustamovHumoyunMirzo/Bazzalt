#include "Runtime/ModuleLoader.h"
#include <filesystem>
#include <map>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Bazzalt::Runtime {
namespace {
void ValidateName(const std::string& name) {
    if (name.empty() || name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("Runtime module and entry names must be ASCII identifiers");
}
std::filesystem::path CoreDirectory() {
#ifdef _WIN32
    HMODULE core = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&LoadSiblingModuleSymbol), &core))
        throw std::runtime_error("Cannot locate Bazzalt core for runtime module loading");
    std::wstring name(32768, L'\0');
    const auto length = GetModuleFileNameW(core, name.data(), static_cast<DWORD>(name.size()));
    if (!length || length >= name.size())
        throw std::runtime_error("Cannot resolve Bazzalt core path");
    name.resize(length);
    return std::filesystem::path(name).parent_path();
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&LoadSiblingModuleSymbol), &info) || !info.dli_fname)
        throw std::runtime_error("Cannot resolve Bazzalt core path");
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}
}

void* LoadSiblingModuleSymbol(const std::string& library, const std::string& symbol) {
    ValidateName(library);
    ValidateName(symbol);
    // Raw OS handles intentionally stay resident through static destruction.
    struct Cache {
        std::mutex Mutex;
        std::map<std::string, void*> Modules;
    };
    static auto& cache = *new Cache;
    std::lock_guard lock(cache.Mutex);
    auto& modules = cache.Modules;
    auto found = modules.find(library);
    if (found == modules.end()) {
#ifdef _WIN32
        const auto path = CoreDirectory() / (library + ".dll");
        auto handle = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle) {
            const auto error = GetLastError();
            throw std::runtime_error("Could not load sibling " + library + ".dll (Windows error " +
                                     std::to_string(error) + ")");
        }
        auto module = reinterpret_cast<void*>(handle);
#else
#ifdef __APPLE__
        const auto path = CoreDirectory() / ("lib" + library + ".dylib");
#else
        const auto path = CoreDirectory() / ("lib" + library + ".so");
#endif
        auto module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!module) {
            const auto* error = dlerror();
            throw std::runtime_error("Could not load sibling " + library + ": " +
                                     (error ? error : "unknown loader error"));
        }
#endif
        found = modules.emplace(library, module).first;
    }
#ifdef _WIN32
    auto address = reinterpret_cast<void*>(GetProcAddress(
        reinterpret_cast<HMODULE>(found->second), symbol.c_str()));
#else
    auto address = dlsym(found->second, symbol.c_str());
#endif
    if (!address)
        throw std::runtime_error("Missing " + symbol + " in sibling " + library);
    return address;
}
}
