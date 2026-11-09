#include "Terrain/TerrainSerialization.h"
#include "Bazzalt/Terrain.h"
#include "Bazzalt/Serialization.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Bazzalt::Runtime {
namespace {
struct Writer {
    PropertyMap& Values;
    template<class T> void OptionalField(const std::string& key,const T& value){Field(key,value);}
    template<class T> void Field(const std::string& key,const T& value){
        std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
        if constexpr(std::is_same_v<T,UUID>)out<<value.ToString();
        else if constexpr(std::is_same_v<T,Vec2>)out<<value.X<<' '<<value.Y;
        else if constexpr(std::is_same_v<T,Vec3>)out<<value.X<<' '<<value.Y<<' '<<value.Z;
        else if constexpr(std::is_same_v<T,Vec4>)out<<value.X<<' '<<value.Y<<' '<<value.Z<<' '<<value.W;
        else if constexpr(std::is_same_v<T,Quaternion>)out<<value.X<<' '<<value.Y<<' '<<value.Z<<' '<<value.W;
        else if constexpr(std::is_same_v<T,std::vector<float>>||std::is_same_v<T,std::vector<std::uint8_t>>){out<<value.size();for(auto v:value)out<<' '<<+v;}
        else if constexpr(std::is_same_v<T,std::string>)out<<value;
        else if constexpr(std::is_enum_v<T>)out<<int(value);
        else out<<+value;
        Values[key]=out.str();
    }
};
struct Reader {
    const PropertyMap& Values;bool Valid=true;std::size_t RemainingSamples=Terrain::MaxSampleValues;
    template<class T> void OptionalField(const std::string& key,T& value){if(Values.contains(key))Field(key,value);}
    template<class T> void Field(const std::string& key,T& value){auto it=Values.find(key);if(it==Values.end()){Valid=false;return;}std::istringstream in(it->second);in.imbue(std::locale::classic());
        if constexpr(std::is_same_v<T,UUID>){Valid&=UUID::TryParse(it->second,value);return;}
        else if constexpr(std::is_same_v<T,std::string>){if(it->second.size()>128){Valid=false;return;}value=it->second;return;}
        else if constexpr(std::is_same_v<T,Vec2>)in>>value.X>>value.Y;
        else if constexpr(std::is_same_v<T,Vec3>)in>>value.X>>value.Y>>value.Z;
        else if constexpr(std::is_same_v<T,Vec4>||std::is_same_v<T,Quaternion>)in>>value.X>>value.Y>>value.Z>>value.W;
        else if constexpr(std::is_same_v<T,std::vector<float>>||std::is_same_v<T,std::vector<std::uint8_t>>){std::size_t count=0;if(!(in>>count)||count>std::size_t(Terrain::MaxResolution)*Terrain::MaxResolution||count>RemainingSamples){Valid=false;return;}RemainingSamples-=count;value.resize(count);for(auto& v:value){if constexpr(std::is_same_v<T,std::vector<std::uint8_t>>){unsigned n;if(!(in>>n)||n>1){Valid=false;return;}v=std::uint8_t(n);}else in>>v;}}
        else if constexpr(std::is_enum_v<T>){int n=0;in>>n;value=static_cast<T>(n);}
        else if constexpr(std::is_same_v<T,std::uint8_t>){unsigned n=0;in>>n;if(n>255)Valid=false;value=std::uint8_t(n);}
        else in>>value;
        if(in.fail()){Valid=false;return;}in>>std::ws;Valid&=in.eof();
    }
};
template<class A,class T> void Archive(A& a,T& t){
#define F(Name) a.Field(#Name,t.Name);
    F(Size) F(Resolution) F(ChunkSize) F(MaxLOD) F(LODDistance) F(SkirtDepth) F(Visible) F(CastShadows) F(ReceiveShadows) F(ShowGrid) F(LayerMask) F(Holes)
#undef F
    a.OptionalField("VerticalRange",t.VerticalRange);
    a.OptionalField("VerticalSamples",t.VerticalSamples);
    auto list=[&](const char* name,auto& rows,unsigned limit,auto fields){unsigned count=unsigned(rows.size());a.Field(std::string(name)+".Count",count);if(count>limit)throw std::invalid_argument("Terrain serialized list limit");if constexpr(!std::is_const_v<T>)rows.resize(count);for(unsigned i=0;i<count;++i)fields(std::string(name)+"."+std::to_string(i)+".",rows[i]);};
    unsigned volumeCount=unsigned(t.Volumes.size());
    a.OptionalField("Volumes.Count",volumeCount);
    if(volumeCount>64)throw std::invalid_argument("Terrain volume limit");
    if constexpr(!std::is_const_v<T>)t.Volumes.resize(volumeCount);
    for(unsigned i=0;i<volumeCount;++i){
        auto prefix="Volumes."+std::to_string(i)+".";
        auto& volume=t.Volumes[i];
        a.Field(prefix+"Name",volume.Name);a.Field(prefix+"Enabled",volume.Enabled);
        a.Field(prefix+"Operation",volume.Operation);a.Field(prefix+"Center",volume.Center);a.Field(prefix+"Radius",volume.Radius);
    }
    list("Layers",t.Layers,Terrain::MaxLayers,[&](auto p,auto& row){
#define F(Name) a.Field(p+#Name,row.Name);
        F(Name) F(Enabled) F(Opacity) F(Blend) F(Heightmap) F(Splatmap) F(Material) F(Color) F(Tiling) F(Heights) F(Weights)
#undef F
    });
    list("Vegetation",t.Vegetation,64,[&](auto p,auto& row){
#define F(Name) a.Field(p+#Name,row.Name);
        F(Name) F(Model) F(Material) F(Enabled) F(AlignToNormal) F(Density) F(MinHeight) F(MaxHeight) F(MinSlope) F(MaxSlope) F(MinScale) F(MaxScale) F(Spacing) F(Seed) F(MaxInstances)
#undef F
    });
    list("Instances",t.Instances,Terrain::MaxInstances,[&](auto p,auto& row){
#define F(Name) a.Field(p+#Name,row.Name);
        F(Prototype) F(Position) F(Rotation) F(Scale)
#undef F
    });
    list("Splines",t.Splines,128,[&](auto p,auto& row){
#define F(Name) a.Field(p+#Name,row.Name);
        F(Name) F(Width) F(Shoulder) F(Depth) F(Strength) F(Closed)
        a.OptionalField(p+"GenerateSurface",row.GenerateSurface);
        a.OptionalField(p+"SurfaceCollision",row.SurfaceCollision);
        a.OptionalField(p+"SurfaceOffset",row.SurfaceOffset);
        a.OptionalField(p+"TextureScale",row.TextureScale);
        a.OptionalField(p+"Material",row.Material);
        a.OptionalField(p+"Color",row.Color);
#undef F
        unsigned count=unsigned(row.Points.size());a.Field(p+"Points.Count",count);if(count>1024)throw std::invalid_argument("Terrain spline point limit");if constexpr(!std::is_const_v<T>)row.Points.resize(count);for(unsigned i=0;i<count;++i)a.Field(p+"Points."+std::to_string(i),row.Points[i]);
    });
}
}
void RegisterTerrainComponents(ComponentSerializationRegistry& registry){registry.Register<Terrain>("Bazzalt.Terrain",1,[](const Terrain& value,PropertyMap& fields){if(!value.IsValid())throw std::invalid_argument("Invalid terrain serialization");Writer writer{fields};Archive(writer,value);},[](Terrain& output,const PropertyMap& fields,std::uint32_t version){if(version!=1)return false;try{Terrain next;Reader reader{fields};Archive(reader,next);if(!reader.Valid||!next.IsValid())return false;next.MarkDirty();output=std::move(next);return true;}catch(const std::exception&){return false;}});}
}
