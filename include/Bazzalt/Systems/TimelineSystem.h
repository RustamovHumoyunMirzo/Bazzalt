#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/System.h"
#include <memory>
namespace Bazzalt {
namespace Runtime {
class Engine;
}
class Cinematics;
class BAZZALT_API TimelineSystem final : public System {
  public:
    TimelineSystem();
    ~TimelineSystem() override;

  private:
    void OnUpdate(Scene &, float) override;
    void OnLateUpdate(Scene &, float) override;
    void Evaluate(Scene &, float);
    void OnDestroy(Scene &) override;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    friend class Cinematics;
    friend class Runtime::Engine;
};
} // namespace Bazzalt
