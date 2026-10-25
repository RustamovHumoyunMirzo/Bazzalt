#pragma once
#include <cstdint>
#include "GUI/GuiSystem.h"
#include "GUI/GuiRenderer.h"
#include "GUI/FontFace.h"
namespace Bazzalt { class ComponentSerializationRegistry; }
namespace Bazzalt::Runtime {
inline constexpr std::uint32_t GuiModuleVersion=5;
// Private, versioned interface. Modules are built with the matching core/toolchain.
struct GuiModuleApi {
    std::uint32_t Version,Size;
    void (*Layout)(GuiSystem&,Scene&,Vec2);
    void (*ProcessInput)(GuiSystem&,Scene&);
    bool (*Focus)(GuiSystem&,Scene&,Entity);
    void (*RegisterComponents)(ComponentSerializationRegistry&);
    void* (*CreateRenderer)(RenderBackend&);
    void (*DestroyRenderer)(void*);
    void (*ClearRenderer)(void*);
    std::size_t (*BatchCount)(const void*);
    void (*PrepareSpatial)(void*,Scene&);
    void (*PrepareOverlay)(void*,Scene&,Vec2,UUID,filament::RenderTarget*);
    void (*RenderOverlay)(void*,Scene&,Vec2);
    void (*RenderCameraOverlay)(void*,Scene&,UUID,Vec2,filament::RenderTarget*);
    bool (*ValidateAsset)(const std::filesystem::path&,const std::string&,std::string&);
    FontInfo (*InspectFont)(UUID,std::uint32_t);
    Vec2 (*MeasureFont)(UUID,std::uint32_t,const std::string&,float,float);
    std::vector<UUID> (*StylesheetReferences)(const std::filesystem::path&);
    FontPreviewImage (*PreviewFont)(UUID,std::uint32_t,const std::string&,float,std::uint32_t);
    filament::RenderTarget* (*SceneInputTarget)(const void*);
    void (*RenderSpatialEffects)(void*);
};
using GetGuiModuleApi=const GuiModuleApi* (*)(std::uint32_t,std::uint32_t);
const GuiModuleApi& GuiModule();
}
