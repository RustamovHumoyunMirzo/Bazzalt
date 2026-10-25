#include "GUI/GuiRendererModule.h"
#include "GUI/GuiSystem.h"
#include "GUI/FontFace.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Console.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include "Bazzalt/Components/Camera.h"
#include "gui_filamat.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/RenderTarget.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <functional>

namespace Bazzalt::Runtime {
struct GuiRendererModule::Impl {
    struct Vertex { float X,Y,Z,U,V,R,G,B,A,LX,LY; };
    struct PaintState {int Mode=0;GuiBounds Bounds{};Vec4 RadiusX{},RadiusY{},Widths{};std::array<Vec4,4> BorderColors{};GuiGradient Gradient;GuiFilter Filter;filament::Texture* Original=nullptr;};
    struct Batch {
        filament::Texture* Texture=nullptr;
        UUID Owner{},CustomMaterial{};Vec4 SourceRect{0,0,1,1};PaintState Paint;
        std::vector<Vertex> Data;
        utils::Entity Entity;
        filament::MaterialInstance* Material=nullptr;
        filament::VertexBuffer* Vertices=nullptr;
        filament::IndexBuffer* Indices=nullptr;
        std::size_t Capacity=0;
    };
    struct Canvas {filament::Texture* Color=nullptr;filament::RenderTarget* Target=nullptr;};
    struct Surface {filament::Scene* Scene=nullptr;filament::View* View=nullptr;filament::Camera* Camera=nullptr;utils::Entity CameraEntity;std::vector<Batch> Batches;
        Canvas Input,Base;
        std::vector<std::unique_ptr<Surface>> Passes;std::vector<Canvas> Canvases;std::vector<std::pair<Surface*,bool>> Plan;std::size_t PassCount=0,CanvasCount=0;Vec2 Size{},RasterSize{};filament::RenderTarget* SceneInput=nullptr;bool Effects=false,Failed=false,ReportedFailure=false;Surface* SceneClear=nullptr;};
    RenderBackend& Backend;
    filament::Engine& Engine;
    filament::Material* Material=nullptr;
    filament::Texture* Atlas=nullptr;
    std::vector<Batch> Spatial;
    std::unordered_map<UUID,Surface> Surfaces;
    std::unordered_map<UUID,Surface> SpatialSurfaces;
    std::unordered_set<UUID> ReportedMaterials;
    PaintState ActivePaint;bool ForceBatch=false,FlatSpatial=false;
    struct AtlasGlyph {FontGlyph Metrics;Vec4 UV{};bool Baked=false;};
    struct FontPage {
        FontFace Face;filament::Texture* Texture=nullptr;
        std::vector<std::uint8_t> Pixels=std::vector<std::uint8_t>(1024*1024*4);
        std::unordered_map<std::uint32_t,AtlasGlyph> Glyphs;
        std::uint32_t X=1,Y=1,Row=0;bool Dirty=false;
    };
    std::unordered_map<std::string,std::unique_ptr<FontPage>> Fonts;
    std::unordered_set<filament::Texture*> UsedFonts;
    bool FontReferenced(filament::Texture* texture)const{
        for(const auto& b:Spatial)if(b.Texture==texture)return true;
        std::function<bool(const Surface&)> references=[&](const Surface& s){for(const auto& b:s.Batches)if(b.Texture==texture)return true;for(const auto& pass:s.Passes)if(references(*pass))return true;return false;};
        for(const auto& [id,s]:Surfaces)if(references(s))return true;
        for(const auto& [id,s]:SpatialSurfaces)if(references(s))return true;
        return false;
    }
    explicit Impl(RenderBackend& backend):Backend(backend),Engine(backend.GetEngine()){
        Material=filament::Material::Builder().package(Embedded::GuiFilamat,sizeof(Embedded::GuiFilamat)).build(Engine);
        BuildAtlas();
    }
    void InitSurface(Surface& s){if(s.View)return;s.Scene=Engine.createScene();s.View=Engine.createView();s.CameraEntity=Engine.getEntityManager().create();s.Camera=Engine.createCamera(s.CameraEntity);s.View->setScene(s.Scene);s.View->setCamera(s.Camera);s.View->setPostProcessingEnabled(false);s.View->setBlendMode(filament::View::BlendMode::TRANSLUCENT);s.View->setVisibleLayers(0xff,0x40);s.Camera->lookAt({0,0,1},{0,0,0},{0,1,0});s.Camera->setExposure(1.0f);}
    Surface& GetSurface(UUID id){auto& s=Surfaces[id];InitSurface(s);return s;}
    void BuildAtlas(){
        constexpr const char* names="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        constexpr std::array<std::array<unsigned char,7>,36> shapes={{
            {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
            {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
            {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
            {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
            {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
            {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
            {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
            {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
            {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}}};
        auto* pixels=new std::vector<std::uint8_t>(128*64*4,0);
        const auto plot=[&](int code,const std::array<unsigned char,7>& rows){for(int y=0;y<7;++y)for(int x=0;x<5;++x)if(rows[y]&(1<<(4-x))){const int at=(((code/16)*8+y)*128+(code%16)*8+x)*4;for(int c=0;c<4;++c)(*pixels)[at+c]=255;}};
        for(std::size_t i=0;i<shapes.size();++i){plot(names[i],shapes[i]);if(i<26)plot(names[i]+32,shapes[i]);}
        plot('.', {0,0,0,0,0,12,12});plot(',',{0,0,0,0,0,4,8});plot(':',{0,4,4,0,4,4,0});plot('!',{4,4,4,4,4,0,4});plot('?',{14,17,1,2,4,0,4});plot('-',{0,0,0,31,0,0,0});plot('_',{0,0,0,0,0,0,31});plot('/',{1,2,2,4,8,8,16});plot('(',{2,4,8,8,8,4,2});plot(')',{8,4,2,2,2,4,8});plot('*',{0,21,14,31,14,21,0});plot(127,{31,17,17,17,17,17,31});
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)for(int c=0;c<4;++c)(*pixels)[(y*128+x)*4+c]=255;
        Atlas=filament::Texture::Builder().width(128).height(64).levels(1).sampler(filament::Texture::Sampler::SAMPLER_2D).format(filament::Texture::InternalFormat::RGBA8).build(Engine);
        Atlas->setImage(Engine,0,filament::Texture::PixelBufferDescriptor(pixels->data(),pixels->size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){delete static_cast<std::vector<std::uint8_t>*>(user);},pixels));
    }
    void Destroy(Batch& b,filament::Scene& scene){
        scene.remove(b.Entity);Engine.destroy(b.Entity);Engine.getEntityManager().destroy(b.Entity);
        if(b.Material)Engine.destroy(b.Material);if(b.Vertices)Engine.destroy(b.Vertices);if(b.Indices)Engine.destroy(b.Indices);
    }
    void ClearSurface(Surface& s){for(auto& pass:s.Passes)ClearSurface(*pass);s.Passes.clear();for(auto& b:s.Batches)Destroy(b,*s.Scene);s.Batches.clear();if(s.View)Engine.destroy(s.View);if(s.Camera){Engine.destroyCameraComponent(s.CameraEntity);Engine.getEntityManager().destroy(s.CameraEntity);}if(s.Scene)Engine.destroy(s.Scene);for(auto& c:s.Canvases){Engine.destroy(c.Target);Engine.destroy(c.Color);}s.Canvases.clear();if(s.Input.Target)Engine.destroy(s.Input.Target);if(s.Input.Color)Engine.destroy(s.Input.Color);s.Input={};if(s.Base.Target)Engine.destroy(s.Base.Target);if(s.Base.Color)Engine.destroy(s.Base.Color);s.Base={};s.SceneInput=nullptr;s.View=nullptr;s.Scene=nullptr;s.Camera=nullptr;s.Plan.clear();s.SceneClear=nullptr;}
    void Clear(){for(auto& b:Spatial)Destroy(b,Backend.GetScene());Spatial.clear();for(auto& [id,s]:SpatialSurfaces)ClearSurface(s);SpatialSurfaces.clear();for(auto& [id,s]:Surfaces)ClearSurface(s);Surfaces.clear();for(auto& [key,font]:Fonts)if(font->Texture)Engine.destroy(font->Texture);Fonts.clear();}
    ~Impl(){Clear();Engine.destroy(Atlas);Engine.destroy(Material);}
    void Quad(std::vector<Batch>& batches,const GuiItem& item,GuiBounds rect,Vec4 color,filament::Texture* texture,Vec4 uv,bool spatial){
        if(!texture||!std::isfinite(color.X)||!std::isfinite(color.Y)||!std::isfinite(color.Z)||!std::isfinite(color.W)||color.W<=0||rect.Size.X<=0||rect.Size.Y<=0)return;
        const float left=std::max(rect.Position.X,item.Clip.Position.X),top=std::max(rect.Position.Y,item.Clip.Position.Y);
        const float right=std::min(rect.Position.X+rect.Size.X,item.Clip.Position.X+item.Clip.Size.X),bottom=std::min(rect.Position.Y+rect.Size.Y,item.Clip.Position.Y+item.Clip.Size.Y);
        if(left>=right||top>=bottom)return;
        if(ForceBatch||batches.empty()||batches.back().Texture!=texture||batches.back().Owner!=item.Owner.GetUUID()||ActivePaint.Mode!=0||batches.back().Paint.Mode!=0){Batch batch;batch.Texture=texture;batch.Owner=item.Owner.GetUUID();batch.Paint=ActivePaint;batches.push_back(std::move(batch));ForceBatch=false;}
        auto& b=batches.back();if(b.Data.size()>400000)return;
        const float u0=uv.X+(uv.Z-uv.X)*(left-rect.Position.X)/rect.Size.X,u1=uv.X+(uv.Z-uv.X)*(right-rect.Position.X)/rect.Size.X;
        const float v0=uv.Y+(uv.W-uv.Y)*(top-rect.Position.Y)/rect.Size.Y,v1=uv.Y+(uv.W-uv.Y)*(bottom-rect.Position.Y)/rect.Size.Y;
        const auto vertex=[&](float x,float y,float u,float v){Vec3 point{x,-y,0};if(spatial&&!FlatSpatial){const auto& frame=item.Root.GetComponent<Frame>();point={(x-frame.ReferenceSize.X*.5f)/frame.PixelsPerUnit,(frame.ReferenceSize.Y*.5f-y)/frame.PixelsPerUnit,0};point=item.Root.GetWorldMatrix().TransformPoint(point);}return Vertex{point.X,point.Y,point.Z,u,v,color.X,color.Y,color.Z,std::clamp(color.W,0.f,1.f),(x-ActivePaint.Bounds.Position.X)/std::max(1.f,ActivePaint.Bounds.Size.X),(y-ActivePaint.Bounds.Position.Y)/std::max(1.f,ActivePaint.Bounds.Size.Y)};};
        b.Data.push_back(vertex(left,top,u0,v0));b.Data.push_back(vertex(left,bottom,u0,v1));b.Data.push_back(vertex(right,top,u1,v0));
        b.Data.push_back(vertex(right,top,u1,v0));b.Data.push_back(vertex(left,bottom,u0,v1));b.Data.push_back(vertex(right,bottom,u1,v1));
    }
    FontPage* FontAtlas(UUID asset,std::uint32_t faceIndex,std::uint32_t pixels){
        if(!asset)return nullptr;auto info=AssetManager::GetAsset(asset);if(!info||info->State!=AssetState::Ready||info->Importer!="Bazzalt.Font")return nullptr;
        auto path=info->CachePath.u8string();const auto key=asset.ToString()+":"+std::to_string(faceIndex)+":"+std::to_string(pixels)+":"+std::string(reinterpret_cast<const char*>(path.data()),path.size());
        if(auto found=Fonts.find(key);found!=Fonts.end()){UsedFonts.insert(found->second->Texture);return found->second.get();}
        if(Fonts.size()>=16){auto unused=std::find_if(Fonts.begin(),Fonts.end(),[&](const auto& pair){return !UsedFonts.contains(pair.second->Texture)&&!FontReferenced(pair.second->Texture);});if(unused==Fonts.end())return nullptr;Engine.destroy(unused->second->Texture);Fonts.erase(unused);}
        auto page=std::make_unique<FontPage>();std::string error;if(!page->Face.Load(info->CachePath,faceIndex,error)||!page->Face.SetSize(pixels))return nullptr;
        page->Texture=filament::Texture::Builder().width(1024).height(1024).levels(1).sampler(filament::Texture::Sampler::SAMPLER_2D).format(filament::Texture::InternalFormat::RGBA8).build(Engine);
        page->Dirty=true;auto* result=page.get();UsedFonts.insert(page->Texture);Fonts.emplace(key,std::move(page));return result;
    }
    const AtlasGlyph& Glyph(FontPage& page,std::uint32_t index){
        if(auto found=page.Glyphs.find(index);found!=page.Glyphs.end())return found->second;
        if(page.Glyphs.size()>=8192){static const AtlasGlyph fallback{FontGlyph{0,0,0,0,0,8},Vec4{},false};return fallback;}
        AtlasGlyph glyph;glyph.Metrics=page.Face.RenderGlyph(index);const auto width=glyph.Metrics.Width,height=glyph.Metrics.Height;
        if(width==0||height==0)glyph.Baked=glyph.Metrics.Index!=0;
        else {
            if(page.X+width+1>=1024){page.X=1;page.Y+=page.Row+1;page.Row=0;}
            if(page.Y+height+1<1024){
                for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){const auto at=((page.Y+y)*1024+page.X+x)*4;page.Pixels[at]=page.Pixels[at+1]=page.Pixels[at+2]=255;page.Pixels[at+3]=glyph.Metrics.Alpha[y*width+x];}
                glyph.UV={float(page.X)/1024,float(page.Y)/1024,float(page.X+width)/1024,float(page.Y+height)/1024};glyph.Baked=true;page.X+=width+1;page.Row=std::max(page.Row,height);page.Dirty=true;
            }
        }
        std::vector<std::uint8_t>().swap(glyph.Metrics.Alpha);return page.Glyphs.emplace(index,std::move(glyph)).first->second;
    }
    void FlushFonts(){for(auto& [key,page]:Fonts)if(page->Dirty){auto* copy=new std::vector<std::uint8_t>(page->Pixels);page->Texture->setImage(Engine,0,filament::Texture::PixelBufferDescriptor(copy->data(),copy->size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){delete static_cast<std::vector<std::uint8_t>*>(user);},copy));page->Dirty=false;}}
    void Text(std::vector<Batch>& batches,GuiItem item,const std::string& text,float size,Vec4 color,bool wrap,bool spatial,UUID font={},std::uint32_t face=0,float letterSpacing=0,float lineSpacing=1,bool kerning=true,GuiTextAlignment horizontal=GuiTextAlignment::Start,GuiTextAlignment vertical=GuiTextAlignment::Start){
        if(!std::isfinite(size)||size<=0)return;
        if(const auto* rect=EffectiveGui<RectTransform>(item.Owner)){const auto p=rect->Padding*item.Scale;item.Bounds.Position=item.Bounds.Position+Vec2{p.X,p.Y};item.Bounds.Size={std::max(0.f,item.Bounds.Size.X-p.X-p.Z),std::max(0.f,item.Bounds.Size.Y-p.Y-p.W)};item.Clip=Intersect(item.Clip,item.Bounds);}
        if(auto* page=FontAtlas(font,face,std::uint32_t(std::ceil(std::clamp(size*item.Scale,1.f,512.f))))){
            const auto shaped=page->Face.Layout(text,wrap?item.Bounds.Size.X:0,letterSpacing*item.Scale,lineSpacing,kerning);
            for(const auto& placed:shaped.Glyphs){
                if(placed.Y>=item.Bounds.Size.Y)continue;
                const auto& glyph=Glyph(*page,placed.Index);
                const auto align=[](GuiTextAlignment a,float remaining){return std::max(0.f,remaining)*(a==GuiTextAlignment::Center?.5f:a==GuiTextAlignment::End?1.f:0.f);};
                const float x=item.Bounds.Position.X+placed.X+align(horizontal,item.Bounds.Size.X-shaped.LineWidths[placed.Line]),y=item.Bounds.Position.Y+placed.Y+align(vertical,item.Bounds.Size.Y-shaped.Size.Y);
                if(glyph.Baked)Quad(batches,item,{{x+glyph.Metrics.Left,y+page->Face.GetAscender()-glyph.Metrics.Top},{float(glyph.Metrics.Width),float(glyph.Metrics.Height)}},color,page->Texture,glyph.UV,spatial);
                else Quad(batches,item,{{x,y},{size*.625f*item.Scale,size*.875f*item.Scale}},color,Atlas,{120.f/128,56.f/64,125.f/128,63.f/64},spatial);
            }return;
        }
        if(!std::isfinite(size)||size<=0)return;size=std::clamp(size*item.Scale,1.f,512.f);const float advance=std::max(.01f,size*.75f+letterSpacing*item.Scale),lineHeight=size*std::clamp(lineSpacing,.1f,10.f);
        std::vector<std::string> lines(1);const auto capacity=std::max(1,int(item.Bounds.Size.X/advance));
        for(std::size_t offset=0;offset<text.size();){unsigned char c=text[offset++];if(c&0x80){while(offset<text.size()&&(static_cast<unsigned char>(text[offset])&0xc0)==0x80)++offset;c=127;}if(c=='\n'){lines.emplace_back();continue;}if(c<32)continue;if(wrap&&lines.back().size()>=capacity)lines.emplace_back();lines.back()+=char(c);}
        const auto align=[](GuiTextAlignment a,float remaining){return std::max(0.f,remaining)*(a==GuiTextAlignment::Center?.5f:a==GuiTextAlignment::End?1.f:0.f);};
        float y=item.Bounds.Position.Y+align(vertical,item.Bounds.Size.Y-lines.size()*lineHeight);
        for(const auto& line:lines){float x=item.Bounds.Position.X+align(horizontal,item.Bounds.Size.X-line.size()*advance);for(unsigned char c:line){if(c!=' ')Quad(batches,item,{{x,y},{size*.625f,size*.875f}},color,Atlas,{float((c%16)*8)/128,float((c/16)*8)/64,float((c%16)*8+5)/128,float((c/16)*8+7)/64},spatial);x+=advance;}y+=lineHeight;if(y>=item.Bounds.Position.Y+item.Bounds.Size.Y)break;}

    }
    void Build(Bazzalt::Scene& scene,std::vector<Batch>& output,bool spatial,UUID camera={}){
        UsedFonts.clear();
        auto& system=scene.GetSystem<GuiSystem>();
        for(const auto& item:system.Items){
            if((item.Root.GetComponent<Frame>().Mode==FrameMode::Spatial)!=spatial)continue;
            const auto& frame=item.Root.GetComponent<Frame>();
            if(camera&&(frame.Mode!=FrameMode::CameraBound||frame.Camera!=camera))continue;
            if(!camera&&!spatial&&frame.Mode==FrameMode::CameraBound){const auto owner=scene.GetEntity(frame.Camera);const auto* source=owner.TryGetComponent<Camera>();const auto* target=owner.TryGetComponent<CameraRenderTarget>();if((source&&source->RenderTarget)||(target&&target->Enabled))continue;}
            if(std::none_of(output.begin(),output.end(),[&](const Batch& b){return b.Owner==item.Root.GetUUID();})){auto computed=system.Computed.find(item.Root.GetUUID());const auto* element=EffectiveGui<GuiElement>(item.Root);if((element&&element->Enabled&&element->MaterialAsset&&element->MaterialIncludesChildren)||(computed!=system.Computed.end()&&(!computed->second.Paint.Filters.empty()||!computed->second.Paint.Backdrop.empty()))){Batch marker;marker.Texture=Atlas;marker.Owner=item.Root.GetUUID();output.push_back(std::move(marker));}}
            const auto state=GUI::GetInteraction(item.Owner);auto bounds=item.Bounds;
            const auto computed=system.Computed.find(item.Owner.GetUUID());
            GuiPaint paint;if(computed!=system.Computed.end())paint=computed->second.Paint;
            ActivePaint={};ActivePaint.Bounds=item.Bounds;ActivePaint.RadiusX=paint.RadiusX*item.Scale;ActivePaint.RadiusY=paint.RadiusY*item.Scale;ForceBatch=true;
            const bool painted=computed!=system.Computed.end()&&(computed->second.Cascade.Properties.contains("background")||computed->second.Cascade.Properties.contains("background-color")||computed->second.Cascade.Properties.contains("background-image")||paint.HasBorder);
            if(const auto* r=item.Owner.TryGetComponent<Rectangle>();r?r->Enabled:painted){
                const auto style=GUI::ResolveStyle(item.Owner);
                Vec4 color=style.Color;
                if(!r&&computed!=system.Computed.end()&&!computed->second.Cascade.Properties.contains("background-color"))color={0,0,0,0};
                const auto* button=EffectiveGui<GuiButton>(item.Owner);const auto* input=EffectiveGui<GuiTextInput>(item.Owner);
                if((button&&(!button->Enabled||!button->Interactable))||(input&&(!input->Enabled||!input->Interactable)))color=style.DisabledColor;
                else if(state.Pressed)color=style.PressedColor;else if(state.Hovered||state.Focused)color=style.HoverColor;
                const float opacity=std::isfinite(style.Opacity)?std::clamp(style.Opacity,0.f,1.f):0;
                ActivePaint.Mode=1;ActivePaint.Widths=paint.HasBorder?paint.Widths*item.Scale:Vec4{style.BorderWidth,style.BorderWidth,style.BorderWidth,style.BorderWidth}*item.Scale;
                ActivePaint.BorderColors.fill(style.BorderColor);
                if(computed!=system.Computed.end()){const char* sides[]={"left","top","right","bottom"};for(int side=0;side<4;++side)if(computed->second.Cascade.Properties.contains("border-color")||computed->second.Cascade.Properties.contains("border")||computed->second.Cascade.Properties.contains(std::string("border-")+sides[side]+"-color"))ActivePaint.BorderColors[side]=paint.BorderColors[side];}
                for(auto& edge:ActivePaint.BorderColors)edge.W*=opacity;
                color.W*=opacity;Quad(output,item,bounds,color,Atlas,{.02f,.04f,.02f,.04f},spatial);
                ActivePaint.BorderColors={};
                for(auto gradient=paint.Gradients.rbegin();gradient!=paint.Gradients.rend();++gradient){ActivePaint.Gradient=*gradient;Quad(output,item,bounds,{1,1,1,opacity},Atlas,{.02f,.04f,.02f,.04f},spatial);}
                ActivePaint.Gradient={};ActivePaint.Mode=0;
                if(computed!=system.Computed.end()&&computed->second.BackgroundTexture)Quad(output,item,bounds,{1,1,1,opacity},Backend.GetAssets().GetGuiTexture(computed->second.BackgroundTexture),{0,0,1,1},spatial);
            }
            ActivePaint.Widths={};
            if(const auto* source=item.Owner.TryGetComponent<GuiImage>();source&&source->Enabled){auto image=GUI::ResolveImage(item.Owner);if(computed!=system.Computed.end()&&computed->second.Cascade.Properties.contains("opacity"))image.Color.W*=computed->second.Style.Opacity;auto* texture=image.Camera?Backend.GetCameraTexture(image.Camera):Backend.GetAssets().GetGuiTexture(image.Texture);Quad(output,item,item.Bounds,image.Color,texture,{0,0,1,1},spatial);}
            ActivePaint.RadiusX=ActivePaint.RadiusY={};
            if(const auto* text=item.Owner.TryGetComponent<GuiText>();text&&text->Enabled){
                auto clipped=item;clipped.Clip=Intersect(item.Clip,item.Bounds);
                auto resolved=GUI::ResolveText(item.Owner);if(computed!=system.Computed.end()&&computed->second.Cascade.Properties.contains("opacity"))resolved.Color.W*=computed->second.Style.Opacity;Text(output,clipped,resolved.Value,resolved.FontSize,resolved.Color,resolved.Wrap,spatial,resolved.Font,resolved.FontFace,resolved.LetterSpacing,resolved.LineSpacing,resolved.Kerning,resolved.HorizontalAlignment,resolved.VerticalAlignment);
            }
            if(const auto* input=EffectiveGui<GuiTextInput>(item.Owner);input&&input->Enabled){auto clipped=item;clipped.Clip=Intersect(item.Clip,item.Bounds);const auto& local=item.Owner.GetComponent<GuiTextInput>();std::string value=local.Value.empty()?local.Placeholder:local.Value;if(input->Password&&!local.Value.empty()){value.clear();for(unsigned char c:local.Value)if((c&0xc0)!=0x80)value+='*';}Vec4 color=computed!=system.Computed.end()?computed->second.Text.Color:Vec4{1,1,1,1};if(local.Value.empty())color.W*=.5f;if(computed!=system.Computed.end()&&computed->second.Cascade.Properties.contains("opacity"))color.W*=computed->second.Style.Opacity;Text(output,clipped,value,input->FontSize,color,false,spatial,input->Font,input->FontFace,0,1,true,input->HorizontalAlignment,input->VerticalAlignment);}
            if(output.empty()||output.back().Owner!=item.Owner.GetUUID()){Batch marker;marker.Texture=Atlas;marker.Owner=item.Owner.GetUUID();output.push_back(std::move(marker));}
        }
        FlushFonts();
    }
    static GuiBounds Intersect(GuiBounds a,GuiBounds b){Vec2 begin{std::max(a.Position.X,b.Position.X),std::max(a.Position.Y,b.Position.Y)};return {begin,{std::max(0.f,std::min(a.Position.X+a.Size.X,b.Position.X+b.Size.X)-begin.X),std::max(0.f,std::min(a.Position.Y+a.Size.Y,b.Position.Y+b.Size.Y)-begin.Y)}};}
    Canvas NewCanvas(Surface& surface){
        if(surface.CanvasCount>=128){surface.Failed=true;return {};}
        if(surface.CanvasCount==surface.Canvases.size()){
            Canvas canvas;canvas.Color=filament::Texture::Builder().width(std::uint32_t(surface.RasterSize.X)).height(std::uint32_t(surface.RasterSize.Y)).levels(1).sampler(filament::Texture::Sampler::SAMPLER_2D).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::SAMPLEABLE).build(Engine);
            canvas.Target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,canvas.Color).build(Engine);surface.Canvases.push_back(canvas);
        }return surface.Canvases[surface.CanvasCount++];
    }
    void Pass(Surface& owner,Canvas canvas,std::vector<Batch> data,bool clear){
        if(!canvas.Target||owner.PassCount>=256){owner.Failed=true;return;}
        if(owner.PassCount==owner.Passes.size())owner.Passes.push_back(std::make_unique<Surface>());
        auto& pass=*owner.Passes[owner.PassCount++];InitSurface(pass);Upload(pass.Batches,data,*pass.Scene,false);
        pass.Camera->setProjection(filament::Camera::Projection::ORTHO,0,owner.Size.X,-owner.Size.Y,0,.1,10);pass.View->setViewport({0,0,std::uint32_t(canvas.Color->getWidth()),std::uint32_t(canvas.Color->getHeight())});pass.View->setRenderTarget(canvas.Target);owner.Plan.emplace_back(&pass,clear);
    }
    std::vector<Batch> TextureQuad(GuiItem item,filament::Texture* texture,GuiBounds rect,PaintState paint,Vec2 fullSize){
        std::vector<Batch> data;ActivePaint=std::move(paint);ActivePaint.Bounds=rect;ForceBatch=true;
        Quad(data,item,rect,{1,1,1,1},texture,{rect.Position.X/fullSize.X,rect.Position.Y/fullSize.Y,(rect.Position.X+rect.Size.X)/fullSize.X,(rect.Position.Y+rect.Size.Y)/fullSize.Y},false);ActivePaint={};return data;
    }
    Canvas FilterChain(Surface& surface,GuiItem item,Canvas input,const std::vector<GuiFilter>& filters){
        const auto* initial=input.Color;
        item.Clip={{0,0},surface.Size};
        for(auto filter:filters){const auto original=input.Color;filter.Amount*=filter.Kind==9||filter.Kind==10?item.Scale:1;
            if(filter.Kind==9||filter.Kind==10){
                if(filter.Amount>0)for(int axis=0;axis<2;++axis){auto next=NewCanvas(surface);PaintState paint;paint.Mode=2;paint.Filter={9,filter.Amount,axis==0?Vec2{1/surface.Size.X,0}:Vec2{0,1/surface.Size.Y}};Pass(surface,next,TextureQuad(item,input.Color,{{0,0},surface.Size},paint,surface.Size),true);input=next;}
                if(filter.Kind==9)continue;
            }
            auto next=NewCanvas(surface);PaintState paint;paint.Mode=2;paint.Filter=filter;
            if(filter.Kind==10){paint.Original=original;paint.Filter.Offset={filter.Offset.X*item.Scale/surface.Size.X,-filter.Offset.Y*item.Scale/surface.Size.Y};}
            Pass(surface,next,TextureQuad(item,input.Color,{{0,0},surface.Size},paint,surface.Size),true);input=next;
            if(surface.Failed)break;
        }
        // Even an identity chain (blur(0)) must not sample the accumulation
        // texture while compositing into that same attachment.
        if(!filters.empty()&&input.Color==initial){auto next=NewCanvas(surface);PaintState copy;copy.Mode=2;Pass(surface,next,TextureQuad(item,input.Color,{{0,0},surface.Size},copy,surface.Size),true);input=next;}
        return input;
    }
    Canvas ApplyCanvasMaterial(Surface& surface,GuiItem item,Canvas input,UUID material){
        if(!material)return input;
        auto* validation=Backend.GetAssets().CreateCanvasMaterial(material);if(!validation){if(ReportedMaterials.size()>=128)ReportedMaterials.clear();if(ReportedMaterials.insert(material).second)Console::Warning("Canvas material is unavailable or is not an unlit surface shader with source/sourceRect and compatible vertex attributes: "+material.ToString(),"GUI");return input;}Engine.destroy(validation);ReportedMaterials.erase(material);
        auto output=NewCanvas(surface);PaintState paint;paint.Mode=2;auto batches=TextureQuad(item,input.Color,item.Bounds,paint,surface.Size);
        for(auto& batch:batches){batch.CustomMaterial=material;batch.SourceRect={item.Bounds.Position.X/surface.Size.X,item.Bounds.Position.Y/surface.Size.Y,item.Bounds.Size.X/surface.Size.X,item.Bounds.Size.Y/surface.Size.Y};for(auto& vertex:batch.Data){vertex.U=vertex.LX;vertex.V=vertex.LY;}}
        Pass(surface,output,std::move(batches),true);return output;
    }
    bool PrepareEffects(Surface& surface,Bazzalt::Scene& scene,Vec2 size,std::vector<Batch>& data,filament::RenderTarget* target,bool spatial=false,Entity spatialRoot={},UUID cameraId={}){
        auto& system=scene.GetSystem<GuiSystem>();std::map<UUID,GuiItem> items;std::map<UUID,GuiPaint> paints;std::vector<UUID> order;std::size_t estimate=0;bool effects=false,backdrop=false;
        for(const auto& batch:data)if(batch.Owner&&!items.contains(batch.Owner)){
            auto found=std::find_if(system.Items.begin(),system.Items.end(),[&](const GuiItem& item){return item.Owner.GetUUID()==batch.Owner||item.Root.GetUUID()==batch.Owner;});if(found==system.Items.end())continue;
            auto entry=*found;if(entry.Owner.GetUUID()!=batch.Owner){entry.Owner=entry.Root;entry.Bounds=entry.Clip=entry.Surface;}
            items[batch.Owner]=entry;order.push_back(batch.Owner);auto computed=system.Computed.find(batch.Owner);GuiPaint paint;if(computed!=system.Computed.end())paint=computed->second.Paint;
            const auto* rect=EffectiveGui<RectTransform>(entry.Owner);const bool clip=paint.HasRadius&&rect&&rect->ClipChildren;
            if(const auto* element=EffectiveGui<GuiElement>(entry.Owner);element&&element->Enabled&&element->MaterialAsset){effects=true;estimate+=2;}
            if(!paint.Filters.empty()||!paint.Backdrop.empty()||clip){effects=true;if(!paint.Filters.empty()||clip)++estimate;const auto cost=[](const GuiFilter& filter){return filter.Kind==9?(filter.Amount>0?2u:1u):filter.Kind==10?(filter.Amount>0?3u:1u):1u;};for(const auto& filter:paint.Filters)estimate+=cost(filter);for(const auto& filter:paint.Backdrop)estimate+=cost(filter);}backdrop|=!paint.Backdrop.empty();paints[batch.Owner]=std::move(paint);
        }
        std::vector<Entity> cameras;
        if(!spatial)for(auto handle:scene.GetRegistry().view<Bazzalt::Camera>()){auto entity=scene.GetEntity(static_cast<Entity::Id>(handle));const auto& camera=entity.GetComponent<Bazzalt::Camera>();if(!camera.Enabled||!camera.Active||!camera.PostProcessing.Enabled||!camera.PostProcessing.MaterialAsset)continue;if(cameraId?entity.GetUUID()!=cameraId:bool(camera.RenderTarget)||entity.HasComponent<CameraRenderTarget>())continue;cameras.push_back(entity);}
        std::stable_sort(cameras.begin(),cameras.end(),[](Entity a,Entity b){return a.GetComponent<Bazzalt::Camera>().Priority<b.GetComponent<Bazzalt::Camera>().Priority;});
        if(!cameras.empty()){effects=true;backdrop=true;estimate+=cameras.size()*2;}
        if(!effects||(order.empty()&&cameras.empty())){if(surface.Effects){ClearSurface(surface);InitSurface(surface);}surface.Effects=false;surface.SceneInput=nullptr;surface.SceneClear=nullptr;return false;}
        if(!std::isfinite(size.X)||!std::isfinite(size.Y)||size.X<1||size.Y<1||size.X>16384||size.Y>16384||double(size.X)*size.Y>33554432){
            const std::string warning="GUI CSS effect destination exceeds the 16384-edge / 32M-pixel safety limit";if(system.StyleDiagnostics.size()<64)system.StyleDiagnostics.push_back(warning);if(!surface.ReportedFailure)Console::Warning(warning,"GUI");surface.ReportedFailure=true;surface.Failed=true;surface.Plan.clear();surface.SceneInput=nullptr;surface.SceneClear=nullptr;return true;
        }
        const float reduction=std::min({1.f,4096.f/std::max(size.X,size.Y),std::sqrt(16777216.f/(std::max(1.f,size.X*size.Y)*float(std::clamp<std::size_t>(estimate,1,128))))});
        Vec2 raster{std::max(1.f,std::floor(size.X*reduction)),std::max(1.f,std::floor(size.Y*reduction))};
        if(surface.RasterSize.X!=raster.X||surface.RasterSize.Y!=raster.Y||surface.Size.X!=size.X||surface.Size.Y!=size.Y){ClearSurface(surface);InitSurface(surface);}
        surface.Size=size;surface.RasterSize=raster;surface.Effects=true;surface.SceneInput=nullptr;surface.PassCount=surface.CanvasCount=0;surface.Plan.clear();surface.Failed=false;surface.SceneClear=nullptr;
        std::map<UUID,std::vector<Batch>> groups;for(auto& batch:data)groups[batch.Owner].push_back(std::move(batch));
        GuiItem item;if(!order.empty())item=items.at(order[0]);else{item.Owner=item.Root=cameras.front();item.Bounds=item.Surface={{0,0},size};}item.Clip={{0,0},size};item.Scale=1;
        const auto fullCanvas=[&](Canvas& canvas){if(!canvas.Color){canvas.Color=filament::Texture::Builder().width(std::uint32_t(size.X)).height(std::uint32_t(size.Y)).levels(1).sampler(filament::Texture::Sampler::SAMPLER_2D).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::SAMPLEABLE).build(Engine);canvas.Target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,canvas.Color).build(Engine);}return canvas;};
        auto base=fullCanvas(surface.Base);std::vector<Batch> initial;
        if(backdrop&&!spatial){filament::Texture* source=nullptr;if(target)source=const_cast<filament::Texture*>(target->getTexture(filament::RenderTarget::AttachmentPoint::COLOR));else {fullCanvas(surface.Input);source=surface.Input.Color;surface.SceneInput=surface.Input.Target;Pass(surface,surface.Input,{},true);surface.SceneClear=surface.Plan.back().first;surface.SceneClear->View->setViewport({0,0,std::uint32_t(size.X),std::uint32_t(size.Y)});surface.Plan.pop_back();}PaintState paint;paint.Mode=2;initial=TextureQuad(item,source,{{0,0},size},paint,size);}
        Pass(surface,base,std::move(initial),true);
        for(auto cameraEntity:cameras){const auto& camera=cameraEntity.GetComponent<Bazzalt::Camera>();auto region=item;region.Owner=cameraEntity;region.Bounds=cameraId?GuiBounds{{0,0},size}:GuiBounds{{camera.Viewport.X*size.X,(1-camera.Viewport.Y-camera.Viewport.Height)*size.Y},{camera.Viewport.Width*size.X,camera.Viewport.Height*size.Y}};region.Clip={{0,0},size};auto filtered=ApplyCanvasMaterial(surface,region,base,camera.PostProcessing.MaterialAsset);if(filtered.Color!=base.Color){PaintState mask;mask.Mode=3;Pass(surface,base,TextureQuad(region,filtered.Color,region.Bounds,mask,size),false);}}

        const auto descendant=[&](UUID identity,UUID parent){for(auto e=scene.GetEntity(identity);e;e=e.GetParent()){if(e.GetUUID()==parent)return true;if(e.HasComponent<Frame>())break;}return false;};
        const auto clips=[&](UUID identity){const auto* rect=EffectiveGui<RectTransform>(items.at(identity).Owner);return rect&&rect->ClipChildren;};
        const auto grouped=[&](UUID identity){const auto& p=paints.at(identity);const auto* element=EffectiveGui<GuiElement>(items.at(identity).Owner);return !p.Filters.empty()||(p.HasRadius&&clips(identity))||(element&&element->Enabled&&element->MaterialAsset&&element->MaterialIncludesChildren);};
        std::function<void(Canvas,const std::vector<UUID>&,UUID,unsigned)> render;
        render=[&](Canvas destination,const std::vector<UUID>& list,UUID context,unsigned depth){
            if(depth>16){surface.Failed=true;return;}std::set<UUID> done;std::vector<Batch> pending;
            auto flush=[&]{if(!pending.empty()){Pass(surface,destination,std::move(pending),false);pending.clear();}};
            for(auto identity:list){if(done.contains(identity))continue;UUID group{};for(auto e=scene.GetEntity(identity);e&&e.GetUUID()!=context;e=e.GetParent()){if(items.contains(e.GetUUID())&&grouped(e.GetUUID()))group=e.GetUUID();if(e.HasComponent<Frame>())break;}
                if(group){flush();std::vector<UUID> members;for(auto candidate:list)if(descendant(candidate,group)){members.push_back(candidate);done.insert(candidate);}auto isolated=NewCanvas(surface);Pass(surface,isolated,{},true);
                    const auto& p=paints.at(group);auto owner=items.at(group);
                    if(!p.Backdrop.empty()){auto filtered=FilterChain(surface,owner,destination,p.Backdrop);PaintState mask;mask.Mode=3;mask.RadiusX=p.RadiusX*owner.Scale;mask.RadiusY=p.RadiusY*owner.Scale;Pass(surface,isolated,TextureQuad(owner,filtered.Color,owner.Bounds,mask,size),false);}
                    render(isolated,members,group,depth+1);const auto* element=EffectiveGui<GuiElement>(owner.Owner);if(element&&element->Enabled&&element->MaterialIncludesChildren)isolated=ApplyCanvasMaterial(surface,owner,isolated,element->MaterialAsset);auto filtered=FilterChain(surface,owner,isolated,p.Filters);PaintState mask;mask.Mode=3;GuiBounds rect{{0,0},size};if(clips(group)){rect=owner.Bounds;mask.RadiusX=p.RadiusX*owner.Scale;mask.RadiusY=p.RadiusY*owner.Scale;}Pass(surface,destination,TextureQuad(owner,filtered.Color,rect,mask,size),false);
                }else {const auto& p=paints.at(identity);auto owner=items.at(identity);
                    if(identity!=context&&!p.Backdrop.empty()){flush();auto filtered=FilterChain(surface,owner,destination,p.Backdrop);PaintState mask;mask.Mode=3;mask.RadiusX=p.RadiusX*owner.Scale;mask.RadiusY=p.RadiusY*owner.Scale;Pass(surface,destination,TextureQuad(owner,filtered.Color,owner.Bounds,mask,size),false);}
                    auto found=groups.find(identity);const auto* element=EffectiveGui<GuiElement>(owner.Owner);
                    if(found!=groups.end()&&element&&element->Enabled&&element->MaterialAsset&&!element->MaterialIncludesChildren){flush();auto isolated=NewCanvas(surface);Pass(surface,isolated,std::move(found->second),true);auto filtered=ApplyCanvasMaterial(surface,owner,isolated,element->MaterialAsset);PaintState mask;mask.Mode=3;Pass(surface,destination,TextureQuad(owner,filtered.Color,owner.Bounds,mask,size),false);}
                    else if(found!=groups.end())for(auto& batch:found->second)pending.push_back(std::move(batch));done.insert(identity);
                }if(surface.Failed)return;
            }flush();
        };
        render(base,order,{},0);
        if(surface.Failed){const std::string warning="GUI CSS effect resource limit exceeded (128 targets / 256 passes / 16 nested groups)";if(system.StyleDiagnostics.size()<64)system.StyleDiagnostics.push_back(warning);if(!surface.ReportedFailure)Console::Warning(warning,"GUI");surface.ReportedFailure=true;surface.Plan.clear();data.clear();return true;}
        surface.ReportedFailure=false;
        while(surface.Passes.size()>surface.PassCount){ClearSurface(*surface.Passes.back());surface.Passes.pop_back();}
        while(surface.Canvases.size()>surface.CanvasCount){auto canvas=surface.Canvases.back();Engine.destroy(canvas.Target);Engine.destroy(canvas.Color);surface.Canvases.pop_back();}
        PaintState copy;copy.Mode=2;item.Clip={{0,0},size};data=TextureQuad(item,base.Color,{{0,0},size},copy,size);return true;
    }
    void RenderPlan(Surface& surface){for(const auto& [pass,clear]:surface.Plan){Backend.GetRenderer().setClearOptions({.clearColor={0,0,0,0},.clear=clear,.discard=false});Backend.GetRenderer().render(pass->View);}}
    void Upload(std::vector<Batch>& cache,std::vector<Batch>& generated,filament::Scene& scene,bool spatial){
        while(cache.size()>generated.size()){Destroy(cache.back(),scene);cache.pop_back();}
        while(cache.size()<generated.size()){Batch b;b.Entity=Engine.getEntityManager().create();Engine.getTransformManager().create(b.Entity);b.Material=Material->createInstance();b.Material->setDepthCulling(spatial);cache.push_back(std::move(b));}
        auto& manager=Engine.getRenderableManager();
        for(std::size_t i=0;i<generated.size();++i){auto& b=cache[i];auto& data=generated[i].Data;const auto count=static_cast<std::uint32_t>(data.size());if(!count){scene.remove(b.Entity);continue;}
            if(b.CustomMaterial!=generated[i].CustomMaterial){auto* previous=b.Material;b.CustomMaterial=generated[i].CustomMaterial;b.Material=b.CustomMaterial?Backend.GetAssets().CreateCanvasMaterial(b.CustomMaterial):nullptr;if(!b.Material){b.CustomMaterial={};b.Material=Material->createInstance();}if(manager.hasComponent(b.Entity))manager.setMaterialInstanceAt(manager.getInstance(b.Entity),0,b.Material);if(previous)Engine.destroy(previous);}
            if(b.CustomMaterial&&!Backend.GetAssets().UpdateCanvasMaterial(b.Material,b.CustomMaterial,generated[i].Texture,generated[i].SourceRect)){auto* replacement=Backend.GetAssets().CreateCanvasMaterial(b.CustomMaterial);if(replacement){auto* previous=b.Material;b.Material=replacement;Backend.GetAssets().UpdateCanvasMaterial(b.Material,b.CustomMaterial,generated[i].Texture,generated[i].SourceRect);if(manager.hasComponent(b.Entity))manager.setMaterialInstanceAt(manager.getInstance(b.Entity),0,b.Material);Engine.destroy(previous);}}
            if(!b.CustomMaterial){b.Material->setParameter("image",generated[i].Texture,Backend.GetAssets().GetTextureSampler(generated[i].Texture));b.Texture=generated[i].Texture;
            const auto& paint=generated[i].Paint;auto f4=[](Vec4 v){return filament::math::float4{v.X,v.Y,v.Z,v.W};};
            b.Material->setParameter("original",paint.Original?paint.Original:generated[i].Texture,Backend.GetAssets().GetTextureSampler(paint.Original?paint.Original:generated[i].Texture));
            b.Material->setParameter("mode",float(paint.Mode));b.Material->setParameter("panelSize",filament::math::float2{paint.Bounds.Size.X,paint.Bounds.Size.Y});
            b.Material->setParameter("radiusX",f4(paint.RadiusX));b.Material->setParameter("radiusY",f4(paint.RadiusY));b.Material->setParameter("borderWidths",f4(paint.Widths));
            std::array<filament::math::float4,4> edges;for(int j=0;j<4;++j)edges[j]=f4(paint.BorderColors[j]);b.Material->setParameter("borderColors",edges.data(),edges.size());
            const auto& gradient=paint.Gradient;b.Material->setParameter("gradient",filament::math::float4{float(gradient.Kind),gradient.Angle*3.14159265f/180,float(gradient.Count),gradient.Repeat?1.f:0.f});
            b.Material->setParameter("gradientGeometry",filament::math::float4{gradient.Center.X,gradient.Center.Y,gradient.Radius.X,gradient.Radius.Y});
            std::array<filament::math::float4,8> colors;for(int j=0;j<8;++j)colors[j]=f4(gradient.Colors[j]);b.Material->setParameter("stopColors",colors.data(),colors.size());b.Material->setParameter("stopPositions",gradient.Stops.data(),gradient.Stops.size());
            b.Material->setParameter("effect",filament::math::float4{float(paint.Filter.Kind),paint.Filter.Amount,paint.Filter.Offset.X,paint.Filter.Offset.Y});b.Material->setParameter("effectColor",f4(paint.Filter.Color));}
            if(b.Capacity<count){manager.destroy(b.Entity);if(b.Vertices)Engine.destroy(b.Vertices);if(b.Indices)Engine.destroy(b.Indices);b.Capacity=std::max<std::size_t>(64,count*2);
                b.Vertices=filament::VertexBuffer::Builder().vertexCount(static_cast<std::uint32_t>(b.Capacity)).bufferCount(1)
                    .attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,sizeof(Vertex))
                    .attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,U),sizeof(Vertex))
                    .attribute(filament::VertexAttribute::UV1,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,LX),sizeof(Vertex))
                    .attribute(filament::VertexAttribute::COLOR,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,R),sizeof(Vertex)).build(Engine);
                b.Indices=filament::IndexBuffer::Builder().indexCount(static_cast<std::uint32_t>(b.Capacity)).bufferType(filament::IndexBuffer::IndexType::UINT).build(Engine);
                auto* indices=new std::vector<std::uint32_t>(b.Capacity);for(std::size_t j=0;j<b.Capacity;++j)(*indices)[j]=static_cast<std::uint32_t>(j);
                b.Indices->setBuffer(Engine,{indices->data(),indices->size()*4,[](void*,std::size_t,void* user){delete static_cast<std::vector<std::uint32_t>*>(user);},indices});
                filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{1,1,1}}).material(0,b.Material).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,b.Vertices,b.Indices,0,count).culling(false).castShadows(false).receiveShadows(false).layerMask(0xff,0x40).globalBlendOrderEnabled(0,true).blendOrder(0,static_cast<std::uint16_t>(std::min<std::size_t>(i,32767))).build(Engine,b.Entity);
            }else manager.setGeometryAt(manager.getInstance(b.Entity),0,filament::RenderableManager::PrimitiveType::TRIANGLES,b.Vertices,b.Indices,0,count);
            auto* storage=new std::vector<Vertex>(std::move(data));b.Vertices->setBufferAt(Engine,0,{storage->data(),storage->size()*sizeof(Vertex),[](void*,std::size_t,void* user){delete static_cast<std::vector<Vertex>*>(user);},storage});scene.addEntity(b.Entity);
        }
    }
};
GuiRendererModule::GuiRendererModule(RenderBackend& backend):m_impl(std::make_unique<Impl>(backend)){}
GuiRendererModule::~GuiRendererModule()=default;
void GuiRendererModule::Clear(){m_impl->Clear();}
std::size_t GuiRendererModule::GetBatchCount() const{std::size_t count=m_impl->Spatial.size();for(const auto& [id,s]:m_impl->Surfaces){(void)id;count+=s.Batches.size();}return count;}
void GuiRendererModule::PrepareSpatial(Bazzalt::Scene& scene){
    std::vector<Impl::Batch> all;m_impl->FlatSpatial=true;m_impl->Build(scene,all,true);m_impl->FlatSpatial=false;
    std::map<UUID,std::vector<Impl::Batch>> groups;for(auto& batch:all){auto owner=scene.GetEntity(batch.Owner);auto root=owner.HasComponent<Frame>()?owner:GUI::GetFrame(owner);if(root)groups[root.GetUUID()].push_back(std::move(batch));}
    std::vector<Impl::Batch> data;
    for(auto& [id,items]:groups){auto root=scene.GetEntity(id);auto& surface=m_impl->SpatialSurfaces[id];m_impl->InitSurface(surface);const auto& frame=root.GetComponent<Frame>();m_impl->PrepareEffects(surface,scene,frame.ReferenceSize,items,nullptr,true,root);
        if(surface.Failed){items.clear();m_impl->Build(scene,items,true);std::erase_if(items,[&](const Impl::Batch& batch){const Entity entity=scene.GetEntity(batch.Owner);return (entity.HasComponent<Frame>()?entity:GUI::GetFrame(entity))!=root;});surface.Effects=false;surface.Plan.clear();}else for(auto& batch:items)for(auto& vertex:batch.Data){auto point=root.GetWorldMatrix().TransformPoint({(vertex.X-frame.ReferenceSize.X*.5f)/frame.PixelsPerUnit,(frame.ReferenceSize.Y*.5f+vertex.Y)/frame.PixelsPerUnit,0});vertex.X=point.X;vertex.Y=point.Y;vertex.Z=point.Z;}
        for(auto& batch:items)data.push_back(std::move(batch));
    }
    m_impl->Upload(m_impl->Spatial,data,m_impl->Backend.GetScene(),true);
    for(auto it=m_impl->SpatialSurfaces.begin();it!=m_impl->SpatialSurfaces.end();)if(!groups.contains(it->first)){m_impl->ClearSurface(it->second);it=m_impl->SpatialSurfaces.erase(it);}else ++it;
}
void GuiRendererModule::RenderSpatialEffects(){
    auto& renderer=m_impl->Backend.GetRenderer();const auto saved=renderer.getClearOptions();
    struct Restore {filament::Renderer& Renderer;filament::Renderer::ClearOptions Options;~Restore(){Renderer.setClearOptions(Options);}} restore{renderer,saved};
    for(auto& [id,surface]:m_impl->SpatialSurfaces)m_impl->RenderPlan(surface);
    const auto& views=m_impl->Backend.GetActiveViews();const bool camera=std::any_of(views.begin(),views.end(),[](const auto* view){return view&&!view->getRenderTarget();});
    for(auto& [id,surface]:m_impl->Surfaces)if(surface.SceneClear){auto options=saved;options.clearColor=camera?saved.clearColor:filament::math::double4{0,0,0,1};options.clear=true;options.discard=false;renderer.setClearOptions(options);renderer.render(surface.SceneClear->View);}
}
filament::RenderTarget* GuiRendererModule::GetSceneInputTarget()const{auto found=m_impl->Surfaces.find(UUID{});return found==m_impl->Surfaces.end()?nullptr:found->second.SceneInput;}
void GuiRendererModule::RenderOverlay(Bazzalt::Scene& scene,Vec2 size){
    RenderCameraOverlay(scene,{},size,nullptr);
}
void GuiRendererModule::RenderCameraOverlay(Bazzalt::Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target){
    (void)scene;(void)size;(void)target;auto found=m_impl->Surfaces.find(camera);if(found==m_impl->Surfaces.end()||found->second.Batches.empty())return;
    m_impl->RenderPlan(found->second);auto& renderer=m_impl->Backend.GetRenderer();renderer.setClearOptions({.clearColor={0,0,0,0},.clear=false,.discard=false});renderer.render(found->second.View);
}
void GuiRendererModule::PrepareOverlay(Bazzalt::Scene& scene,Vec2 size,UUID camera,filament::RenderTarget* target){
    auto& system=scene.GetSystem<GuiSystem>();system.Layout(scene,size);std::vector<Impl::Batch> data;m_impl->Build(scene,data,false,camera);
    auto& s=m_impl->GetSurface(camera);m_impl->PrepareEffects(s,scene,size,data,target,false,{},camera);if(s.Failed){data.clear();m_impl->Build(scene,data,false,camera);s.Plan.clear();s.SceneInput=nullptr;s.Effects=false;}m_impl->Upload(s.Batches,data,*s.Scene,false);
    s.Camera->setProjection(filament::Camera::Projection::ORTHO,0,size.X,-size.Y,0,.1,10);s.View->setViewport({0,0,static_cast<std::uint32_t>(size.X),static_cast<std::uint32_t>(size.Y)});s.View->setRenderTarget(target);
}
}
