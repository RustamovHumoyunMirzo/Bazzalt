#include "Bazzalt/Lighting.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Profiler.h"
#include "Bazzalt/Material.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/LightProbe.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/Mesh.h"
#include "Rendering/ModelGeometry.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Bazzalt/AssetManager.h"
#include <fstream>
#include <map>
#include "Rendering/LightingFields.h"
#include "Rendering/LightParameters.h"
#include "Rendering/LightingSky.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <mutex>
#include <numeric>
#include <thread>
#include <stdexcept>
namespace Bazzalt {
namespace {
bool Finite(Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);}
Vec3 Clamp(Vec3 v,float hi=1){return {std::clamp(v.X,0.f,hi),std::clamp(v.Y,0.f,hi),std::clamp(v.Z,0.f,hi)};}
float Axis(Vec3 v,int a){return a==0?v.X:a==1?v.Y:v.Z;}
Vec3 Min(Vec3 a,Vec3 b){return {std::min(a.X,b.X),std::min(a.Y,b.Y),std::min(a.Z,b.Z)};}
Vec3 Max(Vec3 a,Vec3 b){return {std::max(a.X,b.X),std::max(a.Y,b.Y),std::max(a.Z,b.Z)};}
struct Triangle {Vec3 A,B,C,Albedo;};
struct Source {Light Value;Vec3 Position,Direction;};
struct Probe {UUID Id;Vec3 Position;LightProbe Value;};
struct Surface {UUID Id;Lightmap Value;std::vector<Triangle> Triangles;};
struct Capture {UUID Id;Vec3 Position;ReflectionProbe Value;};
struct Snapshot {UUID SceneId;std::vector<Triangle> Triangles;std::vector<Source> Lights;std::vector<Probe> Probes;std::vector<Surface> Surfaces;std::vector<Capture> Captures;ReflectionProbe Sky;Quaternion SkyInverse;std::uint64_t Hash=1469598103934665603ull;};
void Hash(Snapshot& s,std::uint64_t v){for(int i=0;i<8;++i){s.Hash^=(v>>(i*8))&255;s.Hash*=1099511628211ull;}}
void Hash(Snapshot& s,UUID v){Hash(s,v.GetHigh());Hash(s,v.GetLow());}
void Hash(Snapshot& s,float v){Hash(s,std::uint64_t(std::bit_cast<std::uint32_t>(v)));}
void Hash(Snapshot& s,Vec3 v){Hash(s,v.X);Hash(s,v.Y);Hash(s,v.Z);}
Vec3 Albedo(UUID asset,Vec3 fallback){auto m=Material::Load(asset);if(m.IsValid())for(auto& p:m.GetParameters())if(p.Name=="baseColor"||p.Name=="color"){if(p.Type==ShaderParameterType::Float4){auto c=m.GetVec4(p.Name);return Clamp({c.X,c.Y,c.Z});}if(p.Type==ShaderParameterType::Float3)return Clamp(m.GetVec3(p.Name));}return Clamp(fallback);}
Snapshot Collect(Scene& scene,const LightBakeSettings& settings){
    ProfileScope scope("Lighting.Snapshot");
    unsigned probeCount=0;for(auto h:scene.GetRegistry().view<LightProbe>())if(scene.GetRegistry().get<LightProbe>(h).Enabled)++probeCount;
    if(probeCount>Lighting::MaxProbes)throw std::invalid_argument("Light probe resource budget exceeded");
    Snapshot s;s.SceneId=scene.GetUUID();std::vector<Entity> entities;
    for(auto handle:scene.GetRegistry().view<Identity>())entities.push_back(scene.GetEntity(static_cast<Entity::Id>(handle)));
    std::sort(entities.begin(),entities.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});
    std::map<UUID,std::shared_ptr<Runtime::ModelGeometryAsset>> models;std::size_t texelBudget=0,geometryCount=0;
    for(auto e:entities){
        const auto world=e.GetWorldMatrix();const auto position=world.TransformPoint({});
        if(auto* p=e.TryGetComponent<LightProbe>();p&&p->Enabled){if(!p->IsValid()||!Finite(position))throw std::invalid_argument("Invalid Light Probe");s.Probes.push_back({e.GetUUID(),position,*p});Hash(s,e.GetUUID());Hash(s,position);Hash(s,p->Radius);Hash(s,p->Intensity);}
        if(auto* l=e.TryGetComponent<Light>();l&&l->Enabled){if(int(l->Mode)<0||int(l->Mode)>2)throw std::invalid_argument("Invalid light baking mode");auto value=Runtime::SanitizeLight(*l);auto direction=world.TransformDirection({0,0,-1}).Normalized();if(!Finite(position)||!Finite(direction))throw std::invalid_argument("Invalid light transform");s.Lights.push_back({value,position,direction});Hash(s,e.GetUUID());Hash(s,position);Hash(s,direction);Hash(s,std::uint64_t(l->Mode));Hash(s,std::uint64_t(value.Type));Hash(s,value.Color);Hash(s,value.Intensity);Hash(s,value.Range);Hash(s,value.InnerConeAngle);Hash(s,value.OuterConeAngle);Hash(s,std::uint64_t(value.CastShadows));}
        if(auto* r=e.TryGetComponent<ReflectionProbe>();r&&r->Enabled){if(!r->IsValid()||!Finite(position))throw std::invalid_argument("Invalid Reflection Probe");ReflectionProbe value;value.Resolution=r->Resolution;value.Radius=r->Radius;value.Intensity=r->Intensity;value.BlendDistance=r->BlendDistance;value.BoxProjection=r->BoxProjection;value.BoxSize=r->BoxSize;s.Captures.push_back({e.GetUUID(),position,value});Hash(s,e.GetUUID());Hash(s,position);Hash(s,float(r->Resolution));Hash(s,r->Radius);Hash(s,r->Intensity);Hash(s,r->BlendDistance);Hash(s,r->BoxSize);Hash(s,std::uint64_t(r->BoxProjection));for(unsigned size=r->Resolution;size;size/=2)texelBudget+=6u*size*size;}
        auto* receiver=e.TryGetComponent<Lightmap>();Surface* surface=nullptr;
        if(receiver&&receiver->Enabled){if(!receiver->IsValid())throw std::invalid_argument("Invalid Lightmap");s.Surfaces.push_back({e.GetUUID(),{}, {}});surface=&s.Surfaces.back();surface->Value.Resolution=receiver->Resolution;surface->Value.Padding=receiver->Padding;surface->Value.TransformSignature=Runtime::LightingTransformSignature(world);texelBudget+=std::size_t(receiver->Resolution)*receiver->Resolution;Hash(s,e.GetUUID());Hash(s,float(receiver->Resolution));Hash(s,float(receiver->Padding));}
        if(texelBudget>settings.MaxBakeTexels)throw std::length_error("Lighting bake exceeds MaxBakeTexels; reduce capture/lightmap resolution or increase the budget");
        for(int part=0;part<2;++part){
        auto* primitive=part==0?e.TryGetComponent<PrimitiveObject>():nullptr;auto* mesh=part==1?e.TryGetComponent<Mesh>():nullptr;
        if(!((primitive&&primitive->Enabled&&primitive->Visible&&(primitive->ContributeGI||surface))||(mesh&&mesh->Enabled&&mesh->Visible&&(mesh->ContributeGI||surface))))continue;
        const bool contributes=primitive?primitive->ContributeGI:mesh->ContributeGI;Hash(s,std::uint64_t(contributes));
        std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
        const auto add=[&](Vec3 point){indices.push_back(unsigned(vertices.size()));vertices.push_back(point);if(vertices.size()/3>settings.MaxTriangles)throw std::length_error("Lighting geometry exceeds MaxTriangles");};
        if(primitive){auto geometry=Runtime::GetCachedPrimitiveGeometry(*primitive);for(auto index:geometry->Indices){auto& p=geometry->Vertices[index].Position;add({p[0],p[1],p[2]});}}
        else{
            auto& geometry=models[mesh->MeshAsset];if(!geometry){auto asset=AssetManager::GetAsset(mesh->MeshAsset);if(!asset||asset->State!=AssetState::Ready)throw std::runtime_error("Lighting model is not imported");std::ifstream file(asset->CachePath,std::ios::binary|std::ios::ate);auto bytes=file.tellg();if(bytes<=0||bytes>128*1024*1024)throw std::runtime_error("Lighting model cache is invalid or exceeds 128 MiB");std::vector<std::uint8_t> data(std::size_t(bytes),0);file.seekg(0);if(!file.read(reinterpret_cast<char*>(data.data()),bytes))throw std::runtime_error("Cannot read lighting model");geometry=Runtime::ReadModelGeometry(data,asset->CachePath);if(!geometry)throw std::runtime_error("Cannot decode lighting model");}
            const auto append=[&](unsigned index,const Mat4& local){if(index>=geometry->SourceNodes.size()||!geometry->SourceNodes[index])return;for(auto& triangle:geometry->SourceNodes[index]->Triangles)for(auto point:{triangle.A,triangle.B,triangle.C})add(local.TransformPoint(point));};
            if(mesh->ModelNodeIndex!=Mesh::EntireAsset)append(mesh->ModelNodeIndex,Mat4{});
            else{auto model=AssetManager::LoadModel(mesh->MeshAsset);if(!model)throw std::runtime_error("Cannot read lighting model hierarchy");const auto visit=[&](auto&& self,unsigned index,const Mat4& parent,unsigned depth)->void{if(index>=model->Nodes.size()||depth>256)throw std::runtime_error("Invalid lighting model hierarchy");const auto& n=model->Nodes[index];auto local=parent*Mat4::Transform(n.Position,n.Rotation,n.Scale);if(n.HasMesh())append(n.SourceIndex,local);for(auto child:n.Children)self(self,child,local,depth+1);};for(auto root:model->Roots)visit(visit,root,Mat4{},0);}
        }
        Vec3 color=settings.DefaultAlbedo;if(primitive)color=Albedo(primitive->MaterialAsset,{primitive->Color.X,primitive->Color.Y,primitive->Color.Z});else if(mesh)color=Albedo(mesh->MaterialAsset,color);
        if(indices.size()/3>settings.MaxTriangles-geometryCount)throw std::length_error("Lighting bake exceeds MaxTriangles");geometryCount+=indices.size()/3;
        Hash(s,e.GetUUID());for(std::size_t i=0;i+2<indices.size();i+=3){Triangle t{world.TransformPoint(vertices.at(indices[i])),world.TransformPoint(vertices.at(indices[i+1])),world.TransformPoint(vertices.at(indices[i+2])),color};if(!Finite(t.A)||!Finite(t.B)||!Finite(t.C))throw std::invalid_argument("Non-finite lighting geometry");Hash(s,t.A);Hash(s,t.B);Hash(s,t.C);Hash(s,t.Albedo);if(Vec3::Cross(t.B-t.A,t.C-t.A).LengthSquared()>1e-16f){if(contributes)s.Triangles.push_back(t);if(surface)surface->Triangles.push_back(t);}}
    }
    }
    if(s.Probes.empty()&&s.Surfaces.empty()&&s.Captures.empty())throw std::invalid_argument("Add enabled Light Probes, Lightmaps or Reflection Probes before baking");
    for(auto& surface:s.Surfaces)if(surface.Triangles.empty())throw std::invalid_argument("Lightmap requires enabled visible triangle geometry");
    if(!s.Captures.empty()){s.Sky=Runtime::ReadLightingSky(scene.GetEnvironment());s.SkyInverse=Quaternion::FromEuler(scene.GetEnvironment().Rotation).Inversed();Hash(s,scene.GetEnvironment().Rotation);for(auto v:s.Sky.Radiance)Hash(s,v);}
    return s;
}
struct Tracer {
    const Snapshot& Data;const LightBakeSettings& Settings;const std::atomic<bool>& Cancel;
    struct Node {Vec3 Min,Max;unsigned Begin=0,Count=0,Left=0,Right=0;};
    std::vector<Node> Nodes;std::vector<unsigned> Order;
    Tracer(const Snapshot& data,const LightBakeSettings& settings,const std::atomic<bool>& cancel):Data(data),Settings(settings),Cancel(cancel){Order.resize(data.Triangles.size());std::iota(Order.begin(),Order.end(),0);if(!Order.empty())Build(0,unsigned(Order.size()));}
    unsigned Build(unsigned begin,unsigned count){
        if(Cancel.load())throw std::runtime_error("Lighting bake cancelled");
        const auto index=unsigned(Nodes.size());Nodes.push_back({});Vec3 low{INFINITY},high{-INFINITY};
        for(unsigned i=begin;i<begin+count;++i){auto& t=Data.Triangles[Order[i]];low=Min(low,Min(t.A,Min(t.B,t.C)));high=Max(high,Max(t.A,Max(t.B,t.C)));}
        Nodes[index].Min=low;Nodes[index].Max=high;
        if(count<=8){Nodes[index].Begin=begin;Nodes[index].Count=count;return index;}
        auto d=high-low;int axis=d.X>d.Y?(d.X>d.Z?0:2):(d.Y>d.Z?1:2);const auto middle=begin+count/2;
        std::nth_element(Order.begin()+begin,Order.begin()+middle,Order.begin()+begin+count,[&](unsigned a,unsigned b){auto& x=Data.Triangles[a];auto& y=Data.Triangles[b];return Axis(x.A+x.B+x.C,axis)<Axis(y.A+y.B+y.C,axis);});
        const auto left=Build(begin,count/2),right=Build(middle,count-count/2);Nodes[index].Left=left;Nodes[index].Right=right;return index;
    }
    bool Box(const Node& n,Vec3 o,Vec3 d,float distance)const{
        float near=0,far=distance;for(int a=0;a<3;++a){float v=Axis(d,a),at=Axis(o,a);if(std::abs(v)<1e-10f){if(at<Axis(n.Min,a)||at>Axis(n.Max,a))return false;continue;}float x=(Axis(n.Min,a)-at)/v,y=(Axis(n.Max,a)-at)/v;if(x>y)std::swap(x,y);near=std::max(near,x);far=std::min(far,y);if(near>far)return false;}return true;
    }
    int Hit(Vec3 origin,Vec3 direction,float& distance)const{
        if(Nodes.empty())return -1;int hit=-1;std::array<unsigned,64> stack{};unsigned stackSize=1;
        while(stackSize){auto index=stack[--stackSize];const auto& node=Nodes[index];if(!Box(node,origin,direction,distance))continue;
            if(!node.Count){stack[stackSize++]=node.Left;stack[stackSize++]=node.Right;continue;}
            for(unsigned i=node.Begin;i<node.Begin+node.Count;++i){auto ti=Order[i];auto& t=Data.Triangles[ti];auto e1=t.B-t.A,e2=t.C-t.A,p=Vec3::Cross(direction,e2);float det=Vec3::Dot(e1,p);if(std::abs(det)<1e-10f)continue;float inv=1/det;auto v=origin-t.A;float u=Vec3::Dot(v,p)*inv;if(u<0||u>1)continue;auto q=Vec3::Cross(v,e1);float w=Vec3::Dot(direction,q)*inv;if(w<0||u+w>1)continue;float at=Vec3::Dot(e2,q)*inv;if(at>Settings.RayBias*.1f&&at<distance){distance=at;hit=int(ti);}}
        }return hit;
    }
    bool Visible(Vec3 p,Vec3 d,float distance)const{float at=distance;return Hit(p+d*Settings.RayBias,d,at)<0;}
    // Photometric intensity matches the renderer: lux for distant lights, lumens for local lights.
    Vec3 LightAt(const Source& s,Vec3 p,Vec3& direction,float& distance)const{
        auto& l=s.Value;float scale=l.Intensity;distance=Settings.MaxDistance;
        if(l.Type==LightType::Directional||l.Type==LightType::Sun)direction=-s.Direction;
        else{auto delta=s.Position-p;distance=delta.Length();if(distance<Settings.RayBias||distance>=l.Range)return {};direction=delta/distance;float x=distance/l.Range;float attenuation=std::max(0.f,1-x*x*x*x);scale*=attenuation*attenuation/(4*Pi*distance*distance);
            if(l.Type==LightType::Spot){float cosine=Vec3::Dot(-direction,s.Direction),outer=std::cos(l.OuterConeAngle),inner=std::cos(l.InnerConeAngle);float cone=std::clamp((cosine-outer)/std::max(1e-5f,inner-outer),0.f,1.f);scale*=cone*cone*4;}}
        if(l.CastShadows&&!Visible(p,direction,std::min(Settings.MaxDistance,std::max(0.f,distance-Settings.RayBias*2))))return {};
        return l.Color*scale;
    }
    Vec3 Direct(Vec3 p,Vec3 n,bool bakedOnly=false,bool realtime=false)const{Vec3 value{};for(auto& l:Data.Lights){if((bakedOnly&&l.Value.Mode!=LightMode::Baked)||(!realtime&&l.Value.Mode==LightMode::Realtime))continue;Vec3 direction;float distance;auto incoming=LightAt(l,p,direction,distance);value+=incoming*std::max(0.f,Vec3::Dot(n,direction));}return value;}
    static float Random(std::uint32_t& seed){seed=seed*1664525u+1013904223u;return float(seed>>8)*(1.f/16777216.f);}
    Vec3 Radiance(Vec3 p,Vec3 d,unsigned depth,std::uint32_t& random,bool realtime=false)const{
        if(Cancel.load())return {};float distance=Settings.MaxDistance;auto hit=Hit(p,d,distance);if(hit<0)return Settings.AmbientRadiance+(realtime&&Data.Sky.Baked?Runtime::SampleReflectionRadiance(Data.Sky,Data.SkyInverse.Rotate(d),0):Vec3{});if(!depth)return {};
        auto& t=Data.Triangles[unsigned(hit)];Vec3 n=Vec3::Cross(t.B-t.A,t.C-t.A).Normalized();if(Vec3::Dot(n,d)>0)n=-n;auto point=p+d*distance+n*Settings.RayBias;auto value=Direct(point,n,false,realtime)/Pi;
        if(depth>0){float u=Random(random),phi=2*Pi*Random(random);auto tangent=Vec3::Cross(std::abs(n.Y)<.99f?Vec3{0,1,0}:Vec3{1,0,0},n).Normalized();auto bitangent=Vec3::Cross(n,tangent);Vec3 bounce=tangent*(std::sqrt(u)*std::cos(phi))+bitangent*(std::sqrt(u)*std::sin(phi))+n*std::sqrt(1-u);value+=Radiance(point,bounce,depth-1,random,realtime);}
        return t.Albedo*value;
    }
};
}
struct LightBakeHandle::State {
    std::atomic<LightBakeStatus> Status{LightBakeStatus::Baking};std::atomic<float> Progress{0};std::atomic<bool> Cancelled{false};
    mutable std::mutex Mutex;std::string Error;UUID SceneId;std::uint64_t Hash;LightBakeSettings Settings;std::vector<Probe> Probes;std::vector<Surface> Surfaces;std::vector<Capture> Captures;std::thread::id Owner;
};
bool LightBakeSettings::IsValid()const{return Samples>=16&&Samples<=16384&&Bounces<=4&&LightmapSamples>=1&&LightmapSamples<=4096&&ReflectionSamples>=1&&ReflectionSamples<=4096&&MaxBakeTexels>=1&&MaxBakeTexels<=8388608&&MaxTriangles>=1&&MaxTriangles<=4000000&&std::isfinite(MaxDistance)&&MaxDistance>0&&std::isfinite(RayBias)&&RayBias>0&&RayBias<MaxDistance&&Finite(AmbientRadiance)&&AmbientRadiance.X>=0&&AmbientRadiance.Y>=0&&AmbientRadiance.Z>=0&&Finite(DefaultAlbedo)&&DefaultAlbedo.X>=0&&DefaultAlbedo.X<=1&&DefaultAlbedo.Y>=0&&DefaultAlbedo.Y<=1&&DefaultAlbedo.Z>=0&&DefaultAlbedo.Z<=1;}
LightBakeStatus LightBakeHandle::GetStatus()const{return m_state?m_state->Status.load():LightBakeStatus::Failed;}
float LightBakeHandle::GetProgress()const{return m_state?m_state->Progress.load():0;}
std::string LightBakeHandle::GetError()const{if(!m_state)return "No lighting bake";std::lock_guard lock(m_state->Mutex);return m_state->Error;}
void LightBakeHandle::Cancel()const{if(!m_state)return;std::lock_guard lock(m_state->Mutex);if(m_state->Status==LightBakeStatus::Applied)return;m_state->Cancelled=true;m_state->Status=LightBakeStatus::Cancelled;}
LightBakeHandle Lighting::BeginBake(Scene& scene,const LightBakeSettings& settings){
    if(!settings.IsValid())throw std::invalid_argument("Invalid lighting bake settings");auto data=Collect(scene,settings);LightBakeHandle handle;auto s=std::make_shared<LightBakeHandle::State>();handle.m_state=s;s->SceneId=data.SceneId;s->Hash=data.Hash;s->Settings=settings;s->Owner=std::this_thread::get_id();
    Jobs::Schedule([s,data=std::move(data),settings]()mutable{ProfileScope scope("Lighting.Bake");try{Tracer tracer(data,settings,s->Cancelled);std::vector<Probe> results=data.Probes;std::vector<Surface> surfaces=data.Surfaces;std::vector<Capture> captures=data.Captures;
        for(unsigned index=0;index<results.size();++index){auto& probe=results[index];probe.Value.Coefficients={};
            for(unsigned sample=0;sample<settings.Samples;++sample){if(s->Cancelled.load())return;float z=1-2*(float(sample)+.5f)/settings.Samples,phi=float(sample)*2.39996323f;Vec3 d{std::sqrt(std::max(0.f,1-z*z))*std::cos(phi),std::sqrt(std::max(0.f,1-z*z))*std::sin(phi),z};std::uint32_t random=sample+index*65537u+1;auto incoming=tracer.Radiance(probe.Position,d,settings.Bounces,random);auto basis=Runtime::LightingBasis(d);for(unsigned c=0;c<9;++c)probe.Value.Coefficients[c]+=incoming*(basis[c]*4*Pi/settings.Samples);s->Progress=.25f*(float(index)+float(sample+1)/settings.Samples)/std::max<std::size_t>(1,results.size());}
            // Delta lights contribute to baked direct lighting only; Mixed direct remains realtime.
            for(auto& light:data.Lights)if(light.Value.Mode==LightMode::Baked){Vec3 direction;float distance;auto incoming=tracer.LightAt(light,probe.Position,direction,distance);auto basis=Runtime::LightingBasis(direction);for(unsigned c=0;c<9;++c)probe.Value.Coefficients[c]+=incoming*basis[c];}
            for(unsigned c=0;c<9;++c)probe.Value.Coefficients[c]*=c==0?Pi:c<4?2*Pi/3:Pi/4;probe.Value.Baked=true;if(!probe.Value.IsValid())throw std::runtime_error("Lighting bake produced non-finite irradiance");
        }
        for(unsigned index=0;index<surfaces.size();++index){auto& surface=surfaces[index];auto& map=surface.Value;map.BakedResolution=map.Resolution;map.BakedPadding=map.Padding;map.ChartGrid=unsigned(std::ceil(std::sqrt(double(surface.Triangles.size()))));unsigned tile=map.Resolution/map.ChartGrid;
            if(tile<map.Padding*2+2)throw std::runtime_error("Lightmap resolution is too small for non-overlapping charts and padding; increase Resolution");map.Texels.resize(std::size_t(map.Resolution)*map.Resolution);map.Geometry.reserve(surface.Triangles.size()*3);const unsigned inner=tile-2*map.Padding-1;
            for(unsigned triangle=0;triangle<surface.Triangles.size();++triangle){auto& t=surface.Triangles[triangle];map.Geometry.insert(map.Geometry.end(),{t.A,t.B,t.C});auto normal=Vec3::Cross(t.B-t.A,t.C-t.A).Normalized();auto tangent=(t.B-t.A).Normalized(),bitangent=Vec3::Cross(normal,tangent);unsigned ox=(triangle%map.ChartGrid)*tile,oy=(triangle/map.ChartGrid)*tile;
                // Extend edge irradiance into the chart gutter; charts never share texels.
                for(unsigned y=0;y<tile;++y)for(unsigned x=0;x<tile;++x){if(s->Cancelled)return;float u=std::clamp((float(x)-map.Padding)/inner,0.f,1.f),v=std::clamp((float(y)-map.Padding)/inner,0.f,1.f);if(u+v>1){float excess=(u+v-1)*.5f;u-=excess;v-=excess;}auto point=t.A+(t.B-t.A)*u+(t.C-t.A)*v+normal*settings.RayBias;Vec3 irradiance=tracer.Direct(point,normal,true);std::uint32_t random=(triangle*map.Resolution+y)*map.Resolution+x+1;
                    for(unsigned sample=0;sample<settings.LightmapSamples;++sample){float a=Tracer::Random(random),phi=2*Pi*Tracer::Random(random);auto direction=tangent*(std::sqrt(a)*std::cos(phi))+bitangent*(std::sqrt(a)*std::sin(phi))+normal*std::sqrt(1-a);irradiance+=tracer.Radiance(point,direction,settings.Bounces,random)*(Pi/settings.LightmapSamples);}map.Texels[(oy+y)*map.Resolution+ox+x]=irradiance;
                }
            }map.Baked=true;if(!map.IsValid())throw std::runtime_error("Lightmap baking produced invalid data");surface.Triangles.clear();s->Progress=.25f+.4f*float(index+1)/std::max<std::size_t>(1,surfaces.size());
        }
        for(unsigned index=0;index<captures.size();++index){auto& capture=captures[index];auto& p=capture.Value;p.CapturePosition=capture.Position;p.BakedResolution=p.Resolution;std::size_t count=0;for(unsigned size=p.Resolution;size;size/=2)count+=6u*size*size;p.Radiance.resize(count);p.Baked=true;
            for(unsigned face=0;face<6;++face)for(unsigned y=0;y<p.Resolution;++y)for(unsigned x=0;x<p.Resolution;++x){if(s->Cancelled)return;auto direction=Runtime::LightingCubeDirection(face,(x+.5f)*2/p.Resolution-1,(y+.5f)*2/p.Resolution-1);std::uint32_t random=(face*p.Resolution+y)*p.Resolution+x+1;p.Radiance[(face*p.Resolution+y)*p.Resolution+x]=tracer.Radiance(capture.Position,direction,settings.Bounces+1,random,true);}
            std::size_t offset=6u*p.Resolution*p.Resolution;unsigned levels=0;for(unsigned r=p.Resolution;r>1;r/=2)++levels;unsigned mip=1;
            for(unsigned size=p.Resolution/2;size;size/=2,++mip){float roughness=float(mip)/levels,alpha=roughness*roughness;for(unsigned face=0;face<6;++face)for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){if(s->Cancelled)return;auto n=Runtime::LightingCubeDirection(face,(x+.5f)*2/size-1,(y+.5f)*2/size-1);auto tangent=Vec3::Cross(std::abs(n.Y)<.99f?Vec3{0,1,0}:Vec3{1,0,0},n).Normalized(),bitangent=Vec3::Cross(n,tangent);Vec3 value{};float total=0;std::uint32_t random=(face*size+y)*size+x+mip*65537u;
                    for(unsigned sample=0;sample<settings.ReflectionSamples;++sample){float u=(sample+.5f)/settings.ReflectionSamples,phi=2*Pi*Tracer::Random(random),c=std::sqrt((1-u)/(1+(alpha*alpha-1)*u)),sn=std::sqrt(std::max(0.f,1-c*c));auto h=tangent*(sn*std::cos(phi))+bitangent*(sn*std::sin(phi))+n*c;auto d=h*(2*Vec3::Dot(n,h))-n;float weight=std::max(0.f,Vec3::Dot(n,d));if(weight>0){value+=Runtime::SampleReflectionRadiance(p,d,0)*weight;total+=weight;}}
                    p.Radiance[offset+(face*size+y)*size+x]=total>0?value/total:Vec3{};
                }offset+=6u*size*size;}
            if(!p.IsValid())throw std::runtime_error("Reflection baking produced invalid radiance");s->Progress=.65f+.35f*float(index+1)/std::max<std::size_t>(1,captures.size());
        }
        std::lock_guard lock(s->Mutex);if(s->Cancelled)return;s->Probes=std::move(results);s->Surfaces=std::move(surfaces);s->Captures=std::move(captures);s->Progress=1;s->Status=LightBakeStatus::Complete;
    }catch(const std::exception& e){std::lock_guard lock(s->Mutex);if(!s->Cancelled){s->Error=e.what();s->Status=LightBakeStatus::Failed;}}catch(...){std::lock_guard lock(s->Mutex);if(!s->Cancelled){s->Error="Lighting bake failed";s->Status=LightBakeStatus::Failed;}}});return handle;
}
bool LightBakeHandle::Apply(Scene& scene)const{
    if(!m_state)return false;auto& s=*m_state;if(std::this_thread::get_id()!=s.Owner)throw std::logic_error("Apply lighting bake on the scene owner thread");
    if(s.Status!=LightBakeStatus::Complete)return false;
    try{auto current=Collect(scene,s.Settings);std::lock_guard lock(s.Mutex);if(s.Status!=LightBakeStatus::Complete)return false;if(current.SceneId!=s.SceneId||current.Hash!=s.Hash){s.Error="Scene changed during lighting bake; bake again";s.Status=LightBakeStatus::Failed;return false;}
        auto surfaces=s.Surfaces;auto captures=s.Captures;for(auto& p:s.Probes){auto e=scene.GetEntity(p.Id);auto& stored=e.GetComponent<LightProbe>();stored.Coefficients=p.Value.Coefficients;stored.Baked=true;}for(auto& surface:surfaces)scene.GetEntity(surface.Id).GetComponent<Lightmap>()=std::move(surface.Value);for(auto& capture:captures)scene.GetEntity(capture.Id).GetComponent<ReflectionProbe>()=std::move(capture.Value);Lighting::Invalidate(scene);s.Status=LightBakeStatus::Applied;return true;
    }catch(const std::exception& e){std::lock_guard lock(s.Mutex);s.Error=e.what();s.Status=LightBakeStatus::Failed;return false;}
}
std::vector<Entity> Lighting::CreateProbeGrid(Scene& scene,Vec3 minimum,Vec3 maximum,unsigned x,unsigned y,unsigned z,float radius){
    if(!Finite(minimum)||!Finite(maximum)||minimum.X>maximum.X||minimum.Y>maximum.Y||minimum.Z>maximum.Z||!x||!y||!z||x>MaxProbes||y>MaxProbes||z>MaxProbes||std::uint64_t(x)*y*z>MaxProbes||!std::isfinite(radius)||radius<=0||scene.GetRegistry().view<LightProbe>().size()+x*y*z>MaxProbes)throw std::invalid_argument("Invalid probe grid or probe limit exceeded");
    std::vector<Entity> result;try{for(unsigned k=0;k<z;++k)for(unsigned j=0;j<y;++j)for(unsigned i=0;i<x;++i){auto e=scene.CreateEntity("Light Probe");result.push_back(e);auto fraction=Vec3{x==1?.5f:float(i)/(x-1),y==1?.5f:float(j)/(y-1),z==1?.5f:float(k)/(z-1)};e.GetComponent<Transform>().Position=minimum+(maximum-minimum)*fraction;e.AddComponent<LightProbe>().Radius=radius;}}catch(...){for(auto e:result)scene.DestroyEntity(e);throw;}return result;
}
void Lighting::Clear(Scene& scene){for(auto h:scene.GetRegistry().view<LightProbe>()){auto& p=scene.GetRegistry().get<LightProbe>(h);p.Baked=false;p.Coefficients={};}for(auto h:scene.GetRegistry().view<Lightmap>()){auto& p=scene.GetRegistry().get<Lightmap>(h);p.Baked=false;p.Texels.clear();p.Geometry.clear();p.BakedResolution=0;p.ChartGrid=0;}for(auto h:scene.GetRegistry().view<ReflectionProbe>()){auto& p=scene.GetRegistry().get<ReflectionProbe>(h);p.Baked=false;p.Radiance.clear();p.BakedResolution=0;}Invalidate(scene);}
Vec3 Lighting::Sample(const Scene& source,Vec3 position,Vec3 normal){
    if(!Finite(position)||!Finite(normal)||normal.LengthSquared()<1e-12f)throw std::invalid_argument("Invalid lighting sample");auto& scene=const_cast<Scene&>(source);std::optional<std::pair<Vec3,float>> lightmap;
    for(auto h:scene.GetRegistry().view<Lightmap>()){auto e=scene.GetEntity(static_cast<Entity::Id>(h));const auto& m=e.GetComponent<Lightmap>();if(!m.Enabled||m.TransformSignature!=Runtime::LightingTransformSignature(e.GetWorldMatrix()))continue;auto value=Runtime::SampleLightmapIrradiance(m,position,normal.Normalized());if(value&&(!lightmap||value->second<lightmap->second))lightmap=value;}
    if(lightmap)return lightmap->first;
    auto basis=Runtime::LightingBasis(normal.Normalized());Vec3 result{};float total=0;std::array<std::pair<float,Vec3>,4> best{};
    std::vector<Entity> probes;for(auto h:scene.GetRegistry().view<LightProbe>()){auto e=scene.GetEntity(static_cast<Entity::Id>(h));const auto& p=e.GetComponent<LightProbe>();if(p.Enabled&&p.Baked&&p.IsValid()&&Finite(e.GetWorldMatrix().TransformPoint({})))probes.push_back(e);}
    std::sort(probes.begin(),probes.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});
    for(auto e:probes){auto& p=e.GetComponent<LightProbe>();if(!p.Enabled||!p.Baked||!p.IsValid())continue;auto delta=position-e.GetWorldMatrix().TransformPoint({});float d=delta.Length();if(d>=p.Radius)continue;float t=1-d/p.Radius,w=t*t/std::max(.0001f,delta.LengthSquared());Vec3 value{};for(unsigned c=0;c<9;++c)value+=p.Coefficients[c]*basis[c];for(unsigned i=0;i<best.size();++i)if(w>best[i].first){for(unsigned j=3;j>i;--j)best[j]=best[j-1];best[i]={w,value*p.Intensity};break;}}
    for(auto& [w,value]:best){result+=value*w;total+=w;}
    return total>0?Max(result/total,Vec3{}):Vec3{};
}
Vec3 Lighting::SampleReflection(const Scene& input,Vec3 position,Vec3 direction,float roughness){
    if(!Finite(position)||!Finite(direction)||direction.LengthSquared()<1e-12f||!std::isfinite(roughness)||roughness<0||roughness>1)throw std::invalid_argument("Invalid reflection sample");auto& scene=const_cast<Scene&>(input);std::vector<Entity> probes;for(auto h:scene.GetRegistry().view<ReflectionProbe>())probes.push_back(scene.GetEntity(static_cast<Entity::Id>(h)));std::sort(probes.begin(),probes.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});std::array<std::pair<float,Vec3>,4> best{};
    for(auto e:probes){const auto& p=e.GetComponent<ReflectionProbe>();if(!p.Enabled||!p.Baked||!p.IsValid()||(e.GetWorldMatrix().TransformPoint({})-p.CapturePosition).LengthSquared()>=1e-6f)continue;auto delta=position-p.CapturePosition;float distance=delta.Length();if(distance>=p.Radius)continue;float t=1-distance/p.Radius,w=t*t/std::max(.0001f,delta.LengthSquared());auto value=Runtime::SampleReflectionRadiance(p,Runtime::ReflectionDirection(p,position,direction.Normalized()),roughness)*p.Intensity;for(unsigned i=0;i<4;++i)if(w>best[i].first){for(unsigned j=3;j>i;--j)best[j]=best[j-1];best[i]={w,value};break;}}
    Vec3 value{};float total=0;for(auto& [w,c]:best){value+=c*w;total+=w;}return total>0?value/total:Vec3{};
}

}
