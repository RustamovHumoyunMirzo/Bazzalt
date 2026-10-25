#include "fixtures/SystemIdentityProbe.h"
#include <cassert>
int main(){
    Bazzalt::Scene scene;
    scene.AddSystem<SharedSystemProbe>().Value=91;
    assert(ReadSystemProbe(&scene)==91);
    assert(RemoveSystemProbe(&scene));assert(!scene.HasSystem<SharedSystemProbe>());
    AddSystemProbe(&scene);assert(scene.HasSystem<SharedSystemProbe>());
    assert(scene.GetSystem<SharedSystemProbe>().Value==73);
    const auto& readOnly=scene;assert(readOnly.GetSystem<SharedSystemProbe>().Value==73);
    assert(scene.RemoveSystem<SharedSystemProbe>());assert(ReadSystemProbe(&scene)==-1);
}
