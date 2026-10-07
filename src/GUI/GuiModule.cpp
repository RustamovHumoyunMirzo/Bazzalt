#include "GUI/GuiModuleApi.h"
#include "GUI/GuiRendererModule.h"
namespace Bazzalt::Runtime {
void ModuleRegisterGuiComponents(ComponentSerializationRegistry&);
static const GuiModuleApi Api{
    1,sizeof(GuiModuleApi),
    [](GuiSystem& system,Scene& scene,Vec2 size){system.ModuleLayout(scene,size);},
    [](GuiSystem& system,Scene& scene){system.ModuleProcessInput(scene);},
    [](GuiSystem& system,Scene& scene,Entity entity){return system.ModuleFocus(scene,entity);},
    ModuleRegisterGuiComponents,
    [](RenderBackend& backend)->void*{return new GuiRendererModule(backend);},
    [](void* state){delete static_cast<GuiRendererModule*>(state);},
    [](void* state){static_cast<GuiRendererModule*>(state)->Clear();},
    [](const void* state){return static_cast<const GuiRendererModule*>(state)->GetBatchCount();},
    [](void* state,Scene& scene){static_cast<GuiRendererModule*>(state)->PrepareSpatial(scene);},
    [](void* state,Scene& scene,Vec2 size,UUID camera,filament::RenderTarget* target){static_cast<GuiRendererModule*>(state)->PrepareOverlay(scene,size,camera,target);},
    [](void* state,Scene& scene,Vec2 size){static_cast<GuiRendererModule*>(state)->RenderOverlay(scene,size);},
    [](void* state,Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target){static_cast<GuiRendererModule*>(state)->RenderCameraOverlay(scene,camera,size,target);}
};
}
#if defined(_WIN32)
#define GUI_EXPORT __declspec(dllexport)
#else
#define GUI_EXPORT __attribute__((visibility("default")))
#endif
extern "C" GUI_EXPORT const Bazzalt::Runtime::GuiModuleApi* BazzaltGetGuiModuleApi(std::uint32_t version,std::uint32_t size){
    return version==1&&size==sizeof(Bazzalt::Runtime::GuiModuleApi)?&Bazzalt::Runtime::Api:nullptr;
}
