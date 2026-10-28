#include "Physics/PhysicsSerialization.h"
#include "Physics/PhysicsFields.h"
#include "Utils/NumberParsing.h"
#include <sstream>
#include <iomanip>
#include <limits>
namespace Bazzalt::Runtime {
namespace {
template<class T> std::string Encode(const T& value){
    if constexpr(std::is_same_v<T,bool>)return value?"true":"false";
    else if constexpr(std::is_same_v<T,UUID>)return value.ToString();
    else if constexpr(std::is_enum_v<T>)return std::to_string(int(value));
    else if constexpr(std::is_arithmetic_v<T>){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9)<<value;return out.str();}
    else if constexpr(std::is_same_v<T,std::optional<Vec3>>)return value?Encode(*value):"null";
    else if constexpr(std::is_same_v<T,Vec3>)return Encode(value.X)+","+Encode(value.Y)+","+Encode(value.Z);
    else if constexpr(std::is_same_v<T,Quaternion>)return Encode(value.X)+","+Encode(value.Y)+","+Encode(value.Z)+","+Encode(value.W);
    else {std::string text;for(const auto& v:value){if(!text.empty())text+=',';text+=Encode(v);}return text;}
}
template<class T> bool Decode(const std::string& text,T& value){
    if constexpr(std::is_same_v<T,bool>){if(text!="true"&&text!="false")return false;value=text=="true";return true;}
    else if constexpr(std::is_same_v<T,UUID>)return UUID::TryParse(text,value);
    else if constexpr(std::is_enum_v<T>){int number;if(!Detail::ParseNumber(text,number)||number<0||number>255)return false;value=T(number);return true;}
    else if constexpr(std::is_arithmetic_v<T>)return Detail::ParseNumber(text,value);
    else if constexpr(std::is_same_v<T,std::optional<Vec3>>){if(text=="null"){value.reset();return true;}Vec3 v;if(!Decode(text,v))return false;value=v;return true;}
    else {
        std::vector<std::string> parts;std::size_t begin=0;while(true){auto end=text.find(',',begin);parts.push_back(text.substr(begin,end-begin));if(end==std::string::npos)break;begin=end+1;}
        if constexpr(std::is_same_v<T,Vec3>){return parts.size()==3&&Decode(parts[0],value.X)&&Decode(parts[1],value.Y)&&Decode(parts[2],value.Z);}
        else if constexpr(std::is_same_v<T,Quaternion>){return parts.size()==4&&Decode(parts[0],value.X)&&Decode(parts[1],value.Y)&&Decode(parts[2],value.Z)&&Decode(parts[3],value.W);}
        else {if(parts.size()!=value.size())return false;for(std::size_t i=0;i<parts.size();++i)if(!Decode(parts[i],value[i]))return false;return true;}
    }
}
}
PropertyMap WritePhysicsSettings(const PhysicsSettings& value){PropertyMap p;p["Version"]="1";VisitPhysicsSettings(value,[&](const char* name,const char*,const auto& field){p[name]=Encode(field);});return p;}
bool ReadPhysicsSettings(PhysicsSettings& output,const PropertyMap& p){if(!p.contains("Version")||p.at("Version")!="1")return false;PhysicsSettings value;bool valid=true;VisitPhysicsSettings(value,[&](const char* name,const char*,auto& field){auto it=p.find(name);if(it==p.end()||!Decode(it->second,field))valid=false;});if(!valid||!value.IsValid())return false;output=value;return true;}
void RegisterPhysicsComponents(ComponentSerializationRegistry& registry){
#define REGISTER(Type,Visit) registry.Register<Type>("Bazzalt." #Type,1,[](const Type& value,PropertyMap& p){Visit(value,[&](const char* name,const char*,const auto& field){p[name]=Encode(field);});},[](Type& output,const PropertyMap& p,std::uint32_t version){if(version!=1)return false;Type value;bool valid=true;Visit(value,[&](const char* name,const char*,auto& field){auto it=p.find(name);if(it==p.end()||!Decode(it->second,field))valid=false;});if(!valid||!value.IsValid())return false;output=value;return true;});
    REGISTER(Rigidbody,VisitBody) REGISTER(ShapeCollider,VisitShapeCollider) REGISTER(MeshCollider,VisitMeshCollider)
#undef REGISTER
}
}
