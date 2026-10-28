#include "Physics/PhysicsGeometry.h"
#include "Physics/PhysicsKeys.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/ModelNode.h"
#include "Rendering/ModelGeometry.h"
#include "Rendering/PrimitiveGeometry.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
namespace Bazzalt::Runtime {
namespace {
struct Part {Entity Object;UUID Asset;std::uint32_t Node=Mesh::EntireAsset;Mat4 Local;bool Subtree=false;};
void Parts(Entity entity,UUID asset,std::vector<Part>& parts,const Mat4& local={},int depth=0){
    if(!entity||depth>256)return;
    if(depth>0&&(entity.HasComponent<Rigidbody>()||entity.HasComponent<Cloth>()||entity.HasComponent<Rope>()))return;
    if(asset){
        auto* mesh=entity.TryGetComponent<Mesh>();auto* node=entity.TryGetComponent<ModelNode>();
        if(mesh&&mesh->MeshAsset==asset)parts.push_back({entity,asset,mesh->ModelNodeIndex,local});
        else if(node&&node->ModelAsset==asset)parts.push_back({entity,asset,node->SourceIndex,local,!node->HasMesh});
        else parts.push_back({entity,asset,Mesh::EntireAsset,local});
        return;
    }
    if(entity.HasComponent<PrimitiveObject>()){parts.push_back({entity,{},Mesh::EntireAsset,local});return;}
    if(auto* mesh=entity.TryGetComponent<Mesh>();mesh&&mesh->MeshAsset){parts.push_back({entity,mesh->MeshAsset,mesh->ModelNodeIndex,local});return;}
    if(auto* node=entity.TryGetComponent<ModelNode>();node&&node->HasMesh&&node->ModelAsset){parts.push_back({entity,node->ModelAsset,node->SourceIndex,local});return;}
    for(auto child:entity.GetChildren())Parts(child,{},parts,local*child.GetComponent<Transform>().GetMatrix(),depth+1);
}
std::string PrimitiveKey(const PrimitiveObject& p){std::string key="primitive";AppendPhysicsKey(key,p.Shape);AppendPhysicsKey(key,p.Size);for(float v:{p.Radius,p.Height,p.Width,p.Depth,p.MajorRadius,p.MinorRadius})AppendPhysicsKey(key,v);AppendPhysicsKey(key,p.Segments);AppendPhysicsKey(key,p.Rings);return key;}
std::string Revision(UUID id){
    auto info=AssetManager::GetAsset(id);if(!info||info->State!=AssetState::Ready)return "missing";
    const auto path=info->CachePath.generic_u8string();std::string key(reinterpret_cast<const char*>(path.data()),path.size());
    std::error_code error;auto time=std::filesystem::last_write_time(info->CachePath,error);if(error)return key+"missing";
    const auto elapsed=time.time_since_epoch();const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(elapsed);const auto fraction=std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed-seconds);
    AppendPhysicsKey(key,seconds.count());AppendPhysicsKey(key,fraction.count());return key;
}
struct ModelEntry {std::string Revision;std::shared_ptr<ModelGeometryAsset> Geometry;std::size_t Bytes;};
std::shared_ptr<ModelGeometryAsset> Model(UUID id){
    static std::map<UUID,ModelEntry> cache;static std::size_t bytes=0;constexpr std::size_t Limit=128u*1024u*1024u;
    auto revision=Revision(id);if(auto found=cache.find(id);found!=cache.end()&&found->second.Revision==revision)return found->second.Geometry;
    auto asset=AssetManager::GetAsset(id);if(!asset||asset->State!=AssetState::Ready)return {};
    std::ifstream file(asset->CachePath,std::ios::binary|std::ios::ate);auto size=file.tellg();if(size<=0||size>128*1024*1024)return {};
    std::vector<std::uint8_t> data(std::size_t(size),0);file.seekg(0);if(!file.read(reinterpret_cast<char*>(data.data()),size))return {};
    auto geometry=ReadModelGeometry(data,asset->CachePath);if(!geometry)return {};
    std::size_t used=0;for(auto& node:geometry->SourceNodes)if(node)used+=node->Triangles.size()*sizeof(ModelGeometry::Triangle)+node->Nodes.size()*sizeof(ModelGeometry::Node)+node->Order.size()*sizeof(std::uint32_t);
    if(auto found=cache.find(id);found!=cache.end()){bytes-=found->second.Bytes;cache.erase(found);}
    if(used<=Limit){while(!cache.empty()&&(bytes+used>Limit||cache.size()>=128)){bytes-=cache.begin()->second.Bytes;cache.erase(cache.begin());}cache.emplace(id,ModelEntry{std::move(revision),geometry,used});bytes+=used;}
    return geometry;
}
struct Geometry {std::vector<Vec3> Vertices;std::vector<std::uint32_t> Indices;};
}
std::string PhysicsMeshSignature(Entity entity,UUID asset,PhysicsMeshRevisions& revisions){
    std::vector<Part> parts;Parts(entity,asset,parts);std::string key;
    for(auto& part:parts){AppendPhysicsKey(key,part.Local);AppendPhysicsKey(key,part.Asset);AppendPhysicsKey(key,part.Node);AppendPhysicsKey(key,part.Subtree);
        if(part.Asset){auto found=revisions.find(part.Asset);if(found==revisions.end())found=revisions.emplace(part.Asset,Revision(part.Asset)).first;AppendPhysicsKey(key,found->second.size());key+=found->second;}
        else key+=PrimitiveKey(part.Object.GetComponent<PrimitiveObject>());
    }return key;
}
bool ReadPhysicsMesh(Entity entity,UUID assetId,const std::string& signature,std::vector<Vec3>& vertices,std::vector<std::uint32_t>& indices){
    static std::map<std::string,Geometry> cache;static std::size_t bytes=0;constexpr std::size_t Limit=64u*1024u*1024u;
    if(auto found=cache.find(signature);found!=cache.end()){vertices=found->second.Vertices;indices=found->second.Indices;return !indices.empty();}
    std::vector<Part> parts;Parts(entity,assetId,parts);
    const auto add=[&](Vec3 point){if(vertices.size()>=3000000)throw std::length_error("MeshCollider exceeds one million triangles");indices.push_back(std::uint32_t(vertices.size()));vertices.push_back(point);};
    for(auto& part:parts){
        if(!part.Asset){auto geometry=BuildPrimitiveGeometry(part.Object.GetComponent<PrimitiveObject>());for(auto index:geometry.Indices){const auto& p=geometry.Vertices.at(index).Position;add(part.Local.TransformPoint({p[0],p[1],p[2]}));}continue;}
        auto geometry=Model(part.Asset);if(!geometry)return false;
        const auto append=[&](std::uint32_t node,const Mat4& transform){if(node>=geometry->SourceNodes.size()||!geometry->SourceNodes[node])return;for(auto& triangle:geometry->SourceNodes[node]->Triangles)for(auto point:{triangle.A,triangle.B,triangle.C})add(transform.TransformPoint(point));};
        if(part.Node!=Mesh::EntireAsset&&!part.Subtree)append(part.Node,part.Local);
        else if(auto model=AssetManager::LoadModel(part.Asset)){
            const auto visit=[&](auto&& self,std::uint32_t index,const Mat4& parent,int depth)->void{if(index>=model->Nodes.size()||depth>256)return;const auto& node=model->Nodes[index];const auto world=parent*Mat4::Transform(node.Position,node.Rotation,node.Scale);if(node.HasMesh())append(node.SourceIndex,world);for(auto child:node.Children)self(self,child,world,depth+1);};
            if(part.Subtree){
                auto root=std::find_if(model->Nodes.begin(),model->Nodes.end(),[&](const auto& node){return node.SourceIndex==part.Node;});
                if(root==model->Nodes.end())return false;
                if(root->HasMesh())append(root->SourceIndex,part.Local);
                for(auto child:root->Children)visit(visit,child,part.Local,0);
            }else for(auto root:model->Roots)visit(visit,root,part.Local,0);
        }else return false;
    }
    const auto used=vertices.size()*sizeof(Vec3)+indices.size()*sizeof(std::uint32_t);
    if(!indices.empty()&&used<=Limit){while(!cache.empty()&&(bytes+used>Limit||cache.size()>=128)){bytes-=cache.begin()->second.Vertices.size()*sizeof(Vec3)+cache.begin()->second.Indices.size()*sizeof(std::uint32_t);cache.erase(cache.begin());}cache.emplace(signature,Geometry{vertices,indices});bytes+=used;}
    return !indices.empty();
}
bool ReadSoftMesh(Entity entity,std::vector<SoftMeshPart>& output){
    std::vector<Part> sources;Parts(entity,{},sources);std::size_t total=0;
    for(auto& part:sources){
        Mat4 inverse;if(!part.Local.TryInverse(inverse))throw std::invalid_argument("Cloth mesh has a singular source transform");const auto normals=inverse.Transposed();
        if(!part.Asset){
            const auto& primitive=part.Object.GetComponent<PrimitiveObject>();auto source=BuildPrimitiveGeometry(primitive);SoftMeshPart mesh;mesh.Owner=part.Object.GetUUID();mesh.Indices=source.Indices;
            for(auto& v:source.Vertices){auto& t=v.TangentFrame;auto normal=Quaternion{t[0],t[1],t[2],t[3]}.Rotate({0,0,1});mesh.Vertices.push_back({part.Local.TransformPoint({v.Position[0],v.Position[1],v.Position[2]}),normals.TransformDirection(normal).Normalized(),{v.UV[0],v.UV[1]},{1,1,1,1}});}
            total+=mesh.Indices.size();output.push_back(std::move(mesh));continue;
        }
        if(!part.Object.HasComponent<Mesh>())continue;
        auto asset=Model(part.Asset);if(!asset)return false;
        const auto append=[&](std::uint32_t node,const Mat4& transform){
            if(node>=asset->SourceNodes.size()||!asset->SourceNodes[node])return;
            Mat4 inverse;if(!transform.TryInverse(inverse))throw std::invalid_argument("Cloth source transform is singular");auto normalMatrix=inverse.Transposed();
            std::map<unsigned,std::size_t> slots;std::map<unsigned,std::map<std::string,unsigned>> remaps;
            for(auto& t:asset->SourceNodes[node]->Triangles){
                auto [it,added]=slots.emplace(t.Slot,output.size());if(added){SoftMeshPart mesh;mesh.Owner=part.Object.GetUUID();mesh.Node=node;mesh.Slot=t.Slot;output.push_back(std::move(mesh));}auto& mesh=output[it->second];auto& remap=remaps[t.Slot];
                const Vec3 positions[]{t.A,t.B,t.C},normals[]{t.NA,t.NB,t.NC};const Vec2 uv[]{t.UVA,t.UVB,t.UVC};
                for(int i=0;i<3;++i){GraphicsVertex v;v.Position=transform.TransformPoint(positions[i]);v.Normal=normalMatrix.TransformDirection(normals[i]).Normalized();v.UV=uv[i];std::string key;AppendPhysicsKey(key,v.Position);AppendPhysicsKey(key,v.Normal);AppendPhysicsKey(key,v.UV.X);AppendPhysicsKey(key,v.UV.Y);auto [found,newVertex]=remap.emplace(key,unsigned(mesh.Vertices.size()));if(newVertex)mesh.Vertices.push_back(v);mesh.Indices.push_back(found->second);}
                total+=3;if(total>262144)throw std::length_error("Cloth source exceeds the supported triangle budget");
            }
        };
        if(part.Node!=Mesh::EntireAsset&&!part.Subtree)append(part.Node,part.Local);
        else if(auto model=AssetManager::LoadModel(part.Asset)){
            const auto visit=[&](auto&& self,std::uint32_t index,const Mat4& parent,int depth)->void{if(index>=model->Nodes.size()||depth>256)return;const auto& node=model->Nodes[index];auto transform=parent*Mat4::Transform(node.Position,node.Rotation,node.Scale);if(node.HasMesh())append(node.SourceIndex,transform);for(auto child:node.Children)self(self,child,transform,depth+1);};
            if(part.Subtree){auto root=std::find_if(model->Nodes.begin(),model->Nodes.end(),[&](const auto& node){return node.SourceIndex==part.Node;});if(root==model->Nodes.end())return false;if(root->HasMesh())append(root->SourceIndex,part.Local);for(auto child:root->Children)visit(visit,child,part.Local,0);}
            else for(auto root:model->Roots)visit(visit,root,part.Local,0);
        }
    }
    return !output.empty()&&std::any_of(output.begin(),output.end(),[](const auto& mesh){return !mesh.Indices.empty();});
}

unsigned RefineSoftMesh(std::vector<SoftMeshPart>& parts,unsigned maximumLevel){
    unsigned level=0;
    for(;level<maximumLevel;++level){
        std::size_t vertices=0,indices=0;std::vector<std::map<std::pair<unsigned,unsigned>,unsigned>> edges(parts.size());
        for(std::size_t p=0;p<parts.size();++p){auto& part=parts[p];vertices+=part.Vertices.size();indices+=part.Indices.size()*4;for(std::size_t i=0;i+2<part.Indices.size();i+=3)for(int j=0;j<3;++j){unsigned a=part.Indices[i+j],b=part.Indices[i+(j+1)%3];edges[p].try_emplace({std::min(a,b),std::max(a,b)},0);}vertices+=edges[p].size();}
        if(vertices>16384||indices>262144)break;
        for(std::size_t p=0;p<parts.size();++p){auto& part=parts[p];for(auto& [edge,index]:edges[p]){auto a=part.Vertices[edge.first],b=part.Vertices[edge.second];GraphicsVertex v;v.Position=(a.Position+b.Position)*.5f;v.Normal=(a.Normal+b.Normal).Normalized();v.UV=(a.UV+b.UV)*.5f;v.Color=(a.Color+b.Color)*.5f;index=unsigned(part.Vertices.size());part.Vertices.push_back(v);}
            std::vector<unsigned> refined;refined.reserve(part.Indices.size()*4);const auto middle=[&](unsigned a,unsigned b){return edges[p].at({std::min(a,b),std::max(a,b)});};
            for(std::size_t i=0;i+2<part.Indices.size();i+=3){unsigned a=part.Indices[i],b=part.Indices[i+1],c=part.Indices[i+2],ab=middle(a,b),bc=middle(b,c),ca=middle(c,a);refined.insert(refined.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});}part.Indices=std::move(refined);
        }
    }
    return level;
}

}
