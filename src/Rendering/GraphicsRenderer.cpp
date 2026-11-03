#include "Rendering/GraphicsRenderer.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Rendering/LODFade.h"
#include "Runtime/GraphicsAccess.h"
#include "VFX/VFXInternal.h"
#include <filament/LightManager.h>
#include <cstring>
#include "Bazzalt/Console.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <utils/EntityManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

namespace Bazzalt::Runtime {
namespace {
using Key=std::pair<std::uint64_t,std::uint64_t>;
struct Vertex {float P[3],T[4],UV[2],C[4],Custom[16];};
filament::math::mat4f Matrix(const Mat4& v){filament::math::mat4f out;for(int c=0;c<4;++c)for(int r=0;r<4;++r)out[c][r]=v(r,c);return out;}
Mat4 Matrix(const auto& v){Mat4 out;for(int c=0;c<4;++c)for(int r=0;r<4;++r)out(r,c)=float(v[c][r]);return out;}
}
struct GraphicsRenderer::Impl {
    RenderBackend& Backend;filament::Engine& Engine;
    struct Geometry {filament::VertexBuffer* Vertices=nullptr;filament::IndexBuffer* Indices=nullptr;filament::Box Bounds;GraphicsTopology Topology{};std::size_t VertexCount=0,IndexCount=0;};
    struct Draw {utils::Entity Entity;filament::MaterialInstance* Material=nullptr;UUID Asset,Source;Key GeometryKey{};filament::VertexBuffer* Vertices=nullptr;filament::IndexBuffer* Indices=nullptr;bool Added=false,Valid=false,StateOverride=false,Borrowed=false,Particle=false;UUID ParticleTexture{};unsigned ParticleStyle=0;std::shared_ptr<filament::MaterialInstance> Shared;};
    struct Camera {filament::View* View;GraphicsEvent Event;};
    std::map<Key,Geometry> Geometries;
    std::map<std::tuple<UUID,bool,bool,UUID,unsigned,UUID>,std::weak_ptr<filament::MaterialInstance>> SharedMaterials;
    struct ParticleLamp {utils::Entity Entity;UUID Owner;bool Added=false;};
    std::map<std::pair<UUID,unsigned>,ParticleLamp> ParticleLamps;
    void SyncParticleLights(){
        auto lights=GraphicsAccess::GetParticleLights();std::set<std::pair<UUID,unsigned>> alive;auto& manager=Engine.getLightManager();
        for(const auto& light:lights){auto key=std::pair{light.Owner,light.Id};alive.insert(key);auto& lamp=ParticleLamps[key];if(!lamp.Entity){lamp.Entity=Engine.getEntityManager().create();lamp.Owner=light.Owner;filament::LightManager::Builder(filament::LightManager::Type::POINT).position({light.Position.X,light.Position.Y,light.Position.Z}).color({light.Color.X,light.Color.Y,light.Color.Z}).intensity(light.Intensity).falloff(light.Range).castShadows(light.Shadows).build(Engine,lamp.Entity);}else{auto instance=manager.getInstance(lamp.Entity);manager.setPosition(instance,{light.Position.X,light.Position.Y,light.Position.Z});manager.setColor(instance,{light.Color.X,light.Color.Y,light.Color.Z});manager.setIntensity(instance,light.Intensity);manager.setFalloff(instance,light.Range);manager.setShadowCaster(instance,light.Shadows);}}
        for(auto it=ParticleLamps.begin();it!=ParticleLamps.end();)if(!alive.contains(it->first)){Backend.GetScene().remove(it->second.Entity);Engine.destroy(it->second.Entity);Engine.getEntityManager().destroy(it->second.Entity);it=ParticleLamps.erase(it);}else ++it;
    }
    std::uint64_t Allocations=0,Builds=0;
    filament::Scene* MaskScene=nullptr;std::vector<std::pair<utils::Entity,filament::MaterialInstance*>> MaskMaterials;
    LODFadeSwaps Fades;
    std::vector<Draw> Draws;std::vector<Camera> Cameras;std::set<UUID> InvalidMaterials;
    explicit Impl(RenderBackend& b):Backend(b),Engine(b.GetEngine()){

    }
    void Destroy(Draw& draw){if(draw.Added)Backend.GetScene().remove(draw.Entity);Engine.destroy(draw.Entity);draw.Shared.reset();Engine.getEntityManager().destroy(draw.Entity);}
    void Destroy(Geometry& g){Engine.destroy(g.Vertices);Engine.destroy(g.Indices);}
    ~Impl(){for(auto& [_,lamp]:ParticleLamps){Backend.GetScene().remove(lamp.Entity);Engine.destroy(lamp.Entity);Engine.getEntityManager().destroy(lamp.Entity);}Fades.Clear(Engine);for(auto& draw:Draws)Destroy(draw);for(auto& [key,g]:Geometries)Destroy(g);}
    Geometry Upload(const GraphicsGeometry& data){Geometry result;return Upload(data,result);}
    Geometry Upload(const GraphicsGeometry& data,Geometry result){
        const bool allocate=!result.Vertices;if(allocate)++Allocations;result.Topology=data.Topology;result.VertexCount=data.Vertices.size();result.IndexCount=data.Indices.size();
        std::vector<Vec3> tangents(data.Vertices.size()),bitangents(data.Vertices.size());
        if(data.Topology==GraphicsTopology::Triangles)for(std::size_t i=0;i+2<data.Indices.size();i+=3){auto a=data.Indices[i],b=data.Indices[i+1],c=data.Indices[i+2];auto x=data.Vertices[b].Position-data.Vertices[a].Position,y=data.Vertices[c].Position-data.Vertices[a].Position;auto u=data.Vertices[b].UV-data.Vertices[a].UV,v=data.Vertices[c].UV-data.Vertices[a].UV;float determinant=u.X*v.Y-u.Y*v.X;if(std::abs(determinant)>1e-10f){auto t=(x*v.Y-y*u.Y)/determinant,bt=(y*u.X-x*v.X)/determinant;for(auto index:{a,b,c}){tangents[index]+=t;bitangents[index]+=bt;}}}
        std::size_t vertexIndex=0;
        auto* vertices=new std::vector<Vertex>();vertices->reserve(data.Vertices.size());Vec3 lo=data.Vertices.front().Position,hi=lo;
        for(const auto& v:data.Vertices){auto n=v.Normal.Normalized();auto helper=std::abs(n.Y)<.999f?Vec3{0,1,0}:Vec3{1,0,0};auto tangent=tangents[vertexIndex]-n*Vec3::Dot(n,tangents[vertexIndex]);auto t=tangent.LengthSquared()>1e-12f?tangent.Normalized():Vec3::Cross(helper,n).Normalized(),b=Vec3::Cross(n,t);if(Vec3::Dot(b,bitangents[vertexIndex])<0)b=-b;++vertexIndex;
            auto q=filament::math::mat3f::packTangentFrame({filament::math::float3{t.X,t.Y,t.Z},filament::math::float3{b.X,b.Y,b.Z},filament::math::float3{n.X,n.Y,n.Z}});
            vertices->push_back({{v.Position.X,v.Position.Y,v.Position.Z},{q.x,q.y,q.z,q.w},{v.UV.X,v.UV.Y},{v.Color.X,v.Color.Y,v.Color.Z,v.Color.W}});
            if(vertexIndex-1<data.ParticleData.size())std::memcpy(vertices->back().Custom,data.ParticleData[vertexIndex-1].data(),16*sizeof(float));
            lo={std::min(lo.X,v.Position.X),std::min(lo.Y,v.Position.Y),std::min(lo.Z,v.Position.Z)};hi={std::max(hi.X,v.Position.X),std::max(hi.Y,v.Position.Y),std::max(hi.Z,v.Position.Z)};
        }
        const auto center=(lo+hi)*.5f,extent=(hi-lo)*.5f;result.Bounds={{center.X,center.Y,center.Z},{std::max(extent.X,.0001f),std::max(extent.Y,.0001f),std::max(extent.Z,.0001f)}};
        if(allocate)result.Vertices=filament::VertexBuffer::Builder().vertexCount(std::uint32_t(vertices->size())).bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,offsetof(Vertex,P),sizeof(Vertex))
            .attribute(filament::VertexAttribute::TANGENTS,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,T),sizeof(Vertex))
            .attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,UV),sizeof(Vertex))
            .attribute(filament::VertexAttribute::UV1,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,UV),sizeof(Vertex))
            .attribute(filament::VertexAttribute::CUSTOM0,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,Custom),sizeof(Vertex))
            .attribute(filament::VertexAttribute::CUSTOM1,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,Custom)+4*sizeof(float),sizeof(Vertex))
            .attribute(filament::VertexAttribute::CUSTOM2,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,Custom)+8*sizeof(float),sizeof(Vertex))
            .attribute(filament::VertexAttribute::CUSTOM3,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,Custom)+12*sizeof(float),sizeof(Vertex))
            .attribute(filament::VertexAttribute::COLOR,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,C),sizeof(Vertex)).build(Engine);
        if(!result.Vertices){delete vertices;throw std::runtime_error("Could not allocate Graphics vertex buffer");}
        result.Vertices->setBufferAt(Engine,0,{vertices->data(),vertices->size()*sizeof(Vertex),[](void*,std::size_t,void* p){delete static_cast<std::vector<Vertex>*>(p);},vertices});
        if(allocate)result.Indices=filament::IndexBuffer::Builder().indexCount(std::uint32_t(data.Indices.size())).bufferType(filament::IndexBuffer::IndexType::UINT).build(Engine);
        if(!result.Indices){Engine.destroy(result.Vertices);throw std::runtime_error("Could not allocate Graphics index buffer");}
        auto* indices=new std::vector<std::uint32_t>(data.Indices);result.Indices->setBuffer(Engine,{indices->data(),indices->size()*sizeof(std::uint32_t),[](void*,std::size_t,void* p){delete static_cast<std::vector<std::uint32_t>*>(p);},indices});return result;
    }
};
GraphicsRenderer::GraphicsRenderer(RenderBackend& backend):m_impl(std::make_unique<Impl>(backend)){
    backend.GetAssets().SetDeformationRelease([this](UUID source){auto& impl=*m_impl;for(auto& draw:impl.Draws)if(draw.Borrowed&&draw.Source==source){if(draw.Added){impl.Backend.GetScene().remove(draw.Entity);draw.Added=false;}impl.Engine.getRenderableManager().destroy(draw.Entity);draw.Material=nullptr;draw.Borrowed=false;draw.Valid=false;}});
}
GraphicsRenderer::~GraphicsRenderer(){EndSelectionMask();m_impl->Backend.GetAssets().EndDeformedView();m_impl->Backend.GetAssets().SetDeformationRelease({});}
std::array<std::uint64_t,2> GraphicsRenderer::GetResourceStatistics()const{return {m_impl->Allocations,m_impl->Builds};}
void GraphicsRenderer::BeginFrame(){m_impl->Fades.Clear(m_impl->Engine);m_impl->Cameras.clear();GraphicsAccess::BeginFrame();}
void GraphicsRenderer::AddCamera(filament::View* view,UUID camera,std::uint64_t viewport,bool editor){
    if(!view)return;GraphicsEvent event;event.Camera=camera;event.Viewport=viewport;event.IsEditorCamera=editor;
    const auto bounds=view->getViewport();event.Width=bounds.width;event.Height=bounds.height;event.View=Matrix(view->getCamera().getViewMatrix());event.Projection=Matrix(view->getCamera().getProjectionMatrix());event.ViewProjection=event.Projection*event.View;
    m_impl->Cameras.push_back({view,event});GraphicsAccess::BeforeCamera(event);
}
void GraphicsRenderer::Prepare(){
    auto& impl=*m_impl;impl.SyncParticleLights();GraphicsAccess::BeforeDraws();const auto& commands=GraphicsAccess::Commands();
    // Bound the aggregate geometry allocation, not just individual mesh sizes.
    std::set<Key> used;std::size_t bytes=0;
    for(const auto& c:commands)if(used.insert({c.Geometry->Id,c.Geometry->Revision}).second){bytes+=c.Geometry->Vertices.size()*sizeof(Vertex)+c.Geometry->Indices.size()*4;if(bytes>128u*1024u*1024u)throw std::length_error("Graphics frame geometry exceeds 128 MiB");}
    while(impl.Draws.size()>commands.size()){impl.Destroy(impl.Draws.back());impl.Draws.pop_back();}
    auto& manager=impl.Engine.getRenderableManager();
    for(std::size_t i=0;i<commands.size();++i){const auto& c=commands[i];const Key key{c.Geometry->Id,c.Geometry->Revision};
        if(!impl.Geometries.contains(key)){
            auto previous=std::find_if(impl.Geometries.begin(),impl.Geometries.end(),[&](const auto& item){return item.first.first==key.first&&!used.contains(item.first)&&item.second.VertexCount==c.Geometry->Vertices.size()&&item.second.IndexCount==c.Geometry->Indices.size()&&item.second.Topology==c.Geometry->Topology;});
            if(previous!=impl.Geometries.end()){auto updated=impl.Upload(*c.Geometry,previous->second);impl.Geometries.erase(previous);impl.Geometries.emplace(key,updated);}else impl.Geometries.emplace(key,impl.Upload(*c.Geometry));
        }auto& geometry=impl.Geometries.at(key);
        if(i==impl.Draws.size()){Impl::Draw draw;draw.Entity=impl.Engine.getEntityManager().create();impl.Engine.getTransformManager().create(draw.Entity);impl.Draws.push_back(draw);}
        auto& draw=impl.Draws[i];bool rebuild=!manager.hasComponent(draw.Entity);
        const bool stateOverride=c.Material&&Material::Load(c.Material).IsValid()&&Material::Load(c.Material).GetRenderState().Override;
        auto* inherited=!c.Material&&c.Source?impl.Backend.GetAssets().GetDeformedMaterial(c.Source,c.SourceNode,c.SourceSlot):nullptr;
        if(inherited){
            if(draw.Material!=inherited){if(!rebuild)manager.setMaterialInstanceAt(manager.getInstance(draw.Entity),0,inherited);draw.Shared.reset();draw.Material=inherited;}
            draw.Borrowed=true;draw.Asset={};draw.StateOverride=false;
        }else if(draw.Borrowed||draw.Asset!=c.Material||draw.StateOverride!=stateOverride||draw.Particle!=c.Particle||draw.ParticleTexture!=c.ParticleTexture||draw.ParticleStyle!=c.ParticleStyle||!draw.Material||(c.Material&&!impl.Backend.GetAssets().UpdateSurfaceMaterial(draw.Material,c.Material))){
            const auto key=std::make_tuple(c.Material,stateOverride,c.Particle,c.ParticleTexture,c.ParticleStyle,c.Particle?c.Owner:UUID{});
            auto shared=impl.SharedMaterials[key].lock();
            if(shared&&c.Material&&!impl.Backend.GetAssets().UpdateSurfaceMaterial(shared.get(),c.Material))shared.reset();
            if(!shared){auto* created=c.Material?impl.Backend.GetAssets().CreateSurfaceMaterial(c.Material):(c.Particle?impl.Backend.GetAssets().CreateParticleMaterial(c.ParticleTexture,c.ParticleStyle):impl.Backend.GetAssets().CreateGraphicsMaterial());if(created){shared={created,[&impl](auto* value){impl.Engine.destroy(value);}};impl.SharedMaterials[key]=shared;}}
            auto* replacement=shared.get();
            if(!replacement){draw.Valid=false;if(draw.Added){impl.Backend.GetScene().remove(draw.Entity);draw.Added=false;}if(impl.InvalidMaterials.size()>128)impl.InvalidMaterials.clear();if(impl.InvalidMaterials.insert(c.Material).second)Console::Warning("Skipping a Graphics draw with an unavailable or incompatible surface material: "+c.Material.ToString(),"Graphics");continue;}
            impl.InvalidMaterials.erase(c.Material);draw.StateOverride=stateOverride;
            if(c.Material)impl.Backend.GetAssets().UpdateSurfaceMaterial(replacement,c.Material);
            if(!rebuild)manager.setMaterialInstanceAt(manager.getInstance(draw.Entity),0,replacement);
            draw.Shared=std::move(shared);draw.Material=replacement;draw.Asset=c.Material;draw.Borrowed=false;
        }
        draw.Valid=true;draw.Source=c.Source;draw.Particle=c.Particle;draw.ParticleTexture=c.ParticleTexture;draw.ParticleStyle=c.ParticleStyle;if(c.Particle&&!c.Material)impl.Backend.GetAssets().UpdateParticleMaterial(draw.Material,c.ParticleTexture,c.ParticleStyle,c.ParticleGPU.get());
        using P=filament::RenderableManager::PrimitiveType;static constexpr P types[]{P::POINTS,P::LINES,P::LINE_STRIP,P::TRIANGLES,P::TRIANGLE_STRIP};
        if(rebuild){++impl.Builds;
            const auto result=filament::RenderableManager::Builder(1).boundingBox(geometry.Bounds).culling(false).material(0,draw.Material).geometry(0,types[unsigned(geometry.Topology)],geometry.Vertices,geometry.Indices).build(impl.Engine,draw.Entity);
            if(result!=filament::RenderableManager::Builder::Result::Success)throw std::runtime_error("Could not create Graphics renderable");
        }
        const auto instance=manager.getInstance(draw.Entity);if(!rebuild&&(draw.Vertices!=geometry.Vertices||draw.Indices!=geometry.Indices))manager.setGeometryAt(instance,0,types[unsigned(geometry.Topology)],geometry.Vertices,geometry.Indices,0,geometry.IndexCount);
        draw.Vertices=geometry.Vertices;draw.Indices=geometry.Indices;draw.GeometryKey=key;manager.setAxisAlignedBoundingBox(instance,geometry.Bounds);manager.setLayerMask(instance,0xff,c.Options.LayerMask);manager.setPriority(instance,c.Options.Priority);manager.setCastShadows(instance,c.Options.CastShadows);manager.setReceiveShadows(instance,c.Options.ReceiveShadows);
        impl.Engine.getTransformManager().setTransform(impl.Engine.getTransformManager().getInstance(draw.Entity),Matrix(c.Transform));
        if(draw.Added){impl.Backend.GetScene().remove(draw.Entity);draw.Added=false;}
    }
    std::erase_if(impl.SharedMaterials,[](auto& pair){return pair.second.expired();});
    std::unordered_set<UUID> replaced;for(std::size_t i=0;i<commands.size();++i)if(impl.Draws[i].Valid&&commands[i].Source)replaced.insert(commands[i].Source);impl.Backend.GetAssets().BeginDeformedView(replaced);
    for(auto it=impl.Geometries.begin();it!=impl.Geometries.end();)if(!used.contains(it->first)){impl.Destroy(it->second);it=impl.Geometries.erase(it);}else ++it;
}
bool GraphicsRenderer::BeginSelectionMask(filament::Scene& scene,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered,filament::MaterialInstance* selectedMaterial,filament::MaterialInstance* hoverMaterial){
    EndSelectionMask();auto& impl=*m_impl;impl.MaskScene=&scene;const auto& commands=GraphicsAccess::Commands();auto& manager=impl.Engine.getRenderableManager();
    for(std::size_t i=0;i<impl.Draws.size();++i){auto& draw=impl.Draws[i];const auto& command=commands[i];if(!draw.Valid||!command.Source||impl.Backend.IsLODHidden(command.Source)||impl.Backend.IsLODHidden(command.Owner)||impl.Backend.IsEditorHidden(command.Owner)||impl.Backend.IsEditorHidden(command.Source))continue;auto* material=selected.contains(command.Owner)||selected.contains(command.Source)?selectedMaterial:hovered.contains(command.Owner)||hovered.contains(command.Source)?hoverMaterial:nullptr;auto ri=manager.getInstance(draw.Entity);if(!material||!ri)continue;impl.MaskMaterials.push_back({draw.Entity,manager.getMaterialInstanceAt(ri,0)});manager.setMaterialInstanceAt(ri,0,material);scene.addEntity(draw.Entity);}
    return !impl.MaskMaterials.empty();
}
void GraphicsRenderer::EndSelectionMask(){auto& impl=*m_impl;if(!impl.MaskScene)return;for(auto& [entity,material]:impl.MaskMaterials){auto ri=impl.Engine.getRenderableManager().getInstance(entity);if(ri)impl.Engine.getRenderableManager().setMaterialInstanceAt(ri,0,material);impl.MaskScene->remove(entity);}impl.MaskMaterials.clear();impl.MaskScene=nullptr;}
void GraphicsRenderer::CollectUnsupportedLOD(LODViewState& state) const{if(!state.HasFades)return;const auto& commands=GraphicsAccess::Commands();for(std::size_t i=0;i<std::min(m_impl->Draws.size(),commands.size());++i)if(m_impl->Draws[i].Valid){const bool supported=SupportsLODFade(m_impl->Draws[i].Material);state.RejectUnsupported(commands[i].Owner,supported);state.RejectUnsupported(commands[i].Source,supported);}}
void GraphicsRenderer::PrepareLOD(const LODViewState& state){auto& impl=*m_impl;const auto& commands=GraphicsAccess::Commands();for(std::size_t i=0;i<std::min(impl.Draws.size(),commands.size());++i)if(impl.Draws[i].Valid){auto found=state.Masks.find(commands[i].Source?commands[i].Source:commands[i].Owner);if(found!=state.Masks.end())impl.Fades.Apply(impl.Engine,impl.Draws[i].Entity,found->second);}impl.Fades.Restore(impl.Engine);}
void GraphicsRenderer::BeforeCamera(filament::View* view,std::uint64_t viewport){
    auto& impl=*m_impl;impl.Fades.Restore(impl.Engine);auto context=std::find_if(impl.Cameras.begin(),impl.Cameras.end(),[&](const auto& c){return c.View==view&&c.Event.Viewport==viewport;});
    for(auto& [_,lamp]:impl.ParticleLamps){bool visible=context!=impl.Cameras.end()&&!impl.Backend.IsLODHidden(lamp.Owner)&&(!context->Event.IsEditorCamera||!impl.Backend.IsEditorHidden(lamp.Owner));if(visible&&!lamp.Added)impl.Backend.GetScene().addEntity(lamp.Entity);else if(!visible&&lamp.Added)impl.Backend.GetScene().remove(lamp.Entity);lamp.Added=visible;}
    const auto& commands=GraphicsAccess::Commands();for(std::size_t i=0;i<impl.Draws.size();++i){auto& draw=impl.Draws[i];const bool visible=draw.Valid&&!impl.Backend.IsLODHidden(commands[i].Source)&&!impl.Backend.IsLODHidden(commands[i].Owner)&&context!=impl.Cameras.end()&&GraphicsAccess::Matches(commands[i],context->Event)&&(!context->Event.IsEditorCamera||!commands[i].Owner||(!impl.Backend.IsEditorHidden(commands[i].Owner)&&(!commands[i].Source||!impl.Backend.IsEditorHidden(commands[i].Source))));
        if(visible&&!draw.Added)impl.Backend.GetScene().addEntity(draw.Entity);else if(!visible&&draw.Added)impl.Backend.GetScene().remove(draw.Entity);draw.Added=visible;
        if(visible){auto mask=impl.Backend.GetLODMask(commands[i].Source?commands[i].Source:commands[i].Owner);if(mask.Minimum>0||mask.Maximum<1)impl.Fades.Apply(impl.Engine,draw.Entity,mask);}
    }
}
void GraphicsRenderer::AfterCamera(filament::View* view,std::uint64_t viewport){m_impl->Fades.Restore(m_impl->Engine);for(const auto& c:m_impl->Cameras)if(c.View==view&&c.Event.Viewport==viewport){GraphicsAccess::AfterCamera(c.Event);break;}}
void GraphicsRenderer::EndFrame(){for(auto& [_,lamp]:m_impl->ParticleLamps)if(lamp.Added){m_impl->Backend.GetScene().remove(lamp.Entity);lamp.Added=false;}EndSelectionMask();m_impl->Fades.Clear(m_impl->Engine);for(auto& draw:m_impl->Draws)if(draw.Added){m_impl->Backend.GetScene().remove(draw.Entity);draw.Added=false;}m_impl->Backend.GetAssets().EndDeformedView();GraphicsAccess::EndFrame();}
}
