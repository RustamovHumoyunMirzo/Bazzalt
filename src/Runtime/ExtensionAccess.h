#pragma once
#include "Bazzalt/Extension.h"
#include <filesystem>
#include <map>
namespace Bazzalt::Runtime {
BAZZALT_API ExtensionValue InvokeLuaExtension(const std::string& module,const std::string& function,const ExtensionArguments& args);
BAZZALT_API void ClearLuaExtensions();
struct BAZZALT_API ExtensionAccess {
    static void Configure(const std::filesystem::path& projectRoot);
    static void Clear();
    static std::filesystem::path Root();
    static bool Visible(const std::string& name,const std::string& caller="");
    static std::string ComponentOwner(const std::string& type);
    static std::string Owner(const std::string& name);
    static std::string SetCaller(std::string caller);
    static std::filesystem::path LuaModule(const std::string& name);
    static std::vector<std::string> Functions(const std::string& name);
};
}
