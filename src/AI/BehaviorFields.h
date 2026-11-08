#pragma once
#include "Bazzalt/Components/BehaviorTreeAgent.h"
namespace Bazzalt::Runtime {
template<class T,class F>void VisitBehaviorTreeAgent(T& value,F field){field("Tree",value.Tree);field("Loop",value.Loop);}
}
