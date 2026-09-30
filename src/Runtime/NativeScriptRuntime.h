#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
namespace Bazzalt::Runtime {
struct ScriptBinding { std::filesystem::path Module;std::string Entity;std::string TypeName;std::unordered_map<std::string,std::string> Properties; };
class NativeScriptRuntime final {
public:
    NativeScriptRuntime();
    ~NativeScriptRuntime();
    NativeScriptRuntime(const NativeScriptRuntime&)=delete;
    NativeScriptRuntime& operator=(const NativeScriptRuntime&)=delete;
    bool Configure(std::vector<ScriptBinding> bindings,std::string& error);
    bool Start(std::string& error);
    void Update(float deltaTime);
    void Stop() noexcept;
private:
    struct Instance;std::vector<ScriptBinding> m_bindings;std::vector<Instance> m_instances;
};
}
