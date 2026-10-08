#include "GUI/GuiSystem.h"
#include "Bazzalt/Input.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Bazzalt::Runtime {
namespace {
bool Finite(Vec2 v){return std::isfinite(v.X)&&std::isfinite(v.Y);}
GuiBounds Intersect(GuiBounds a,GuiBounds b){
    Vec2 begin{std::max(a.Position.X,b.Position.X),std::max(a.Position.Y,b.Position.Y)};
    return {begin,{std::max(0.f,std::min(a.Position.X+a.Size.X,b.Position.X+b.Size.X)-begin.X),
                  std::max(0.f,std::min(a.Position.Y+a.Size.Y,b.Position.Y+b.Size.Y)-begin.Y)}};
}
bool Enabled(Entity e){const auto* b=e.TryGetComponent<GuiButton>();const auto* i=e.TryGetComponent<GuiTextInput>();
    return (b&&b->Enabled&&b->Interactable)||(i&&i->Enabled&&i->Interactable);}
bool HitTarget(Entity e){const auto* r=e.TryGetComponent<Rectangle>();const auto* i=e.TryGetComponent<GuiImage>();
    return Enabled(e)||(r&&r->Enabled&&r->RaycastTarget)||(i&&i->Enabled&&i->RaycastTarget);}
bool VisibleArea(const GuiItem& item){const auto clipped=Intersect(item.Bounds,item.Clip);return clipped.Size.X>0&&clipped.Size.Y>0;}
bool DirectInput(const GuiItem& item){const auto& frame=item.Root.GetComponent<Frame>();if(frame.Mode!=FrameMode::CameraBound)return true;const auto camera=item.Root.GetScene()->GetEntity(frame.Camera);const auto* source=camera.TryGetComponent<Camera>();const auto* target=camera.TryGetComponent<CameraRenderTarget>();return (!source||!source->RenderTarget)&&(!target||!target->Enabled);}
std::size_t Characters(const std::string& text){std::size_t count=0;for(unsigned char c:text)if((c&0xc0)!=0x80)++count;return count;}
void Backspace(std::string& text){if(text.empty())return;std::size_t start=text.size()-1;while(start&&(static_cast<unsigned char>(text[start])&0xc0)==0x80)--start;text.erase(start);}
}
void GuiSystem::Descend(Entity parent,Entity root,GuiBounds bounds,GuiBounds clip,GuiBounds surface,float scale,std::size_t depth){
    if(depth>128)return;
    auto children=parent.GetChildren();
    std::stable_sort(children.begin(),children.end(),[](Entity a,Entity b){
        const auto* x=a.TryGetComponent<RectTransform>();const auto* y=b.TryGetComponent<RectTransform>();
        return (x?x->ZIndex:0)<(y?y->ZIndex:0);
    });
    for(auto child:children){
        if(child.HasComponent<Frame>())continue; // A nested Frame is an independent render root.
        const auto* layout=child.TryGetComponent<RectTransform>();
        if(!layout){Descend(child,root,bounds,clip,surface,scale,depth+1);continue;}
        if(!layout->Enabled||!layout->Visible||!Finite(layout->Position)||!Finite(layout->Size)||!Finite(layout->AnchorMin)||!Finite(layout->AnchorMax)||!Finite(layout->Pivot)||!Finite(layout->MinSize)||!Finite(layout->MaxSize))continue;
        const auto& v=*layout;
        Vec2 size{bounds.Size.X*(v.AnchorMax.X-v.AnchorMin.X)+v.Size.X*scale,bounds.Size.Y*(v.AnchorMax.Y-v.AnchorMin.Y)+v.Size.Y*scale};
        size.X=std::clamp(size.X,std::max(0.f,v.MinSize.X*scale),std::max(std::max(0.f,v.MinSize.X*scale),v.MaxSize.X*scale));
        size.Y=std::clamp(size.Y,std::max(0.f,v.MinSize.Y*scale),std::max(std::max(0.f,v.MinSize.Y*scale),v.MaxSize.Y*scale));
        GuiBounds resolved{{bounds.Position.X+bounds.Size.X*v.AnchorMin.X+v.Position.X*scale-v.Pivot.X*size.X,
                            bounds.Position.Y+bounds.Size.Y*v.AnchorMin.Y+v.Position.Y*scale-v.Pivot.Y*size.Y},size};
        if(!Finite(resolved.Position)||!Finite(resolved.Size))continue;
        Items.push_back({child,root,resolved,clip,surface,scale,Items.size()});
        Descend(child,root,resolved,v.ClipChildren?Intersect(clip,resolved):clip,surface,scale,depth+1);
    }
}
void GuiSystem::ModuleLayout(Scene& scene,Vec2 size){
    if(!Finite(size)||size.X<=0||size.Y<=0)return;
    PresentationSize=size;Items.clear();
    std::vector<Entity> roots;for(auto handle:scene.GetRegistry().view<Frame>())roots.push_back(scene.GetEntity(static_cast<Entity::Id>(handle)));
    std::sort(roots.begin(),roots.end(),[](Entity a,Entity b){const auto& x=a.GetComponent<Frame>();const auto& y=b.GetComponent<Frame>();return x.ZIndex==y.ZIndex?a.GetUUID()<b.GetUUID():x.ZIndex<y.ZIndex;});
    for(auto root:roots){
        const auto& frame=root.GetComponent<Frame>();
        if(!frame.Enabled||!frame.Visible||!Finite(frame.ReferenceSize)||frame.ReferenceSize.X<=0||frame.ReferenceSize.Y<=0||!std::isfinite(frame.MatchWidthOrHeight)||!std::isfinite(frame.PixelsPerUnit)||frame.PixelsPerUnit<=0)continue;
        GuiBounds surface{{},size};
        if(frame.Mode==FrameMode::CameraBound){
            Entity camera=scene.GetEntity(frame.Camera);const auto* c=camera.TryGetComponent<Camera>();
            if(!c||!c->Enabled||!c->Active)continue;
            if(!std::isfinite(c->Viewport.X)||!std::isfinite(c->Viewport.Y)||!std::isfinite(c->Viewport.Width)||!std::isfinite(c->Viewport.Height)||c->Viewport.Width<=0||c->Viewport.Height<=0)continue;
            surface={{size.X*c->Viewport.X,size.Y*(1-c->Viewport.Y-c->Viewport.Height)},{size.X*c->Viewport.Width,size.Y*c->Viewport.Height}};
        }else if(frame.Mode==FrameMode::Spatial)surface={{},frame.ReferenceSize};
        float scale=1;
        if(frame.Mode!=FrameMode::Spatial&&frame.ScaleMode==GuiScaleMode::ScaleWithViewport){
            const float match=std::clamp(frame.MatchWidthOrHeight,0.f,1.f);
            scale=std::pow(surface.Size.X/frame.ReferenceSize.X,1-match)*std::pow(surface.Size.Y/frame.ReferenceSize.Y,match);
        }
        if(!std::isfinite(scale)||scale<=0)continue;
        Descend(root,root,surface,surface,surface,scale,0);
    }
    std::unordered_set<UUID> alive;for(const auto& item:Items)alive.insert(item.Owner.GetUUID());
    std::erase_if(Interactions,[&](const auto& pair){return !alive.contains(pair.first);});
    const auto eligible=[&](UUID id){return std::any_of(Items.begin(),Items.end(),[&](const auto& item){return item.Owner.GetUUID()==id&&item.Root.GetComponent<Frame>().Interactable&&Enabled(item.Owner)&&VisibleArea(item)&&DirectInput(item);});};
    if(!alive.contains(Focused)||!eligible(Focused))Focused={};if(!alive.contains(Captured)||!eligible(Captured))Captured={};
}
bool GuiSystem::ModuleFocus(Scene&,Entity entity){
    const auto found=std::find_if(Items.begin(),Items.end(),[&](const auto& item){return item.Owner==entity;});
    if(found==Items.end()||!Enabled(entity)||!found->Root.GetComponent<Frame>().Interactable||!VisibleArea(*found)||!DirectInput(*found))return false;
    if(Focused)Interactions[Focused].Focused=false;
    Focused=entity.GetUUID();Interactions[Focused].Focused=true;return true;
}
void GuiSystem::ModuleProcessInput(Scene& scene){
    for(auto& [id,state]:Interactions){(void)id;state.PointerEntered=state.PointerExited=state.Clicked=state.ValueChanged=state.Submitted=false;state.Hovered=state.Pressed=false;state.Focused=false;}
    const auto previous=Hovered;Hovered={};
    if(!Input::IsActive()){Focused=Captured={};if(previous)Interactions[previous].PointerExited=true;return;}
    const Vec2 pointer=Input::GetMousePosition();float spatialDistance=std::numeric_limits<float>::max();
    for(auto it=Items.rbegin();it!=Items.rend();++it){
        const auto& frame=it->Root.GetComponent<Frame>();if(!frame.Interactable||!HitTarget(it->Owner)||!DirectInput(*it))continue;
        if(frame.Mode!=FrameMode::Spatial){if(it->Bounds.Contains(pointer)&&it->Clip.Contains(pointer)){Hovered=it->Owner.GetUUID();break;}continue;}
        auto camera=scene.GetEntity(frame.Camera);if(!camera){for(auto handle:scene.GetRegistry().view<Camera>()){auto candidate=scene.GetEntity(static_cast<Entity::Id>(handle));const auto& c=candidate.GetComponent<Camera>();if(c.Enabled&&c.Active){camera=candidate;break;}}}
        if(!camera)continue;
        const auto* c=camera.TryGetComponent<Camera>();if(!c||!c->Enabled||!c->Active)continue;
        SceneRay ray;try{ray=scene.ScreenPointToRay(camera,pointer,PresentationSize);}catch(const std::exception&){continue;}Mat4 inverse;
        if(!it->Root.GetWorldMatrix().TryInverse(inverse))continue;
        Vec3 origin=inverse.TransformPoint(ray.Origin),direction=inverse.TransformDirection(ray.Direction);
        if(std::fabs(direction.Z)<1e-6f)continue;float distance=-origin.Z/direction.Z;
        if(distance<0||distance>=spatialDistance)continue;
        Vec3 point=origin+direction*distance;Vec2 local{point.X*frame.PixelsPerUnit+frame.ReferenceSize.X*.5f,-point.Y*frame.PixelsPerUnit+frame.ReferenceSize.Y*.5f};
        if(it->Bounds.Contains(local)&&it->Clip.Contains(local)){Hovered=it->Owner.GetUUID();spatialDistance=distance;}
    }
    if(previous!=Hovered){if(previous)Interactions[previous].PointerExited=true;if(Hovered)Interactions[Hovered].PointerEntered=true;}
    if(Hovered)Interactions[Hovered].Hovered=true;
    if(Input::GetMouseButtonDown(MouseButton::Left)){Captured=Hovered;if(!Focus(scene,scene.GetEntity(Hovered)))Focused={};}
    if(Captured&&Input::GetMouseButton(MouseButton::Left))Interactions[Captured].Pressed=true;
    if(Input::GetMouseButtonUp(MouseButton::Left)){
        if(Captured&&Captured==Hovered&&Enabled(scene.GetEntity(Captured)))Interactions[Captured].Clicked=true;
        Captured={};
    }
    if(Input::GetKeyDown(KeyCode::Tab)){
        std::vector<Entity> targets;for(const auto& item:Items)if(item.Root.GetComponent<Frame>().Interactable&&Enabled(item.Owner)&&VisibleArea(item)&&DirectInput(item))targets.push_back(item.Owner);
        const auto index=[](Entity e){if(auto* t=e.TryGetComponent<GuiTextInput>())return t->TabIndex;return e.GetComponent<GuiButton>().TabIndex;};
        std::stable_sort(targets.begin(),targets.end(),[&](Entity a,Entity b){return index(a)<index(b);});
        std::erase_if(targets,[&](Entity e){return index(e)<0;});
        if(!targets.empty()){auto current=std::find_if(targets.begin(),targets.end(),[&](Entity e){return e.GetUUID()==Focused;});std::size_t next=0;if(current!=targets.end()){const auto at=std::size_t(current-targets.begin());next=Input::GetKey(KeyCode::LeftShift)||Input::GetKey(KeyCode::RightShift)?(at+targets.size()-1)%targets.size():(at+1)%targets.size();}Focus(scene,targets[next]);}
    }
    if(Input::GetKeyDown(KeyCode::Escape))Focused={};
    auto focused=scene.GetEntity(Focused);
    if(focused&&Enabled(focused)){
        auto& state=Interactions[Focused];state.Focused=true;
        if(Input::GetKeyDown(KeyCode::Enter)){state.Submitted=true;if(focused.HasComponent<GuiButton>())state.Clicked=true;}
        if(focused.HasComponent<GuiButton>()&&Input::GetKeyDown(KeyCode::Space))state.Clicked=true;
        if(auto* text=focused.TryGetComponent<GuiTextInput>();text&&text->Enabled&&!text->ReadOnly){
            const auto previousValue=text->Value;
            if(Input::GetKeyDown(KeyCode::Backspace))Backspace(text->Value);
            const auto& input=Input::GetTextInput();std::size_t count=Characters(text->Value);
            for(std::size_t offset=0;offset<input.size()&&count<std::min<std::size_t>(text->MaxLength,65536);){
                const auto begin=offset++;while(offset<input.size()&&(static_cast<unsigned char>(input[offset])&0xc0)==0x80)++offset;
                if(static_cast<unsigned char>(input[begin])>=32){text->Value.append(input,begin,offset-begin);++count;}
            }
            state.ValueChanged=text->Value!=previousValue;
        }
    }else Focused={};
}
}
