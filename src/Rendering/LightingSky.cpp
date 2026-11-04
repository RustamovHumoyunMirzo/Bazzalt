#include "Rendering/LightingSky.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Material.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>
namespace Bazzalt::Runtime {
namespace {
float Half(std::uint16_t h){unsigned e=(h>>10)&31,m=h&1023;if(e==31)return INFINITY;float value=e?std::ldexp(1.f+m/1024.f,int(e)-15):std::ldexp(float(m),-24);return h&32768?-value:value;}
float UnsignedFloat(unsigned value,unsigned bits){unsigned m=value&((1u<<bits)-1),e=value>>bits;if(e==31)return INFINITY;return e?std::ldexp(1.f+float(m)/(1u<<bits),int(e)-15):std::ldexp(float(m),1-15-int(bits));}
}
ReflectionProbe ReadLightingSky(const SceneEnvironment& environment){
    ReflectionProbe sky;if(!environment.ImageBasedLighting||environment.Intensity<=0)return sky;UUID source=environment.SourceAsset;
    if(environment.Mode==SceneEnvironmentMode::Material){source={};auto m=Material::Load(environment.MaterialAsset);if(m.IsValid())for(auto& p:m.GetParameters())if(p.Type==ShaderParameterType::Texture2D){auto id=m.GetTexture(p.Name);auto asset=AssetManager::GetAsset(id);if(asset){auto ext=asset->SourcePath.extension();if(ext==".hdr"||ext==".exr"||ext==".ktx"){source=id;break;}}}}
    if(!source)return sky;auto asset=AssetManager::GetAsset(source);if(!asset||asset->State!=AssetState::Ready)throw std::runtime_error("Import the environment before baking reflections");auto path=asset->CachePath;
    if(path.extension()!=".ktx"){auto folder=path;folder+=".environment";auto name=folder.stem();name+="_skybox.ktx";path=folder/name;}
    std::ifstream file(path,std::ios::binary|std::ios::ate);auto size=file.tellg();if(size<64||size>256*1024*1024)throw std::runtime_error("Missing or oversized cached environment cubemap");std::vector<unsigned char> bytes(std::size_t(size),0);file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Cannot read environment cubemap");
    return DecodeLightingSky(bytes,environment.Intensity);
}
ReflectionProbe DecodeLightingSky(const std::vector<std::uint8_t>& bytes,float intensity){
    if(bytes.size()<64||bytes.size()>256u*1024u*1024u||!std::isfinite(intensity)||intensity<0)throw std::runtime_error("Invalid environment cubemap data");ReflectionProbe sky;
    const unsigned char magic[]={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};if(!std::equal(std::begin(magic),std::end(magic),bytes.begin()))throw std::runtime_error("Unsupported environment cache container");
    const auto word=[&](std::size_t at){if(at+4>bytes.size())throw std::runtime_error("Truncated environment cubemap");return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);};
    auto type=word(16),format=word(24),internal=word(28),width=word(36),height=word(40);if(word(12)!=0x04030201||!width||width!=height||width>16384||word(52)!=6||word(48)>1||word(44)>1)throw std::runtime_error("Invalid environment cubemap dimensions");unsigned channels=format==0x1907?3:format==0x1908?4:0,unit=type==0x1406?4:type==0x140b?2:type==0x1401?1:0;bool packed=type==0x8c3b||type==0x8c3e;if(!channels||(!unit&&!packed)||word(20)!=(packed?4:unit)||(width&(width-1)))throw std::runtime_error("Unsupported HDR cubemap pixel format");auto stride=packed?4:channels*unit;std::size_t row=(std::size_t(width)*stride+3)&~std::size_t(3),at=64u+std::size_t(word(60));auto faceBytes=word(at);at+=4;if(faceBytes<row*height||at+6ull*((faceBytes+3ull)&~3ull)>bytes.size())throw std::runtime_error("Truncated environment cubemap faces");
    sky.Resolution=sky.BakedResolution=std::max(4u,std::min(256u,width));std::size_t total=0;for(auto r=sky.Resolution;r;r/=2)total+=6u*r*r;sky.Radiance.resize(total);sky.Baked=true;
    for(unsigned face=0;face<6;++face)for(unsigned y=0;y<sky.Resolution;++y)for(unsigned x=0;x<sky.Resolution;++x){auto ox=std::min(width-1,unsigned((x+.5f)*width/sky.Resolution)),oy=std::min(height-1,unsigned((y+.5f)*height/sky.Resolution));auto pixel=at+face*((faceBytes+3ull)&~3ull)+oy*row+ox*stride;Vec3 color{};
        if(packed){auto value=word(pixel);if(type==0x8c3b)color={UnsignedFloat(value&2047,6),UnsignedFloat((value>>11)&2047,6),UnsignedFloat(value>>22,5)};else{float scale=std::ldexp(1.f,int(value>>27)-24);color={float(value&511)*scale,float((value>>9)&511)*scale,float((value>>18)&511)*scale};}}
        else{float values[3];for(unsigned c=0;c<3;++c){auto offset=pixel+c*unit;values[c]=unit==4?std::bit_cast<float>(word(offset)):unit==2?Half(std::uint16_t(bytes[offset]|(unsigned(bytes[offset+1])<<8))):bytes[offset]/255.f;}color={values[0],values[1],values[2]};}
        const auto convert=[&](float v){if(!std::isfinite(v))throw std::runtime_error("Non-finite environment radiance");v=std::max(0.f,v);if(internal==0x8c41||internal==0x8c43)v=v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);v*=intensity;if(!std::isfinite(v))throw std::runtime_error("Environment intensity overflows HDR radiance");return v;};sky.Radiance[(face*sky.Resolution+y)*sky.Resolution+x]={convert(color.X),convert(color.Y),convert(color.Z)};
    }return sky;
}
}
