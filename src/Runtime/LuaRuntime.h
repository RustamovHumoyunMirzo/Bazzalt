#pragma once
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Export.h"
#include <memory>
namespace Bazzalt::Runtime {
// Private VM/lifecycle control. Lua itself is a separately shipped shared library.
class BAZZALT_API LuaRuntime final {
public:
    LuaRuntime();
    ~LuaRuntime();
    bool Configure(std::vector<ScriptBinding> bindings,std::string& error);
    bool Start(std::string& error);
    void Update(float deltaTime);
    void FixedUpdate(float deltaTime);
    void Stop() noexcept;
    static bool Import(const std::filesystem::path& source,const std::filesystem::path& output,std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
