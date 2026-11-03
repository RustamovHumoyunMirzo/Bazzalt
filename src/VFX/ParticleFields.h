#pragma once
#include "Bazzalt/Components/ParticleEmitter.h"
namespace Bazzalt::Runtime {
template<class V,class F> void VisitParticleEmitter(V& v,F f){f("Graph",v.Graph);f("MaterialOverride",v.MaterialOverride);f("MaxParticles",v.MaxParticles);f("Seed",v.Seed);f("SimulationSpeed",v.SimulationSpeed);f("RateMultiplier",v.RateMultiplier);f("SizeMultiplier",v.SizeMultiplier);f("Tint",v.Tint);f("Space",v.Space);f("Backend",v.Backend);f("PlayOnAwake",v.PlayOnAwake);f("UseUnscaledTime",v.UseUnscaledTime);}
}
