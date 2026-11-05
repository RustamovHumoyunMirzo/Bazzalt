#pragma once
#include "Bazzalt/Export.h"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace Bazzalt {
using CVarValue=std::variant<bool,std::int64_t,double,std::string>;
enum class CVarType { Boolean,Integer,Float,String };
enum class CVarFlags : unsigned { None=0,ReadOnly=1,Archive=2,Cheat=4,RestartRequired=8 };
constexpr CVarFlags operator|(CVarFlags a,CVarFlags b){return CVarFlags(unsigned(a)|unsigned(b));}
struct CVarDefinition {
    std::string Name,Description;
    CVarValue DefaultValue=false;
    CVarFlags Flags=CVarFlags::None;
    std::optional<double> Minimum,Maximum;
};
struct CVarInfo {
    CVarDefinition Definition;
    CVarType Type=CVarType::Boolean;
    CVarValue Value=false;
    std::optional<CVarValue> PendingValue;
};
struct CVarChange {std::string Name;CVarValue Previous=false,Value=false;bool Pending=false;};
/// Process-wide tunables. Registration, reads and mutation belong on the engine thread.
class BAZZALT_API CVar final {
public:
    using Callback=std::function<void(const CVarChange&)>;
    using Subscription=std::uint64_t;
    static void Register(CVarDefinition definition,Callback changed={});
    static bool Unregister(const std::string& name);
    static bool Exists(const std::string& name);
    static CVarValue GetValue(const std::string& name);
    static CVarInfo GetInfo(const std::string& name);
    static std::vector<CVarInfo> GetAll(const std::string& prefix="");
    static void SetValue(const std::string& name,CVarValue value);
    static void SetFromString(const std::string& name,const std::string& value);
    static void Reset(const std::string& name);
    static void Toggle(const std::string& name);
    static Subscription Subscribe(Callback changed);
    static bool Unsubscribe(Subscription subscription);
    static void ApplyPendingChanges();
    // Data only: callers choose a platform-appropriate file/save slot. Import is validated atomically.
    static std::map<std::string,std::string> ExportArchive();
    static void ImportArchive(const std::map<std::string,std::string>& values);
    static std::string ToString(const CVarValue& value);
};
}
