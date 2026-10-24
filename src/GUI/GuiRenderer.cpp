#include "GUI/GuiRendererModule.h"
#include "GUI/GuiSystem.h"
#include "GUI/FontFace.h"
#include "Bazzalt/AssetManager.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
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

namespace Bazzalt::Runtime {
struct GuiRendererModule::Impl {
    struct Vertex { float X,Y,Z,U,V,R,G,B,A; };
    struct Batch {
        filament::Texture* Texture=nullptr;
        std::vector<Vertex> Data;
        utils::Entity Entity;
        filament::MaterialInstance* Material=nullptr;
        filament::VertexBuffer* Vertices=nullptr;
        filament::IndexBuffer* Indices=nullptr;
        std::size_t Capacity=0;
    };
    struct Surface {filament::Scene* Scene=nullptr;filament::View* View=nullptr;filament::Camera* Camera=nullptr;utils::Entity CameraEntity;std::vector<Batch> Batches;};
    RenderBackend& Backend;
    filament::Engine& Engine;
    filament::Material* Material=nullptr;
    filament::Texture* Atlas=nullptr;
    std::vector<Batch> Spatial;
    std::unordered_map<UUID,Surface> Surfaces;
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
        for(const auto& [id,s]:Surfaces)for(const auto& b:s.Batches)if(b.Texture==texture)return true;
        return false;
    }
    explicit Impl(RenderBackend& backend):Backend(backend),Engine(backend.GetEngine()){
        Material=filament::Material::Builder().package(Embedded::GuiFilamat,sizeof(Embedded::GuiFilamat)).build(Engine);
        BuildAtlas();
    }
    Surface& GetSurface(UUID id){auto [found,created]=Surfaces.try_emplace(id);auto& s=found->second;if(created){s.Scene=Engine.createScene();s.View=Engine.createView();s.CameraEntity=Engine.getEntityManager().create();s.Camera=Engine.createCamera(s.CameraEntity);s.View->setScene(s.Scene);s.View->setCamera(s.Camera);s.View->setPostProcessingEnabled(false);s.View->setBlendMode(filament::View::BlendMode::TRANSLUCENT);s.View->setVisibleLayers(0xff,0x40);s.Camera->lookAt({0,0,1},{0,0,0},{0,1,0});s.Camera->setExposure(1.0f);}return s;}
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
    void Clear(){for(auto& [id,s]:Surfaces){(void)id;for(auto& b:s.Batches)Destroy(b,*s.Scene);Engine.destroy(s.View);Engine.destroyCameraComponent(s.CameraEntity);Engine.getEntityManager().destroy(s.CameraEntity);Engine.destroy(s.Scene);}Surfaces.clear();for(auto& b:Spatial)Destroy(b,Backend.GetScene());Spatial.clear();for(auto& [key,font]:Fonts)if(font->Texture)Engine.destroy(font->Texture);Fonts.clear();}
    ~Impl(){Clear();Engine.destroy(Atlas);Engine.destroy(Material);}
    void Quad(std::vector<Batch>& batches,const GuiItem& item,GuiBounds rect,Vec4 color,filament::Texture* texture,Vec4 uv,bool spatial){
        if(!texture||!std::isfinite(color.X)||!std::isfinite(color.Y)||!std::isfinite(color.Z)||!std::isfinite(color.W)||color.W<=0||rect.Size.X<=0||rect.Size.Y<=0)return;
        const float left=std::max(rect.Position.X,item.Clip.Position.X),top=std::max(rect.Position.Y,item.Clip.Position.Y);
        const float right=std::min(rect.Position.X+rect.Size.X,item.Clip.Position.X+item.Clip.Size.X),bottom=std::min(rect.Position.Y+rect.Size.Y,item.Clip.Position.Y+item.Clip.Size.Y);
        if(left>=right||top>=bottom)return;
        if(batches.empty()||batches.back().Texture!=texture)batches.push_back({texture});
        auto& b=batches.back();if(b.Data.size()>400000)return;
        const float u0=uv.X+(uv.Z-uv.X)*(left-rect.Position.X)/rect.Size.X,u1=uv.X+(uv.Z-uv.X)*(right-rect.Position.X)/rect.Size.X;
        const float v0=uv.Y+(uv.W-uv.Y)*(top-rect.Position.Y)/rect.Size.Y,v1=uv.Y+(uv.W-uv.Y)*(bottom-rect.Position.Y)/rect.Size.Y;
        const auto vertex=[&](float x,float y,float u,float v){Vec3 point{x,-y,0};if(spatial){const auto& frame=item.Root.GetComponent<Frame>();point={(x-frame.ReferenceSize.X*.5f)/frame.PixelsPerUnit,(frame.ReferenceSize.Y*.5f-y)/frame.PixelsPerUnit,0};point=item.Root.GetWorldMatrix().TransformPoint(point);}return Vertex{point.X,point.Y,point.Z,u,v,color.X,color.Y,color.Z,std::clamp(color.W,0.f,1.f)};};
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
    const AtlasGlyph& Glyph(FontPage& page,std::uint32_t cp){
        if(auto found=page.Glyphs.find(cp);found!=page.Glyphs.end())return found->second;
        if(page.Glyphs.size()>=8192){static const AtlasGlyph fallback{FontGlyph{0,0,0,0,0,8},Vec4{},false};return fallback;}
        AtlasGlyph glyph;glyph.Metrics=page.Face.Render(cp);const auto width=glyph.Metrics.Width,height=glyph.Metrics.Height;
        if(width==0||height==0)glyph.Baked=glyph.Metrics.Index!=0||cp==32||cp==160;
        else {
            if(page.X+width+1>=1024){page.X=1;page.Y+=page.Row+1;page.Row=0;}
            if(page.Y+height+1<1024){
                for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){const auto at=((page.Y+y)*1024+page.X+x)*4;page.Pixels[at]=page.Pixels[at+1]=page.Pixels[at+2]=255;page.Pixels[at+3]=glyph.Metrics.Alpha[y*width+x];}
                glyph.UV={float(page.X)/1024,float(page.Y)/1024,float(page.X+width)/1024,float(page.Y+height)/1024};glyph.Baked=true;page.X+=width+1;page.Row=std::max(page.Row,height);page.Dirty=true;
            }
        }
        std::vector<std::uint8_t>().swap(glyph.Metrics.Alpha);return page.Glyphs.emplace(cp,std::move(glyph)).first->second;
    }
    void FlushFonts(){for(auto& [key,page]:Fonts)if(page->Dirty){auto* copy=new std::vector<std::uint8_t>(page->Pixels);page->Texture->setImage(Engine,0,filament::Texture::PixelBufferDescriptor(copy->data(),copy->size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){delete static_cast<std::vector<std::uint8_t>*>(user);},copy));page->Dirty=false;}}
    void Text(std::vector<Batch>& batches,GuiItem item,const std::string& text,float size,Vec4 color,bool wrap,bool spatial,UUID font={},std::uint32_t face=0,float letterSpacing=0,float lineSpacing=1,bool kerning=true){
        if(!std::isfinite(size)||size<=0)return;
        if(auto* page=FontAtlas(font,face,std::uint32_t(std::ceil(std::clamp(size*item.Scale,1.f,512.f))))){
            float x=item.Bounds.Position.X,y=item.Bounds.Position.Y;std::uint32_t previous=0;
            const auto height=std::max(1.f,page->Face.GetLineHeight())*std::clamp(lineSpacing,.1f,10.f);
            for(std::size_t at=0;at<text.size()&&at<256*1024;){auto cp=NextCodepoint(text,at);if(cp=='\n'){x=item.Bounds.Position.X;y+=height;previous=0;continue;}if(cp<32)continue;
                const auto& glyph=Glyph(*page,cp);auto gap=kerning?page->Face.GetKerning(previous,glyph.Metrics.Index):0;
                if(wrap&&x>item.Bounds.Position.X&&x+gap+glyph.Metrics.Advance>item.Bounds.Position.X+item.Bounds.Size.X){x=item.Bounds.Position.X;y+=height;gap=0;previous=0;}
                if(y>=item.Bounds.Position.Y+item.Bounds.Size.Y)break;
                x+=gap;
                if(glyph.Baked)Quad(batches,item,{{x+glyph.Metrics.Left,y+page->Face.GetAscender()-glyph.Metrics.Top},{float(glyph.Metrics.Width),float(glyph.Metrics.Height)}},color,page->Texture,glyph.UV,spatial);
                else Quad(batches,item,{{x,y},{size*.625f*item.Scale,size*.875f*item.Scale}},color,Atlas,{120.f/128,56.f/64,125.f/128,63.f/64},spatial);
                x+=glyph.Metrics.Advance+letterSpacing*item.Scale;previous=glyph.Metrics.Index;
            }return;
        }
        if(!std::isfinite(size)||size<=0)return;size=std::clamp(size*item.Scale,1.f,512.f);const float advance=size*.75f;
        float x=item.Bounds.Position.X,y=item.Bounds.Position.Y;
        for(std::size_t offset=0;offset<text.size();){unsigned char c=text[offset++];if((c&0x80)!=0){while(offset<text.size()&&(static_cast<unsigned char>(text[offset])&0xc0)==0x80)++offset;c=127;}
            if(c=='\n'){x=item.Bounds.Position.X;y+=size;continue;}if(c<32)continue;
            if(wrap&&x+advance>item.Bounds.Position.X+item.Bounds.Size.X&&x>item.Bounds.Position.X){x=item.Bounds.Position.X;y+=size;}
            if(y>=item.Bounds.Position.Y+item.Bounds.Size.Y)break;
            if(c!=' ')Quad(batches,item,{{x,y},{size*.625f,size*.875f}},color,Atlas,{float((c%16)*8)/128,float((c/16)*8)/64,float((c%16)*8+5)/128,float((c/16)*8+7)/64},spatial);
            x+=advance;
        }
    }
    void Build(Bazzalt::Scene& scene,std::vector<Batch>& output,bool spatial,UUID camera={}){
        UsedFonts.clear();
        auto& system=scene.GetSystem<GuiSystem>();
        for(const auto& item:system.Items){
            if((item.Root.GetComponent<Frame>().Mode==FrameMode::Spatial)!=spatial)continue;
            const auto& frame=item.Root.GetComponent<Frame>();
            if(camera&&(frame.Mode!=FrameMode::CameraBound||frame.Camera!=camera))continue;
            if(!camera&&!spatial&&frame.Mode==FrameMode::CameraBound){const auto owner=scene.GetEntity(frame.Camera);const auto* source=owner.TryGetComponent<Camera>();const auto* target=owner.TryGetComponent<CameraRenderTarget>();if((source&&source->RenderTarget)||(target&&target->Enabled))continue;}
            const auto state=GUI::GetInteraction(item.Owner);auto bounds=item.Bounds;
            const auto computed=system.Computed.find(item.Owner.GetUUID());
            const bool painted=computed!=system.Computed.end()&&(computed->second.Cascade.Properties.contains("background-color")||computed->second.Cascade.Properties.contains("background-image")||computed->second.Cascade.Properties.contains("border-width"));
            if(const auto* r=item.Owner.TryGetComponent<Rectangle>();r?r->Enabled:painted){
                const auto style=GUI::ResolveStyle(item.Owner);
                Vec4 color=style.Color;
                if(!r&&computed!=system.Computed.end()&&!computed->second.Cascade.Properties.contains("background-color"))color={0,0,0,0};
                const auto* button=EffectiveGui<GuiButton>(item.Owner);const auto* input=EffectiveGui<GuiTextInput>(item.Owner);
                if((button&&(!button->Enabled||!button->Interactable))||(input&&(!input->Enabled||!input->Interactable)))color=style.DisabledColor;
                else if(state.Pressed)color=style.PressedColor;else if(state.Hovered||state.Focused)color=style.HoverColor;
                const float opacity=std::isfinite(style.Opacity)?std::clamp(style.Opacity,0.f,1.f):0;const float border=std::isfinite(style.BorderWidth)?std::clamp(style.BorderWidth*item.Scale,0.f,std::min(bounds.Size.X,bounds.Size.Y)*.5f):0;
                if(border>0){auto edge=style.BorderColor;edge.W*=opacity;const Vec4 white{.02f,.04f,.02f,.04f};
                    Quad(output,item,{bounds.Position,{bounds.Size.X,border}},edge,Atlas,white,spatial);
                    Quad(output,item,{{bounds.Position.X,bounds.Position.Y+bounds.Size.Y-border},{bounds.Size.X,border}},edge,Atlas,white,spatial);
                    Quad(output,item,{{bounds.Position.X,bounds.Position.Y+border},{border,bounds.Size.Y-border*2}},edge,Atlas,white,spatial);
                    Quad(output,item,{{bounds.Position.X+bounds.Size.X-border,bounds.Position.Y+border},{border,bounds.Size.Y-border*2}},edge,Atlas,white,spatial);
                    bounds.Position.X+=border;bounds.Position.Y+=border;bounds.Size.X-=border*2;bounds.Size.Y-=border*2;
                }
                color.W*=opacity;Quad(output,item,bounds,color,Atlas,{.02f,.04f,.02f,.04f},spatial);
                if(computed!=system.Computed.end()&&computed->second.BackgroundTexture)Quad(output,item,bounds,{1,1,1,opacity},Backend.GetAssets().GetGuiTexture(computed->second.BackgroundTexture),{0,0,1,1},spatial);
            }
            if(const auto* source=item.Owner.TryGetComponent<GuiImage>();source&&source->Enabled){auto image=GUI::ResolveImage(item.Owner);if(computed!=system.Computed.end()&&computed->second.Cascade.Properties.contains("opacity"))image.Color.W*=computed->second.Style.Opacity;auto* texture=image.Camera?Backend.GetCameraTexture(image.Camera):Backend.GetAssets().GetGuiTexture(image.Texture);Quad(output,item,item.Bounds,image.Color,texture,{0,0,1,1},spatial);}
            if(const auto* text=item.Owner.TryGetComponent<GuiText>();text&&text->Enabled){
                auto clipped=item;clipped.Clip=Intersect(item.Clip,item.Bounds);
                auto resolved=GUI::ResolveText(item.Owner);if(computed!=system.Computed.end()&&computed->second.Cascade.Properties.contains("opacity"))resolved.Color.W*=computed->second.Style.Opacity;Text(output,clipped,text->Value,resolved.FontSize,resolved.Color,resolved.Wrap,spatial,resolved.Font,resolved.FontFace,resolved.LetterSpacing,resolved.LineSpacing,resolved.Kerning);
            }
            if(const auto* input=EffectiveGui<GuiTextInput>(item.Owner);input&&input->Enabled){auto clipped=item;clipped.Clip=Intersect(item.Clip,item.Bounds);const auto& local=item.Owner.GetComponent<GuiTextInput>();std::string value=local.Value.empty()?local.Placeholder:local.Value;if(input->Password&&!local.Value.empty()){value.clear();for(unsigned char c:local.Value)if((c&0xc0)!=0x80)value+='*';}Vec4 color=computed!=system.Computed.end()?computed->second.Text.Color:Vec4{1,1,1,1};if(local.Value.empty())color.W*=.5f;if(computed!=system.Computed.end()&&computed->second.Cascade.Properties.contains("opacity"))color.W*=computed->second.Style.Opacity;Text(output,clipped,value,input->FontSize,color,false,spatial,input->Font,input->FontFace);}
        }
        FlushFonts();
    }
    static GuiBounds Intersect(GuiBounds a,GuiBounds b){Vec2 begin{std::max(a.Position.X,b.Position.X),std::max(a.Position.Y,b.Position.Y)};return {begin,{std::max(0.f,std::min(a.Position.X+a.Size.X,b.Position.X+b.Size.X)-begin.X),std::max(0.f,std::min(a.Position.Y+a.Size.Y,b.Position.Y+b.Size.Y)-begin.Y)}};}
    void Upload(std::vector<Batch>& cache,std::vector<Batch>& generated,filament::Scene& scene,bool spatial){
        while(cache.size()>generated.size()){Destroy(cache.back(),scene);cache.pop_back();}
        while(cache.size()<generated.size()){Batch b;b.Entity=Engine.getEntityManager().create();Engine.getTransformManager().create(b.Entity);b.Material=Material->createInstance();b.Material->setDepthCulling(spatial);cache.push_back(std::move(b));}
        auto& manager=Engine.getRenderableManager();
        for(std::size_t i=0;i<generated.size();++i){auto& b=cache[i];auto& data=generated[i].Data;const auto count=static_cast<std::uint32_t>(data.size());if(!count)continue;
            b.Material->setParameter("image",generated[i].Texture,Backend.GetAssets().GetTextureSampler(generated[i].Texture));b.Texture=generated[i].Texture;
            if(b.Capacity<count){manager.destroy(b.Entity);if(b.Vertices)Engine.destroy(b.Vertices);if(b.Indices)Engine.destroy(b.Indices);b.Capacity=std::max<std::size_t>(64,count*2);
                b.Vertices=filament::VertexBuffer::Builder().vertexCount(static_cast<std::uint32_t>(b.Capacity)).bufferCount(1)
                    .attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,sizeof(Vertex))
                    .attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,U),sizeof(Vertex))
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
void GuiRendererModule::PrepareSpatial(Bazzalt::Scene& scene){std::vector<Impl::Batch> data;m_impl->Build(scene,data,true);m_impl->Upload(m_impl->Spatial,data,m_impl->Backend.GetScene(),true);}
void GuiRendererModule::RenderOverlay(Bazzalt::Scene& scene,Vec2 size){
    RenderCameraOverlay(scene,{},size,nullptr);
}
void GuiRendererModule::RenderCameraOverlay(Bazzalt::Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target){
    (void)scene;(void)size;(void)target;auto found=m_impl->Surfaces.find(camera);if(found==m_impl->Surfaces.end()||found->second.Batches.empty())return;
    auto& renderer=m_impl->Backend.GetRenderer();renderer.setClearOptions({.clearColor={0,0,0,0},.clear=false,.discard=false});renderer.render(found->second.View);
}
void GuiRendererModule::PrepareOverlay(Bazzalt::Scene& scene,Vec2 size,UUID camera,filament::RenderTarget* target){
    auto& system=scene.GetSystem<GuiSystem>();system.Layout(scene,size);std::vector<Impl::Batch> data;m_impl->Build(scene,data,false,camera);
    auto& s=m_impl->GetSurface(camera);m_impl->Upload(s.Batches,data,*s.Scene,false);
    s.Camera->setProjection(filament::Camera::Projection::ORTHO,0,size.X,-size.Y,0,.1,10);s.View->setViewport({0,0,static_cast<std::uint32_t>(size.X),static_cast<std::uint32_t>(size.Y)});s.View->setRenderTarget(target);
}
}
