#pragma once
#include "Bazzalt/Entity.h"
#include "Bazzalt/Export.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace Bazzalt::AI {
enum class Status { Success, Failure, Running };
using Value = std::variant<bool,double,std::string,Vec3>;
// Each NPC owns its blackboard and execution state. Trees never share mutable state.
class Blackboard {
public:
    std::map<std::string,Value> Values;
    void Set(std::string key,Value value) { Values.insert_or_assign(std::move(key),std::move(value)); }
    bool Contains(const std::string& key) const { return Values.contains(key); }
    void Erase(const std::string& key) { Values.erase(key); }
    template<class T> const T* Get(const std::string& key) const {
        auto it=Values.find(key);return it==Values.end()?nullptr:std::get_if<T>(&it->second);
    }
    double Number(const std::string& key,double fallback=0) const { auto p=Get<double>(key);return p?*p:fallback; }
};
struct Context {
    Entity Self;
    Blackboard& Memory;
    float DeltaTime=0;
    // Custom actions receive false on tick and true on cancellation/reset.
    std::function<Status(const std::string&,Context&,bool)> Action;
    std::function<bool(const std::string&,Context&)> Condition;
};
using ActionHandler = std::function<Status(const std::string&,Context&,bool)>;
using ConditionHandler = std::function<bool(const std::string&,Context&)>;
class BAZZALT_API BehaviorTree final {
public:
    static BehaviorTree FromSource(const std::string& xml);
    static BehaviorTree Load(UUID asset);
    BehaviorTree();
    ~BehaviorTree();
    BehaviorTree(BehaviorTree&&) noexcept;
    BehaviorTree& operator=(BehaviorTree&&) noexcept;
    BehaviorTree(const BehaviorTree&)=delete;
    BehaviorTree& operator=(const BehaviorTree&)=delete;
    Status Tick(Context& context);
    void Reset(Context& context);
    void Initialize(Blackboard& memory) const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Bazzalt::AI
