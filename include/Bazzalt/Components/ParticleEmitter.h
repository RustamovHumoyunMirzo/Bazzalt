#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Math.h"
namespace Bazzalt {
enum class ParticleSimulationBackend { Automatic, CPU, GPUAnalytic };
enum class ParticleSimulationSpace { Local, World };
struct BAZZALT_API ParticleEmitter : Component {
    UUID Graph{},MaterialOverride{};
    unsigned MaxParticles=4096,Seed=1;
    float SimulationSpeed=1,RateMultiplier=1,SizeMultiplier=1;
    Vec4 Tint{1,1,1,1};
    ParticleSimulationBackend Backend=ParticleSimulationBackend::CPU;
    ParticleSimulationSpace Space=ParticleSimulationSpace::Local;
    bool PlayOnAwake=true,UseUnscaledTime=false;
    [[nodiscard]] bool IsValid() const;
};
}
