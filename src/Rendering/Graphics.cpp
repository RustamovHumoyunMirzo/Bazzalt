#include "Runtime/GraphicsAccess.h"
#include <set>
#include "Physics/PhysicsModuleApi.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <numeric>
#include <thread>

namespace Bazzalt {
namespace Detail {struct GraphicsMeshState {std::shared_ptr<const Runtime::GraphicsGeometry> Geometry;};}
namespace {
struct Listener {std::shared_ptr<void> Module;Graphics::Callback Callback;GraphicsEventType Type;UUID Owner;const void* Scope=nullptr;};
struct State {
    Scene* ScenePtr=nullptr;std::thread::id Thread;
    std::vector<Runtime::GraphicsCommand> Commands;
    std::map<std::uint64_t,std::shared_ptr<Listener>> Listeners;
    std::map<std::pair<UUID,std::uint64_t>,std::pair<std::uint32_t,std::uint32_t>> Sizes;
    std::vector<GraphicsEvent> Cameras;
    std::uint64_t Next=1,Frame=0;bool InCallback=false,CanDraw=true;
    GraphicsEvent Context;
    std::set<UUID> Deformed;
};
State& Get(){static State value;return value;}
thread_local const void* CurrentScope=nullptr;
void ThreadCheck(){const auto& s=Get();if(s.ScenePtr&&s.Thread!=std::this_thread::get_id())throw std::logic_error("Graphics must be used on the engine thread");}
bool Finite(float value){return std::isfinite(value)&&std::abs(value)<=1e12f;}
std::shared_ptr<Runtime::GraphicsGeometry> Geometry(std::vector<GraphicsVertex> vertices,std::vector<std::uint32_t> indices,GraphicsTopology topology){
    if(vertices.empty()||vertices.size()>Graphics::MaxVertices||indices.size()>Graphics::MaxIndices)throw std::length_error("Invalid Graphics mesh size");
    if(unsigned(topology)>unsigned(GraphicsTopology::TriangleStrip))throw std::invalid_argument("Invalid Graphics topology");
    for(const auto& v:vertices){for(float f:{v.Position.X,v.Position.Y,v.Position.Z,v.Normal.X,v.Normal.Y,v.Normal.Z,v.UV.X,v.UV.Y,v.Color.X,v.Color.Y,v.Color.Z,v.Color.W})if(!Finite(f))throw std::invalid_argument("Non-finite Graphics vertex");if(v.Normal.LengthSquared()<1e-12f)throw std::invalid_argument("Graphics normals must not be zero");}
    if(indices.empty()){indices.resize(vertices.size());std::iota(indices.begin(),indices.end(),0u);}
    const auto count=indices.size();if((topology==GraphicsTopology::Lines&&count%2)||(topology==GraphicsTopology::Triangles&&count%3)||(topology==GraphicsTopology::LineStrip&&count<2)||(topology==GraphicsTopology::TriangleStrip&&count<3))throw std::invalid_argument("Graphics index count does not match topology");
    for(auto i:indices)if(i>=vertices.size())throw std::out_of_range("Graphics index is outside the vertex buffer");
    auto result=std::make_shared<Runtime::GraphicsGeometry>();result->Vertices=std::move(vertices);result->Indices=std::move(indices);result->Topology=topology;return result;
}
void Emit(GraphicsEvent event,bool drawing){
    auto& s=Get();event.Scene=s.ScenePtr?s.ScenePtr->GetUUID():UUID{};event.Frame=s.Frame;
    const auto old=s.Context;const bool in=s.InCallback,can=s.CanDraw;s.Context=event;s.InCallback=true;s.CanDraw=drawing;
    std::vector<std::pair<std::uint64_t,std::shared_ptr<Listener>>> listeners(s.Listeners.begin(),s.Listeners.end());
    for(auto& [id,entry]:listeners){if(!s.Listeners.contains(id)||entry->Type!=event.Type)continue;
        if(entry->Owner&&(!s.ScenePtr||!s.ScenePtr->GetEntity(entry->Owner))){s.Listeners.erase(id);continue;}
        try{struct Scope {std::shared_ptr<void> Previous;const void* Owner;~Scope(){Detail::SetCurrentGameplayModule(std::move(Previous));CurrentScope=Owner;}} scope{Detail::SetCurrentGameplayModule(entry->Module),CurrentScope};CurrentScope=entry->Scope;entry->Callback(event);}catch(const std::exception& e){s.Listeners.erase(id);Console::Error(std::string("Graphics callback disabled: ")+e.what(),"Graphics");}catch(...){s.Listeners.erase(id);Console::Error("Graphics callback disabled after an unknown exception","Graphics");}
    }s.Context=old;s.InCallback=in;s.CanDraw=can;
}
}
bool GraphicsMesh::IsValid()const{return m_state&&bool(m_state->Geometry);}
std::uint64_t GraphicsMesh::GetId()const{return IsValid()?m_state->Geometry->Id:0;}
std::uint64_t GraphicsMesh::GetRevision()const{return IsValid()?m_state->Geometry->Revision:0;}
GraphicsTopology GraphicsMesh::GetTopology()const{return IsValid()?m_state->Geometry->Topology:GraphicsTopology::Triangles;}
std::size_t GraphicsMesh::GetVertexCount()const{return IsValid()?m_state->Geometry->Vertices.size():0;}
std::size_t GraphicsMesh::GetIndexCount()const{return IsValid()?m_state->Geometry->Indices.size():0;}
std::vector<GraphicsVertex> GraphicsMesh::GetVertices()const{return IsValid()?m_state->Geometry->Vertices:std::vector<GraphicsVertex>{};}
std::vector<std::uint32_t> GraphicsMesh::GetIndices()const{return IsValid()?m_state->Geometry->Indices:std::vector<std::uint32_t>{};}
void GraphicsMesh::SetGeometry(std::vector<GraphicsVertex> vertices,std::vector<std::uint32_t> indices,GraphicsTopology topology){
    ThreadCheck();if(!IsValid())throw std::logic_error("Graphics mesh has been destroyed");auto value=Geometry(std::move(vertices),std::move(indices),topology);value->Id=GetId();value->Revision=GetRevision()+1;m_state->Geometry=std::move(value);
}
void GraphicsMesh::Destroy(){ThreadCheck();if(m_state)m_state->Geometry.reset();}
GraphicsMesh Graphics::CreateMesh(std::vector<GraphicsVertex> vertices,std::vector<std::uint32_t> indices,GraphicsTopology topology){ThreadCheck();static std::atomic<std::uint64_t> next{1};auto geometry=Geometry(std::move(vertices),std::move(indices),topology);geometry->Id=next++;geometry->Revision=1;GraphicsMesh mesh;mesh.m_state=std::make_shared<Detail::GraphicsMeshState>();mesh.m_state->Geometry=std::move(geometry);return mesh;}
void Graphics::DrawMesh(GraphicsMesh mesh,Material material,Mat4 transform,GraphicsDrawOptions options){
    ThreadCheck();auto& s=Get();if(!s.ScenePtr)throw std::logic_error("Graphics rendering is not initialized");if(!s.CanDraw)throw std::logic_error("Draw during OnUpdate, OnBeginFrame or OnBeforeCamera; after-render callbacks are notifications only");
    if(!mesh.IsValid())throw std::invalid_argument("Invalid Graphics mesh");if(s.Commands.size()>=MaxDraws)throw std::length_error("Graphics frame draw limit exceeded");
    if(material.GetAssetUUID()&&!material.IsValid())throw std::invalid_argument("Invalid Graphics material");if(options.Priority>7||options.LayerMask&0x80)throw std::invalid_argument("Invalid Graphics priority or reserved editor layer");
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(!Finite(transform(r,c)))throw std::invalid_argument("Invalid Graphics transform");
    if(std::abs(transform(3,0))+std::abs(transform(3,1))+std::abs(transform(3,2))>1e-6f||std::abs(transform(3,3)-1)>1e-6f)throw std::invalid_argument("Graphics model transforms must be affine; unproject clip vertices with the camera matrix first");
    Runtime::GraphicsCommand command{mesh.m_state->Geometry,material.GetAssetUUID(),transform,options};
    command.Scoped=s.InCallback&&s.Context.Type==GraphicsEventType::OnBeforeCamera;
    command.Camera=s.Context.Camera;command.Viewport=s.Context.Viewport;s.Commands.push_back(std::move(command));
}
void Graphics::DrawVertices(std::vector<GraphicsVertex> v,std::vector<std::uint32_t> i,GraphicsTopology t,Material m,Mat4 transform,GraphicsDrawOptions o){DrawMesh(CreateMesh(std::move(v),std::move(i),t),m,transform,o);}
void Graphics::DrawLine(Vec3 a,Vec3 b,Vec4 color,GraphicsDrawOptions o){DrawVertices({{a,{0,0,1},{},color},{b,{0,0,1},{},color}},{},GraphicsTopology::Lines,{},Mat4{},o);}
void Graphics::DrawTriangle(Vec3 a,Vec3 b,Vec3 c,Vec4 color,GraphicsDrawOptions o){auto normal=Vec3::Cross(b-a,c-a).Normalized();DrawVertices({{a,normal,{},color},{b,normal,{},color},{c,normal,{},color}},{},GraphicsTopology::Triangles,{},Mat4{},o);}
void Graphics::ClearDraws(){ThreadCheck();if(!Get().CanDraw)throw std::logic_error("Cannot clear draws from notification callbacks");Get().Commands.clear();}
bool Graphics::IsAvailable(){return Get().ScenePtr!=nullptr;}
Graphics::Subscription Graphics::Subscribe(GraphicsEventType type,Callback callback,Entity owner){return Runtime::GraphicsAccess::SubscribeScoped(type,std::move(callback),owner,CurrentScope);}
bool Graphics::Unsubscribe(Subscription id){ThreadCheck();return Get().Listeners.erase(id)!=0;}
namespace Runtime {
Graphics::Subscription GraphicsAccess::SubscribeScoped(GraphicsEventType type,Graphics::Callback callback,Entity owner,const void* scope){ThreadCheck();auto& s=Get();if(!s.ScenePtr)throw std::logic_error("Graphics is not initialized");if(!callback||unsigned(type)>unsigned(GraphicsEventType::OnEndFrame))throw std::invalid_argument("Invalid Graphics callback");if(s.Listeners.size()>=4096)throw std::length_error("Graphics subscription limit exceeded");if(owner&&s.ScenePtr->GetEntity(owner.GetUUID())!=owner)throw std::invalid_argument("Graphics callback owner belongs to another scene");auto entry=std::make_shared<Listener>(Listener{Detail::GetCurrentGameplayModule(),std::move(callback),type,owner?owner.GetUUID():UUID{},scope});auto id=s.Next++;s.Listeners[id]=std::move(entry);return id;}
const void* GraphicsAccess::SetScope(const void* scope){auto old=CurrentScope;CurrentScope=scope;return old;}
void GraphicsAccess::DisconnectScope(const void* scope){if(!scope)return;std::erase_if(Get().Listeners,[&](const auto& v){return v.second->Scope==scope;});}
void GraphicsAccess::DrawEntity(GraphicsMesh mesh,UUID material,UUID owner,GraphicsDrawOptions options,UUID source,unsigned node,unsigned slot){auto surface=Material::Load(material);Graphics::DrawMesh(mesh,surface.IsValid()?surface:Material{}, {},options);auto& command=Get().Commands.back();command.Owner=owner;command.Source=source;command.SourceNode=node;command.SourceSlot=slot;if(source)Get().Deformed.insert(source);}
bool GraphicsAccess::IsDeformed(UUID source){return Get().Deformed.contains(source);}
void GraphicsAccess::BindScene(Scene* scene){auto& s=Get();if(s.ScenePtr!=scene){s.Listeners.clear();s.Commands.clear();s.Sizes.clear();s.Cameras.clear();s.ScenePtr=scene;s.Deformed.clear();}s.Thread=std::this_thread::get_id();}
void GraphicsAccess::BeginFrame(){auto& s=Get();ThreadCheck();++s.Frame;s.Cameras.clear();s.Deformed.clear();GraphicsEvent event;event.Type=GraphicsEventType::OnBeginFrame;Emit(event,true);if(s.ScenePtr)RenderPhysicsScene(*s.ScenePtr);}
void GraphicsAccess::BeforeCamera(GraphicsEvent event){auto& s=Get();event.Frame=s.Frame;event.Scene=s.ScenePtr?s.ScenePtr->GetUUID():UUID{};s.Cameras.push_back(event);auto key=std::pair{event.Camera,event.Viewport};auto size=std::pair{event.Width,event.Height};if(s.Sizes[key]!=size){s.Sizes[key]=size;event.Type=GraphicsEventType::OnCameraResize;Emit(event,false);}event.Type=GraphicsEventType::OnBeforeCamera;Emit(event,true);}
bool GraphicsAccess::Matches(const GraphicsCommand& c,const GraphicsEvent& e){return (!c.Options.Camera||c.Options.Camera==e.Camera)&&(!c.Scoped||(c.Camera==e.Camera&&c.Viewport==e.Viewport));}
void GraphicsAccess::BeforeDraws(){auto& s=Get();for(auto event:s.Cameras){event.Type=GraphicsEventType::OnBeforeDraw;event.DrawCount=std::count_if(s.Commands.begin(),s.Commands.end(),[&](const auto& c){return Matches(c,event);});std::size_t index=0;for(const auto& c:s.Commands)if(Matches(c,event)){event.DrawIndex=index++;Emit(event,false);}}}
void GraphicsAccess::AfterCamera(GraphicsEvent event){auto& s=Get();event.Type=GraphicsEventType::OnAfterDraw;event.DrawCount=std::count_if(s.Commands.begin(),s.Commands.end(),[&](const auto& c){return Matches(c,event);});for(event.DrawIndex=0;event.DrawIndex<event.DrawCount;++event.DrawIndex)Emit(event,false);event.Type=GraphicsEventType::OnAfterCamera;Emit(event,false);}
void GraphicsAccess::EndFrame(){GraphicsEvent e;e.Type=GraphicsEventType::OnEndFrame;Emit(e,false);CancelFrame();}
void GraphicsAccess::CancelFrame() noexcept{Get().Commands.clear();Get().Cameras.clear();Get().CanDraw=true;}
const std::vector<GraphicsCommand>& GraphicsAccess::Commands(){return Get().Commands;}
}
}
