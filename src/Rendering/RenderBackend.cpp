#include "Rendering/RenderBackend.h"

#include <algorithm>
#include <cmath>

#include <filament/Engine.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/IndirectLight.h>
#include <filament/Texture.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Box.h>
#include <backend/BufferDescriptor.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>
#include <image/Ktx1Bundle.h>
#include <ktxreader/Ktx1Reader.h>
#include "Rendering/RenderAssets.h"
#include "default_environment_ibl.h"
#include "default_environment_skybox.h"
#include "editor_gizmo_filamat.h"
#include "editor_translate_gizmo_glb.h"
#include "editor_scale_gizmo_glb.h"
#include "editor_rotate_gizmo_glb.h"

namespace Bazzalt::Runtime {

struct RenderBackend::ViewportResource {
    ViewportKind Kind = ViewportKind::Game;
    filament::SwapChain* SwapChain = nullptr;
    filament::View* View = nullptr;
    filament::Camera* Camera = nullptr;
    utils::Entity CameraEntity;
    std::uint32_t Width = 1;
    std::uint32_t Height = 1;
    filament::math::float3 Eye{6.0f, 4.0f, 8.0f};
};

struct RenderBackend::GizmoResource {
    filament::Material* Material = nullptr;
    std::array<filament::VertexBuffer*, 3> Vertices{};
    std::array<filament::IndexBuffer*, 3> Indices{};
    std::array<filament::MaterialInstance*, 3> Instances{};
    std::array<utils::Entity, 9> Entities{};
    filament::VertexBuffer* GridVertices = nullptr;
    filament::IndexBuffer* GridIndices = nullptr;
    filament::MaterialInstance* GridInstance = nullptr;
    utils::Entity GridEntity{};
};

namespace {
std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
const std::uint8_t* GlbBinary(const std::uint8_t* bytes, std::size_t size) {
    if (size < 28 || ReadU32(bytes) != 0x46546c67u) return nullptr;
    const std::size_t binaryHeader = 20u + ReadU32(bytes + 12);
    return binaryHeader + 8 <= size ? bytes + binaryHeader + 8 : nullptr;
}
}

RenderBackend::RenderBackend() = default;

RenderBackend::~RenderBackend() { Shutdown(); }

bool RenderBackend::Initialize() {
    if (m_engine != nullptr) return true;
    m_engine = filament::Engine::create();
    if (m_engine == nullptr) return false;
    m_renderer = m_engine->createRenderer();
    m_scene = m_engine->createScene();
    if (m_renderer == nullptr || m_scene == nullptr) { Shutdown(); return false; }
    m_renderer->setClearOptions({.clearColor = {0.055, 0.065, 0.085, 1.0}, .clear = true});
    auto* iblBundle = new image::Ktx1Bundle(Embedded::DefaultEnvironmentIbl,
                                            Embedded::DefaultEnvironmentIblSize);
    auto* skyboxBundle = new image::Ktx1Bundle(Embedded::DefaultEnvironmentSkybox,
                                               Embedded::DefaultEnvironmentSkyboxSize);
    m_environmentIblTexture = ktxreader::Ktx1Reader::createTexture(m_engine, iblBundle, false);
    m_environmentSkyboxTexture = ktxreader::Ktx1Reader::createTexture(m_engine, skyboxBundle, false);
    if (m_environmentIblTexture && m_environmentSkyboxTexture) {
        m_indirectLight = filament::IndirectLight::Builder()
            .reflections(m_environmentIblTexture).intensity(30000.0f).build(*m_engine);
        m_skybox = filament::Skybox::Builder()
            .environment(m_environmentSkyboxTexture).intensity(30000.0f).showSun(false).build(*m_engine);
        m_scene->setIndirectLight(m_indirectLight);
        m_scene->setSkybox(m_skybox);
    }
    m_gizmo = std::make_unique<GizmoResource>();
    m_gizmo->Material = filament::Material::Builder()
        .package(Embedded::EditorGizmoFilamat, Embedded::EditorGizmoFilamatSize).build(*m_engine);
    struct Model { const std::uint8_t* Bytes; std::size_t Size; std::uint32_t Vertices, Indices, IndexOffset; };
    const Model models[] = {
        {Embedded::EditorTranslateGizmoGlb, Embedded::EditorTranslateGizmoGlbSize, 75, 198, 2400},
        {Embedded::EditorRotateGizmoGlb, Embedded::EditorRotateGizmoGlbSize, 297, 1536, 9504},
        {Embedded::EditorScaleGizmoGlb, Embedded::EditorScaleGizmoGlbSize, 64, 168, 2048}
    };
    for (int model=0; model<3; ++model) {
        const auto* binary = GlbBinary(models[model].Bytes, models[model].Size);
        if (!binary) { Shutdown(); return false; }
        m_gizmo->Vertices[model] = filament::VertexBuffer::Builder().vertexCount(models[model].Vertices).bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, filament::VertexBuffer::AttributeType::FLOAT3)
            .build(*m_engine);
        m_gizmo->Vertices[model]->setBufferAt(*m_engine, 0,
            {binary, models[model].Vertices * sizeof(float) * 3});
        m_gizmo->Indices[model] = filament::IndexBuffer::Builder().indexCount(models[model].Indices)
            .bufferType(filament::IndexBuffer::IndexType::USHORT).build(*m_engine);
        m_gizmo->Indices[model]->setBuffer(*m_engine,
            {binary + models[model].IndexOffset, models[model].Indices * sizeof(std::uint16_t)});
    }
    const filament::math::float4 colors[] = {{.95f,.18f,.15f,1},{.25f,.9f,.25f,1},{.2f,.45f,1,1}};
    for (int axis=0; axis<3; ++axis) {
        m_gizmo->Instances[axis] = m_gizmo->Material->createInstance();
        m_gizmo->Instances[axis]->setParameter("color", colors[axis]);
        m_gizmo->Instances[axis]->setDoubleSided(true);
        m_gizmo->Instances[axis]->setDepthWrite(true);
        m_gizmo->Instances[axis]->setDepthCulling(false);
        for (int model=0; model<3; ++model) {
            const int index=model*3+axis;m_gizmo->Entities[index]=m_engine->getEntityManager().create();
            m_engine->getTransformManager().create(m_gizmo->Entities[index]);
            filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{1.1f,1.1f,1.1f}})
                .material(0,m_gizmo->Instances[axis]).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,
                    m_gizmo->Vertices[model],m_gizmo->Indices[model]).culling(false).castShadows(false).receiveShadows(false)
                .layerMask(0xff,0x80).priority(7)
                .build(*m_engine,m_gizmo->Entities[index]);
            auto instance=m_engine->getRenderableManager().getInstance(m_gizmo->Entities[index]);
            m_engine->getRenderableManager().setLayerMask(instance,0xff,0x80);
        }
    }
    constexpr int half=200;constexpr int lineCount=(half*2+1)*2;constexpr int vertexCount=lineCount*4;constexpr int indexCount=lineCount*6;constexpr float width=.0125f;
    auto* grid=new float[vertexCount*3];auto* gridIndices=new std::uint16_t[indexCount];int vertexCursor=0,indexCursor=0,base=0;
    const auto quad=[&](float x0,float y0,float x1,float y1){const float values[]={x0,y0,0,x1,y0,0,x1,y1,0,x0,y1,0};for(float v:values)grid[vertexCursor++]=v;const std::uint16_t ids[]={std::uint16_t(base),std::uint16_t(base+1),std::uint16_t(base+2),std::uint16_t(base),std::uint16_t(base+2),std::uint16_t(base+3)};for(auto v:ids)gridIndices[indexCursor++]=v;base+=4;};
    for(int i=-half;i<=half;++i){quad(float(-half),float(i)-width,float(half),float(i)+width);quad(float(i)-width,float(-half),float(i)+width,float(half));}
    m_gizmo->GridVertices=filament::VertexBuffer::Builder().vertexCount(vertexCount).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3).build(*m_engine);
    m_gizmo->GridVertices->setBufferAt(*m_engine,0,{grid,vertexCount*3*sizeof(float),[](void* b,size_t,void*){delete[] static_cast<float*>(b);}});
    m_gizmo->GridIndices=filament::IndexBuffer::Builder().indexCount(indexCount).bufferType(filament::IndexBuffer::IndexType::USHORT).build(*m_engine);
    m_gizmo->GridIndices->setBuffer(*m_engine,{gridIndices,indexCount*sizeof(std::uint16_t),[](void* b,size_t,void*){delete[] static_cast<std::uint16_t*>(b);}});
    m_gizmo->GridInstance=m_gizmo->Material->createInstance();m_gizmo->GridInstance->setParameter("color",filament::math::float4{.32f,.34f,.38f,1.0f});m_gizmo->GridInstance->setDoubleSided(true);m_gizmo->GridInstance->setDepthWrite(true);m_gizmo->GridInstance->setDepthCulling(true);
    m_gizmo->GridEntity=m_engine->getEntityManager().create();m_engine->getTransformManager().create(m_gizmo->GridEntity);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{201,201,.1f}}).material(0,m_gizmo->GridInstance).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,m_gizmo->GridVertices,m_gizmo->GridIndices).culling(false).castShadows(false).receiveShadows(false).layerMask(0xff,0x80).priority(7).build(*m_engine,m_gizmo->GridEntity);
    m_engine->getRenderableManager().setLayerMask(m_engine->getRenderableManager().getInstance(m_gizmo->GridEntity),0xff,0x80);
    m_assets = std::make_unique<RenderAssets>(*m_engine, *m_scene);
    SetEditorGrid(m_gridVisible,m_gridPlane);SetEditorGizmo(m_gizmoVisible,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);
    return true;
}

bool RenderBackend::CreateViewport(std::uint64_t id, std::uintptr_t nativeWindow,
                                   ViewportKind kind, std::uint32_t width,
                                   std::uint32_t height) {
    if (!m_engine || nativeWindow == 0) return false;
    DestroyViewport(id);
    auto viewport = std::make_unique<ViewportResource>();
    viewport->Kind = kind;
    viewport->Width = std::max(1u, width);
    viewport->Height = std::max(1u, height);
    viewport->SwapChain = m_engine->createSwapChain(reinterpret_cast<void*>(nativeWindow));
    if (!viewport->SwapChain) return false;
    viewport->CameraEntity = m_engine->getEntityManager().create();
    viewport->Camera = m_engine->createCamera(viewport->CameraEntity);
    viewport->View = m_engine->createView();
    viewport->View->setScene(m_scene);
    viewport->View->setCamera(viewport->Camera);
    viewport->View->setPostProcessingEnabled(true);
    viewport->View->setVisibleLayers(0xff, kind == ViewportKind::Scene ? 0xff : 0x7f);
    if (kind == ViewportKind::Scene) {
        viewport->Camera->lookAt({6.0, 4.0, 8.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    } else {
        viewport->Camera->lookAt({0.0, 2.0, 6.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0});
    }
    m_viewports.emplace(id, std::move(viewport));
    ResizeViewport(id, width, height);
    if(kind==ViewportKind::Scene){SetEditorGrid(m_gridVisible,m_gridPlane);SetEditorGizmo(m_gizmoVisible,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);}
    return true;
}

void RenderBackend::ResizeViewport(std::uint64_t id, std::uint32_t width,
                                   std::uint32_t height) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end()) return;
    auto& viewport = *found->second;
    viewport.Width = std::max(1u, width);
    viewport.Height = std::max(1u, height);
    if (viewport.View) {
        viewport.View->setViewport({0, 0, viewport.Width, viewport.Height});
        viewport.Camera->setProjection(60.0,
            static_cast<double>(viewport.Width) / viewport.Height, 0.05, 5000.0,
            filament::Camera::Fov::VERTICAL);
    }
    if (viewport.Kind == ViewportKind::Game)
        SetPresentationSize(viewport.Width, viewport.Height);
    else if(m_gizmoVisible)
        SetEditorGizmo(true,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);
}

void RenderBackend::DestroyViewport(std::uint64_t id) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end()) return;
    auto& viewport = *found->second;
    if (viewport.View) m_engine->destroy(viewport.View);
    if (viewport.Camera) m_engine->destroyCameraComponent(viewport.CameraEntity);
    if (viewport.CameraEntity) m_engine->getEntityManager().destroy(viewport.CameraEntity);
    if (viewport.SwapChain) m_engine->destroy(viewport.SwapChain);
    m_viewports.erase(found);
}

void RenderBackend::SetSceneCamera(std::uint64_t id, float eyeX, float eyeY, float eyeZ,
                                   float targetX, float targetY, float targetZ) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end() || !found->second->Camera) return;
    found->second->Camera->lookAt({eyeX, eyeY, eyeZ}, {targetX, targetY, targetZ},
                                  {0.0, 1.0, 0.0});
    found->second->Eye={eyeX,eyeY,eyeZ};
    if (found->second->Kind == ViewportKind::Scene) {
        const float dx=eyeX-targetX,dy=eyeY-targetY,dz=eyeZ-targetZ;
        const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        m_gridScale=std::pow(10.0f,std::max(0.0f,std::floor(std::log10(std::max(1.0f,distance/20.0f)))));
        const float snap=10.0f*m_gridScale;
        m_gridCenterX=std::round(targetX/snap)*snap;
        m_gridCenterY=std::round(targetY/snap)*snap;
        m_gridCenterZ=std::round(targetZ/snap)*snap;
        SetEditorGrid(m_gridVisible,m_gridPlane);
        if(m_gizmoVisible)SetEditorGizmo(true,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);
    }
}

void RenderBackend::SetEditorGizmo(bool visible, float x, float y, float z, int mode) {
    m_gizmoVisible=visible;m_gizmoX=x;m_gizmoY=y;m_gizmoZ=z;m_gizmoMode=mode;
    if (!m_gizmo || !m_engine) return;
    for(const auto& [id,viewport]:m_viewports){(void)id;if(viewport->Kind!=ViewportKind::Scene)continue;const float dx=viewport->Eye.x-x,dy=viewport->Eye.y-y,dz=viewport->Eye.z-z;const float depth=std::sqrt(dx*dx+dy*dy+dz*dz);constexpr float desiredPixels=96.0f;constexpr float modelLength=.9f;m_gizmoScale=std::clamp(depth*2.0f*std::tan(0.5235988f)*desiredPixels/(std::max(1u,viewport->Height)*modelLength),.01f,10000.0f);break;}
    auto& transforms = m_engine->getTransformManager();
    const float scale = mode == 0 ? 0.0f : m_gizmoScale;
    for (int model=0;model<3;++model) for (int axis=0; axis<3; ++axis) {
        const int index=model*3+axis;auto instance = transforms.getInstance(m_gizmo->Entities[index]);
        filament::math::mat4f rotation;
        const auto base=filament::math::mat4f::rotation(-1.5707963f,filament::math::float3{0,0,1});
        filament::math::mat4f axisRotation;
        if(axis==1)axisRotation=filament::math::mat4f::rotation(1.5707963f,filament::math::float3{0,0,1});
        else if(axis==2)axisRotation=filament::math::mat4f::rotation(-1.5707963f,filament::math::float3{0,1,0});
        rotation=axisRotation*base;
        const auto modelOffset=model==1?filament::math::mat4f{}:filament::math::mat4f::translation(filament::math::float3{0,.8f,0});
        const auto transform = filament::math::mat4f::translation(filament::math::float3{x,y,z}) *
            filament::math::mat4f::scaling(filament::math::float3{scale,scale,scale}) * rotation * modelOffset;
        transforms.setTransform(instance, transform);
        const bool active=visible&&scale>0&&model==mode-1;
        if (active) m_scene->addEntity(m_gizmo->Entities[index]); else m_scene->remove(m_gizmo->Entities[index]);
    }
}

void RenderBackend::SetEditorGizmoHover(int axis) {
    m_gizmoHover=std::clamp(axis,-1,2);
    if(!m_gizmo)return;
    constexpr filament::math::float4 colors[]={{.95f,.18f,.15f,1},{.25f,.9f,.25f,1},{.2f,.45f,1,1}};
    for(int i=0;i<3;++i){auto color=colors[i];if(i==m_gizmoHover){color.x+=(1-color.x)*.42f;color.y+=(1-color.y)*.42f;color.z+=(1-color.z)*.42f;}m_gizmo->Instances[i]->setParameter("color",color);}
}

void RenderBackend::SetEditorGrid(bool visible,int plane){m_gridVisible=visible;m_gridPlane=std::clamp(plane,0,2);if(!m_gizmo||!m_engine)return;auto& tm=m_engine->getTransformManager();auto instance=tm.getInstance(m_gizmo->GridEntity);filament::math::mat4f rotation;if(m_gridPlane==1)rotation=filament::math::mat4f::rotation(1.5707963f,filament::math::float3{1,0,0});else if(m_gridPlane==2)rotation=filament::math::mat4f::rotation(1.5707963f,filament::math::float3{0,1,0});filament::math::float3 center;if(m_gridPlane==0)center={m_gridCenterX,m_gridCenterY,0};else if(m_gridPlane==1)center={m_gridCenterX,0,m_gridCenterZ};else center={0,m_gridCenterY,m_gridCenterZ};const auto scale=filament::math::mat4f::scaling(filament::math::float3{m_gridScale,m_gridScale,m_gridScale});tm.setTransform(instance,filament::math::mat4f::translation(center)*rotation*scale);if(visible)m_scene->addEntity(m_gizmo->GridEntity);else m_scene->remove(m_gizmo->GridEntity);}

void RenderBackend::Render() {
    if (!m_renderer) return;
    for (const auto& [id, resource] : m_viewports) {
        (void)id;
        if (!resource->SwapChain || !m_renderer->beginFrame(resource->SwapChain)) continue;
        if (resource->Kind == ViewportKind::Scene) {
            if (resource->View) m_renderer->render(resource->View);
        } else {
            if (m_activeViews.empty()) {
                if (resource->View) m_renderer->render(resource->View);
            } else {
                for (filament::View* view : m_activeViews)
                    if (view) m_renderer->render(view);
            }
        }
        m_renderer->endFrame();
    }
}

void RenderBackend::Shutdown() {
    if (m_engine == nullptr) return;
    while (!m_viewports.empty()) DestroyViewport(m_viewports.begin()->first);
    m_activeViews.clear();
    m_postProcessEffects.clear();
    m_assets.reset();
    if (m_gizmo) {
        for (int axis=0; axis<9; ++axis) {
            m_scene->remove(m_gizmo->Entities[axis]);
            m_engine->destroy(m_gizmo->Entities[axis]);
            m_engine->getEntityManager().destroy(m_gizmo->Entities[axis]);
        }
        for (int axis=0;axis<3;++axis) {
            if (m_gizmo->Instances[axis]) m_engine->destroy(m_gizmo->Instances[axis]);
        }
        m_scene->remove(m_gizmo->GridEntity);m_engine->destroy(m_gizmo->GridEntity);m_engine->getEntityManager().destroy(m_gizmo->GridEntity);
        if(m_gizmo->GridInstance)m_engine->destroy(m_gizmo->GridInstance);if(m_gizmo->GridVertices)m_engine->destroy(m_gizmo->GridVertices);if(m_gizmo->GridIndices)m_engine->destroy(m_gizmo->GridIndices);
        for(auto* value:m_gizmo->Vertices)if(value)m_engine->destroy(value);
        for(auto* value:m_gizmo->Indices)if(value)m_engine->destroy(value);
        if (m_gizmo->Material) m_engine->destroy(m_gizmo->Material);
        m_gizmo.reset();
    }
    if (m_scene) { m_scene->setSkybox(nullptr); m_scene->setIndirectLight(nullptr); }
    if (m_skybox) m_engine->destroy(m_skybox);
    if (m_indirectLight) m_engine->destroy(m_indirectLight);
    if (m_environmentSkyboxTexture) m_engine->destroy(m_environmentSkyboxTexture);
    if (m_environmentIblTexture) m_engine->destroy(m_environmentIblTexture);
    m_skybox = nullptr; m_indirectLight = nullptr;
    m_environmentSkyboxTexture = nullptr; m_environmentIblTexture = nullptr;
    if (m_scene != nullptr) m_engine->destroy(m_scene);
    if (m_renderer != nullptr) m_engine->destroy(m_renderer);
    m_scene = nullptr;
    m_renderer = nullptr;
    filament::Engine::destroy(&m_engine);
}

} // namespace Bazzalt::Runtime
