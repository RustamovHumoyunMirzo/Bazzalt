#include "Terrain/TerrainRenderer.h"
#include "Terrain/TerrainGeometryInternal.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Terrain.h"
#include "Bazzalt/Console.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Rendering/ModelGeometry.h"
#include <math/mat3.h>
#include <bit>
#include <map>
#include <limits>
#include <unordered_set>

namespace Bazzalt::Runtime {
namespace {
std::uint64_t Fingerprint(const Terrain& t,unsigned x,unsigned z){std::uint64_t hash=14695981039346656037ull;auto mix=[&](std::uint64_t v){hash^=v;hash*=1099511628211ull;};auto f=[&](float v){mix(std::bit_cast<std::uint32_t>(v));};
    f(t.Size.X);f(t.Size.Y);mix(t.Resolution);mix(t.ChunkSize);mix(t.ShowGrid);f(t.SkirtDepth);
    unsigned loX=x*t.ChunkSize,loZ=z*t.ChunkSize,hiX=std::min(t.Resolution-1,loX+t.ChunkSize+1),hiZ=std::min(t.Resolution-1,loZ+t.ChunkSize+1);loX=loX?loX-1:0;loZ=loZ?loZ-1:0;
    for(const auto& l:t.Layers){mix(l.Enabled);mix(unsigned(l.Blend));f(l.Opacity);f(l.Color.X);f(l.Color.Y);f(l.Color.Z);f(l.Color.W);f(l.Tiling);mix(std::hash<UUID>{}(l.Material));for(unsigned iz=loZ;iz<=hiZ;++iz)for(unsigned ix=loX;ix<=hiX;++ix){f(l.Heights.empty()?0:l.Heights[iz*t.Resolution+ix]);f(l.Weights.empty()?0:l.Weights[iz*t.Resolution+ix]);}}
    for(unsigned iz=loZ;iz<hiZ;++iz)for(unsigned ix=loX;ix<hiX;++ix)mix(t.IsHole(ix,iz));return hash;
}
std::shared_ptr<PrimitiveGeometry> Convert(const TerrainMesh& source){auto g=std::make_shared<PrimitiveGeometry>();g->Vertices.reserve(source.Vertices.size());auto picking=std::make_shared<ModelGeometry>();
    for(const auto& v:source.Vertices){auto n=v.Normal,tangent=Vec3::Cross(std::abs(n.Y)<.999f?Vec3{0,1,0}:Vec3{1,0,0},n).Normalized(),b=Vec3::Cross(n,tangent);auto q=filament::math::mat3f::packTangentFrame({filament::math::float3{tangent.X,tangent.Y,tangent.Z},filament::math::float3{b.X,b.Y,b.Z},filament::math::float3{n.X,n.Y,n.Z}});g->Vertices.push_back({{v.Position.X,v.Position.Y,v.Position.Z},{q.x,q.y,q.z,q.w},{v.UV.X,v.UV.Y}});g->Colors.push_back(v.Color);g->Extents.X=std::max(g->Extents.X,std::abs(v.Position.X));g->Extents.Y=std::max(g->Extents.Y,std::abs(v.Position.Y)+.001f);g->Extents.Z=std::max(g->Extents.Z,std::abs(v.Position.Z));}
    if(!source.Vertices.empty()){auto lo=source.Vertices.front().Position,hi=lo;for(const auto& v:source.Vertices){lo={std::min(lo.X,v.Position.X),std::min(lo.Y,v.Position.Y),std::min(lo.Z,v.Position.Z)};hi={std::max(hi.X,v.Position.X),std::max(hi.Y,v.Position.Y),std::max(hi.Z,v.Position.Z)};}g->Center=(lo+hi)*.5f;g->Extents=(hi-lo)*.5f;g->Extents.Y=std::max(g->Extents.Y,.001f);}
    g->Indices=source.Indices;for(std::size_t i=0;i<g->Indices.size();i+=3)picking->Triangles.push_back({source.Vertices[g->Indices[i]].Position,source.Vertices[g->Indices[i+1]].Position,source.Vertices[g->Indices[i+2]].Position});picking->Build();g->Picking=picking;return g;
}
PrimitiveObject Options(const Terrain& t){PrimitiveObject p;p.Color={1,1,1,1};p.Visible=t.Visible;p.CastShadows=t.CastShadows;p.ReceiveShadows=t.ReceiveShadows;p.LayerMask=t.LayerMask;return p;}
}
void TerrainSystem::Destroy(Resource& resource){auto& assets=m_backend.GetAssets();for(auto& chunk:resource.Chunks)for(auto handle:chunk.Handles)assets.DestroyPrimitive(handle);for(auto handle:resource.Vegetation)assets.DestroyMesh(handle);resource={};}
void TerrainSystem::OnDestroy(Scene&){for(auto& [_,resource]:m_resources)Destroy(resource);m_resources.clear();}
void TerrainSystem::OnUpdate(Scene& scene,float){auto& assets=m_backend.GetAssets();std::unordered_set<UUID> alive;auto view=GetView(scene.GetRegistry());std::vector<Vec3> cameras;
    if(auto editor=m_backend.GetEditorCameraWorld())cameras.push_back(editor->TransformPoint({}));for(auto handle:scene.GetRegistry().view<Camera>()){auto entity=scene.GetEntity(static_cast<Entity::Id>(handle));const auto& c=entity.GetComponent<Camera>();if(c.Enabled&&c.Active)cameras.push_back(entity.GetWorldMatrix().TransformPoint({}));}
    for(auto handle:view){auto entity=scene.GetEntity(static_cast<Entity::Id>(handle));auto id=entity.GetUUID();const auto& t=view.get<Terrain>(handle);if(!t.Enabled||!t.Visible||!t.IsValid())continue;alive.insert(id);auto& resource=m_resources[id];unsigned side=(t.Resolution-1)/t.ChunkSize;if(resource.Chunks.size()!=side*side){Destroy(resource);resource.Chunks.resize(side*side);}auto world=entity.GetWorldMatrix();auto options=Options(t);
        for(unsigned z=0;z<side;++z)for(unsigned x=0;x<side;++x){auto& chunk=resource.Chunks[z*side+x];Vec3 center=world.TransformPoint({((x+.5f)/side-.5f)*t.Size.X,0,((z+.5f)/side-.5f)*t.Size.Y});float distance=std::numeric_limits<float>::max();for(auto eye:cameras)distance=std::min(distance,(center-eye).Length());unsigned lod=cameras.empty()?0:unsigned(std::max(0.f,std::floor(std::log2(std::max(1.f,distance/t.LODDistance)))));lod=std::min(lod,t.MaxLOD);auto fingerprint=resource.Revision!=t.Revision||!chunk.Fingerprint?Fingerprint(t,x,z):chunk.Fingerprint;
            if(chunk.Fingerprint!=fingerprint||chunk.LOD!=lod){for(auto h:chunk.Handles)assets.DestroyPrimitive(h);chunk.Handles.clear();auto mesh=BuildTerrainChunk(t,x,z,lod);std::map<unsigned,std::vector<std::uint32_t>> batches;
                for(std::size_t j=0;j<mesh.Indices.size();j+=3){auto point=(mesh.Vertices[mesh.Indices[j]].Position+mesh.Vertices[mesh.Indices[j+1]].Position+mesh.Vertices[mesh.Indices[j+2]].Position)/3;unsigned sx=unsigned(std::clamp(point.X/t.Size.X+.5f,0.f,1.f)*(t.Resolution-1)),sz=unsigned(std::clamp(point.Z/t.Size.Y+.5f,0.f,1.f)*(t.Resolution-1));float best=-1;unsigned selected=0;for(std::size_t k=0;k<t.Layers.size();++k){const auto& layer=t.Layers[k];float weight=layer.Enabled?layer.Opacity*(layer.Weights.empty()?(k==0?1.f:0.f):layer.Weights[sz*t.Resolution+sx]):0;if(weight>best){best=weight;selected=unsigned(k);}}auto& indices=batches[selected];indices.insert(indices.end(),{mesh.Indices[j],mesh.Indices[j+1],mesh.Indices[j+2]});}
                for(auto& [layer,indices]:batches){auto batch=mesh;batch.Indices=std::move(indices);const auto& palette=t.Layers[layer];if(palette.Material)for(auto& v:batch.Vertices)v.UV=v.UV*palette.Tiling;auto geometry=Convert(batch);auto settings=options;settings.MaterialAsset=palette.Material;chunk.Handles.push_back(assets.CreateGeometry(geometry,settings,t.ShowGrid,true));}chunk.Fingerprint=fingerprint;chunk.LOD=lod;}
            for(auto h:chunk.Handles){assets.UpdatePrimitive(h,world,options);assets.SetEditorOwner(h,id);}
        }
        std::vector<std::filesystem::path> modelPaths;modelPaths.reserve(t.Vegetation.size());for(const auto& prototype:t.Vegetation){auto asset=AssetManager::GetAsset(prototype.Model);modelPaths.push_back(asset&&asset->State==AssetState::Ready?asset->CachePath:std::filesystem::path{});}
        if(resource.Revision!=t.Revision||modelPaths!=resource.ModelPaths){
            std::vector<VegetationKey> keys;keys.reserve(t.Instances.size());
            const auto bits=[](float v){return std::bit_cast<std::uint32_t>(v==0?0.f:v);};
            for(const auto& v:t.Instances){const auto& p=t.Vegetation[v.Prototype];keys.emplace_back(p.Model,p.Material,p.Enabled,std::array<std::uint32_t,8>{bits(v.Position.X),bits(v.Position.Y),bits(v.Position.Z),bits(v.Rotation.X),bits(v.Rotation.Y),bits(v.Rotation.Z),bits(v.Rotation.W),bits(v.Scale)},modelPaths[v.Prototype]);}
            std::size_t prefix=0;while(prefix<keys.size()&&prefix<resource.VegetationKeys.size()&&keys[prefix]==resource.VegetationKeys[prefix])++prefix;
            if(prefix!=keys.size()||prefix!=resource.VegetationKeys.size()){
                std::multimap<VegetationKey,RenderAssets::Handle> previous;for(std::size_t i=prefix;i<resource.Vegetation.size();++i)previous.emplace(resource.VegetationKeys[i],resource.Vegetation[i]);resource.Vegetation.resize(prefix);
                for(std::size_t i=prefix;i<keys.size();++i){auto found=previous.find(keys[i]);if(found!=previous.end()){resource.Vegetation.push_back(found->second);previous.erase(found);}else{const auto& prototype=t.Vegetation[t.Instances[i].Prototype];Mesh mesh;mesh.MeshAsset=prototype.Model;mesh.MaterialAsset=prototype.Material;mesh.Visible=prototype.Enabled;resource.Vegetation.push_back(mesh.Visible&&mesh.MeshAsset?assets.CreateMesh(mesh):RenderAssets::InvalidHandle);}}
                for(const auto& [_,handle]:previous)assets.DestroyMesh(handle);resource.VegetationKeys=std::move(keys);
            }resource.ModelPaths=std::move(modelPaths);
        }
        for(std::size_t i=0;i<t.Instances.size();++i){const auto& v=t.Instances[i];const auto& p=t.Vegetation[v.Prototype];Mesh mesh;mesh.MeshAsset=p.Model;mesh.MaterialAsset=p.Material;mesh.Visible=p.Enabled;if(resource.Vegetation[i]==RenderAssets::PendingHandle)resource.Vegetation[i]=assets.CreateMesh(mesh);assets.UpdateMesh(resource.Vegetation[i],world*Mat4::Transform(v.Position,v.Rotation,Vec3{v.Scale}),mesh);assets.SetEditorOwner(resource.Vegetation[i],id);}resource.Revision=t.Revision;
    }
    for(auto it=m_resources.begin();it!=m_resources.end();)if(!alive.contains(it->first)){Destroy(it->second);it=m_resources.erase(it);}else ++it;
}
}
