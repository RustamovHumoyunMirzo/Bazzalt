#include "Rendering/BakedLightingAtlas.h"
#include "Rendering/LightingFields.h"
#include "Bazzalt/Lighting.h"
#include "Bazzalt/Scene.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
namespace Bazzalt::Runtime {
namespace {
bool Finite(Vec3 p){return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z);}
Vec3 Min(Vec3 a,Vec3 b){return {std::min(a.X,b.X),std::min(a.Y,b.Y),std::min(a.Z,b.Z)};}
Vec3 Max(Vec3 a,Vec3 b){return {std::max(a.X,b.X),std::max(a.Y,b.Y),std::max(a.Z,b.Z)};}
void Hash(std::uint64_t& h,std::uint64_t v){for(unsigned i=0;i<8;++i){h^=(v>>(8*i))&255;h*=1099511628211ull;}}
void Hash(std::uint64_t& h,float f){Hash(h,std::uint64_t(std::bit_cast<std::uint32_t>(f)));}
void Hash(std::uint64_t& h,Vec3 v){Hash(h,v.X);Hash(h,v.Y);Hash(h,v.Z);}
std::vector<Entity> Ordered(Scene& scene){std::vector<Entity> result;const auto append=[&]<class T>(){for(auto h:scene.GetRegistry().view<T>())result.push_back(scene.GetEntity(static_cast<Entity::Id>(h)));};append.template operator()<LightProbe>();append.template operator()<Lightmap>();append.template operator()<ReflectionProbe>();std::sort(result.begin(),result.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});result.erase(std::unique(result.begin(),result.end(),[](Entity a,Entity b){return a.GetUUID()==b.GetUUID();}),result.end());return result;}
struct Region {Vec3 Min,Max;unsigned Type,Index;};
}
std::uint64_t BakedLightingFingerprint(Scene& scene){
    std::uint64_t h=1469598103934665603ull;Hash(h,LightingRevision(scene));for(auto e:Ordered(scene)){
        auto* p=e.TryGetComponent<LightProbe>();auto* m=e.TryGetComponent<Lightmap>();auto* r=e.TryGetComponent<ReflectionProbe>();if(!p&&!m&&!r)continue;Hash(h,e.GetUUID().GetHigh());Hash(h,e.GetUUID().GetLow());auto world=e.GetWorldMatrix();for(int x=0;x<4;++x)for(int y=0;y<4;++y)Hash(h,world(x,y));
        if(p){Hash(h,std::uint64_t(p->Enabled));Hash(h,std::uint64_t(p->Baked));Hash(h,p->Radius);Hash(h,p->Intensity);for(auto c:p->Coefficients)Hash(h,c);}
        if(m){Hash(h,std::uint64_t(m->Enabled));Hash(h,std::uint64_t(m->Baked));Hash(h,std::uint64_t(m->BakedResolution));Hash(h,std::uint64_t(m->BakedPadding));Hash(h,std::uint64_t(m->ChartGrid));Hash(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(m->Geometry.data())));Hash(h,std::uint64_t(m->Geometry.size()));Hash(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(m->Texels.data())));Hash(h,std::uint64_t(m->Texels.size()));for(unsigned char c:m->TransformSignature)Hash(h,std::uint64_t(c));}
        if(r){Hash(h,std::uint64_t(r->Enabled));Hash(h,std::uint64_t(r->Baked));Hash(h,r->Radius);Hash(h,r->Intensity);Hash(h,r->BlendDistance);Hash(h,r->BoxSize);Hash(h,std::uint64_t(r->BoxProjection));Hash(h,r->CapturePosition);Hash(h,std::uint64_t(r->BakedResolution));Hash(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(r->Radiance.data())));Hash(h,std::uint64_t(r->Radiance.size()));}
    }return h;
}
BakedLightingAtlas BuildBakedLightingAtlas(Scene& scene,unsigned textureLimit){
    BakedLightingAtlas out;std::vector<Entity> probes,maps,reflections;std::size_t charts=0;for(auto e:Ordered(scene)){
        if(auto* p=e.TryGetComponent<LightProbe>();p&&p->Enabled&&p->Baked&&p->IsValid()&&Finite(e.GetWorldMatrix().TransformPoint({})))probes.push_back(e);
        if(auto* m=e.TryGetComponent<Lightmap>();m&&m->Enabled&&m->Baked&&m->IsValid()&&m->TransformSignature==LightingTransformSignature(e.GetWorldMatrix())){maps.push_back(e);charts+=m->Geometry.size()/3;}
        if(auto* r=e.TryGetComponent<ReflectionProbe>();r&&r->Enabled&&r->Baked&&r->IsValid()&&(e.GetWorldMatrix().TransformPoint({})-r->CapturePosition).LengthSquared()<1e-6f)reflections.push_back(e);
    }
    if(probes.size()>Lighting::MaxProbes)throw std::length_error("Light probe resource budget exceeded");out.ReflectionCount=unsigned(reflections.size());
    std::vector<Vec4> data(4+probes.size()*10+charts*5+reflections.size()*4);const unsigned po=4,co=po+unsigned(probes.size())*10,ro=co+unsigned(charts)*5;std::vector<Region> regions;Vec3 minimum{INFINITY},maximum{-INFINITY};
    const auto region=[&](Vec3 low,Vec3 high,unsigned type,unsigned index){if(!Finite(low)||!Finite(high))throw std::runtime_error("Non-finite baked lighting influence bounds");regions.push_back({low,high,type,index});minimum=Min(minimum,low);maximum=Max(maximum,high);};
    for(unsigned i=0;i<probes.size();++i){auto e=probes[i];auto& p=e.GetComponent<LightProbe>();auto position=e.GetWorldMatrix().TransformPoint({});data[po+i*10]={position.X,position.Y,position.Z,p.Radius};for(unsigned c=0;c<9;++c){auto v=p.Coefficients[c]*p.Intensity;data[po+i*10+c+1]={v.X,v.Y,v.Z,0};}region(position-Vec3{p.Radius},position+Vec3{p.Radius},0,i);}
    unsigned chart=0;for(auto e:maps){const auto& m=e.GetComponent<Lightmap>();const auto pixels=unsigned(data.size());for(auto v:m.Texels)data.push_back({v.X,v.Y,v.Z,0});const auto tile=m.BakedResolution/m.ChartGrid;
        for(unsigned i=0;i<m.Geometry.size()/3;++i,++chart){const auto a=m.Geometry[i*3],b=m.Geometry[i*3+1],c=m.Geometry[i*3+2],ab=b-a,ac=c-a,n=Vec3::Cross(ab,ac).Normalized();float epsilon=std::max(.0001f,std::max(ab.Length(),ac.Length())*.00001f);const auto at=co+chart*5;data[at]={a.X,a.Y,a.Z,float(pixels)};data[at+1]={ab.X,ab.Y,ab.Z,float(m.BakedResolution)};data[at+2]={ac.X,ac.Y,ac.Z,float((i%m.ChartGrid)*tile+m.BakedPadding)};data[at+3]={n.X,n.Y,n.Z,float((i/m.ChartGrid)*tile+m.BakedPadding)};data[at+4]={float(tile-2*m.BakedPadding-1),epsilon,0,0};region(Min(a,Min(b,c))-Vec3{epsilon},Max(a,Max(b,c))+Vec3{epsilon},1,chart);}
    }
    for(unsigned i=0;i<reflections.size();++i){const auto& p=reflections[i].GetComponent<ReflectionProbe>();const auto pixels=unsigned(data.size());unsigned levels=0;for(unsigned r=p.BakedResolution;r>1;r/=2)++levels;data[ro+i*4]={p.CapturePosition.X,p.CapturePosition.Y,p.CapturePosition.Z,p.Radius};data[ro+i*4+1]={p.BoxSize.X,p.BoxSize.Y,p.BoxSize.Z,float(p.BakedResolution)};data[ro+i*4+2]={float(pixels),p.Intensity,p.BoxProjection?1.f:0.f,float(levels)};data[ro+i*4+3]={p.BlendDistance,0,0,0};for(auto v:p.Radiance)data.push_back({v.X,v.Y,v.Z,0});region(p.CapturePosition-Vec3{p.Radius},p.CapturePosition+Vec3{p.Radius},2,i);}
    unsigned grid=regions.empty()?0:regions.size()>512?32:16;if(grid&&!Finite(maximum-minimum)){grid=0;out.SpatialFallback=true;}unsigned gridOffset=unsigned(data.size());Vec3 scale{};
    // Cluster lists contain every overlapping influence; a budget overflow falls back to exact global lookup.
    if(grid){auto span=maximum-minimum;span=Max(span,Vec3{.001f});scale={grid/span.X,grid/span.Y,grid/span.Z};using Lists=std::array<std::vector<unsigned>,3>;std::vector<Lists> cells(grid*grid*grid);std::size_t entries=0;bool fallback=false;
        const auto cell=[&](Vec3 p){return std::array<unsigned,3>{unsigned(std::clamp(int((p.X-minimum.X)*scale.X),0,int(grid-1))),unsigned(std::clamp(int((p.Y-minimum.Y)*scale.Y),0,int(grid-1))),unsigned(std::clamp(int((p.Z-minimum.Z)*scale.Z),0,int(grid-1)))};};
        for(const auto& r:regions){auto a=cell(r.Min),b=cell(r.Max);if(std::uint64_t(entries)+std::uint64_t(b[0]-a[0]+1)*(b[1]-a[1]+1)*(b[2]-a[2]+1)>4194304){fallback=true;break;}for(unsigned z=a[2];z<=b[2];++z)for(unsigned y=a[1];y<=b[1];++y)for(unsigned x=a[0];x<=b[0];++x){cells[(z*grid+y)*grid+x][r.Type].push_back(r.Index);++entries;}}
        if(fallback){out.SpatialFallback=true;grid=0;}else{data.resize(data.size()+cells.size()*2);for(unsigned i=0;i<cells.size();++i){Vec4 header0{},header1{};for(unsigned type=0;type<3;++type){auto& list=cells[i][type];auto offset=unsigned(data.size());for(std::size_t at=0;at<list.size();at+=4){Vec4 v{-1,-1,-1,-1};for(unsigned k=0;k<4&&at+k<list.size();++k){if(k==0)v.X=float(list[at+k]);else if(k==1)v.Y=float(list[at+k]);else if(k==2)v.Z=float(list[at+k]);else v.W=float(list[at+k]);}data.push_back(v);}if(type==0){header0.X=float(offset);header0.Y=float(list.size());}else if(type==1){header0.Z=float(offset);header0.W=float(list.size());}else{header1.X=float(offset);header1.Y=float(list.size());}}data[gridOffset+i*2]=header0;data[gridOffset+i*2+1]=header1;}}
    }
    data[0]={float(probes.size()),float(charts),float(reflections.size()),float(grid)};data[1]={float(po),float(co),float(ro),float(gridOffset)};data[2]={Finite(minimum)?minimum.X:0,Finite(minimum)?minimum.Y:0,Finite(minimum)?minimum.Z:0,0};data[3]={scale.X,scale.Y,scale.Z,0};
    const auto limit=std::min<unsigned>(4096,textureLimit);if(!limit||data.size()>std::size_t(limit)*limit||data.size()>16777216)throw std::length_error("Baked lighting exceeds the GPU atlas budget");out.Width=std::min<unsigned>(1024,limit);while(std::size_t(out.Width)*limit<data.size())out.Width*=2;out.Height=std::max(1u,unsigned((data.size()+out.Width-1)/out.Width));out.Pixels.resize(std::size_t(out.Width)*out.Height*4,0);for(std::size_t i=0;i<data.size();++i){out.Pixels[i*4]=data[i].X;out.Pixels[i*4+1]=data[i].Y;out.Pixels[i*4+2]=data[i].Z;out.Pixels[i*4+3]=data[i].W;}return out;
}
}
