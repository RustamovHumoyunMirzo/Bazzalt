#include "GUI/GuiModuleApi.h"
#include "GUI/GuiSerialization.h"
#include "GUI/GuiAssetImport.h"
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include "Runtime/ModuleLoader.h"
namespace Bazzalt::Runtime {
namespace {
const GuiModuleApi* Load(){
    const auto* api = LoadSiblingModuleApi<GuiModuleApi>("bazzalt_gui",
        "BazzaltGetGuiModuleApi", GuiModuleVersion);
    if(!api||api->Version!=GuiModuleVersion||api->Size!=sizeof(GuiModuleApi)||!api->Layout||!api->ProcessInput||!api->Focus||!api->RegisterComponents||!api->CreateRenderer||!api->DestroyRenderer||!api->ClearRenderer||!api->BatchCount||!api->PrepareSpatial||!api->PrepareOverlay||!api->RenderOverlay||!api->RenderCameraOverlay)throw std::runtime_error("Incompatible GUI runtime. Core and bazzalt_gui must come from the same build.");
    if(!api->ValidateAsset||!api->InspectFont||!api->MeasureFont||!api->StylesheetReferences||!api->PreviewFont||!api->SceneInputTarget||!api->RenderSpatialEffects||!api->StylesheetProperties||!api->GetStyleProperty||!api->SetStyleProperty||!api->EditCSS||!api->ReadStylesheetCSS)throw std::runtime_error("Incomplete GUI module");
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
filament::RenderTarget* GuiRenderer::GetSceneInputTarget()const{return GuiModule().SceneInputTarget(m_impl);}
void GuiRenderer::RenderSpatialEffects(){GuiModule().RenderSpatialEffects(m_impl);}
std::size_t GuiRenderer::GetBatchCount() const{return GuiModule().BatchCount(m_impl);}
void GuiRenderer::PrepareSpatial(Scene& scene){GuiModule().PrepareSpatial(m_impl,scene);}
void GuiRenderer::PrepareOverlay(Scene& scene,Vec2 size,UUID camera,filament::RenderTarget* target){GuiModule().PrepareOverlay(m_impl,scene,size,camera,target);}
void GuiRenderer::RenderOverlay(Scene& scene,Vec2 size){GuiModule().RenderOverlay(m_impl,scene,size);}
void GuiRenderer::RenderCameraOverlay(Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target){GuiModule().RenderCameraOverlay(m_impl,scene,camera,size,target);}
}
namespace Bazzalt {
namespace { Runtime::GuiSystem* SystemFor(Entity entity){auto* scene=entity.GetScene();return entity&&scene&&scene->HasSystem<Runtime::GuiSystem>()?&scene->GetSystem<Runtime::GuiSystem>():nullptr;} }
GuiInteraction GUI::GetInteraction(Entity entity){auto* system=SystemFor(entity);if(!system)return {};auto it=system->Interactions.find(entity.GetUUID());return it==system->Interactions.end()?GuiInteraction{}:it->second;}
std::vector<GuiStyleProperty> GUI::GetStylesheetProperties(UUID sheet){return Runtime::GuiModule().StylesheetProperties(sheet);}
std::string GUI::GetStyleProperty(Entity entity,const std::string& name){return Runtime::GuiModule().GetStyleProperty(entity,name);}
bool GUI::SetStyleProperty(Entity entity,const std::string& name,const std::string& value){return Runtime::GuiModule().SetStyleProperty(entity,name,value);}
bool GUI::ResetStyleProperty(Entity entity,const std::string& name){auto* element=entity.TryGetComponent<GuiElement>();return element&&element->StyleProperties.erase(name)!=0;}
GuiBounds GUI::GetBounds(Entity entity){auto* system=SystemFor(entity);if(system)for(const auto& item:system->Items)if(item.Owner==entity)return item.Bounds;return {};}
Entity GUI::GetFrame(Entity entity){auto current=entity.GetParent();for(std::size_t depth=0;current&&depth<128;++depth){if(current.HasComponent<Frame>())return current;current=current.GetParent();}return {};}
bool GUI::SetParent(Entity child,Entity parent,bool preservePosition){
    if(!child||!parent||child.GetScene()!=parent.GetScene())return false;
    auto* scene=child.GetScene();auto* system=SystemFor(child);auto* rect=child.TryGetComponent<RectTransform>();
    if(!preservePosition)return child.SetParent(parent,false);
    if(!rect||!system)return child.SetParent(parent,true);
    system->Layout(*scene,system->PresentationSize);const auto bounds=GetBounds(child);const bool hadBounds=std::any_of(system->Items.begin(),system->Items.end(),[&](const auto& item){return item.Owner==child;});const auto oldFrame=GetFrame(child);const auto newFrame=parent.HasComponent<Frame>()?parent:GetFrame(parent);
    if(!oldFrame||!newFrame)return child.SetParent(parent,true);
    const auto world=child.GetWorldMatrix();Mat4 inverse;Transform local;if(!parent.GetWorldMatrix().TryInverse(inverse)||!(inverse*world).Decompose(local.Position,local.Rotation,local.Scale))return false;
    if(!child.SetParent(parent,false))return false;child.GetComponent<Transform>()=local;
    system->Layout(*scene,system->PresentationSize);const Runtime::GuiItem* entry=nullptr;for(const auto& item:system->Items)if(item.Owner==child){entry=&item;break;}if(!entry||!hadBounds)return true;
    GuiBounds content=entry->Surface;Entity container=parent;while(container!=newFrame&&!container.HasComponent<RectTransform>())container=container.GetParent();for(const auto& item:system->Items)if(item.Owner==container){content=item.Bounds;break;}
    const auto* layout=Runtime::EffectiveGui<RectTransform>(container);if(container==newFrame){auto computed=system->Computed.find(container.GetUUID());if(computed!=system->Computed.end())layout=&computed->second.Rect;}
    const float scale=entry->Scale;if(layout){content.Position=content.Position+Vec2{layout->Padding.X,layout->Padding.Y}*scale;content.Size={std::max(0.f,content.Size.X-(layout->Padding.X+layout->Padding.Z)*scale),std::max(0.f,content.Size.Y-(layout->Padding.Y+layout->Padding.W)*scale)};if(container==parent&&layout->Layout!=GuiLayoutMode::Absolute){system->Layout(*scene,system->PresentationSize);return true;}}
    rect->Size={bounds.Size.X/scale-content.Size.X/scale*(rect->AnchorMax.X-rect->AnchorMin.X),bounds.Size.Y/scale-content.Size.Y/scale*(rect->AnchorMax.Y-rect->AnchorMin.Y)};
    rect->Position={(bounds.Position.X-content.Position.X-content.Size.X*rect->AnchorMin.X+bounds.Size.X*rect->Pivot.X)/scale,(bounds.Position.Y-content.Position.Y-content.Size.Y*rect->AnchorMin.Y+bounds.Size.Y*rect->Pivot.Y)/scale};system->Layout(*scene,system->PresentationSize);return true;
}
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
    if(auto* system=SystemFor(entity)){auto found=system->Computed.find(entity.GetUUID());if(found!=system->Computed.end()){auto value=found->second.Text;if(auto* local=entity.TryGetComponent<GuiText>();local&&!found->second.Cascade.Properties.contains("content"))value.Value=local->Value;return value;}}
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

namespace Bazzalt {
std::string GUI::GetCSS(Entity e){auto* element=e.TryGetComponent<GuiElement>();return element?element->RuntimeCSS:std::string{};}
std::string GUI::GetStylesheetCSS(Entity e){return Runtime::GuiModule().ReadStylesheetCSS(e);}
bool GUI::SetCSS(Entity e,const std::string& css){return Runtime::GuiModule().EditCSS(e,"replace","",css);}
bool GUI::AppendCSS(Entity e,const std::string& css){return Runtime::GuiModule().EditCSS(e,"append","",css);}
std::string GUI::GetCSSProperty(Entity e,const std::string& name){auto* element=e.TryGetComponent<GuiElement>();if(!element)return {};auto i=element->InlineCSS.find(name);return i==element->InlineCSS.end()?std::string{}:i->second;}
bool GUI::SetCSSProperty(Entity e,const std::string& name,const std::string& value){return Runtime::GuiModule().EditCSS(e,"set",name,value);}
bool GUI::RemoveCSSProperty(Entity e,const std::string& name){return Runtime::GuiModule().EditCSS(e,"remove",name,"");}
bool GUI::SetCSSRule(Entity e,const std::string& selector,const std::string& declarations){return Runtime::GuiModule().EditCSS(e,"rule",selector,declarations);}
bool GUI::RemoveCSSRule(Entity e,const std::string& selector){return Runtime::GuiModule().EditCSS(e,"remove-rule",selector,"");}
bool GUI::SetCSSKeyframes(Entity e,const std::string& name,const std::string& frames){return Runtime::GuiModule().EditCSS(e,"keyframes",name,frames);}
bool GUI::RemoveCSSKeyframes(Entity e,const std::string& name){return Runtime::GuiModule().EditCSS(e,"remove-keyframes",name,"");}
}
