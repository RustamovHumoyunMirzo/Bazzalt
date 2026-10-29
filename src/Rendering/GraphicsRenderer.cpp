#include "Rendering/GraphicsRenderer.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Runtime/GraphicsAccess.h"
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
struct Vertex {float P[3],T[4],UV[2],C[4];};
filament::math::mat4f Matrix(const Mat4& v){filament::math::mat4f out;for(int c=0;c<4;++c)for(int r=0;r<4;++r)out[c][r]=v(r,c);return out;}
Mat4 Matrix(const auto& v){Mat4 out;for(int c=0;c<4;++c)for(int r=0;r<4;++r)out(r,c)=float(v[c][r]);return out;}
}
struct GraphicsRenderer::Impl {
    RenderBackend& Backend;filament::Engine& Engine;
    struct Geometry {filament::VertexBuffer* Vertices=nullptr;filament::IndexBuffer* Indices=nullptr;filament::Box Bounds;GraphicsTopology Topology{};std::size_t VertexCount=0,IndexCount=0;};
    struct Draw {utils::Entity Entity;filament::MaterialInstance* Material=nullptr;UUID Asset,Source;Key GeometryKey{};filament::VertexBuffer* Vertices=nullptr;filament::IndexBuffer* Indices=nullptr;bool Added=false,Valid=false,StateOverride=false,Borrowed=false;};
    struct Camera {filament::View* View;GraphicsEvent Event;};
    std::map<Key,Geometry> Geometries;
    std::uint64_t Allocations=0,Builds=0;
    filament::Scene* MaskScene=nullptr;std::vector<std::pair<utils::Entity,filament::MaterialInstance*>> MaskMaterials;
    std::vector<Draw> Draws;std::vector<Camera> Cameras;std::set<UUID> InvalidMaterials;
    explicit Impl(RenderBackend& b):Backend(b),Engine(b.GetEngine()){

    }
    void Destroy(Draw& draw){if(draw.Added)Backend.GetScene().remove(draw.Entity);Engine.destroy(draw.Entity);if(draw.Material&&!draw.Borrowed)Engine.destroy(draw.Material);Engine.getEntityManager().destroy(draw.Entity);}
    void Destroy(Geometry& g){Engine.destroy(g.Vertices);Engine.destroy(g.Indices);}
    ~Impl(){for(auto& draw:Draws)Destroy(draw);for(auto& [key,g]:Geometries)Destroy(g);}
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
            lo={std::min(lo.X,v.Position.X),std::min(lo.Y,v.Position.Y),std::min(lo.Z,v.Position.Z)};hi={std::max(hi.X,v.Position.X),std::max(hi.Y,v.Position.Y),std::max(hi.Z,v.Position.Z)};
        }
        const auto center=(lo+hi)*.5f,extent=(hi-lo)*.5f;result.Bounds={{center.X,center.Y,center.Z},{std::max(extent.X,.0001f),std::max(extent.Y,.0001f),std::max(extent.Z,.0001f)}};
        if(allocate)result.Vertices=filament::VertexBuffer::Builder().vertexCount(std::uint32_t(vertices->size())).bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,offsetof(Vertex,P),sizeof(Vertex))
            .attribute(filament::VertexAttribute::TANGENTS,0,filament::VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,T),sizeof(Vertex))
            .attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,UV),sizeof(Vertex))
            .attribute(filament::VertexAttribute::UV1,0,filament::VertexBuffer::AttributeType::FLOAT2,offsetof(Vertex,UV),sizeof(Vertex))
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
void GraphicsRenderer::BeginFrame(){m_impl->Cameras.clear();GraphicsAccess::BeginFrame();}
void GraphicsRenderer::AddCamera(filament::View* view,UUID camera,std::uint64_t viewport,bool editor){
    if(!view)return;GraphicsEvent event;event.Camera=camera;event.Viewport=viewport;event.IsEditorCamera=editor;
    const auto bounds=view->getViewport();event.Width=bounds.width;event.Height=bounds.height;event.View=Matrix(view->getCamera().getViewMatrix());event.Projection=Matrix(view->getCamera().getProjectionMatrix());event.ViewProjection=event.Projection*event.View;
    m_impl->Cameras.push_back({view,event});GraphicsAccess::BeforeCamera(event);
}
void GraphicsRenderer::Prepare(){
    auto& impl=*m_impl;GraphicsAccess::BeforeDraws();const auto& commands=GraphicsAccess::Commands();
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
            if(draw.Material!=inherited){if(!rebuild)manager.setMaterialInstanceAt(manager.getInstance(draw.Entity),0,inherited);if(draw.Material&&!draw.Borrowed)impl.Engine.destroy(draw.Material);draw.Material=inherited;}
            draw.Borrowed=true;draw.Asset={};draw.StateOverride=false;
        }else if(draw.Borrowed||draw.Asset!=c.Material||draw.StateOverride!=stateOverride||!draw.Material||(c.Material&&!impl.Backend.GetAssets().UpdateSurfaceMaterial(draw.Material,c.Material))){
            auto* replacement=c.Material?impl.Backend.GetAssets().CreateSurfaceMaterial(c.Material):impl.Backend.GetAssets().CreateGraphicsMaterial();
            if(!replacement){draw.Valid=false;if(draw.Added){impl.Backend.GetScene().remove(draw.Entity);draw.Added=false;}if(impl.InvalidMaterials.size()>128)impl.InvalidMaterials.clear();if(impl.InvalidMaterials.insert(c.Material).second)Console::Warning("Skipping a Graphics draw with an unavailable or incompatible surface material: "+c.Material.ToString(),"Graphics");continue;}
            impl.InvalidMaterials.erase(c.Material);draw.StateOverride=stateOverride;
            if(c.Material)impl.Backend.GetAssets().UpdateSurfaceMaterial(replacement,c.Material);
            if(!rebuild)manager.setMaterialInstanceAt(manager.getInstance(draw.Entity),0,replacement);
            if(draw.Material&&!draw.Borrowed)impl.Engine.destroy(draw.Material);draw.Material=replacement;draw.Asset=c.Material;draw.Borrowed=false;
        }
        draw.Valid=true;draw.Source=c.Source;
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
    std::unordered_set<UUID> replaced;for(std::size_t i=0;i<commands.size();++i)if(impl.Draws[i].Valid&&commands[i].Source)replaced.insert(commands[i].Source);impl.Backend.GetAssets().BeginDeformedView(replaced);
    for(auto it=impl.Geometries.begin();it!=impl.Geometries.end();)if(!used.contains(it->first)){impl.Destroy(it->second);it=impl.Geometries.erase(it);}else ++it;
}
bool GraphicsRenderer::BeginSelectionMask(filament::Scene& scene,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered,filament::MaterialInstance* selectedMaterial,filament::MaterialInstance* hoverMaterial){
    EndSelectionMask();auto& impl=*m_impl;impl.MaskScene=&scene;const auto& commands=GraphicsAccess::Commands();auto& manager=impl.Engine.getRenderableManager();
    for(std::size_t i=0;i<impl.Draws.size();++i){auto& draw=impl.Draws[i];const auto& command=commands[i];if(!draw.Valid||!command.Source||impl.Backend.IsLODHidden(command.Source)||impl.Backend.IsLODHidden(command.Owner)||impl.Backend.IsEditorHidden(command.Owner)||impl.Backend.IsEditorHidden(command.Source))continue;auto* material=selected.contains(command.Owner)||selected.contains(command.Source)?selectedMaterial:hovered.contains(command.Owner)||hovered.contains(command.Source)?hoverMaterial:nullptr;auto ri=manager.getInstance(draw.Entity);if(!material||!ri)continue;impl.MaskMaterials.push_back({draw.Entity,manager.getMaterialInstanceAt(ri,0)});manager.setMaterialInstanceAt(ri,0,material);scene.addEntity(draw.Entity);}
    return !impl.MaskMaterials.empty();
}
void GraphicsRenderer::EndSelectionMask(){auto& impl=*m_impl;if(!impl.MaskScene)return;for(auto& [entity,material]:impl.MaskMaterials){auto ri=impl.Engine.getRenderableManager().getInstance(entity);if(ri)impl.Engine.getRenderableManager().setMaterialInstanceAt(ri,0,material);impl.MaskScene->remove(entity);}impl.MaskMaterials.clear();impl.MaskScene=nullptr;}
void GraphicsRenderer::BeforeCamera(filament::View* view,std::uint64_t viewport){
    auto& impl=*m_impl;auto context=std::find_if(impl.Cameras.begin(),impl.Cameras.end(),[&](const auto& c){return c.View==view&&c.Event.Viewport==viewport;});
    const auto& commands=GraphicsAccess::Commands();for(std::size_t i=0;i<impl.Draws.size();++i){auto& draw=impl.Draws[i];const bool visible=draw.Valid&&!impl.Backend.IsLODHidden(commands[i].Source)&&!impl.Backend.IsLODHidden(commands[i].Owner)&&context!=impl.Cameras.end()&&GraphicsAccess::Matches(commands[i],context->Event)&&(!context->Event.IsEditorCamera||!commands[i].Owner||(!impl.Backend.IsEditorHidden(commands[i].Owner)&&(!commands[i].Source||!impl.Backend.IsEditorHidden(commands[i].Source))));
        if(visible&&!draw.Added)impl.Backend.GetScene().addEntity(draw.Entity);else if(!visible&&draw.Added)impl.Backend.GetScene().remove(draw.Entity);draw.Added=visible;
    }
}
void GraphicsRenderer::AfterCamera(filament::View* view,std::uint64_t viewport){for(const auto& c:m_impl->Cameras)if(c.View==view&&c.Event.Viewport==viewport){GraphicsAccess::AfterCamera(c.Event);break;}}
void GraphicsRenderer::EndFrame(){EndSelectionMask();for(auto& draw:m_impl->Draws)if(draw.Added){m_impl->Backend.GetScene().remove(draw.Entity);draw.Added=false;}m_impl->Backend.GetAssets().EndDeformedView();GraphicsAccess::EndFrame();}
}
