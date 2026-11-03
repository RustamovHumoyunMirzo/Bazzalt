#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "Bazzalt/VFX.h"
#include "Bazzalt/Systems/ParticleSystem.h"
#include "Bazzalt/Scene.h"
namespace py=pybind11;
namespace Bazzalt::EditorBridge {
void BindVFXPreview(py::module_& module){module.def("preview_vfx",[](const std::string& source,float seconds){auto graph=VFXGraph::FromSource(source);if(!graph.IsValid())throw std::invalid_argument(graph.GetError());Scene scene;scene.AddSystem<ParticleSystem>();auto e=scene.CreateEntity();e.AddComponent<ParticleEmitter>().MaxParticles=4096;VFX::SetGraph(e,graph);VFX::Simulate(e,seconds);std::vector<std::array<float,8>> points;for(const auto& p:VFX::GetParticles(e))points.push_back({p.Position.X,p.Position.Y,p.Position.Z,p.Size,p.Color.X,p.Color.Y,p.Color.Z,p.Color.W});return points;});}
}
