#include "Rendering/LightingFields.h"
#include "Bazzalt/Lighting.h"
#include "Bazzalt/Scene.h"
#include <atomic>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace Bazzalt {
namespace {
bool Finite(Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);}
bool Vectors(const std::vector<Vec3>& values){return std::all_of(values.begin(),values.end(),Finite);}
std::size_t CubeTexels(unsigned size){std::size_t n=0;while(size){n+=6u*size*size;size/=2;}return n;}
}
bool Lightmap::IsValid()const{
    if(Resolution<16||Resolution>4096||(Resolution&(Resolution-1))||Padding>16||Padding*2+2>Resolution)return false;
    if(!Baked)return Geometry.empty()&&Texels.empty();
    return BakedResolution>=16&&BakedResolution<=4096&&!(BakedResolution&(BakedResolution-1))&&BakedPadding<=16&&ChartGrid>0&&ChartGrid<=BakedResolution&&BakedResolution/ChartGrid>=BakedPadding*2+2&&Geometry.size()%3==0&&!Geometry.empty()&&Geometry.size()/3<=std::size_t(ChartGrid)*ChartGrid&&Texels.size()==std::size_t(BakedResolution)*BakedResolution&&TransformSignature.size()==16&&Vectors(Geometry)&&Vectors(Texels);
}
bool ReflectionProbe::IsValid()const{
    if(Resolution<4||Resolution>256||(Resolution&(Resolution-1))||!std::isfinite(Radius)||Radius<=0||!std::isfinite(Intensity)||Intensity<0||!std::isfinite(BlendDistance)||BlendDistance<0||!Finite(BoxSize)||BoxSize.X<=0||BoxSize.Y<=0||BoxSize.Z<=0||!Finite(CapturePosition))return false;
    if(!Baked)return Radiance.empty();
    return BakedResolution>=4&&BakedResolution<=256&&!(BakedResolution&(BakedResolution-1))&&Radiance.size()==CubeTexels(BakedResolution)&&Vectors(Radiance);
}
namespace Runtime {
namespace {struct Revision {std::uint64_t Value;};std::atomic<std::uint64_t> NextRevision{1};}
std::uint64_t LightingRevision(Scene& scene){auto& ctx=scene.GetRegistry().ctx();if(!ctx.contains<Revision>())ctx.emplace<Revision>(NextRevision.fetch_add(1));return ctx.get<Revision>().Value;}
Vec3 LightingCubeDirection(unsigned face,float u,float v){switch(face){case 0:return Vec3{1,-v,-u}.Normalized();case 1:return Vec3{-1,-v,u}.Normalized();case 2:return Vec3{u,1,v}.Normalized();case 3:return Vec3{u,-1,-v}.Normalized();case 4:return Vec3{u,-v,1}.Normalized();default:return Vec3{-u,-v,-1}.Normalized();}}
std::string LightingTransformSignature(const Mat4& world){std::uint64_t hash=1469598103934665603ull;for(int r=0;r<4;++r)for(int c=0;c<4;++c){auto bits=std::bit_cast<std::uint32_t>(world(r,c));for(int k=0;k<4;++k){hash^=(bits>>(k*8))&255;hash*=1099511628211ull;}}std::string result(16,'0');for(int k=15;k>=0;--k){result[k]="0123456789abcdef"[hash&15];hash>>=4;}return result;}
std::string EncodeLightingVectors(const std::vector<Vec3>& values){
    if(values.size()>16777216||!Vectors(values))throw std::invalid_argument("Invalid baked lighting vectors");
    static constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;result.reserve(values.size()*16);unsigned buffer=0,bits=0;
    const auto byte=[&](unsigned b){buffer=(buffer<<8)|(b&255);bits+=8;while(bits>=6){bits-=6;result.push_back(chars[(buffer>>bits)&63]);}};
    for(auto v:values)for(float f:{v.X,v.Y,v.Z}){auto word=std::bit_cast<std::uint32_t>(f);for(int k=0;k<4;++k)byte(word>>(k*8));}
    if(bits)result.push_back(chars[(buffer<<(6-bits))&63]);while(result.size()%4)result.push_back('=');return result;
}
bool DecodeLightingVectors(const std::string& text,std::vector<Vec3>& values){
    // Twelve bytes per vector means this format never needs padding.
    if(text.size()>268435456||text.size()%16)return false;std::vector<Vec3> output;output.reserve(text.size()/16);unsigned buffer=0,bits=0,at=0;std::uint32_t word=0;Vec3 v{};
    for(unsigned char c:text){int n=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;if(n<0)return false;buffer=(buffer<<6)|unsigned(n);bits+=6;if(bits>=8){bits-=8;word|=((buffer>>bits)&255)<<(8*(at%4));++at;if(at%4==0){float f=std::bit_cast<float>(word);word=0;if(!std::isfinite(f))return false;switch((at/4-1)%3){case 0:v.X=f;break;case 1:v.Y=f;break;default:v.Z=f;output.push_back(v);break;}}}}
    if(at%12||bits)return false;values=std::move(output);return true;
}
std::optional<std::pair<Vec3,float>> SampleLightmapIrradiance(const Lightmap& m,Vec3 position,Vec3 normal){
    if(!m.Baked||!m.IsValid())return {};float best=INFINITY;Vec3 value{};const auto tile=m.BakedResolution/m.ChartGrid;const auto bilinear=[&](float x,float y){x=std::clamp(x,0.f,float(m.BakedResolution-1));y=std::clamp(y,0.f,float(m.BakedResolution-1));auto ix=unsigned(x),iy=unsigned(y);float u=x-ix,v=y-iy;const auto texel=[&](unsigned a,unsigned b){return m.Texels[std::min(b,m.BakedResolution-1)*m.BakedResolution+std::min(a,m.BakedResolution-1)];};return (texel(ix,iy)*(1-u)+texel(ix+1,iy)*u)*(1-v)+(texel(ix,iy+1)*(1-u)+texel(ix+1,iy+1)*u)*v;};
    for(unsigned i=0;i<m.Geometry.size()/3;++i){auto a=m.Geometry[i*3],ab=m.Geometry[i*3+1]-a,ac=m.Geometry[i*3+2]-a,n=Vec3::Cross(ab,ac).Normalized(),delta=position-a;float epsilon=std::max(.0001f,std::max(ab.Length(),ac.Length())*.00001f),plane=std::abs(Vec3::Dot(delta,n)),alignment=std::abs(Vec3::Dot(normal,n));if(plane>epsilon||alignment<.1f)continue;float aa=Vec3::Dot(ab,ab),bb=Vec3::Dot(ac,ac),cross=Vec3::Dot(ab,ac),denom=aa*bb-cross*cross;if(denom<1e-16f)continue;float da=Vec3::Dot(delta,ab),db=Vec3::Dot(delta,ac),u=(bb*da-cross*db)/denom,v=(aa*db-cross*da)/denom;if(u<-.001f||v<-.001f||u+v>1.001f)continue;float score=plane+epsilon*(1-alignment);if(score<best){best=score;float inner=float(tile-2*m.BakedPadding-1);value=bilinear((i%m.ChartGrid)*tile+m.BakedPadding+u*inner,(i/m.ChartGrid)*tile+m.BakedPadding+v*inner);}}
    if(!std::isfinite(best))return {};return std::pair<Vec3,float>{value,best};
}
Vec3 ReflectionDirection(const ReflectionProbe& p,Vec3 position,Vec3 direction){
    if(!p.BoxProjection)return direction;auto local=position-p.CapturePosition;if(std::abs(local.X)>p.BoxSize.X||std::abs(local.Y)>p.BoxSize.Y||std::abs(local.Z)>p.BoxSize.Z)return direction;float distance=std::numeric_limits<float>::infinity();
    for(int axis=0;axis<3;++axis){float d=axis==0?direction.X:axis==1?direction.Y:direction.Z;if(std::abs(d)<1e-8f)continue;float o=axis==0?local.X:axis==1?local.Y:local.Z,e=axis==0?p.BoxSize.X:axis==1?p.BoxSize.Y:p.BoxSize.Z;distance=std::min(distance,((d>0?e:-e)-o)/d);}
    return std::isfinite(distance)?local+direction*distance:direction;
}
Vec3 SampleReflectionRadiance(const ReflectionProbe& p,Vec3 direction,float roughness){
    if(!p.Baked||p.BakedResolution<4||p.Radiance.size()!=CubeTexels(p.BakedResolution)||!Finite(direction)||direction.LengthSquared()<1e-12f)return {};
    auto n=direction.Normalized();float u=0,v=0;unsigned face=0;const auto a=Vec3{std::abs(n.X),std::abs(n.Y),std::abs(n.Z)};
    if(a.X>=a.Y&&a.X>=a.Z){face=n.X>=0?0:1;u=(n.X>=0?-n.Z:n.Z)/a.X;v=-n.Y/a.X;}
    else if(a.Y>=a.Z){face=n.Y>=0?2:3;u=n.X/a.Y;v=(n.Y>=0?n.Z:-n.Z)/a.Y;}
    else{face=n.Z>=0?4:5;u=(n.Z>=0?n.X:-n.X)/a.Z;v=-n.Y/a.Z;}
    unsigned levels=0;for(unsigned r=p.BakedResolution;r>1;r/=2)++levels;float lod=std::clamp(roughness,0.f,1.f)*levels;unsigned lower=unsigned(lod),upper=std::min(lower+1,levels);
    const auto sample=[&](unsigned mip){unsigned size=p.BakedResolution;std::size_t offset=0;for(unsigned i=0;i<mip;++i){offset+=6u*size*size;size/=2;}float x=std::clamp((u*.5f+.5f)*size-.5f,0.f,float(size-1)),y=std::clamp((v*.5f+.5f)*size-.5f,0.f,float(size-1));auto ix=unsigned(x),iy=unsigned(y);float fx=x-ix,fy=y-iy;auto texel=[&](unsigned a,unsigned b){return p.Radiance[offset+face*size*size+std::min(b,size-1)*size+std::min(a,size-1)];};return (texel(ix,iy)*(1-fx)+texel(ix+1,iy)*fx)*(1-fy)+(texel(ix,iy+1)*(1-fx)+texel(ix+1,iy+1)*fx)*fy;};
    return sample(lower)*(1-(lod-lower))+sample(upper)*(lod-lower);
}
}
void Lighting::Invalidate(Scene& scene){Runtime::LightingRevision(scene);auto& revision=scene.GetRegistry().ctx().get<Runtime::Revision>();revision.Value=Runtime::NextRevision.fetch_add(1);}

}
