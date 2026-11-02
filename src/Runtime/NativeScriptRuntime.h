#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "Bazzalt/ApplicationEvents.h"
namespace Bazzalt::Runtime {
struct ScriptBinding { std::filesystem::path Module;std::string Entity;std::string TypeName;std::unordered_map<std::string,std::string> Properties; };
class NativeScriptRuntime final {
public:
    NativeScriptRuntime();
    ~NativeScriptRuntime();
    NativeScriptRuntime(const NativeScriptRuntime&)=delete;
    NativeScriptRuntime& operator=(const NativeScriptRuntime&)=delete;
    bool Configure(std::vector<ScriptBinding> bindings,std::string& error);
    bool Start(std::string& error, bool application = false, bool reloading = false);
    bool PreserveFrom(const NativeScriptRuntime& previous,std::string& error);
    const std::vector<ScriptBinding>& GetBindings()const{return m_bindings;}
    void ActivateReload(bool notify=true);
    void NotifyReload();
    void DiscardForReload() noexcept;
    void Update(float deltaTime);
    void FixedUpdate(float deltaTime);
    void LateUpdate(float deltaTime);
    void ApplicationEvent(const Bazzalt::ApplicationEvent& event);
    void Stop() noexcept;
private:
    bool m_reloading=false;
    struct Instance;std::vector<ScriptBinding> m_bindings;std::vector<Instance> m_instances;
};
}
