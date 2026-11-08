#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
struct BehaviorTreeAgent : Component {
    UUID Tree{};
    bool Loop=true;
    bool IsValid() const {return true;}
};
}
