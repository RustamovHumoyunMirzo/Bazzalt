#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/System.h"
#include <memory>
namespace Bazzalt {
class Animation;
class BAZZALT_API AnimationSystem final:public System {
public:AnimationSystem();~AnimationSystem()override;
private:
 void OnUpdate(Scene&,float)override;void OnFixedUpdate(Scene&,float)override;void OnLateUpdate(Scene&,float)override;void OnDestroy(Scene&)override;
 struct Impl;std::unique_ptr<Impl> m_impl;friend class Animation;
};
}
