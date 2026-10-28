#include "Bazzalt/Scene.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Rendering/ModelGeometry.h"
#include "Rendering/PrimitiveGeometry.h"
#include <fstream>
#include <map>
namespace Bazzalt::Runtime {
bool ReadPhysicsMesh(Entity entity,UUID assetId,std::vector<Vec3>& vertices,std::vector<std::uint32_t>& indices){
    if(!assetId)if(const auto* primitive=entity.TryGetComponent<PrimitiveObject>()){
        auto geometry=BuildPrimitiveGeometry(*primitive);vertices.reserve(geometry.Vertices.size());for(const auto& v:geometry.Vertices)vertices.push_back({v.Position[0],v.Position[1],v.Position[2]});indices=std::move(geometry.Indices);return !indices.empty();
    }
    auto* mesh=entity.TryGetComponent<Mesh>();if(!assetId&&mesh)assetId=mesh->MeshAsset;if(!assetId)return false;
    auto asset=AssetManager::GetAsset(assetId);if(!asset||asset->State!=AssetState::Ready)return false;
    struct Cache {std::filesystem::path Path;std::filesystem::file_time_type Stamp;std::shared_ptr<ModelGeometryAsset> Geometry;};
    static std::map<UUID,Cache> cache;std::error_code error;auto stamp=std::filesystem::last_write_time(asset->CachePath,error);if(error)return false;auto found=cache.find(assetId);
    if(found==cache.end()||found->second.Path!=asset->CachePath||found->second.Stamp!=stamp){
        std::ifstream file(asset->CachePath,std::ios::binary|std::ios::ate);auto size=file.tellg();if(size<=0||size>128*1024*1024)return false;std::vector<std::uint8_t> bytes(std::size_t(size),0);file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),size))return false;
        auto geometry=ReadModelGeometry(bytes,asset->CachePath);if(cache.size()>128)cache.clear();found=cache.insert_or_assign(assetId,Cache{asset->CachePath,stamp,std::move(geometry)}).first;
    }
    auto geometry=found->second.Geometry;if(!geometry)return false;
    const auto append=[&](std::uint32_t index,const Mat4& transform){if(index>=geometry->SourceNodes.size()||!geometry->SourceNodes[index])return;for(const auto& triangle:geometry->SourceNodes[index]->Triangles){if(vertices.size()>3000000)throw std::length_error("MeshCollider exceeds one million triangles");for(auto point:{triangle.A,triangle.B,triangle.C}){indices.push_back(std::uint32_t(vertices.size()));vertices.push_back(transform.TransformPoint(point));}}};
    if(mesh&&mesh->MeshAsset==assetId&&mesh->ModelNodeIndex!=Mesh::EntireAsset)append(mesh->ModelNodeIndex,Mat4{});
    else if(auto model=AssetManager::LoadModel(assetId)){
        const auto visit=[&](auto&& self,std::uint32_t index,const Mat4& parent,int depth)->void{if(index>=model->Nodes.size()||depth>256)return;const auto& node=model->Nodes[index];const auto world=parent*Mat4::Transform(node.Position,node.Rotation,node.Scale);if(node.HasMesh())append(node.SourceIndex,world);for(auto child:node.Children)self(self,child,world,depth+1);};
        for(auto root:model->Roots)visit(visit,root,Mat4{},0);
    }else for(std::uint32_t i=0;i<geometry->SourceNodes.size();++i)append(i,Mat4{});
    return !indices.empty();
}
}
