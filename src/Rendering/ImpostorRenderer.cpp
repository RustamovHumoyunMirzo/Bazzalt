#include "Rendering/ImpostorRenderer.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/Impostor.h"
#include "Physics/PhysicsModuleApi.h"
#include "Runtime/TextureLibrary.h"
#include <filament/Engine.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <bit>
#include <map>
#include <tuple>
#include "lod_impostor_filamat.h"
namespace Bazzalt::Runtime {
namespace {
Mat4 PublicMatrix(const auto& matrix){Mat4 out;for(int r=0;r<4;++r)for(int c=0;c<4;++c)out(r,c)=float(matrix[c][r]);return out;}
filament::math::mat4f NativeMatrix(const Mat4& matrix){filament::math::mat4f out;for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[c][r]=matrix(r,c);return out;}
}
struct ImpostorRenderer::Impl {
    RenderBackend& Backend;filament::Engine& Engine;
    filament::VertexBuffer* Vertices=nullptr;filament::IndexBuffer* Indices=nullptr;filament::Material* Shader=nullptr;
    struct Resource {utils::Entity Entity;std::shared_ptr<filament::MaterialInstance> Material;};
    std::unordered_map<UUID,Resource> Resources;
    std::unordered_map<UUID,filament::Texture*> Textures;
    using Key=std::tuple<UUID,unsigned,unsigned,unsigned,std::array<std::uint32_t,7>>;
    std::map<Key,std::shared_ptr<filament::MaterialInstance>> Materials;
    std::map<const filament::MaterialInstance*,std::array<std::shared_ptr<filament::MaterialInstance>,2>> MaskMaterials;
    filament::View* Active=nullptr;filament::Scene* MaskScene=nullptr;
    struct Mask {utils::Entity Entity;filament::MaterialInstance* Copy;const filament::MaterialInstance* Original;};
    std::vector<Mask> Masks;
    explicit Impl(RenderBackend& backend):Backend(backend),Engine(backend.GetEngine()){
        const auto* positions=new std::array<float,20>{0,0,0,0,0,1,0,0,1,0,1,1,0,1,1,0,1,0,0,1};
        Vertices=filament::VertexBuffer::Builder().vertexCount(4).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,5*sizeof(float)).attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,3*sizeof(float),5*sizeof(float)).build(Engine);
        Vertices->setBufferAt(Engine,0,{positions->data(),sizeof(*positions),[](void*,std::size_t,void* p){delete static_cast<const std::array<float,20>*>(p);},const_cast<std::array<float,20>*>(positions)});
        auto* indices=new std::array<std::uint16_t,6>{0,1,2,0,2,3};Indices=filament::IndexBuffer::Builder().indexCount(6).bufferType(filament::IndexBuffer::IndexType::USHORT).build(Engine);Indices->setBuffer(Engine,{indices->data(),sizeof(*indices),[](void*,std::size_t,void* p){delete static_cast<std::array<std::uint16_t,6>*>(p);},indices});
        Shader=filament::Material::Builder().package(Embedded::LodImpostorFilamat,Embedded::LodImpostorFilamatSize).build(Engine);
        if(!Shader)throw std::runtime_error("Could not prepare impostor shader");
    }
    void Destroy(Resource& value){Backend.GetScene().remove(value.Entity);Engine.destroy(value.Entity);Engine.getEntityManager().destroy(value.Entity);value.Material.reset();}
    ~Impl(){for(auto& [_,value]:Resources)Destroy(value);MaskMaterials.clear();Materials.clear();Engine.destroy(Shader);Engine.destroy(Vertices);Engine.destroy(Indices);}
};
ImpostorRenderer::ImpostorRenderer(RenderBackend& b):m_impl(std::make_unique<Impl>(b)){}
ImpostorRenderer::~ImpostorRenderer(){EndSelectionMask();EndView();}
bool ImpostorRenderer::IsActive(filament::View* view)const{return m_impl->Active==view;}
void ImpostorRenderer::EndView(){auto& impl=*m_impl;for(auto& [_,value]:impl.Resources)impl.Backend.GetScene().remove(value.Entity);impl.Active=nullptr;}
void ImpostorRenderer::InvalidateTextures(){EndSelectionMask();EndView();auto& impl=*m_impl;for(auto& [_,value]:impl.Resources)impl.Destroy(value);impl.Resources.clear();impl.MaskMaterials.clear();impl.Materials.clear();impl.Textures.clear();}
void ImpostorRenderer::Prepare(Bazzalt::Scene& scene){
    EndView();auto& impl=*m_impl;impl.MaskMaterials.clear();impl.Materials.clear();impl.Textures.clear();std::unordered_set<UUID> alive;
    for(auto handle:scene.GetRegistry().view<Impostor>()){
        auto entity=scene.GetEntity(static_cast<Entity::Id>(handle));const auto id=entity.GetUUID();alive.insert(id);const auto& c=entity.GetComponent<Impostor>();if(!c.Enabled||!c.Visible||!c.IsValid()||!c.Atlas)continue;
        if(!impl.Textures.contains(c.Atlas))impl.Textures[c.Atlas]=impl.Backend.GetAssets().GetGuiTexture(c.Atlas);if(!impl.Textures[c.Atlas])continue;
        auto& resource=impl.Resources[id];if(!resource.Entity){resource.Entity=impl.Engine.getEntityManager().create();impl.Engine.getTransformManager().create(resource.Entity);auto result=filament::RenderableManager::Builder(1).boundingBox({{.5f,.5f,0},{.5f,.5f,.001f}}).material(0,impl.Shader->getDefaultInstance()).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,impl.Vertices,impl.Indices).build(impl.Engine,resource.Entity);if(result!=filament::RenderableManager::Builder::Result::Success)throw std::runtime_error("Could not create impostor renderable");}
    }
    for(auto it=impl.Resources.begin();it!=impl.Resources.end();)if(!alive.contains(it->first)){impl.Destroy(it->second);it=impl.Resources.erase(it);}else ++it;
}
void ImpostorRenderer::BeginView(Bazzalt::Scene& scene,filament::View* view,const std::unordered_set<UUID>& hidden,const std::unordered_set<UUID>& editorHidden,const std::unordered_map<UUID,LODMask>& masks){
    EndView();if(!view||!view->hasCamera())return;auto& impl=*m_impl;impl.Active=view;
    const auto camera=PublicMatrix(view->getCamera().getModelMatrix());const auto eye=camera.TransformPoint({});std::unordered_set<UUID> alive;

    for(auto handle:scene.GetRegistry().view<Impostor>()){
        auto e=scene.GetEntity(static_cast<Entity::Id>(handle));const auto id=e.GetUUID();alive.insert(id);const auto& c=e.GetComponent<Impostor>();
        if(!c.Enabled||!c.Visible||!c.IsValid()||!c.Atlas||hidden.contains(id)||editorHidden.contains(id))continue;
        auto foundTexture=impl.Textures.find(c.Atlas);auto* texture=foundTexture==impl.Textures.end()?nullptr:foundTexture->second;if(GetTextureDescriptor(c.Atlas))texture=impl.Backend.GetAssets().GetGuiTexture(c.Atlas);if(!texture||!impl.Resources.contains(id))continue;
        const auto world=PhysicsRenderMatrix(scene,e);const auto frame=c.SelectFrame(world,eye);auto rect=c.GetAtlasRect(frame);if(texture->getWidth()<c.Columns||texture->getHeight()<c.Rows)continue;const float texelX=1.f/texture->getWidth(),texelY=1.f/texture->getHeight();rect.X+=texelX*.5f;rect.Y+=texelY*.5f;rect.Z=std::max(0.f,rect.Z-texelX);rect.W=std::max(0.f,rect.W-texelY);LODMask fade;if(auto found=masks.find(id);found!=masks.end())fade=found->second;
        std::array<std::uint32_t,7> values{std::bit_cast<std::uint32_t>(c.Color.X),std::bit_cast<std::uint32_t>(c.Color.Y),std::bit_cast<std::uint32_t>(c.Color.Z),std::bit_cast<std::uint32_t>(c.Color.W),std::bit_cast<std::uint32_t>(c.AlphaCutoff),std::bit_cast<std::uint32_t>(fade.Minimum),std::bit_cast<std::uint32_t>(fade.Maximum)};
        Impl::Key key{c.Atlas,frame,c.Columns,c.Rows,values};auto material=impl.Materials[key];if(!material){material={impl.Shader->createInstance(),[&impl](auto* p){impl.Engine.destroy(p);}};impl.Materials[key]=material;}
        material->setParameter("atlas",texture,impl.Backend.GetAssets().GetTextureSampler(texture));material->setParameter("atlasRect",filament::math::float4{rect.X,rect.Y,rect.Z,rect.W});material->setParameter("tint",filament::math::float4{c.Color.X,c.Color.Y,c.Color.Z,c.Color.W});material->setParameter("cutoff",c.AlphaCutoff);material->setParameter("maskColor",filament::math::float3{0,0,0});material->setParameter("bazzaltLodFade",filament::math::float4{fade.Minimum,fade.Maximum,1,0});
        // Immutable selection variants must also exist before beginFrame.
        auto& variants=impl.MaskMaterials[material.get()];if(!variants[0])for(unsigned i=0;i<2;++i){variants[i]={filament::MaterialInstance::duplicate(material.get()),[&impl](auto* p){impl.Engine.destroy(p);}};variants[i]->setParameter("maskColor",i==0?filament::math::float3{1,0,1}:filament::math::float3{0,1,1});}
        auto& value=impl.Resources.at(id);auto& manager=impl.Engine.getRenderableManager();
        auto instance=manager.getInstance(value.Entity);manager.setMaterialInstanceAt(instance,0,material.get());manager.setLayerMask(instance,0xff,c.LayerMask);manager.setCastShadows(instance,c.CastShadows);manager.setReceiveShadows(instance,false);impl.Engine.getTransformManager().setTransform(impl.Engine.getTransformManager().getInstance(value.Entity),NativeMatrix(c.GetMatrix(world,camera)));value.Material=std::move(material);impl.Backend.GetScene().addEntity(value.Entity);
    }
    for(auto it=impl.Resources.begin();it!=impl.Resources.end();)if(!alive.contains(it->first)){impl.Destroy(it->second);it=impl.Resources.erase(it);}else ++it;

}
bool ImpostorRenderer::BeginSelectionMask(filament::Scene& target,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered){
    EndSelectionMask();auto& impl=*m_impl;impl.MaskScene=&target;auto& manager=impl.Engine.getRenderableManager();
    for(auto& [owner,value]:impl.Resources){if(!impl.Backend.GetScene().hasEntity(value.Entity)||(!selected.contains(owner)&&!hovered.contains(owner)))continue;auto variants=impl.MaskMaterials.find(value.Material.get());if(variants==impl.MaskMaterials.end())continue;auto* copy=variants->second[selected.contains(owner)?0:1].get();manager.setMaterialInstanceAt(manager.getInstance(value.Entity),0,copy);target.addEntity(value.Entity);impl.Masks.push_back({value.Entity,copy,value.Material.get()});}
    return !impl.Masks.empty();
}
void ImpostorRenderer::EndSelectionMask(){auto& impl=*m_impl;if(!impl.MaskScene)return;auto& manager=impl.Engine.getRenderableManager();for(const auto& mask:impl.Masks){impl.MaskScene->remove(mask.Entity);auto instance=manager.getInstance(mask.Entity);if(instance)manager.setMaterialInstanceAt(instance,0,mask.Original);}impl.Masks.clear();impl.MaskScene=nullptr;}
}
