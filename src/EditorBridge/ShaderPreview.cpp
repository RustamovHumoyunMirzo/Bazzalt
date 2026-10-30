// Private offscreen material previews. Never shares an entity, scene or surface
// with the user's editor/game viewports.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Skybox.h>
#include <filament/TextureSampler.h>
#include <backend/BufferDescriptor.h>
#include <utils/EntityManager.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <vector>

namespace py=pybind11;
namespace Bazzalt::EditorBridge {
class ShaderPreview {
    filament::Engine* Engine=nullptr;
    filament::Renderer* Renderer=nullptr;
    filament::Scene* Scene=nullptr;
    filament::View* View=nullptr;
    filament::Camera* Camera=nullptr;
    filament::SwapChain* Swap=nullptr;
    filament::RenderTarget* Target=nullptr;
    filament::Texture *Color=nullptr,*Depth=nullptr;
    filament::Skybox* Sky=nullptr;
    utils::Entity CameraEntity{},Light{},Mesh{};
    filament::Material* Material=nullptr;
    filament::MaterialInstance* Instance=nullptr;
    filament::VertexBuffer* Vertices=nullptr;
    filament::IndexBuffer* Indices=nullptr;
    std::vector<filament::Texture*> Textures;
    static constexpr std::uint32_t Size=256;
    void ClearMesh(){
        if(Mesh){Scene->remove(Mesh);Engine->destroy(Mesh);utils::EntityManager::get().destroy(Mesh);Mesh={};}
        if(Instance){Engine->destroy(Instance);Instance=nullptr;}
        if(Material){Engine->destroy(Material);Material=nullptr;}
        for(auto* texture:Textures)Engine->destroy(texture);Textures.clear();
        if(Vertices){Engine->destroy(Vertices);Vertices=nullptr;}
        if(Indices){Engine->destroy(Indices);Indices=nullptr;}
    }
public:
    explicit ShaderPreview(const std::string& backend){
        const auto kind=backend=="metal"?filament::Engine::Backend::METAL:backend=="opengl"?filament::Engine::Backend::OPENGL:filament::Engine::Backend::VULKAN;
        Engine=filament::Engine::create(kind);if(!Engine)throw std::runtime_error("Cannot create shader preview renderer");
        Renderer=Engine->createRenderer();Scene=Engine->createScene();View=Engine->createView();Swap=Engine->createSwapChain(Size,Size);
        CameraEntity=utils::EntityManager::get().create();Camera=Engine->createCamera(CameraEntity);Camera->setProjection(45.,1.,.05,20.,filament::Camera::Fov::VERTICAL);Camera->setExposure(16.f,1.f/125.f,100.f);
        Color=filament::Texture::Builder().width(Size).height(Size).levels(1).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::SAMPLEABLE|filament::Texture::Usage::BLIT_SRC).build(*Engine);
        Depth=filament::Texture::Builder().width(Size).height(Size).levels(1).format(filament::Texture::InternalFormat::DEPTH32F).usage(filament::Texture::Usage::DEPTH_ATTACHMENT).build(*Engine);
        Target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,Color).texture(filament::RenderTarget::AttachmentPoint::DEPTH,Depth).build(*Engine);
        View->setScene(Scene);View->setCamera(Camera);View->setViewport({0,0,Size,Size});View->setRenderTarget(Target);
        Sky=filament::Skybox::Builder().color({.035f,.035f,.035f,1.f}).build(*Engine);Scene->setSkybox(Sky);
        Light=utils::EntityManager::get().create();filament::LightManager::Builder(filament::LightManager::Type::SUN).color({1.f,.96f,.9f}).intensity(100000.f).direction({-.5f,-.7f,-1.f}).castShadows(false).build(*Engine,Light);Scene->addEntity(Light);
    }
    ~ShaderPreview(){
        if(!Engine)return;Engine->flushAndWait();ClearMesh();Scene->remove(Light);Engine->destroy(Light);utils::EntityManager::get().destroy(Light);
        Engine->destroy(View);Engine->destroyCameraComponent(CameraEntity);utils::EntityManager::get().destroy(CameraEntity);Engine->destroy(Target);Engine->destroy(Color);Engine->destroy(Depth);Engine->destroy(Sky);Engine->destroy(Scene);Engine->destroy(Renderer);Engine->destroy(Swap);filament::Engine::destroy(&Engine);
    }
    py::bytes Render(py::bytes package,const std::vector<std::array<float,13>>& vertices,const std::vector<std::uint32_t>& indices,const py::list& parameters,double yaw,double pitch,double time){
        if(vertices.empty()||vertices.size()>100000||indices.empty()||indices.size()>300000||indices.size()%3)throw std::invalid_argument("Invalid preview geometry size");
        for(const auto& v:vertices)for(float x:v)if(!std::isfinite(x))throw std::invalid_argument("Invalid preview vertex");
        for(auto index:indices)if(index>=vertices.size())throw std::invalid_argument("Invalid preview index");
        std::string bytes=package;if(bytes.empty()||bytes.size()>64*1024*1024)throw std::invalid_argument("Invalid preview material package");
        ClearMesh();Material=filament::Material::Builder().package(bytes.data(),bytes.size()).build(*Engine);
        if(!Material||Material->getMaterialDomain()!=filament::MaterialDomain::SURFACE)throw std::invalid_argument("Preview requires a surface material");
        Instance=Material->createInstance();
        for(auto object:parameters){
            auto p=py::cast<py::dict>(object);auto name=py::cast<std::string>(p["name"]);auto kind=py::cast<int>(p["kind"]);if(!Material->hasParameter(name.c_str()))continue;
            if(kind==5)Instance->setParameter(name.c_str(),py::cast<bool>(p["value"]));
            else if(kind==4)Instance->setParameter(name.c_str(),py::cast<int>(p["value"]));
            else if(kind==0)Instance->setParameter(name.c_str(),py::cast<float>(p["value"]));
            else if(kind>=1&&kind<=3){auto v=py::cast<std::vector<float>>(p["value"]);if(v.size()!=std::size_t(kind+1))throw std::invalid_argument("Invalid preview parameter");if(kind==1)Instance->setParameter(name.c_str(),filament::math::float2{v[0],v[1]});if(kind==2)Instance->setParameter(name.c_str(),filament::math::float3{v[0],v[1],v[2]});if(kind==3)Instance->setParameter(name.c_str(),filament::math::float4{v[0],v[1],v[2],v[3]});}
            else if(kind==6){
                auto width=py::cast<std::uint32_t>(p["width"]),height=py::cast<std::uint32_t>(p["height"]);std::string pixels=py::cast<py::bytes>(p["pixels"]);
                if(!width||!height||width>4096||height>4096||pixels.size()!=std::size_t(width)*height*4)throw std::invalid_argument("Invalid preview texture");
                const auto format=name=="baseColorTexture"?filament::Texture::InternalFormat::SRGB8_A8:filament::Texture::InternalFormat::RGBA8;
                auto* texture=filament::Texture::Builder().width(width).height(height).levels(1).format(format).usage(filament::Texture::Usage::SAMPLEABLE|filament::Texture::Usage::UPLOADABLE).build(*Engine);Textures.push_back(texture);
                auto* data=new std::string(std::move(pixels));texture->setImage(*Engine,0,filament::Texture::PixelBufferDescriptor(data->data(),data->size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* owner){delete static_cast<std::string*>(owner);},data));Instance->setParameter(name.c_str(),texture,filament::TextureSampler(filament::TextureSampler::MinFilter::LINEAR,filament::TextureSampler::MagFilter::LINEAR,filament::TextureSampler::WrapMode::REPEAT));
            }
        }
        auto* vertexData=new std::vector<std::array<float,13>>(vertices);auto* indexData=new std::vector<std::uint32_t>(indices);
        Vertices=filament::VertexBuffer::Builder().vertexCount(static_cast<std::uint32_t>(vertices.size())).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,52).attribute(filament::VertexAttribute::TANGENTS,0,filament::VertexBuffer::AttributeType::FLOAT4,12,52).attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,28,52).attribute(filament::VertexAttribute::COLOR,0,filament::VertexBuffer::AttributeType::FLOAT4,36,52).build(*Engine);
        Indices=filament::IndexBuffer::Builder().indexCount(static_cast<std::uint32_t>(indices.size())).bufferType(filament::IndexBuffer::IndexType::UINT).build(*Engine);
        Vertices->setBufferAt(*Engine,0,filament::VertexBuffer::BufferDescriptor(vertexData->data(),vertexData->size()*sizeof((*vertexData)[0]),[](void*,std::size_t,void* data){delete static_cast<std::vector<std::array<float,13>>*>(data);},vertexData));
        Indices->setBuffer(*Engine,filament::IndexBuffer::BufferDescriptor(indexData->data(),indexData->size()*4,[](void*,std::size_t,void* data){delete static_cast<std::vector<std::uint32_t>*>(data);},indexData));
        Mesh=utils::EntityManager::get().create();filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{1.5f,1.5f,1.5f}}).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,Vertices,Indices).material(0,Instance).culling(false).castShadows(false).receiveShadows(false).build(*Engine,Mesh);Scene->addEntity(Mesh);
        double radius=0.;for(const auto& v:vertices)radius=std::max(radius,std::hypot(double(v[0]),double(v[1]),double(v[2])));
        const double distance=3.2*std::clamp(radius,.1,2.);
        Camera->lookAt({distance*std::cos(pitch)*std::sin(yaw),distance*std::sin(pitch),distance*std::cos(pitch)*std::cos(yaw)},{0,0,0});Renderer->setMaterialTimeEpoch(std::chrono::steady_clock::now()-std::chrono::nanoseconds(static_cast<std::int64_t>(time*1e9)));
        struct Readback {std::vector<std::uint8_t> Pixels=std::vector<std::uint8_t>(Size*Size*4);std::atomic_bool Complete=false;};
        auto readback=std::make_shared<Readback>();
        {
            py::gil_scoped_release release;
            if(!Renderer->beginFrame(Swap))throw std::runtime_error("Preview frame unavailable");
            Renderer->setClearOptions({.clearColor={.035f,.035f,.035f,1.f},.clear=true});Renderer->render(View);
            auto* owner=new std::shared_ptr<Readback>(readback);
            Renderer->readPixels(Target,0,0,Size,Size,filament::Texture::PixelBufferDescriptor(readback->Pixels.data(),readback->Pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* data){std::unique_ptr<std::shared_ptr<Readback>> owner(static_cast<std::shared_ptr<Readback>*>(data));(*owner)->Complete.store(true,std::memory_order_release);},owner));Renderer->endFrame();Engine->flushAndWait();
        }
        if(!readback->Complete.load(std::memory_order_acquire))throw std::runtime_error("Preview readback did not complete");
        return py::bytes(reinterpret_cast<const char*>(readback->Pixels.data()),readback->Pixels.size());
    }
};
void BindShaderPreview(py::module_& module){py::class_<ShaderPreview>(module,"ShaderPreview").def(py::init<const std::string&>()).def("render",&ShaderPreview::Render);}
}
