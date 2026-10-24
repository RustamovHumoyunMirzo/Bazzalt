#include "GUI/GuiModuleApi.h"
#include "GUI/GuiSerialization.h"
#include "GUI/GuiAssetImport.h"
#include <filesystem>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
namespace {
const GuiModuleApi* Load(){
    // Resolve relative to the core library, never the project or working directory.
    // Keep the module loaded for process lifetime: serializers retain callbacks.
#if defined(_WIN32)
    HMODULE core=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&GuiModule),&core))throw std::runtime_error("Cannot locate Bazzalt core for GUI loading");
    std::wstring filename(32768,L'\0');const auto length=GetModuleFileNameW(core,filename.data(),static_cast<DWORD>(filename.size()));
    if(!length||length>=filename.size())throw std::runtime_error("Cannot resolve Bazzalt core path");
    filename.resize(length);const auto path=std::filesystem::path(filename).parent_path()/L"bazzalt_gui.dll";
    auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)throw std::runtime_error("Could not load sibling bazzalt_gui.dll (Windows error "+std::to_string(GetLastError())+"). Reinstall the matching runtime modules.");
    auto get=reinterpret_cast<GetGuiModuleApi>(GetProcAddress(module,"BazzaltGetGuiModuleApi"));
#else
    Dl_info info{};if(!dladdr(reinterpret_cast<void*>(&GuiModule),&info)||!info.dli_fname)throw std::runtime_error("Cannot resolve Bazzalt core path");
#if defined(__APPLE__)
    const auto path=std::filesystem::path(info.dli_fname).parent_path()/"libbazzalt_gui.dylib";
#else
    const auto path=std::filesystem::path(info.dli_fname).parent_path()/"libbazzalt_gui.so";
#endif
    auto module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!module){const auto* error=dlerror();throw std::runtime_error(std::string("Could not load sibling GUI runtime: ")+(error?error:"unknown error"));}
    auto get=reinterpret_cast<GetGuiModuleApi>(dlsym(module,"BazzaltGetGuiModuleApi"));
#endif
    const auto* api=get?get(GuiModuleVersion,sizeof(GuiModuleApi)):nullptr;
    if(!api||api->Version!=GuiModuleVersion||api->Size!=sizeof(GuiModuleApi)||!api->Layout||!api->ProcessInput||!api->Focus||!api->RegisterComponents||!api->CreateRenderer||!api->DestroyRenderer||!api->ClearRenderer||!api->BatchCount||!api->PrepareSpatial||!api->PrepareOverlay||!api->RenderOverlay||!api->RenderCameraOverlay)throw std::runtime_error("Incompatible GUI runtime. Core and bazzalt_gui must come from the same build.");
    if(!api->ValidateAsset||!api->InspectFont||!api->MeasureFont||!api->StylesheetReferences||!api->PreviewFont)throw std::runtime_error("Incomplete GUI asset/font module");
    return api;
}
}
const GuiModuleApi& GuiModule(){static const auto* api=Load();return *api;}
bool ValidateGuiAssetFile(const std::filesystem::path& path,const std::string& kind,std::string& error){return GuiModule().ValidateAsset(path,kind,error);}
std::vector<UUID> GetStylesheetReferences(const std::filesystem::path& path){return GuiModule().StylesheetReferences(path);}
FontPreviewImage GetFontPreview(UUID asset,std::uint32_t face,const std::string& text,float size,std::uint32_t width){return GuiModule().PreviewFont(asset,face,text,size,width);}
void GuiSystem::Layout(Scene& scene,Vec2 size){GuiModule().Layout(*this,scene,size);}
void GuiSystem::ProcessInput(Scene& scene){GuiModule().ProcessInput(*this,scene);}
bool GuiSystem::Focus(Scene& scene,Entity entity){return GuiModule().Focus(*this,scene,entity);}
void RegisterGuiComponents(ComponentSerializationRegistry& registry){GuiModule().RegisterComponents(registry);}
GuiRenderer::GuiRenderer(RenderBackend& backend):m_impl(GuiModule().CreateRenderer(backend)){}
GuiRenderer::~GuiRenderer(){if(m_impl)GuiModule().DestroyRenderer(m_impl);}
void GuiRenderer::Clear(){GuiModule().ClearRenderer(m_impl);}
std::size_t GuiRenderer::GetBatchCount() const{return GuiModule().BatchCount(m_impl);}
void GuiRenderer::PrepareSpatial(Scene& scene){GuiModule().PrepareSpatial(m_impl,scene);}
void GuiRenderer::PrepareOverlay(Scene& scene,Vec2 size,UUID camera,filament::RenderTarget* target){GuiModule().PrepareOverlay(m_impl,scene,size,camera,target);}
void GuiRenderer::RenderOverlay(Scene& scene,Vec2 size){GuiModule().RenderOverlay(m_impl,scene,size);}
void GuiRenderer::RenderCameraOverlay(Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target){GuiModule().RenderCameraOverlay(m_impl,scene,camera,size,target);}
}
namespace Bazzalt {
namespace { Runtime::GuiSystem* SystemFor(Entity entity){auto* scene=entity.GetScene();return entity&&scene&&scene->HasSystem<Runtime::GuiSystem>()?&scene->GetSystem<Runtime::GuiSystem>():nullptr;} }
GuiInteraction GUI::GetInteraction(Entity entity){auto* system=SystemFor(entity);if(!system)return {};auto it=system->Interactions.find(entity.GetUUID());return it==system->Interactions.end()?GuiInteraction{}:it->second;}
GuiBounds GUI::GetBounds(Entity entity){auto* system=SystemFor(entity);if(system)for(const auto& item:system->Items)if(item.Owner==entity)return item.Bounds;return {};}
Entity GUI::GetFrame(Entity entity){auto current=entity.GetParent();for(std::size_t depth=0;current&&depth<128;++depth){if(current.HasComponent<Frame>())return current;current=current.GetParent();}return {};}
GuiStyle GUI::ResolveStyle(Entity entity){
    if(auto* system=SystemFor(entity)){auto found=system->Computed.find(entity.GetUUID());if(found!=system->Computed.end())return found->second.Style;}
    auto* local=entity.TryGetComponent<Rectangle>();GuiStyle value=local?local->Style:GuiStyle{};
    if(!entity||entity.HasComponent<Frame>())return value;
    auto flags=value;
    std::size_t depth=0;
    for(auto parent=entity.GetParent();parent&&depth++<128&&(flags.InheritColor||flags.InheritHoverColor||flags.InheritPressedColor||flags.InheritDisabledColor||flags.InheritBorderColor||flags.InheritBorderWidth||flags.InheritOpacity);parent=parent.GetParent()){
        if(const auto* source=parent.TryGetComponent<Rectangle>();source&&source->Enabled){
            auto style=source->Style;
            if(auto* system=SystemFor(parent)){auto found=system->Computed.find(parent.GetUUID());if(found!=system->Computed.end())style=found->second.Style;}
#define INHERIT(Field) if(flags.Inherit##Field){value.Field=style.Field;flags.Inherit##Field=style.Inherit##Field;}
            INHERIT(Color) INHERIT(HoverColor) INHERIT(PressedColor) INHERIT(DisabledColor)
            INHERIT(BorderColor) INHERIT(BorderWidth) INHERIT(Opacity)
#undef INHERIT
        }
        if(parent.HasComponent<Frame>())break;
    }
    return value;
}
GuiText GUI::ResolveText(Entity entity){
    if(auto* system=SystemFor(entity)){auto found=system->Computed.find(entity.GetUUID());if(found!=system->Computed.end()){auto value=found->second.Text;if(auto* local=entity.TryGetComponent<GuiText>())value.Value=local->Value;return value;}}
    auto* local=entity.TryGetComponent<GuiText>();GuiText value=local?*local:GuiText{};
    if(!entity||entity.HasComponent<Frame>())return value;
    auto flags=value;
    std::size_t depth=0;
    for(auto parent=entity.GetParent();parent&&depth++<128&&(flags.InheritColor||flags.InheritFontSize||flags.InheritWrap||flags.InheritFont);parent=parent.GetParent()){
        if(const auto* source=parent.TryGetComponent<GuiText>();source&&source->Enabled){
            auto effective=*source;
            if(auto* system=SystemFor(parent)){auto found=system->Computed.find(parent.GetUUID());if(found!=system->Computed.end())effective=found->second.Text;}
#define INHERIT(Field) if(flags.Inherit##Field){value.Field=effective.Field;flags.Inherit##Field=effective.Inherit##Field;}
            INHERIT(Color) INHERIT(FontSize) INHERIT(Wrap)
            if(flags.InheritFont){value.Font=effective.Font;value.FontFace=effective.FontFace;flags.InheritFont=effective.InheritFont;}
#undef INHERIT
        }
        if(parent.HasComponent<Frame>())break;
    }
    return value;
}
GuiImage GUI::ResolveImage(Entity entity){
    if(auto* system=SystemFor(entity)){auto found=system->Computed.find(entity.GetUUID());if(found!=system->Computed.end())return found->second.Image;}
    auto* local=entity.TryGetComponent<GuiImage>();GuiImage value=local?*local:GuiImage{};
    if(!entity||entity.HasComponent<Frame>())return value;
    bool inherit=value.InheritColor;
    std::size_t depth=0;
    for(auto parent=entity.GetParent();parent&&inherit&&depth++<128;parent=parent.GetParent()){
        if(const auto* source=parent.TryGetComponent<GuiImage>();source&&source->Enabled){auto effective=*source;if(auto* system=SystemFor(parent)){auto found=system->Computed.find(parent.GetUUID());if(found!=system->Computed.end())effective=found->second.Image;}value.Color=effective.Color;inherit=effective.InheritColor;}
        if(parent.HasComponent<Frame>())break;
    }
    return value;
}
bool GUI::Focus(Entity entity){auto* system=SystemFor(entity);return system&&system->Focus(*entity.GetScene(),entity);}
void GUI::ClearFocus(Scene& scene){if(scene.HasSystem<Runtime::GuiSystem>()){auto& s=scene.GetSystem<Runtime::GuiSystem>();if(s.Focused)s.Interactions[s.Focused].Focused=false;s.Focused={};}}
bool GUI::IsPointerOverUI(Scene& scene){return scene.HasSystem<Runtime::GuiSystem>()&&bool(scene.GetSystem<Runtime::GuiSystem>().Hovered);}
std::vector<std::string> GUI::GetStyleDiagnostics(Entity entity){auto* system=SystemFor(entity);return system?system->StyleDiagnostics:std::vector<std::string>{};}
bool Font::IsValid()const{return GetInfo().Valid;}
FontInfo Font::GetInfo()const{return Runtime::GuiModule().InspectFont(m_asset,m_face);}
Vec2 Font::MeasureText(const std::string& text,float size,float wrapWidth)const{return Runtime::GuiModule().MeasureFont(m_asset,m_face,text,size,wrapWidth);}
}
