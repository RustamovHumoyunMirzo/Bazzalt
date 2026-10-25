#include "SystemIdentityProbe.h"
extern "C" SYSTEM_PROBE_API int ReadSystemProbe(Bazzalt::Scene* scene){
    return scene->HasSystem<SharedSystemProbe>()?scene->GetSystem<SharedSystemProbe>().Value:-1;
}
extern "C" SYSTEM_PROBE_API bool RemoveSystemProbe(Bazzalt::Scene* scene){return scene->RemoveSystem<SharedSystemProbe>();}
extern "C" SYSTEM_PROBE_API void AddSystemProbe(Bazzalt::Scene* scene){scene->AddSystem<SharedSystemProbe>().Value=73;}
