#include "GUI/GuiSystem.h"
#include "Bazzalt/Input.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <unordered_map>

namespace Bazzalt::Runtime {
namespace {
bool Finite(Vec2 v){return std::isfinite(v.X)&&std::isfinite(v.Y);}
bool Insets(Vec4 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z)&&std::isfinite(v.W)&&v.X>=0&&v.Y>=0&&v.Z>=0&&v.W>=0;}
bool ValidLayout(const RectTransform& v){
    return v.Enabled&&v.Visible&&static_cast<unsigned>(v.Layout)<=2&&static_cast<unsigned>(v.Align)<=3&&static_cast<unsigned>(v.Justify)<=3&&Finite(v.Position)&&Finite(v.Size)&&Finite(v.AnchorMin)&&Finite(v.AnchorMax)&&Finite(v.Pivot)&&Finite(v.MinSize)&&Finite(v.MaxSize)&&
        v.MinSize.X>=0&&v.MinSize.Y>=0&&v.MaxSize.X>=v.MinSize.X&&v.MaxSize.Y>=v.MinSize.Y&&v.AnchorMax.X>=v.AnchorMin.X&&v.AnchorMax.Y>=v.AnchorMin.Y&&
        Insets(v.Padding)&&Insets(v.Margin)&&std::isfinite(v.Gap)&&v.Gap>=0&&std::isfinite(v.FlexGrow)&&v.FlexGrow>=0&&std::isfinite(v.FlexShrink)&&v.FlexShrink>=0;
}
GuiBounds ContentBounds(GuiBounds bounds,const RectTransform* group,float scale){
    if(!group||!ValidLayout(*group))return bounds;
    const auto p=group->Padding*scale;
    return {bounds.Position+Vec2{p.X,p.Y},{std::max(0.f,bounds.Size.X-p.X-p.Z),std::max(0.f,bounds.Size.Y-p.Y-p.W)}};
}
// Resolve layout in hierarchy order, independently of ZIndex paint/hit order.
std::unordered_map<UUID,GuiBounds> FlowBounds(const std::vector<Entity>& children,GuiBounds content,const RectTransform* group,float scale){
    std::unordered_map<UUID,GuiBounds> result;
    if(!group||!ValidLayout(*group)||group->Layout==GuiLayoutMode::Absolute)return result;
    const bool row=group->Layout==GuiLayoutMode::Row;
    struct Entry {Entity Owner;const RectTransform* Rect;float Main,Cross,Minimum,Maximum,Basis,Before,After,CrossBefore,CrossAfter;};
    std::vector<Entry> entries;
    const float mainSize=row?content.Size.X:content.Size.Y,crossSize=row?content.Size.Y:content.Size.X;
    float used=0;
    for(auto child:children){
        const auto* r=child.TryGetComponent<RectTransform>();
        if(child.HasComponent<Frame>()||!r||!ValidLayout(*r)||r->IgnoreLayout)continue;
        const auto size=r->Size*scale,min=r->MinSize*scale,max=r->MaxSize*scale;
        const auto margin=r->Margin*scale;
        const float main=std::clamp(row?size.X:size.Y,row?min.X:min.Y,row?max.X:max.Y);
        entries.push_back({child,r,main,std::clamp(row?size.Y:size.X,row?min.Y:min.X,row?max.Y:max.X),row?min.X:min.Y,row?max.X:max.Y,main,
                           row?margin.X:margin.Y,row?margin.Z:margin.W,row?margin.Y:margin.X,row?margin.W:margin.Z});
        used+=main+entries.back().Before+entries.back().After;
    }
    if(entries.empty())return result;
    float gap=group->Gap*scale;
    used+=gap*float(entries.size()-1);
    // Iteratively freeze min/max-constrained children and redistribute remaining
    // space. Shrink is weighted by each child's original preferred size.
    for(std::size_t pass=0;pass<=entries.size();++pass){
        const float free=mainSize-used;if(std::fabs(free)<.001f)break;
        float weight=0;
        for(const auto& e:entries)if(free>0?e.Main<e.Maximum:e.Main>e.Minimum)weight+=free>0?e.Rect->FlexGrow:e.Rect->FlexShrink*e.Basis;
        if(weight<=0||!std::isfinite(weight))break;
        float applied=0;
        for(auto& e:entries){
            if(!(free>0?e.Main<e.Maximum:e.Main>e.Minimum))continue;
            const float factor=free>0?e.Rect->FlexGrow:e.Rect->FlexShrink*e.Basis;
            const float next=std::clamp(e.Main+free*factor/weight,e.Minimum,e.Maximum);
            applied+=next-e.Main;e.Main=next;
        }
        used+=applied;if(std::fabs(applied)<.001f)break;
    }
    const float spare=std::max(0.f,mainSize-used);
    float cursor=group->Justify==GuiJustify::Center?spare*.5f:group->Justify==GuiJustify::End?spare:0;
    if(group->Justify==GuiJustify::SpaceBetween&&entries.size()>1)gap+=spare/float(entries.size()-1);
    for(auto& e:entries){
        const float available=std::max(0.f,crossSize-e.CrossBefore-e.CrossAfter);
        if(group->Align==GuiAlignment::Stretch)e.Cross=std::clamp(available,(row?e.Rect->MinSize.Y:e.Rect->MinSize.X)*scale,(row?e.Rect->MaxSize.Y:e.Rect->MaxSize.X)*scale);
        const float remaining=std::max(0.f,available-e.Cross);
        const float cross=e.CrossBefore+(group->Align==GuiAlignment::Center?remaining*.5f:group->Align==GuiAlignment::End?remaining:0);
        cursor+=e.Before;
        result.emplace(e.Owner.GetUUID(),GuiBounds{content.Position+(row?Vec2{cursor,cross}:Vec2{cross,cursor}),row?Vec2{e.Main,e.Cross}:Vec2{e.Cross,e.Main}});
        cursor+=e.Main+e.After+gap;
    }
    return result;
}
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
    const auto* group=parent.TryGetComponent<RectTransform>();
    bounds=ContentBounds(bounds,group,scale);
    const auto flow=FlowBounds(children,bounds,group,scale);
    std::stable_sort(children.begin(),children.end(),[](Entity a,Entity b){
        const auto* x=a.TryGetComponent<RectTransform>();const auto* y=b.TryGetComponent<RectTransform>();
        return (x?x->ZIndex:0)<(y?y->ZIndex:0);
    });
    for(auto child:children){
        if(child.HasComponent<Frame>())continue; // A nested Frame is an independent render root.
        const auto* layout=child.TryGetComponent<RectTransform>();
        if(!layout){Descend(child,root,bounds,clip,surface,scale,depth+1);continue;}
        if(!ValidLayout(*layout))continue;
        const auto& v=*layout;
        Vec2 size{bounds.Size.X*(v.AnchorMax.X-v.AnchorMin.X)+v.Size.X*scale,bounds.Size.Y*(v.AnchorMax.Y-v.AnchorMin.Y)+v.Size.Y*scale};
        size.X=std::clamp(size.X,std::max(0.f,v.MinSize.X*scale),std::max(std::max(0.f,v.MinSize.X*scale),v.MaxSize.X*scale));
        size.Y=std::clamp(size.Y,std::max(0.f,v.MinSize.Y*scale),std::max(std::max(0.f,v.MinSize.Y*scale),v.MaxSize.Y*scale));
        GuiBounds resolved{{bounds.Position.X+bounds.Size.X*v.AnchorMin.X+v.Position.X*scale-v.Pivot.X*size.X,
                            bounds.Position.Y+bounds.Size.Y*v.AnchorMin.Y+v.Position.Y*scale-v.Pivot.Y*size.Y},size};
        if(auto placed=flow.find(child.GetUUID());placed!=flow.end())resolved=placed->second;
        if(!Finite(resolved.Position)||!Finite(resolved.Size))continue;
        Items.push_back({child,root,resolved,clip,surface,scale,Items.size()});
        Descend(child,root,resolved,v.ClipChildren?Intersect(clip,resolved):clip,surface,scale,depth+1);
    }
}
void GuiSystem::ModuleLayout(Scene& scene,Vec2 size){
    Items.clear();
    if(!Finite(size)||size.X<=0||size.Y<=0){Interactions.clear();Focused=Hovered=Captured={};return;}
    PresentationSize=size;
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
