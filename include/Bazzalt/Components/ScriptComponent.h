#pragma once
#include <string>
#include <vector>
#include "Bazzalt/Component.h"

namespace Bazzalt {
struct ScriptPropertyValue { std::string Name, Type, Value; };
struct ScriptAttachment {
    std::string Source;
    std::string TypeName;
    bool Enabled = true;
    std::vector<ScriptPropertyValue> Properties;
};
// Multiple user behaviors may be attached to one entity. This data is stable
// scene state; executable module handles remain private runtime state.
struct ScriptComponents final : Component { std::vector<ScriptAttachment> Values; };
}
