#include "VFX/VFXInternal.h"
#include "Runtime/GraphicsAccess.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/Console.h"
#include <unordered_map>
#include <map>
#include <algorithm>
#include <numbers>
namespace Bazzalt {
namespace {
constexpr double Step=1.0/120;
constexpr unsigned SceneBudget=131072;
struct Live {Particle Value;float InitialSize=1;};
struct EmitterState {
    VFXGraph Graph;UUID Assigned{};std::vector<Live> Particles;std::vector<ParticleEvent> Events;
    std::map<std::pair<UUID,std::uint64_t>,GraphicsMesh> Meshes;
    ParticleStatistics Stats;double Accumulator=0,Fraction=0;std::int64_t BurstCycle=-1;
    unsigned Random=1,NextId=1;bool Initialized=false,Finished=false,Override=false;
    float Unit(){Random^=Random<<13;Random^=Random>>17;Random^=Random<<5;return float(Random>>8)/16777216.f;}
    float Range(float a,float b){return a+(b-a)*Unit();}
    void Event(ParticleEventType type,unsigned id=0,Vec3 position={}){if(Events.size()<1024)Events.push_back({type,id,position});}
};
Entity Check(Entity entity){if(!entity||!entity.GetScene()||!entity.HasComponent<ParticleEmitter>()||!entity.GetScene()->HasSystem<ParticleSystem>())throw std::invalid_argument("VFX needs an entity with ParticleEmitter in a scene with ParticleSystem");if(!entity.GetComponent<ParticleEmitter>().IsValid())throw std::invalid_argument("Invalid ParticleEmitter settings");return entity;}
void Initialize(EmitterState& s,Entity entity){const auto& c=entity.GetComponent<ParticleEmitter>();if(s.Initialized&&s.Assigned==c.Graph)return;s={};s.Initialized=true;s.Assigned=c.Graph;s.Random=c.Seed?c.Seed:1;s.Stats.Capacity=c.MaxParticles;if(c.Graph)s.Graph=VFXGraph::Load(c.Graph);if(c.Graph&&!s.Graph.IsValid())Console::Error("VFX: "+s.Graph.GetError(),"VFX");s.Stats.Playing=c.PlayOnAwake&&s.Graph.IsValid();}
void Spawn(EmitterState& s,Entity entity,unsigned count,unsigned& total){const auto& c=entity.GetComponent<ParticleEmitter>();auto program=Runtime::VFXAccess::Program(s.Graph);if(!program)return;const auto& p=*program;const auto capacity=std::min(c.MaxParticles,p.Capacity);s.Stats.Capacity=capacity;const unsigned available=std::min(capacity> s.Particles.size()?capacity-unsigned(s.Particles.size()):0,SceneBudget>total?SceneBudget-total:0),accepted=std::min(count,available);s.Stats.Dropped+=count-accepted;if(s.Particles.size()+accepted>s.Particles.capacity())s.Particles.reserve(std::min(std::size_t(capacity),std::max(s.Particles.size()+accepted,std::max(std::size_t(64),s.Particles.capacity()*2))));const auto world=entity.GetWorldMatrix();
    for(unsigned i=0;i<accepted;++i){Live live;auto& v=live.Value;v.Id=s.NextId++;v.Lifetime=s.Range(p.LifeMin,p.LifeMax);live.InitialSize=v.Size=s.Range(p.SizeMin,p.SizeMax);v.Color=p.StartColor;v.Rotation=s.Range(0,2*std::numbers::pi_v<float>);Vec3 point{s.Range(-p.Box.X,p.Box.X),s.Range(-p.Box.Y,p.Box.Y),s.Range(-p.Box.Z,p.Box.Z)};
        if(p.Radius>0){float y=s.Range(-1,1),angle=s.Range(0,2*std::numbers::pi_v<float>),r=p.Radius*std::cbrt(s.Unit()),xz=std::sqrt(std::max(0.f,1-y*y));point={r*xz*std::cos(angle),r*y,r*xz*std::sin(angle)};}
        float cosine=s.Range(std::cos(p.Cone*std::numbers::pi_v<float>/180),1),angle=s.Range(0,2*std::numbers::pi_v<float>),sine=std::sqrt(std::max(0.f,1-cosine*cosine));v.Velocity=Vec3{sine*std::cos(angle),cosine,sine*std::sin(angle)}*s.Range(p.SpeedMin,p.SpeedMax)+p.Velocity;v.Position=point;
        if(c.Space==ParticleSimulationSpace::World){v.Position=world.TransformPoint(v.Position);v.Velocity=world.TransformDirection(v.Velocity);}s.Event(ParticleEventType::Born,v.Id,v.Position);s.Particles.push_back(live);++s.Stats.Spawned;++total;
    }s.Stats.Alive=unsigned(s.Particles.size());
}
void Advance(EmitterState& s,Entity entity,float dt,unsigned& total){auto program=Runtime::VFXAccess::Program(s.Graph);if(!program||s.Stats.Paused)return;const auto& p=*program;const auto& c=entity.GetComponent<ParticleEmitter>();const double start=s.Stats.Time;s.Stats.Time+=dt;
    if(s.Stats.Playing&&s.Stats.Time>=p.Delay){const double age=std::max(0.0,s.Stats.Time-p.Delay);const auto cycle=std::int64_t(age/p.Duration);if((p.Loop||age<p.Duration)&&cycle!=s.BurstCycle){s.BurstCycle=cycle;Spawn(s,entity,p.Burst,total);}const double a=std::max(0.0,start-p.Delay),b=p.Loop?age:std::min(age,double(p.Duration));const double emitted=std::max(0.0,b-a)*p.Rate*c.RateMultiplier+s.Fraction;const unsigned amount=unsigned(std::min(emitted,1000000.0));s.Fraction=emitted-amount;if(amount)Spawn(s,entity,amount,total);if(!p.Loop&&age>=p.Duration)s.Stats.Playing=false;}
    for(std::size_t i=0;i<s.Particles.size();){auto& live=s.Particles[i];auto& v=live.Value;v.Age+=dt;if(v.Age>=v.Lifetime){s.Event(ParticleEventType::Died,v.Id,v.Position);s.Particles[i]=s.Particles.back();s.Particles.pop_back();--total;continue;}
        auto gravity=p.Gravity;float phase=v.Age*p.Frequency+float(v.Id%65536)*.17f;Vec3 noise{std::sin(phase+v.Position.Y),std::sin(phase*1.31f+v.Position.Z),std::cos(phase*.73f+v.Position.X)};v.Velocity=(v.Velocity+(gravity+noise*p.Turbulence)*dt)*std::exp(-p.Drag*dt);v.Position+=v.Velocity*dt;float t=std::clamp(v.Age/v.Lifetime,0.f,1.f);v.Color=p.StartColor*(1-t)+p.EndColor*t;v.Size=live.InitialSize*(1+(p.EndSize-1)*t);v.Rotation+=p.AngularSpeed*dt;++i;
    }s.Stats.Alive=unsigned(s.Particles.size());if(!s.Stats.Playing&&s.Particles.empty()&&!s.Finished){s.Finished=true;s.Event(ParticleEventType::Completed);}
}
}
struct ParticleSystem::Impl {std::unordered_map<UUID,EmitterState> States;unsigned Total()const{unsigned total=0;for(const auto& [_,s]:States)total+=unsigned(s.Particles.size());return total;}
    EmitterState& Get(Entity entity){Check(entity);auto& s=States[entity.GetUUID()];Initialize(s,entity);if(auto program=Runtime::VFXAccess::Program(s.Graph))s.Stats.Capacity=std::min(entity.GetComponent<ParticleEmitter>().MaxParticles,program->Capacity);return s;}
};
ParticleSystem::ParticleSystem():m_impl(std::make_unique<Impl>()){}ParticleSystem::~ParticleSystem()=default;
void ParticleSystem::OnDestroy(Scene&){m_impl->States.clear();}
void ParticleSystem::OnUpdate(Scene& scene,float dt){std::erase_if(m_impl->States,[&](const auto& entry){auto e=scene.GetEntity(entry.first);return !e||!e.HasComponent<ParticleEmitter>();});unsigned total=m_impl->Total();std::vector<UUID> alive;for(auto h:scene.GetRegistry().view<ParticleEmitter>())alive.push_back(scene.GetEntity(static_cast<Entity::Id>(h)).GetUUID());std::sort(alive.begin(),alive.end());
    for(auto id:alive){auto e=scene.GetEntity(id);const auto& c=e.GetComponent<ParticleEmitter>();if(!c.IsValid())continue;unsigned oldCount=0;if(auto old=m_impl->States.find(id);old!=m_impl->States.end())oldCount=unsigned(old->second.Particles.size());auto& s=m_impl->Get(e);total=total-oldCount+unsigned(s.Particles.size());s.Events.clear();if(!c.Enabled||s.Stats.Paused)continue;const float delta=(c.UseUnscaledTime?Time::GetUnscaledDeltaTime():dt)*c.SimulationSpeed;if(!std::isfinite(delta)||delta<0)continue;s.Accumulator=std::min(s.Accumulator+delta,.5);while(s.Accumulator+1e-9>=Step){Advance(s,e,Step,total);s.Accumulator-=Step;}}
    std::erase_if(m_impl->States,[&](const auto& entry){auto e=scene.GetEntity(entry.first);return !e||!e.HasComponent<ParticleEmitter>();});
}
namespace {ParticleSystem& SystemFor(Entity e){Check(e);return e.GetScene()->GetSystem<ParticleSystem>();}}
void VFX::Play(Entity e){auto& s=SystemFor(e).m_impl->Get(e);if(!s.Graph.IsValid())throw std::invalid_argument("ParticleEmitter has no valid VFX graph");if(s.Finished){s.Stats.Time=0;s.Fraction=0;s.BurstCycle=-1;}s.Stats.Playing=true;s.Stats.Paused=false;s.Finished=false;}
void VFX::Restart(Entity e){auto& s=SystemFor(e).m_impl->Get(e);auto graph=s.Graph;auto assigned=s.Assigned;s={};s.Initialized=true;s.Assigned=assigned;s.Graph=std::move(graph);s.Random=e.GetComponent<ParticleEmitter>().Seed?e.GetComponent<ParticleEmitter>().Seed:1;s.Stats.Capacity=std::min(e.GetComponent<ParticleEmitter>().MaxParticles,Runtime::VFXAccess::Program(s.Graph)?Runtime::VFXAccess::Program(s.Graph)->Capacity:e.GetComponent<ParticleEmitter>().MaxParticles);Play(e);}
void VFX::Pause(Entity e){SystemFor(e).m_impl->Get(e).Stats.Paused=true;}void VFX::Resume(Entity e){SystemFor(e).m_impl->Get(e).Stats.Paused=false;}
void VFX::Stop(Entity e,bool clear){auto& s=SystemFor(e).m_impl->Get(e);s.Stats.Playing=false;s.Stats.Paused=false;if(clear)Clear(e);}
void VFX::Clear(Entity e){auto& s=SystemFor(e).m_impl->Get(e);s.Particles.clear();s.Events.clear();s.Stats.Alive=0;}
void VFX::Emit(Entity e,unsigned count){if(count>65535)throw std::length_error("VFX Emit exceeds 65535 particles");auto& system=SystemFor(e);auto& s=system.m_impl->Get(e);if(!s.Graph.IsValid())throw std::invalid_argument("ParticleEmitter has no valid VFX graph");unsigned total=system.m_impl->Total();Spawn(s,e,count,total);}
void VFX::SetGraph(Entity e,VFXGraph graph){if(!graph.IsValid())throw std::invalid_argument(graph.GetError());auto& s=SystemFor(e).m_impl->Get(e);s.Particles.clear();s.Events.clear();s.Meshes.clear();s.Graph=std::move(graph);s.Stats={};s.Accumulator=s.Fraction=0;s.BurstCycle=-1;s.Finished=false;s.Random=e.GetComponent<ParticleEmitter>().Seed?e.GetComponent<ParticleEmitter>().Seed:1;s.NextId=1;s.Stats.Capacity=std::min(e.GetComponent<ParticleEmitter>().MaxParticles,Runtime::VFXAccess::Program(s.Graph)->Capacity);s.Stats.Playing=e.GetComponent<ParticleEmitter>().PlayOnAwake;}
void VFX::Simulate(Entity e,float seconds,bool restart){if(!std::isfinite(seconds)||seconds<0||seconds>30)throw std::invalid_argument("VFX Simulate accepts 0..30 seconds");if(restart)Restart(e);auto& system=SystemFor(e);auto& s=system.m_impl->Get(e);s.Events.clear();unsigned total=system.m_impl->Total();s.Accumulator+=seconds*e.GetComponent<ParticleEmitter>().SimulationSpeed;unsigned steps=0;while(s.Accumulator+1e-9>=Step&&steps++<360000){Advance(s,e,Step,total);s.Accumulator-=Step;}}
ParticleStatistics VFX::GetStatistics(Entity e){return SystemFor(e).m_impl->Get(e).Stats;}
std::vector<Particle> VFX::GetParticles(Entity e){std::vector<Particle> result;for(const auto& p:SystemFor(e).m_impl->Get(e).Particles)result.push_back(p.Value);return result;}
std::vector<ParticleEvent> VFX::GetEvents(Entity e){return SystemFor(e).m_impl->Get(e).Events;}
namespace Runtime {
void VFXAccess::Update(Scene& scene,float delta){scene.GetSystem<ParticleSystem>().OnUpdate(scene,delta);}
void VFXAccess::Render(Scene& scene,const GraphicsEvent& event){if(!scene.HasSystem<ParticleSystem>())return;auto& system=scene.GetSystem<ParticleSystem>();if(!system.IsEnabled())return;Mat4 camera;if(!event.View.TryInverse(camera))return;Vec3 right=camera.TransformDirection({1,0,0}).Normalized(),up=camera.TransformDirection({0,1,0}).Normalized(),normal=Vec3::Cross(right,up);unsigned budget=65536;
    std::vector<UUID> owners;for(const auto& [id,_]:system.m_impl->States)owners.push_back(id);std::sort(owners.begin(),owners.end());
    for(auto id:owners){auto e=scene.GetEntity(id);if(!e||!e.HasComponent<ParticleEmitter>())continue;const auto& c=e.GetComponent<ParticleEmitter>();auto& s=system.m_impl->States.at(id);if(!c.Enabled||!c.IsValid()||s.Particles.empty())continue;auto program=Program(s.Graph);if(!program)continue;auto world=e.GetWorldMatrix();std::vector<const Live*> sorted;for(const auto& p:s.Particles)sorted.push_back(&p);auto position=[&](const Live* p){return c.Space==ParticleSimulationSpace::Local?world.TransformPoint(p->Value.Position):p->Value.Position;};const auto eye=camera.TransformPoint({});std::stable_sort(sorted.begin(),sorted.end(),[&](const auto* a,const auto* b){return (position(a)-eye).LengthSquared()>(position(b)-eye).LengthSquared();});const unsigned count=std::min({unsigned(sorted.size()),budget,32768u});budget-=count;if(!count)break;
        std::vector<GraphicsVertex> vertices;std::vector<std::uint32_t> indices;vertices.reserve(count*4);indices.reserve(count*6);const float parentScale=c.Space==ParticleSimulationSpace::Local?std::max({world.TransformDirection({1,0,0}).Length(),world.TransformDirection({0,1,0}).Length(),world.TransformDirection({0,0,1}).Length()}):1;
        for(unsigned i=0;i<count;++i){const auto& p=sorted[i]->Value;auto center=position(sorted[i]);float size=p.Size*c.SizeMultiplier*parentScale*.5f,sine=std::sin(p.Rotation),cosine=std::cos(p.Rotation);auto r=(right*cosine+up*sine)*size,u=(up*cosine-right*sine)*size;Vec4 color{p.Color.X*c.Tint.X,p.Color.Y*c.Tint.Y,p.Color.Z*c.Tint.Z,p.Color.W*c.Tint.W};const unsigned frame=std::min(unsigned(p.Age/p.Lifetime*program->Columns*program->Rows),program->Columns*program->Rows-1);float x=float(frame%program->Columns)/program->Columns,y=float(frame/program->Columns)/program->Rows,w=1.f/program->Columns,h=1.f/program->Rows;unsigned base=unsigned(vertices.size());vertices.insert(vertices.end(),{{center-r-u,normal,{x,y+h},color},{center+r-u,normal,{x+w,y+h},color},{center+r+u,normal,{x+w,y},color},{center-r+u,normal,{x,y},color}});indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});}
        const auto key=std::pair{event.Camera,event.Viewport};if(!s.Meshes.contains(key)&&s.Meshes.size()>=64)s.Meshes.erase(s.Meshes.begin());auto& mesh=s.Meshes[key];if(mesh.IsValid())mesh.SetGeometry(std::move(vertices),std::move(indices));else mesh=Graphics::CreateMesh(std::move(vertices),std::move(indices));GraphicsAccess::DrawParticles(mesh,c.MaterialOverride?c.MaterialOverride:program->Material,program->Texture,id,event);
    }
}
}
}
