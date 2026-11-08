#include "Rendering/LightingFields.h"
#include "Serialization/AdvancedLODSerialization.h"
#include "Bazzalt/Serialization.h"
#include "Rendering/AdvancedLODFields.h"
#include "VFX/ParticleFields.h"
#include "Animation/AnimationFields.h"
#include "AI/BehaviorFields.h"
#include "Runtime/StreamingFields.h"
#include "Navigation/NavigationFields.h"
#include <sstream>
#include <iomanip>
namespace Bazzalt::Runtime {
namespace {
template<class T> void Encode(PropertyMap& p,const std::string& name,const T& v){
 if constexpr(std::is_same_v<T,std::vector<Vec3>>)p[name]=EncodeLightingVectors(v);
 else if constexpr(std::is_same_v<T,std::string>)p[name]=v;
 else if constexpr(std::is_same_v<T,UUID>)p[name]=v.ToString();
 else if constexpr(std::is_same_v<T,std::vector<UUID>>){p[name+".Count"]=std::to_string(v.size());for(std::size_t i=0;i<v.size();++i)p[name+"."+std::to_string(i)]=v[i].ToString();}
 else if constexpr(std::is_same_v<T,Vec2>){Encode(p,name+".X",v.X);Encode(p,name+".Y",v.Y);}
 else if constexpr(std::is_same_v<T,Vec3>){Encode(p,name+".X",v.X);Encode(p,name+".Y",v.Y);Encode(p,name+".Z",v.Z);}
 else if constexpr(std::is_same_v<T,Vec4>){Encode(p,name+".X",v.X);Encode(p,name+".Y",v.Y);Encode(p,name+".Z",v.Z);Encode(p,name+".W",v.W);}
 else if constexpr(std::is_enum_v<T>)Encode(p,name,int(v));
 else if constexpr(std::is_same_v<T,bool>)p[name]=v?"true":"false";
 else {std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9)<<+v;p[name]=out.str();}
}
template<class T> bool Decode(const PropertyMap& p,const std::string& name,T& v){
 if constexpr(std::is_same_v<T,std::vector<Vec3>>){auto it=p.find(name);return it!=p.end()&&DecodeLightingVectors(it->second,v);}
 else if constexpr(std::is_same_v<T,std::vector<UUID>>){unsigned count;if(!Decode(p,name+".Count",count)||count>256)return false;v.resize(count);for(unsigned i=0;i<count;++i)if(!Decode(p,name+"."+std::to_string(i),v[i]))return false;return true;}
 else if constexpr(std::is_same_v<T,Vec2>)return Decode(p,name+".X",v.X)&&Decode(p,name+".Y",v.Y);
 else if constexpr(std::is_same_v<T,Vec3>)return Decode(p,name+".X",v.X)&&Decode(p,name+".Y",v.Y)&&Decode(p,name+".Z",v.Z);
 else if constexpr(std::is_same_v<T,Vec4>)return Decode(p,name+".X",v.X)&&Decode(p,name+".Y",v.Y)&&Decode(p,name+".Z",v.Z)&&Decode(p,name+".W",v.W);
 else {auto it=p.find(name);if(it==p.end())return false;
  if constexpr(std::is_same_v<T,std::string>){if(it->second.size()>256)return false;v=it->second;return true;}
  else if constexpr(std::is_same_v<T,UUID>)return UUID::TryParse(it->second,v);
  else if constexpr(std::is_same_v<T,bool>){if(it->second!="true"&&it->second!="false")return false;v=it->second=="true";return true;}
  else if constexpr(std::is_enum_v<T>){int value;if(!Decode(p,name,value))return false;v=static_cast<T>(value);return true;}
  else {std::istringstream in(it->second);in.imbue(std::locale::classic());if constexpr(std::is_same_v<T,std::uint8_t>){unsigned value;if(!(in>>value)||value>255)return false;v=std::uint8_t(value);}else if(!(in>>v))return false;return in.peek()==std::char_traits<char>::eof();}
 }
}
}
void RegisterAdvancedLODComponents(ComponentSerializationRegistry& registry){
#define REGISTER(Type,Visit) registry.Register<Type>("Bazzalt." #Type,1,[](const Type& value,PropertyMap& p){Visit(value,[&](const char* name,const auto& field){Encode(p,name,field);});},[](Type& output,const PropertyMap& p,std::uint32_t version){if(version!=1)return false;Type value;bool valid=true;Visit(value,[&](const char* name,auto& field){if constexpr(std::is_same_v<Type,ParticleEmitter>){if(std::string(name)=="Backend"&&!p.contains(name))return;}valid&=Decode(p,name,field);});if(!valid||!value.IsValid())return false;output=std::move(value);return true;});
 REGISTER(BehaviorTreeAgent,VisitBehaviorTreeAgent) REGISTER(NavMeshSurface,VisitNavMeshSurface) REGISTER(NavigationAgent,VisitNavigationAgent) REGISTER(NavigationObstacle,VisitNavigationObstacle) REGISTER(NavMeshModifier,VisitNavMeshModifier) REGISTER(TimelineDirector,VisitTimelineDirector) REGISTER(Lightmap,VisitLightmap) REGISTER(ReflectionProbe,VisitReflectionProbe) REGISTER(LightProbe,VisitLightProbe) REGISTER(WorldCell,VisitWorldCell) REGISTER(StreamingSource,VisitStreamingSource) REGISTER(IKConstraint,VisitIKConstraint) REGISTER(Animator,VisitAnimator) REGISTER(HLOD,VisitHLOD) REGISTER(LODTransition,VisitLODTransition) REGISTER(Impostor,VisitImpostor) REGISTER(ParticleEmitter,VisitParticleEmitter)
#undef REGISTER
 registry.RegisterEntityReference("Bazzalt.NavigationAgent","Surface");
 registry.RegisterEntityReference("Bazzalt.IKConstraint","AnimatorEntity");registry.RegisterEntityReference("Bazzalt.IKConstraint","Target");registry.RegisterEntityReference("Bazzalt.IKConstraint","PoleTarget");
}
}
