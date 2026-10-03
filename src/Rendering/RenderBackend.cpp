#include "Rendering/RenderBackend.h"

#include <algorithm>
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filament/Engine.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
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
#include "Rendering/RenderAssets.h"
#include "editor_gizmo_filamat.h"
#include "editor_grid_filamat.h"
#include "editor_icon_filamat.h"
#include "scene_cam_rgba.h"
#include "scene_light_rgba.h"
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
    float PixelRatio = 1.0f;
    filament::math::float3 Eye{6.0f, 4.0f, 8.0f};
    filament::View* HelperView = nullptr;
    filament::Camera* HelperCamera = nullptr;
    utils::Entity HelperCameraEntity;
};

struct RenderBackend::GizmoResource {
    struct Icon { utils::Entity Entity; filament::MaterialInstance* Instance=nullptr; };
    struct Guide { utils::Entity Entity; filament::MaterialInstance* Instance=nullptr; };
    filament::Material* Material = nullptr;
    std::array<filament::VertexBuffer*, 3> Vertices{};
    std::array<filament::IndexBuffer*, 3> Indices{};
    std::array<filament::MaterialInstance*, 3> Instances{};
    std::array<utils::Entity, 9> Entities{};
    filament::VertexBuffer* GridVertices = nullptr;
    filament::IndexBuffer* GridIndices = nullptr;
    filament::Material* GridMaterial = nullptr;
    filament::MaterialInstance* GridInstance = nullptr;
    utils::Entity GridEntity{};
    filament::Scene* HelperScene = nullptr;
    filament::VertexBuffer* HelperVertices = nullptr;
    filament::IndexBuffer* HelperIndices = nullptr;
    std::array<filament::MaterialInstance*,3> HelperInstances{};
    std::array<utils::Entity,3> HelperEntities{};
    std::vector<Icon> Icons;
    std::vector<Guide> Guides;
    filament::Material* IconMaterial=nullptr;
    filament::Texture* CameraIconTexture=nullptr;
    filament::Texture* LightIconTexture=nullptr;
    filament::VertexBuffer* IconVertices=nullptr;
    filament::IndexBuffer* IconIndices=nullptr;
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

void ConfigureEditorFog(filament::View& view, float gridScale) {
    filament::View::FogOptions fog;
    fog.enabled = true;
    // Keep the useful editing area clear, then blend distant scene geometry
    // into the editor background before the adaptive grid can reach its edge.
    fog.distance = 60.0f * gridScale;
    fog.density = 0.025f / gridScale;
    fog.maximumOpacity = 1.0f;
    fog.heightFalloff = 0.0f;
    fog.color = {0.055f, 0.065f, 0.085f};
    view.setFogOptions(fog);
}
}

RenderBackend::RenderBackend() = default;

RenderBackend::~RenderBackend() { Shutdown(); }

std::vector<std::string> RenderBackend::SupportedBackends() {
    std::vector<std::string> result{"automatic"};
#if BAZZALT_FILAMENT_OPENGL
    result.push_back("opengl");
#endif
#if BAZZALT_FILAMENT_VULKAN
    result.push_back("vulkan");
#endif
#if BAZZALT_FILAMENT_METAL && defined(__APPLE__)
    result.push_back("metal");
#endif
#if BAZZALT_FILAMENT_WEBGPU
    result.push_back("webgpu");
#endif
    return result;
}

bool RenderBackend::ConfigureBackend(const std::string& backend) {
    const auto supported=SupportedBackends();
    if(std::find(supported.begin(),supported.end(),backend)==supported.end())return false;
    if(m_engine)return backend==m_backend;
    m_backend=backend;return true;
}

bool RenderBackend::Initialize() {
    if (m_engine != nullptr) return true;
    auto backend=filament::Engine::Backend::DEFAULT;
    if(m_backend=="opengl")backend=filament::Engine::Backend::OPENGL;
    else if(m_backend=="vulkan")backend=filament::Engine::Backend::VULKAN;
    else if(m_backend=="metal")backend=filament::Engine::Backend::METAL;
    else if(m_backend=="webgpu")backend=filament::Engine::Backend::WEBGPU;
    m_engine = filament::Engine::create(backend);
    if (m_engine == nullptr) return false;
    m_renderer = m_engine->createRenderer();
    m_scene = m_engine->createScene();
    if (m_renderer == nullptr || m_scene == nullptr) { Shutdown(); return false; }
    m_renderer->setClearOptions({.clearColor = {0.055, 0.065, 0.085, 1.0}, .clear = true});
    m_gizmo = std::make_unique<GizmoResource>();
    m_gizmo->Material = filament::Material::Builder()
        .package(Embedded::EditorGizmoFilamat, Embedded::EditorGizmoFilamatSize).build(*m_engine);
    m_gizmo->GridMaterial = filament::Material::Builder()
        .package(Embedded::EditorGridFilamat, Embedded::EditorGridFilamatSize).build(*m_engine);
    if (!m_gizmo->Material || !m_gizmo->GridMaterial) { Shutdown(); return false; }
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
    auto* grid=new float[vertexCount*5];auto* gridIndices=new std::uint16_t[indexCount];int vertexCursor=0,indexCursor=0,base=0;
    const auto quad=[&](float x0,float y0,float x1,float y1){const float values[]={x0,y0,0,x0,y0,x1,y0,0,x1,y0,x1,y1,0,x1,y1,x0,y1,0,x0,y1};for(float v:values)grid[vertexCursor++]=v;const std::uint16_t ids[]={std::uint16_t(base),std::uint16_t(base+1),std::uint16_t(base+2),std::uint16_t(base),std::uint16_t(base+2),std::uint16_t(base+3)};for(auto v:ids)gridIndices[indexCursor++]=v;base+=4;};
    for(int i=-half;i<=half;++i){quad(float(-half),float(i)-width,float(half),float(i)+width);quad(float(i)-width,float(-half),float(i)+width,float(half));}
    m_gizmo->GridVertices=filament::VertexBuffer::Builder().vertexCount(vertexCount).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,20).attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,12,20).build(*m_engine);
    m_gizmo->GridVertices->setBufferAt(*m_engine,0,{grid,vertexCount*5*sizeof(float),[](void* b,size_t,void*){delete[] static_cast<float*>(b);}});
    m_gizmo->GridIndices=filament::IndexBuffer::Builder().indexCount(indexCount).bufferType(filament::IndexBuffer::IndexType::USHORT).build(*m_engine);
    m_gizmo->GridIndices->setBuffer(*m_engine,{gridIndices,indexCount*sizeof(std::uint16_t),[](void* b,size_t,void*){delete[] static_cast<std::uint16_t*>(b);}});
    m_gizmo->GridInstance=m_gizmo->GridMaterial->createInstance();m_gizmo->GridInstance->setParameter("color",filament::math::float4{.32f,.34f,.38f,1.0f});m_gizmo->GridInstance->setDoubleSided(true);m_gizmo->GridInstance->setDepthWrite(false);m_gizmo->GridInstance->setDepthCulling(true);
    m_gizmo->GridEntity=m_engine->getEntityManager().create();m_engine->getTransformManager().create(m_gizmo->GridEntity);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{201,201,.1f}}).material(0,m_gizmo->GridInstance).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,m_gizmo->GridVertices,m_gizmo->GridIndices).culling(false).castShadows(false).receiveShadows(false).layerMask(0xff,0x80).priority(7).build(*m_engine,m_gizmo->GridEntity);
    m_engine->getRenderableManager().setLayerMask(m_engine->getRenderableManager().getInstance(m_gizmo->GridEntity),0xff,0x80);
    m_gizmo->HelperScene=m_engine->createScene();
    static constexpr float cubeVertices[]={-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1, -1,-1,1, 1,-1,1, 1,1,1, -1,1,1};
    static constexpr std::uint16_t cubeIndices[]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,2,3,7,2,7,6,1,2,6,1,6,5,3,0,4,3,4,7};
    m_gizmo->HelperVertices=filament::VertexBuffer::Builder().vertexCount(8).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3).build(*m_engine);
    m_gizmo->HelperVertices->setBufferAt(*m_engine,0,{cubeVertices,sizeof(cubeVertices)});
    m_gizmo->HelperIndices=filament::IndexBuffer::Builder().indexCount(36).bufferType(filament::IndexBuffer::IndexType::USHORT).build(*m_engine);
    m_gizmo->HelperIndices->setBuffer(*m_engine,{cubeIndices,sizeof(cubeIndices)});
    // Native scene-orientation widget: three minimal, colored axis lines.  The
    // unit cube is scaled from the origin toward +X/+Y/+Z; no center cube or
    // background geometry is used.
    constexpr filament::math::float4 helperColors[]={{.95f,.18f,.15f,1},{.25f,.9f,.25f,1},{.2f,.45f,1,1}};
    constexpr filament::math::float3 helperScale[]={{.5f,.026f,.026f},{.026f,.5f,.026f},{.026f,.026f,.5f}};
    constexpr filament::math::float3 helperPosition[]={{.5f,0,0},{0,.5f,0},{0,0,.5f}};
    for(int i=0;i<3;++i){m_gizmo->HelperInstances[i]=m_gizmo->Material->createInstance();m_gizmo->HelperInstances[i]->setParameter("color",helperColors[i]);m_gizmo->HelperInstances[i]->setDepthWrite(true);m_gizmo->HelperInstances[i]->setDepthCulling(true);m_gizmo->HelperEntities[i]=m_engine->getEntityManager().create();m_engine->getTransformManager().create(m_gizmo->HelperEntities[i]);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{1,1,1}}).material(0,m_gizmo->HelperInstances[i]).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,m_gizmo->HelperVertices,m_gizmo->HelperIndices).culling(false).castShadows(false).receiveShadows(false).build(*m_engine,m_gizmo->HelperEntities[i]);auto ti=m_engine->getTransformManager().getInstance(m_gizmo->HelperEntities[i]);m_engine->getTransformManager().setTransform(ti,filament::math::mat4f::translation(helperPosition[i])*filament::math::mat4f::scaling(helperScale[i]));m_gizmo->HelperScene->addEntity(m_gizmo->HelperEntities[i]);}
    m_gizmo->IconMaterial=filament::Material::Builder().package(Embedded::EditorIconFilamat,Embedded::EditorIconFilamatSize).build(*m_engine);
    auto makeTexture=[this](const std::uint8_t* pixels,std::size_t size){auto* texture=filament::Texture::Builder().width(64).height(64).levels(1).sampler(filament::Texture::Sampler::SAMPLER_2D).format(filament::Texture::InternalFormat::RGBA8).build(*m_engine);texture->setImage(*m_engine,0,filament::backend::PixelBufferDescriptor(pixels,size,filament::backend::PixelDataFormat::RGBA,filament::backend::PixelDataType::UBYTE));return texture;};
    m_gizmo->CameraIconTexture=makeTexture(Embedded::SceneCameraIconRgba,Embedded::SceneCameraIconRgbaSize);m_gizmo->LightIconTexture=makeTexture(Embedded::SceneLightIconRgba,Embedded::SceneLightIconRgbaSize);
    static constexpr float iconVertices[]={-.5f,-.5f,0, 0,0, .5f,-.5f,0, 1,0, .5f,.5f,0, 1,1, -.5f,.5f,0, 0,1};static constexpr std::uint16_t iconIndices[]={0,1,2,0,2,3};
    m_gizmo->IconVertices=filament::VertexBuffer::Builder().vertexCount(4).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,20).attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,12,20).build(*m_engine);m_gizmo->IconVertices->setBufferAt(*m_engine,0,{iconVertices,sizeof(iconVertices)});m_gizmo->IconIndices=filament::IndexBuffer::Builder().indexCount(6).bufferType(filament::IndexBuffer::IndexType::USHORT).build(*m_engine);m_gizmo->IconIndices->setBuffer(*m_engine,{iconIndices,sizeof(iconIndices)});
    m_assets = std::make_unique<RenderAssets>(*m_engine, *m_scene);
    SetEditorGrid(m_gridVisible,m_gridPlane);SetEditorGizmo(m_gizmoVisible,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);
    return true;
}

bool RenderBackend::CreateViewport(std::uint64_t id, std::uintptr_t nativeWindow,
                                   ViewportKind kind, std::uint32_t width,
                                   std::uint32_t height, float pixelRatio) {
    if (!m_engine || nativeWindow == 0) return false;
#ifdef _WIN32
    // Reject synthetic Qt IDs and stale/destroyed HWNDs before entering Filament.
    if(!IsWindow(reinterpret_cast<HWND>(nativeWindow)))return false;
#endif
    DestroyViewport(id);
    auto viewport = std::make_unique<ViewportResource>();
    viewport->Kind = kind;
    viewport->Width = std::max(1u, width);
    viewport->Height = std::max(1u, height);
    viewport->PixelRatio = std::max(1.0f,pixelRatio);
    viewport->SwapChain = m_engine->createSwapChain(reinterpret_cast<void*>(nativeWindow));
    if (!viewport->SwapChain) return false;
    viewport->CameraEntity = m_engine->getEntityManager().create();
    viewport->Camera = m_engine->createCamera(viewport->CameraEntity);
    viewport->View = m_engine->createView();
    // The private viewport camera is only an editor Scene-view camera.  A Game
    // viewport must never use it as an implicit scene camera: runtime output is
    // driven exclusively by active Camera components.
    viewport->View->setScene(kind == ViewportKind::Scene ? m_scene : nullptr);
    viewport->View->setCamera(viewport->Camera);
    viewport->View->setPostProcessingEnabled(true);
    viewport->View->setVisibleLayers(0xff, kind == ViewportKind::Scene ? 0xff : 0x7f);
    if (kind == ViewportKind::Scene) {
        ConfigureEditorFog(*viewport->View, m_gridScale);
        viewport->Camera->lookAt({6.0, 4.0, 8.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        viewport->HelperCameraEntity=m_engine->getEntityManager().create();viewport->HelperCamera=m_engine->createCamera(viewport->HelperCameraEntity);viewport->HelperView=m_engine->createView();viewport->HelperView->setScene(m_gizmo->HelperScene);viewport->HelperView->setCamera(viewport->HelperCamera);viewport->HelperView->setPostProcessingEnabled(false);viewport->HelperView->setBlendMode(filament::View::BlendMode::TRANSLUCENT);viewport->HelperView->setChannelDepthClearEnabled(0,true);viewport->HelperCamera->lookAt({2.1,1.4,2.8},{0,0,0},{0,1,0});viewport->HelperCamera->setProjection(38.0,1.0,0.1,10.0,filament::Camera::Fov::VERTICAL);
    } else {
        viewport->Camera->lookAt({0.0, 2.0, 6.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0});
    }
    m_viewports.emplace(id, std::move(viewport));
    ResizeViewport(id, width, height, pixelRatio);
    if(kind==ViewportKind::Scene){SetEditorGrid(m_gridVisible,m_gridPlane);SetEditorGizmo(m_gizmoVisible,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);}
    return true;
}

void RenderBackend::ResizeViewport(std::uint64_t id, std::uint32_t width,
                                   std::uint32_t height, float pixelRatio) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end()) return;
    auto& viewport = *found->second;
    viewport.Width = std::max(1u, width);
    viewport.Height = std::max(1u, height);
    viewport.PixelRatio = std::max(1.0f,pixelRatio);
    if (viewport.View) {
        viewport.View->setViewport({0, 0, viewport.Width, viewport.Height});
        viewport.Camera->setProjection(60.0,
            static_cast<double>(viewport.Width) / viewport.Height, 0.05, 5000.0,
            filament::Camera::Fov::VERTICAL);
    }
    if(viewport.HelperView){const auto size=std::max(1u,static_cast<std::uint32_t>(std::lround(72.0f*viewport.PixelRatio)));const auto padding=static_cast<std::uint32_t>(std::lround(8.0f*viewport.PixelRatio));const auto availableWidth=viewport.Width>padding?viewport.Width-padding:viewport.Width;const auto availableHeight=viewport.Height>padding?viewport.Height-padding:viewport.Height;const auto inset=std::min(size,std::min(availableWidth,availableHeight));const auto x=viewport.Width>inset+padding?viewport.Width-inset-padding:0u;const auto y=viewport.Height>inset+padding?viewport.Height-inset-padding:0u;viewport.HelperView->setViewport({static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),inset,inset});}
    if (viewport.Kind == ViewportKind::Game)
        SetPresentationSize(viewport.Width, viewport.Height);
    else if(m_gizmoVisible)
        SetEditorGizmo(true,m_gizmoX,m_gizmoY,m_gizmoZ,m_gizmoMode);
}

void RenderBackend::DestroyViewport(std::uint64_t id) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end()) return;
    auto& viewport = *found->second;
    if(viewport.HelperView)m_engine->destroy(viewport.HelperView);
    if(viewport.HelperCamera)m_engine->destroyCameraComponent(viewport.HelperCameraEntity);
    if(viewport.HelperCameraEntity)m_engine->getEntityManager().destroy(viewport.HelperCameraEntity);
    if (viewport.View) m_engine->destroy(viewport.View);
    if (viewport.Camera) m_engine->destroyCameraComponent(viewport.CameraEntity);
    if (viewport.CameraEntity) m_engine->getEntityManager().destroy(viewport.CameraEntity);
    if (viewport.SwapChain) {
        m_engine->destroy(viewport.SwapChain);
        // Qt may destroy/reparent the native surface as soon as Detach returns.
        // Complete the driver-side swapchain destruction while its HWND is alive.
        m_engine->flushAndWait();
    }
    m_viewports.erase(found);
}

void RenderBackend::SetSceneCamera(std::uint64_t id, float eyeX, float eyeY, float eyeZ,
                                   float targetX, float targetY, float targetZ) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end() || !found->second->Camera) return;
    found->second->Camera->lookAt({eyeX, eyeY, eyeZ}, {targetX, targetY, targetZ},
                                  {0.0, 1.0, 0.0});
    found->second->Eye={eyeX,eyeY,eyeZ};
    if(found->second->HelperCamera){const float lx=eyeX-targetX,ly=eyeY-targetY,lz=eyeZ-targetZ;const float length=std::max(.0001f,std::sqrt(lx*lx+ly*ly+lz*lz));found->second->HelperCamera->lookAt({2.9f*lx/length,2.9f*ly/length,2.9f*lz/length},{0,0,0},{0,1,0});}
    if (found->second->Kind == ViewportKind::Scene) {
        const float dx=eyeX-targetX,dy=eyeY-targetY,dz=eyeZ-targetZ;
        const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        m_gridScale=std::pow(10.0f,std::max(0.0f,std::floor(std::log10(std::max(1.0f,distance/20.0f)))));
        const float snap=10.0f*m_gridScale;
        m_gridCenterX=std::round(targetX/snap)*snap;
        m_gridCenterY=std::round(targetY/snap)*snap;
        m_gridCenterZ=std::round(targetZ/snap)*snap;
        ConfigureEditorFog(*found->second->View, m_gridScale);
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

void RenderBackend::SetEditorGrid(bool visible,int plane){m_gridVisible=visible;m_gridPlane=std::clamp(plane,0,2);if(!m_gizmo||!m_engine)return;auto& tm=m_engine->getTransformManager();auto instance=tm.getInstance(m_gizmo->GridEntity);filament::math::mat4f rotation;if(m_gridPlane==1)rotation=filament::math::mat4f::rotation(1.5707963f,filament::math::float3{1,0,0});else if(m_gridPlane==2)rotation=filament::math::mat4f::rotation(1.5707963f,filament::math::float3{0,1,0});filament::math::float3 center;if(m_gridPlane==0)center={m_gridCenterX,m_gridCenterY,0};else if(m_gridPlane==1)center={m_gridCenterX,0,m_gridCenterZ};else center={0,m_gridCenterY,m_gridCenterZ};const auto scale=filament::math::mat4f::scaling(filament::math::float3{m_gridScale,m_gridScale,m_gridScale});tm.setTransform(instance,filament::math::mat4f::translation(center)*rotation*scale);if(m_gizmo->GridInstance){m_gizmo->GridInstance->setParameter("fogStart",125.0f);m_gizmo->GridInstance->setParameter("fogEnd",180.0f);}if(visible)m_scene->addEntity(m_gizmo->GridEntity);else m_scene->remove(m_gizmo->GridEntity);}

void RenderBackend::SetEditorIcons(const std::vector<EditorIcon>& icons) {
    if(!m_gizmo||!m_engine)return;
    while(m_gizmo->Icons.size()>icons.size()){auto icon=m_gizmo->Icons.back();m_scene->remove(icon.Entity);m_engine->destroy(icon.Entity);m_engine->getEntityManager().destroy(icon.Entity);if(icon.Instance)m_engine->destroy(icon.Instance);m_gizmo->Icons.pop_back();}
    while(m_gizmo->Icons.size()<icons.size()){GizmoResource::Icon icon;icon.Instance=m_gizmo->IconMaterial->createInstance();icon.Entity=m_engine->getEntityManager().create();m_engine->getTransformManager().create(icon.Entity);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{1,1,.01f}}).material(0,icon.Instance).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,m_gizmo->IconVertices,m_gizmo->IconIndices).culling(false).castShadows(false).receiveShadows(false).layerMask(0xff,0x80).priority(7).build(*m_engine,icon.Entity);m_engine->getRenderableManager().setLayerMask(m_engine->getRenderableManager().getInstance(icon.Entity),0xff,0x80);m_scene->addEntity(icon.Entity);m_gizmo->Icons.push_back(icon);}
    filament::math::float3 eye{6,4,8};for(const auto& [id,viewport]:m_viewports){(void)id;if(viewport->Kind==ViewportKind::Scene){eye=viewport->Eye;break;}}
    auto& transforms=m_engine->getTransformManager();
    const filament::TextureSampler sampler(filament::TextureSampler::MinFilter::LINEAR,filament::TextureSampler::MagFilter::LINEAR);
    for(std::size_t i=0;i<icons.size();++i){const auto& data=icons[i];auto& icon=m_gizmo->Icons[i];icon.Instance->setParameter("iconTexture",data.Camera?m_gizmo->CameraIconTexture:m_gizmo->LightIconTexture,sampler);const filament::math::float3 position{data.X,data.Y,data.Z};auto forward=normalize(eye-position);auto right=cross(filament::math::float3{0,1,0},forward);if(length(right)<.001f)right={1,0,0};else right=normalize(right);auto up=normalize(cross(forward,right));constexpr float scale=.42f;filament::math::mat4f transform{filament::math::float4{right*scale,0},filament::math::float4{up*scale,0},filament::math::float4{forward,0},filament::math::float4{position,1}};transforms.setTransform(transforms.getInstance(icon.Entity),transform);}
}

void RenderBackend::SetEditorGuides(const std::vector<EditorGuide>& guides){
    if(!m_gizmo||!m_engine)return;
    while(m_gizmo->Guides.size()>guides.size()){auto value=m_gizmo->Guides.back();m_scene->remove(value.Entity);m_engine->destroy(value.Entity);m_engine->getEntityManager().destroy(value.Entity);if(value.Instance)m_engine->destroy(value.Instance);m_gizmo->Guides.pop_back();}
    while(m_gizmo->Guides.size()<guides.size()){GizmoResource::Guide value;value.Instance=m_gizmo->Material->createInstance();value.Instance->setDepthWrite(false);value.Instance->setDepthCulling(true);value.Entity=m_engine->getEntityManager().create();m_engine->getTransformManager().create(value.Entity);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{1,1,1}}).material(0,value.Instance).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,m_gizmo->HelperVertices,m_gizmo->HelperIndices).culling(false).castShadows(false).receiveShadows(false).layerMask(0xff,0x80).priority(6).build(*m_engine,value.Entity);m_scene->addEntity(value.Entity);m_gizmo->Guides.push_back(value);}
    auto& transforms=m_engine->getTransformManager();
    for(std::size_t i=0;i<guides.size();++i){
        const auto& line=guides[i];auto& value=m_gizmo->Guides[i];
        value.Instance->setParameter("color",filament::math::float4{line.R,line.G,line.B,line.A});
        // Selection outlines sit above their own surface, avoiding coplanar
        // depth rejection on thin planes. Other camera/light guides use depth.
        value.Instance->setDepthCulling(!line.Outline);
        auto instance=m_engine->getRenderableManager().getInstance(value.Entity);
        m_engine->getRenderableManager().setLayerMask(instance,0xff,0x80);
        filament::math::float3 a{line.AX,line.AY,line.AZ},b{line.BX,line.BY,line.BZ},delta=b-a;
        float lengthValue=length(delta);if(lengthValue<.0001f){m_scene->remove(value.Entity);continue;}
        m_scene->addEntity(value.Entity);auto x=normalize(delta);
        auto helper=std::abs(x.y)<.99f?filament::math::float3{0,1,0}:filament::math::float3{1,0,0};
        auto z=normalize(cross(x,helper));auto y=normalize(cross(z,x));float thickness=.0125f;
        if(line.Outline)for(const auto& [_,viewport]:m_viewports)if(viewport->Kind==ViewportKind::Scene){
            const float depth=std::max(.05f,length(viewport->Eye-(a+b)*.5f));
            // Half-width of a three-logical-pixel stroke at the editor's 60° FOV.
            thickness=depth*2.0f*std::tan(.5235988f)*1.5f*viewport->PixelRatio/std::max(1u,viewport->Height);break;
        }
        filament::math::mat4f matrix{filament::math::float4{x*(lengthValue*.5f),0},filament::math::float4{y*thickness,0},filament::math::float4{z*thickness,0},filament::math::float4{(a+b)*.5f,1}};
        transforms.setTransform(transforms.getInstance(value.Entity),matrix);
    }
}

void RenderBackend::Render() {
    if (!m_renderer) return;
    for (const auto& [id, resource] : m_viewports) {
        (void)id;
        if (!resource->SwapChain || !m_renderer->beginFrame(resource->SwapChain)) continue;
        if (resource->Kind == ViewportKind::Scene) {
            if (resource->View) m_renderer->render(resource->View);
            if (resource->HelperView && m_orientationVisible) {
                m_renderer->setClearOptions({.clearColor={0,0,0,0},.clear=false});
                m_renderer->render(resource->HelperView);
                m_renderer->setClearOptions({.clearColor={0.055,0.065,0.085,1.0},.clear=true});
            }
        } else {
            if (m_activeViews.empty()) {
                // Render an empty view solely to clear the swap chain.  Its
                // scene is deliberately null, so a missing game camera can
                // never expose the editor fallback camera.
                m_renderer->setClearOptions({.clearColor={0,0,0,1},.clear=true});
                if (resource->View) m_renderer->render(resource->View);
                m_renderer->setClearOptions({.clearColor={0.055,0.065,0.085,1.0},.clear=true});
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
        if(m_gizmo->GridInstance)m_engine->destroy(m_gizmo->GridInstance);if(m_gizmo->GridVertices)m_engine->destroy(m_gizmo->GridVertices);if(m_gizmo->GridIndices)m_engine->destroy(m_gizmo->GridIndices);if(m_gizmo->GridMaterial)m_engine->destroy(m_gizmo->GridMaterial);
        for(int i=0;i<3;++i){if(m_gizmo->HelperScene)m_gizmo->HelperScene->remove(m_gizmo->HelperEntities[i]);m_engine->destroy(m_gizmo->HelperEntities[i]);m_engine->getEntityManager().destroy(m_gizmo->HelperEntities[i]);if(m_gizmo->HelperInstances[i])m_engine->destroy(m_gizmo->HelperInstances[i]);}
        for(auto& icon:m_gizmo->Icons){m_scene->remove(icon.Entity);m_engine->destroy(icon.Entity);m_engine->getEntityManager().destroy(icon.Entity);if(icon.Instance)m_engine->destroy(icon.Instance);}m_gizmo->Icons.clear();
        for(auto& guide:m_gizmo->Guides){m_scene->remove(guide.Entity);m_engine->destroy(guide.Entity);m_engine->getEntityManager().destroy(guide.Entity);if(guide.Instance)m_engine->destroy(guide.Instance);}m_gizmo->Guides.clear();
        if(m_gizmo->IconVertices)m_engine->destroy(m_gizmo->IconVertices);if(m_gizmo->IconIndices)m_engine->destroy(m_gizmo->IconIndices);if(m_gizmo->CameraIconTexture)m_engine->destroy(m_gizmo->CameraIconTexture);if(m_gizmo->LightIconTexture)m_engine->destroy(m_gizmo->LightIconTexture);if(m_gizmo->IconMaterial)m_engine->destroy(m_gizmo->IconMaterial);
        if(m_gizmo->HelperVertices)m_engine->destroy(m_gizmo->HelperVertices);if(m_gizmo->HelperIndices)m_engine->destroy(m_gizmo->HelperIndices);if(m_gizmo->HelperScene)m_engine->destroy(m_gizmo->HelperScene);
        for(auto* value:m_gizmo->Vertices)if(value)m_engine->destroy(value);
        for(auto* value:m_gizmo->Indices)if(value)m_engine->destroy(value);
        if (m_gizmo->Material) m_engine->destroy(m_gizmo->Material);
        m_gizmo.reset();
    }
    if (m_scene != nullptr) m_engine->destroy(m_scene);
    if (m_renderer != nullptr) m_engine->destroy(m_renderer);
    m_scene = nullptr;
    m_renderer = nullptr;
    filament::Engine::destroy(&m_engine);
}

} // namespace Bazzalt::Runtime
