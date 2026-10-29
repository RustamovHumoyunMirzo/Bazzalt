#include "Physics/PhysicsSerialization.h"
#include "Physics/PhysicsFields.h"
#include "Bazzalt/Components/LOD.h"
#include "Utils/NumberParsing.h"
#include <sstream>
#include <iomanip>
#include <limits>
#include <string_view>
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
    REGISTER(Rigidbody,VisitBody) REGISTER(ShapeCollider,VisitShapeCollider) REGISTER(MeshCollider,VisitMeshCollider) REGISTER(Joint,VisitJoint) REGISTER(DistanceJoint,VisitJoint) REGISTER(HingeJoint,VisitJoint) REGISTER(SliderJoint,VisitJoint) REGISTER(BallAndSocketJoint,VisitJoint) REGISTER(FixedJoint,VisitJoint) REGISTER(Rope,VisitRope)
    REGISTER(Vehicle,VisitVehicle) REGISTER(VehicleWheel,VisitVehicleWheel) REGISTER(WheelSuspension,VisitWheelSuspension) REGISTER(WheelTire,VisitWheelTire) REGISTER(WheelBrake,VisitWheelBrake) REGISTER(WheelSteering,VisitWheelSteering) REGISTER(VehicleEngine,VisitVehicleEngine) REGISTER(VehicleTransmission,VisitVehicleTransmission) REGISTER(VehicleDifferential,VisitVehicleDifferential) REGISTER(VehicleAntiRollBar,VisitVehicleAntiRollBar) REGISTER(VehicleAerodynamics,VisitVehicleAerodynamics)
    registry.Register<LOD>("Bazzalt.LOD",1,[](const LOD& v,PropertyMap& p){
        p["Center"]=Encode(v.Center);p["Size"]=Encode(v.Size);p["Bias"]=Encode(v.Bias);p["Hysteresis"]=Encode(v.Hysteresis);p["Cull"]=Encode(v.Cull);p["ForcedLevel"]=Encode(v.ForcedLevel);p["Count"]=Encode(v.Levels.size());
        for(std::size_t i=0;i<v.Levels.size();++i){auto prefix="Level."+std::to_string(i)+".";p[prefix+"ScreenHeight"]=Encode(v.Levels[i].ScreenHeight);p[prefix+"Count"]=Encode(v.Levels[i].Roots.size());for(std::size_t j=0;j<v.Levels[i].Roots.size();++j)p[prefix+"Root."+std::to_string(j)]=Encode(v.Levels[i].Roots[j]);}
    },[](LOD& v,const PropertyMap& p,std::uint32_t version){
        if(version!=1)return false;const auto read=[&](const std::string& key,auto& output){auto it=p.find(key);return it!=p.end()&&Decode(it->second,output);};unsigned count=0;
        if(!read("Center",v.Center)||!read("Size",v.Size)||!read("Bias",v.Bias)||!read("Hysteresis",v.Hysteresis)||!read("Cull",v.Cull)||!read("ForcedLevel",v.ForcedLevel)||!read("Count",count)||count<1||count>8)return false;
        v.Levels.resize(count);for(unsigned i=0;i<count;++i){auto prefix="Level."+std::to_string(i)+".";unsigned roots=0;if(!read(prefix+"ScreenHeight",v.Levels[i].ScreenHeight)||!read(prefix+"Count",roots)||roots>256)return false;v.Levels[i].Roots.resize(roots);for(unsigned j=0;j<roots;++j)if(!read(prefix+"Root."+std::to_string(j),v.Levels[i].Roots[j]))return false;}
        return v.IsValid();
    });
    registry.Register<Cloth>("Bazzalt.Cloth",2,[](const Cloth& value,PropertyMap& p){VisitCloth(value,[&](const char* name,const char*,const auto& field){p[name]=Encode(field);});},[](Cloth& output,const PropertyMap& p,std::uint32_t version){
        if(version!=1&&version!=2)return false;Cloth value;bool valid=true;VisitCloth(value,[&](const char* name,const char*,auto& field){auto it=p.find(name);if(it==p.end()){const std::string_view key=name;if(version!=1||(key!="BendMode"&&key!="SubdivisionLevel"&&key!="Pressure"&&key!="CollisionRadius"))valid=false;}else if(!Decode(it->second,field))valid=false;});
        if(version==1&&!p.contains("BendMode")){
            if(value.Compliance==0&&value.ShearCompliance==0&&value.BendCompliance==.0001f){const Cloth defaults;value.Compliance=defaults.Compliance;value.ShearCompliance=defaults.ShearCompliance;value.BendCompliance=defaults.BendCompliance;}
            else value.BendMode=ClothBendMode::Dihedral;
        }
        if(!valid||!value.IsValid())return false;output=value;return true;
    });
#undef REGISTER
}
}
