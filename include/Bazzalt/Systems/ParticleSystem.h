#pragma once
#include "Bazzalt/System.h"
#include "Bazzalt/Export.h"
#include <memory>
namespace Bazzalt {
namespace Runtime {struct VFXAccess;}
// Added automatically by Engine. Custom standalone scenes may add this system.
class BAZZALT_API ParticleSystem final : public System {
public:
    ParticleSystem();~ParticleSystem() override;
private:
    void OnUpdate(Scene&,float) override;
    void OnDestroy(Scene&) override;
    struct Impl;std::unique_ptr<Impl> m_impl;
    friend struct Runtime::VFXAccess;
    friend class VFX;
};
}
