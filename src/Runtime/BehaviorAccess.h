#pragma once
#include "Bazzalt/Script.h"
namespace Bazzalt::Runtime {
struct BehaviorAccess {
    static void Register(const std::string& entity,const std::string& name,Behavior* behavior);
    static void Remove(Behavior* behavior);
};
}
