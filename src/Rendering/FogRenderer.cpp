#include "Bazzalt/Components/LocalFog.h"
#include "Rendering/FogRenderer.h"
#include "Rendering/LightParameters.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/Light.h"
#include "volumetric_fog_filamat.h"
#include "fog_medium_filamat.h"
#include "fog_integrate_filamat.h"
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/Exposure.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/RenderTarget.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/mat4.h>
#include <map>
#include <algorithm>
#include <set>
#include <tuple>
#include <math/mat3.h>
namespace Bazzalt::Runtime {
using ::operator|;
struct FogRenderer::Impl {
    filament::Engine &Engine;
    filament::Renderer &Renderer;
    filament::Material *Material = nullptr;
    filament::Material *MediumMaterial = nullptr, *IntegratedMaterial = nullptr;
    filament::Skybox *ClearSky = nullptr;
    filament::Camera *Camera = nullptr;
    utils::Entity CameraEntity;
    filament::VertexBuffer *Vertices = nullptr;
    filament::IndexBuffer *Indices = nullptr;
    struct VolumePass {
        filament::Texture* Texture = nullptr;
        filament::RenderTarget* Target = nullptr;
        filament::MaterialInstance* Material = nullptr;
        filament::Scene* Scene = nullptr;
        filament::View* View = nullptr;
        utils::Entity Quad;
    };
    struct Target {
        unsigned Width = 0, Height = 0;
        unsigned GridWidth = 0, GridHeight = 0, Capacity = 128, Rows = 16;
        VolumePass Medium, Integrated;
        std::map<UUID,VolumePass> Locals;
        filament::Texture *Color = nullptr;
        filament::Texture *Depth = nullptr;
        filament::RenderTarget *Framebuffer = nullptr;
        filament::RenderTarget *ColorTarget = nullptr;
        filament::MaterialInstance *Material = nullptr;
        filament::Scene* Scene=nullptr;
        filament::View *View=nullptr,*DepthView=nullptr;
        utils::Entity Quad;
        struct CoverageBinding {
            filament::RenderableManager::Instance Instance;
            std::size_t Primitive;
            filament::MaterialInstance *Original, *Depth;
        };
        struct CoverageMaterial {
            const filament::Material* Source=nullptr;
            filament::MaterialInstance* Depth=nullptr;
            std::array<float,13> Parameters{};
        };
        std::map<filament::MaterialInstance*,CoverageMaterial> CoverageMaterials;
        std::uint64_t MaterialRevision=0;
        std::vector<CoverageBinding> CoverageBindings;
    };
    using TargetKey=std::tuple<filament::View*,unsigned,unsigned,unsigned>;
    std::map<TargetKey, Target> Targets;
    std::uint64_t CoverageCreations=0,TargetCreations=0,PeakTextureBytes=0;
    Impl(filament::Engine &engine, filament::Renderer &renderer) : Engine(engine), Renderer(renderer) {
        Material = filament::Material::Builder()
                       .package(Embedded::VolumetricFogFilamat, Embedded::VolumetricFogFilamatSize)
                       .build(Engine);
        if (!Material)
            throw std::runtime_error("Could not create volumetric fog material");
        MediumMaterial = filament::Material::Builder().package(Embedded::FogMediumFilamat, Embedded::FogMediumFilamatSize).build(Engine);
        IntegratedMaterial = filament::Material::Builder().package(Embedded::FogIntegrateFilamat, Embedded::FogIntegrateFilamatSize).build(Engine);
        if (!MediumMaterial || !IntegratedMaterial) throw std::runtime_error("Could not create froxel volume materials");
        ClearSky = filament::Skybox::Builder().color({0, 0, 0, 1}).showSun(false).build(Engine);
        CameraEntity = Engine.getEntityManager().create();
        Camera = Engine.createCamera(CameraEntity);
        Camera->setProjection(filament::Camera::Projection::ORTHO, -1, 1, -1, 1, 0, 1);
        Vertices = filament::VertexBuffer::Builder()
                       .vertexCount(3)
                       .bufferCount(1)
                       .attribute(filament::VertexAttribute::POSITION, 0,
                                  filament::VertexBuffer::AttributeType::FLOAT3)
                       .build(Engine);
        auto *positions = new float[9]{-1, -1, .5f, 3, -1, .5f, -1, 3, .5f};
        Vertices->setBufferAt(Engine, 0, {positions, sizeof(float) * 9, [](void *data, std::size_t, void *) {
                                              delete[] static_cast<float *>(data);
                                          }});
        Indices = filament::IndexBuffer::Builder()
                      .indexCount(3)
                      .bufferType(filament::IndexBuffer::IndexType::USHORT)
                      .build(Engine);
        auto *indices = new std::uint16_t[3]{0, 1, 2};
        Indices->setBuffer(Engine, {indices, sizeof(std::uint16_t) * 3, [](void *data, std::size_t, void *) {
                                        delete[] static_cast<std::uint16_t *>(data);
                                    }});
    }
    void DestroyVolume(VolumePass& pass) {
        if (pass.View) Engine.destroy(pass.View);
        if (pass.Quad) { Engine.destroy(pass.Quad); Engine.getEntityManager().destroy(pass.Quad); }
        if (pass.Scene) Engine.destroy(pass.Scene);
        if (pass.Material) Engine.destroy(pass.Material);
        if (pass.Target) Engine.destroy(pass.Target);
        if (pass.Texture) Engine.destroy(pass.Texture);
        pass = {};
    }
    void CreateVolume(VolumePass& pass, filament::Material& material, unsigned width, unsigned height, filament::RenderTarget* shared = nullptr) {
        using T = filament::Texture;
        if (!shared) {
            pass.Texture = T::Builder().width(width).height(height).levels(1)
            .sampler(T::Sampler::SAMPLER_2D).format(T::InternalFormat::RGBA16F)
            .usage(T::Usage::COLOR_ATTACHMENT | T::Usage::SAMPLEABLE).build(Engine);
            pass.Target = filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR, pass.Texture).build(Engine);
        }
        pass.Material = material.createInstance();
        pass.Scene = Engine.createScene(); pass.View = Engine.createView();
        pass.View->setScene(pass.Scene); pass.View->setCamera(Camera);
        pass.View->setPostProcessingEnabled(false); pass.View->setShadowingEnabled(false);
        pass.View->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
        pass.View->setRenderTarget(shared ? shared : pass.Target); pass.View->setViewport({0,0,width,height});
        pass.Quad = Engine.getEntityManager().create(); Engine.getTransformManager().create(pass.Quad);
        filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{3,3,1}})
            .material(0,pass.Material).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,Vertices,Indices)
            .culling(false).castShadows(false).receiveShadows(false).build(Engine,pass.Quad);
        pass.Scene->addEntity(pass.Quad);
    }
    void Destroy(Target &target) {
        for(auto& [_,pass]:target.Locals)DestroyVolume(pass);
        target.Locals.clear();
        DestroyVolume(target.Medium);DestroyVolume(target.Integrated);
        for(auto& [_,material]:target.CoverageMaterials)Engine.destroy(material.Depth);
        target.CoverageMaterials.clear();
        if(target.DepthView)Engine.destroy(target.DepthView);
        if(target.View)Engine.destroy(target.View);
        if(target.Quad){Engine.destroy(target.Quad);Engine.getEntityManager().destroy(target.Quad);}
        if(target.Scene)Engine.destroy(target.Scene);
        if(target.Material)Engine.destroy(target.Material);
        if (target.Framebuffer)
            Engine.destroy(target.Framebuffer);
        if (target.ColorTarget)
            Engine.destroy(target.ColorTarget);
        if (target.Color)
            Engine.destroy(target.Color);
        if (target.Depth)
            Engine.destroy(target.Depth);
        target = {};
    }
    ~Impl() {
        for (auto &[_, target] : Targets)
            Destroy(target);
        Engine.destroy(Vertices);
        Engine.destroy(Indices);
        Engine.destroy(ClearSky);
        Engine.destroyCameraComponent(CameraEntity);
        Engine.getEntityManager().destroy(CameraEntity);
        Engine.destroy(Material);
        Engine.destroy(MediumMaterial); Engine.destroy(IntegratedMaterial);
    }
    static filament::math::mat4f Matrix(const Mat4 &matrix) {
        filament::math::mat4f result;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                result[c][r] = matrix(r, c);
        return result;
    }
    static Mat4 Matrix(const filament::math::mat4 &matrix) {
        Mat4 result;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                result(r, c) = float(matrix[c][r]);
        return result;
    }
    void PrepareCoverage(filament::View& input,Target& target,std::uint64_t revision) {
        if(target.MaterialRevision!=revision){
            for(auto& [_,material]:target.CoverageMaterials)Engine.destroy(material.Depth);
            target.CoverageMaterials.clear();target.MaterialRevision=revision;
        }
        target.CoverageBindings.clear();
        auto* scene=input.getScene();if(!scene)return;
        auto& manager=Engine.getRenderableManager();
        std::set<filament::MaterialInstance*> alive;
        scene->forEach([&](utils::Entity entity){
            const auto instance=manager.getInstance(entity);if(!instance)return;
            for(std::size_t index=0;index<manager.getPrimitiveCount(instance);++index){
                auto* original=manager.getMaterialInstanceAt(instance,index);
                if(!original||!original->getMaterial()->hasParameter("bazzaltFogDepth"))continue;
                const bool first=alive.insert(original).second;
                auto& cached=target.CoverageMaterials[original];
                if(!first){target.CoverageBindings.push_back({instance,index,original,cached.Depth});continue;}
                const auto color=original->getParameter<filament::math::float4>("baseColorFactor");
                const bool transformed=original->getMaterial()->hasParameter("baseColorUvMatrix");
                const auto uv=transformed?original->getParameter<filament::math::mat3f>("baseColorUvMatrix"):filament::math::mat3f{};
                std::array<float,13> parameters{color.x,color.y,color.z,color.w};
                for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)parameters[4+c*3+r]=uv[c][r];
                const bool fresh=!cached.Depth||cached.Source!=original->getMaterial();
                if(fresh){
                    if(cached.Depth)Engine.destroy(cached.Depth);
                    cached.Depth=filament::MaterialInstance::duplicate(original);++CoverageCreations;
                    cached.Source=original->getMaterial();
                    cached.Depth->setParameter("bazzaltFogDepth",1.f);
                    cached.Depth->setDepthWrite(true);cached.Depth->setColorWrite(false);
                }
                if(fresh||cached.Parameters!=parameters){
                    cached.Depth->setParameter("baseColorFactor",color);
                    if(transformed)cached.Depth->setParameter("baseColorUvMatrix",uv);
                    cached.Parameters=parameters;
                }
                target.CoverageBindings.push_back({instance,index,original,cached.Depth});
            }
        });
        for(auto it=target.CoverageMaterials.begin();it!=target.CoverageMaterials.end();){
            if(!alive.contains(it->first)){Engine.destroy(it->second.Depth);it=target.CoverageMaterials.erase(it);}else ++it;
        }
    }
    Target& Prepare(filament::View& input,unsigned width,unsigned height,unsigned capacity) {
        auto& target=Targets[{&input,width,height,capacity}];
        if(target.Framebuffer)return target;
        ++TargetCreations;
        target.Width=width;target.Height=height;
        target.GridWidth=(width+15)/16; target.GridHeight=(height+15)/16;
        target.Capacity=capacity;target.Rows=(capacity+7)/8;
        const auto atlasWidth=target.GridWidth*8,atlasHeight=target.GridHeight*target.Rows;
        const auto maxSize=filament::Texture::getMaxTextureSize(Engine,filament::Texture::Sampler::SAMPLER_2D);
        if(width>maxSize||height>maxSize||atlasWidth>maxSize||atlasHeight>maxSize)throw std::runtime_error("Fog viewport exceeds the device texture limit");
        CreateVolume(target.Medium,*MediumMaterial,atlasWidth,atlasHeight);
        CreateVolume(target.Integrated,*IntegratedMaterial,atlasWidth,atlasHeight);
        using T = filament::Texture;
        target.Color = T::Builder()
                           .width(target.Width)
                           .height(target.Height)
                           .levels(1)
                           .sampler(T::Sampler::SAMPLER_2D)
                           .format(T::InternalFormat::RGBA16F)
                           .usage(T::Usage::COLOR_ATTACHMENT | T::Usage::SAMPLEABLE)
                           .build(Engine);
        target.Depth = T::Builder()
                           .width(target.Width)
                           .height(target.Height)
                           .levels(1)
                           .sampler(T::Sampler::SAMPLER_2D)
                           .format(T::InternalFormat::DEPTH32F)
                           .usage(T::Usage::DEPTH_ATTACHMENT | T::Usage::SAMPLEABLE)
                           .build(Engine);
        target.Framebuffer = filament::RenderTarget::Builder()
                                 .texture(filament::RenderTarget::AttachmentPoint::COLOR, target.Color)
                                 .texture(filament::RenderTarget::AttachmentPoint::DEPTH, target.Depth)
                                 .build(Engine);
        target.ColorTarget = filament::RenderTarget::Builder()
                                 .texture(filament::RenderTarget::AttachmentPoint::COLOR, target.Color)
                                 .build(Engine);
        target.Material = Material->createInstance();
        target.Scene=Engine.createScene();target.View=Engine.createView();target.DepthView=Engine.createView();
        target.DepthView->setPostProcessingEnabled(false);target.DepthView->setShadowingEnabled(false);
        target.View->setScene(target.Scene);target.View->setCamera(Camera);
        target.View->setPostProcessingEnabled(false);target.View->setShadowingEnabled(false);
        target.Quad=Engine.getEntityManager().create();Engine.getTransformManager().create(target.Quad);
        filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{3,3,1}})
            .material(0,target.Material).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,Vertices,Indices)
            .culling(false).castShadows(false).receiveShadows(false).build(Engine,target.Quad);
        target.Scene->addEntity(target.Quad);
        const filament::TextureSampler sampler(filament::TextureSampler::MinFilter::NEAREST,
                                               filament::TextureSampler::MagFilter::NEAREST);
        target.Material->setParameter("sceneColor", target.Color, sampler);
        target.Material->setParameter("sceneDepth", target.Depth, sampler);
        target.Medium.Material->setParameter("sceneDepth", target.Depth, sampler);
        const filament::TextureSampler linear(filament::TextureSampler::MinFilter::LINEAR,filament::TextureSampler::MagFilter::LINEAR);
        target.Integrated.Material->setParameter("medium",target.Medium.Texture,linear);
        target.Material->setParameter("medium", target.Medium.Texture, linear);
        target.Material->setParameter("integrated", target.Integrated.Texture, linear);
        return target;
    }
};
FogRenderer::FogRenderer(filament::Engine &engine, filament::Renderer &renderer)
    : m_impl(std::make_unique<Impl>(engine, renderer)) {}
FogRenderer::~FogRenderer() = default;
void FogRenderer::Prepare(const std::vector<filament::View*>& views,std::uint64_t materialRevision,const std::vector<Viewport>& presentations,Scene* scene,const FogSettings* settings) {
    auto& impl=*m_impl;
    unsigned capacity=128;
    if(settings){
        unsigned quality=settings->Enabled&&settings->Mode==FogMode::Volumetric?settings->Steps:4;
        if(scene)for(auto handle:scene->GetRegistry().view<LocalFog>()){
            const auto& local=scene->GetRegistry().get<LocalFog>(handle);
            if(local.Enabled&&local.Fog.Enabled&&local.IsValid())quality=std::max(quality,local.Fog.Steps);
        }
        capacity=4;while(capacity<quality&&capacity<128)capacity*=2;
    }
    std::set<Impl::TargetKey> needed;
    for(auto* view:views)if(view){const auto viewport=view->getViewport();if(viewport.width&&viewport.height)needed.emplace(view,viewport.width,viewport.height,capacity);}
    for(const auto& viewport:presentations)if(viewport.View&&viewport.Width&&viewport.Height)needed.emplace(viewport.View,viewport.Width,viewport.Height,capacity);
    for(auto it=impl.Targets.begin();it!=impl.Targets.end();){
        if(!needed.contains(it->first)){impl.Destroy(it->second);it=impl.Targets.erase(it);}else ++it;
    }
    for(const auto& [view,width,height,slices]:needed) {
        auto& target=impl.Prepare(*view,width,height,slices);
        impl.PrepareCoverage(*view,target,materialRevision);
        std::set<UUID> alive;
        if(scene)for(auto handle:scene->GetRegistry().view<LocalFog>()){
            const auto& local=scene->GetRegistry().get<LocalFog>(handle);
            if(!local.Enabled||!local.Fog.Enabled||!local.IsValid()||local.Fog.Density<=0||local.Fog.MaximumOpacity<=0)continue;
            auto entity=scene->GetEntity(static_cast<Entity::Id>(handle));const auto id=entity.GetUUID();alive.insert(id);
            auto& pass=target.Locals[id];
            if(!pass.View){
                impl.CreateVolume(pass,*impl.MediumMaterial,target.GridWidth*8,target.GridHeight*target.Rows,target.Medium.Target);
                const filament::TextureSampler sampler(filament::TextureSampler::MinFilter::NEAREST,filament::TextureSampler::MagFilter::NEAREST);
                pass.Material->setParameter("sceneDepth",target.Depth,sampler);
            }
        }
        for(auto it=target.Locals.begin();it!=target.Locals.end();)if(!alive.contains(it->first)){impl.DestroyVolume(it->second);it=target.Locals.erase(it);}else ++it;
    }
    GetResourceCounts();
}
std::array<std::uint64_t,5> FogRenderer::GetResourceCounts() const {
    std::uint64_t live=0,bytes=0;
    for(const auto& [_,target]:m_impl->Targets){
        live+=target.CoverageMaterials.size();
        bytes+=std::uint64_t(target.Width)*target.Height*12+std::uint64_t(target.GridWidth)*target.GridHeight*8*target.Rows*16;
    }
    m_impl->PeakTextureBytes=std::max(m_impl->PeakTextureBytes,bytes);
    return {m_impl->CoverageCreations,live,m_impl->TargetCreations,bytes,m_impl->PeakTextureBytes};
}
void FogRenderer::Render(filament::View &input, const FogSettings &fog, Bazzalt::Scene *scene, float time,filament::Renderer* activeRenderer,const std::unordered_set<UUID>* hidden) {
    auto &impl = *m_impl;
    auto& renderer=activeRenderer?*activeRenderer:impl.Renderer;
    auto viewport = input.getViewport();
    const auto found=std::find_if(impl.Targets.begin(),impl.Targets.end(),[&](const auto& entry){
        return std::get<0>(entry.first)==&input&&std::get<1>(entry.first)==viewport.width&&std::get<2>(entry.first)==viewport.height;
    });
    if(found==impl.Targets.end())throw std::logic_error("Fog viewport was not prepared before frame submission");
    auto& target=found->second;
    auto *original = input.getRenderTarget();
    const auto &camera = input.getCamera();
    auto projection = Impl::Matrix(camera.getProjectionMatrix());
    auto world = Impl::Matrix(camera.getModelMatrix());
    const bool global=fog.Enabled&&fog.Mode==FogMode::Volumetric;
    const auto activeLocal=[&](UUID id)->const LocalFog* {
        if(!scene||(hidden&&hidden->contains(id)))return nullptr;
        const auto entity=scene->GetEntity(id);
        const auto* local=entity?entity.TryGetComponent<LocalFog>():nullptr;
        return local&&local->Enabled&&local->Fog.Enabled&&local->IsValid()&&local->Fog.Density>0&&local->Fog.MaximumOpacity>0?local:nullptr;
    };
    const bool locals=std::any_of(target.Locals.begin(),target.Locals.end(),[&](const auto& entry){return activeLocal(entry.first)!=nullptr;});
    float range=global?fog.MaxDistance:1.f;unsigned steps=global?fog.Steps:4;
    bool affectSky=global&&fog.AffectSky;
    for(const auto& [id,pass]:target.Locals){
        const auto* value=activeLocal(id);if(!value)continue;
        const auto entity=scene->GetEntity(id);const auto& local=*value;
        const auto matrix=entity.GetWorldMatrix();const auto center=matrix.TransformPoint({0,0,0});
        const float radius=matrix.TransformDirection({local.Size.X,0,0}).Length()*.5f+matrix.TransformDirection({0,local.Size.Y,0}).Length()*.5f+matrix.TransformDirection({0,0,local.Size.Z}).Length()*.5f;
        const auto eye=world.TransformPoint({0,0,0});
        range=std::max(range,std::min(local.Fog.MaxDistance,(center-eye).Length()+radius));
        steps=std::max(steps,local.Fog.Steps);affectSky|=local.Fog.AffectSky;
    }
    steps=std::min(steps,target.Capacity);
    const auto common=[&](filament::MaterialInstance* pass,unsigned stride=0){
        pass->setParameter("atlasInfo",filament::math::float4{8,float(target.Rows),float(stride),0});
        pass->setParameter("inverseProjection",Impl::Matrix(projection.Inversed()));
        pass->setParameter("gridInfo",filament::math::float4{float(target.GridWidth),float(target.GridHeight),float(steps),range});
        pass->setParameter("cameraInfo",filament::math::float4{camera.getNear(),camera.getCullingFar(),std::abs(projection(3,3))>.5f?1.f:0.f,affectSky?1.f:0.f});
        pass->setParameter("fogInfo",filament::math::float4{1,locals?0.f:fog.StartDistance,range,locals?1.f:fog.MaximumOpacity});
    };
    common(target.Material);common(target.Integrated.Material);
    const auto medium=[&](filament::MaterialInstance* material,const FogSettings& settings,const Mat4& inverse,Vec3 size,float blend,bool local){
        common(material);
        material->setParameter("cameraWorld",Impl::Matrix(world));
        material->setParameter("worldToVolume",Impl::Matrix(inverse));
        material->setParameter("volumeInfo",filament::math::float4{size.X,size.Y,size.Z,local?1.f:0.f});
        material->setParameter("cameraInfo",filament::math::float4{local?blend:0.f,0,std::abs(projection(3,3))>.5f?1.f:0.f,settings.AffectSky?1.f:0.f});
        material->setParameter("fogInfo",filament::math::float4{settings.Enabled?settings.Density:0.f,settings.StartDistance,settings.MaxDistance,settings.MaximumOpacity});
        material->setParameter("heightInfo",filament::math::float4{settings.Height,settings.Mode==FogMode::Exponential?0.f:settings.HeightFalloff,locals?1.f:0.f,0});
        material->setParameter("noiseInfo",filament::math::float4{settings.Mode==FogMode::Volumetric?settings.NoiseAmount:0.f,settings.NoiseScale,settings.Mode==FogMode::Volumetric?settings.Anisotropy:0.f,time});
        material->setParameter("fogColor",filament::math::float3{settings.Color.X,settings.Color.Y,settings.Color.Z});
        material->setParameter("wind",filament::math::float3{settings.Wind.X,settings.Wind.Y,settings.Wind.Z});
    };
    auto globalSettings=fog;if(!global)globalSettings.Enabled=false;
    medium(target.Medium.Material,globalSettings,Mat4{},Vec3{1,1,1},0,false);
    Vec3 direction{0, -1, 0}, color{};
    float intensity = 0;
    if (scene)
        for (auto handle : scene->GetRegistry().view<Light>()) {
            const auto light = SanitizeLight(scene->GetRegistry().get<Light>(handle));
            if (!light.Enabled || (light.Type != LightType::Directional && light.Type != LightType::Sun) ||
                light.Intensity <= intensity)
                continue;
            auto entity = scene->GetEntity(static_cast<Entity::Id>(handle));
            direction = entity.GetWorldMatrix().TransformDirection({0, 0, -1}).Normalized();
            intensity = light.Intensity;
            // Match the source camera exposure instead of an arbitrary lux multiplier.
            color = light.Color * (intensity * filament::Exposure::exposure(camera));
        }
    target.Medium.Material->setParameter("sunDirection", filament::math::float3{direction.X, direction.Y, direction.Z});
    target.Medium.Material->setParameter("sunColor", filament::math::float3{color.X, color.Y, color.Z});
    const auto clear = renderer.getClearOptions();
    auto *sourceScene = input.getScene();
    auto *sky = sourceScene ? sourceScene->getSkybox() : nullptr;
    struct SkyScope {
        filament::Scene *Scene;
        filament::Skybox *Sky;
        ~SkyScope() {
            if (Scene)
                Scene->setSkybox(Sky);
        }
    } skyScope{sourceScene, sky};
    if (sourceScene && !sky) {
        impl.ClearSky->setColor({float(clear.clearColor.r), float(clear.clearColor.g),
                                 float(clear.clearColor.b), float(clear.clearColor.a)});
        sourceScene->setSkybox(impl.ClearSky);
    }
    auto sourceClear = clear;
    sourceClear.clear = true;
    renderer.setClearOptions(sourceClear);
    // Export opaque and alpha-covered foliage depth without changing the color
    // pass's blending. A blended leaf must not inherit the sky's fog distance.
    {
        auto& manager=impl.Engine.getRenderableManager();
        auto bindings=target.CoverageBindings;
        struct CoverageScope {
            filament::RenderableManager& Manager;
            const std::vector<Impl::Target::CoverageBinding>& Bindings;
            ~CoverageScope(){for(const auto& entry:Bindings)Manager.setMaterialInstanceAt(entry.Instance,entry.Primitive,entry.Original);}
        } coverage{manager,bindings};
        for(auto& entry:bindings){
            // Restore the actual draw binding, which can be a per-view LOD variant.
            entry.Original=manager.getMaterialInstanceAt(entry.Instance,entry.Primitive);
            manager.setMaterialInstanceAt(entry.Instance,entry.Primitive,entry.Depth);
        }
        target.DepthView->setScene(input.getScene());
        target.DepthView->setCamera(const_cast<filament::Camera *>(&camera));
        target.DepthView->setVisibleLayers(0xff, input.getVisibleLayers());
        target.DepthView->setFrustumCullingEnabled(input.isFrustumCullingEnabled());
        for (unsigned index = 0; index < 4; ++index)
            target.DepthView->setMaterialGlobal(index, input.getMaterialGlobal(index));
        target.DepthView->setRenderTarget(target.Framebuffer);
        target.DepthView->setViewport({0, 0, target.Width, target.Height});
        try {
            renderer.render(target.DepthView);
        } catch (...) {
            target.DepthView->setRenderTarget(nullptr);
            target.DepthView->setCamera(nullptr);
            target.DepthView->setScene(nullptr);
            renderer.setClearOptions(clear);
            throw;
        }
        target.DepthView->setRenderTarget(nullptr);
        target.DepthView->setCamera(nullptr);
        target.DepthView->setScene(nullptr);
    }
    input.setRenderTarget(target.ColorTarget);
    input.setViewport({0, 0, target.Width, target.Height});
    try {
        renderer.render(&input);
    } catch (...) {
        input.setRenderTarget(original);
        input.setViewport(viewport);
        renderer.setClearOptions(clear);
        throw;
    }
    input.setRenderTarget(original);
    input.setViewport(viewport);
    renderer.setClearOptions(clear);
    // Each pass reads only completed earlier attachments. Filament orders the
    // render-target transitions and texture visibility for every backend.
    auto volumeClear = clear; volumeClear.clear = true; volumeClear.clearColor = {0,0,0,0};
    renderer.setClearOptions(volumeClear);
    try {
        renderer.render(target.Medium.View);
        volumeClear.clear=false;renderer.setClearOptions(volumeClear);
        for(auto& [id,pass]:target.Locals){
            const auto* value=activeLocal(id);if(!value)continue;
            const auto entity=scene->GetEntity(id);const auto& local=*value;Mat4 inverse;
            if(!entity.GetWorldMatrix().TryInverse(inverse))continue;
            medium(pass.Material,local.Fog,inverse,local.Size,local.BlendDistance,true);
            pass.Material->setParameter("sunDirection",filament::math::float3{direction.X,direction.Y,direction.Z});
            pass.Material->setParameter("sunColor",filament::math::float3{color.X,color.Y,color.Z});
            renderer.render(pass.View);
        }
        volumeClear.clear=true;renderer.setClearOptions(volumeClear);
        renderer.render(target.Integrated.View);
    } catch (...) { renderer.setClearOptions(clear); throw; }
    renderer.setClearOptions(clear);
    target.View->setBlendMode(input.getBlendMode());
    target.View->setRenderTarget(const_cast<filament::RenderTarget *>(original));
    target.View->setViewport(viewport);
    try {
        renderer.render(target.View);
    } catch (...) {
        target.View->setRenderTarget(nullptr);
        throw;
    }
    target.View->setRenderTarget(nullptr);
}
} // namespace Bazzalt::Runtime
