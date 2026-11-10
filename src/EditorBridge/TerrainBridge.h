#pragma once
#include "Bazzalt/Terrain.h"
#include "Bazzalt/AssetManager.h"
#include <fstream>
#include <set>
#include <map>
#include <cctype>
extern "C" float* stbi_loadf_from_memory(const unsigned char*,int,int*,int*,int*,int);
extern "C" int stbi_info_from_memory(const unsigned char*,int,int*,int*,int*);
extern "C" void stbi_image_free(void*);
extern "C" unsigned char* stbi_load_from_memory(const unsigned char*,int,int*,int*,int*,int);
extern "C" unsigned short* stbi_load_16_from_memory(const unsigned char*,int,int*,int*,int*,int);
extern "C" int stbi_is_hdr_from_memory(const unsigned char*,int);
extern "C" int stbi_is_16_bit_from_memory(const unsigned char*,int);
namespace Bazzalt::EditorBridge {
inline py::dict TerrainLayerInfo(const TerrainLayer& l,unsigned index){py::dict d;d["_Index"]=index;d["Name"]=l.Name;d["Enabled"]=l.Enabled;d["Opacity"]=l.Opacity;d["Blend"]=int(l.Blend);d["Heightmap"]=l.Heightmap.ToString();d["Splatmap"]=l.Splatmap.ToString();d["Material"]=l.Material.ToString();d["Color"]=py::make_tuple(l.Color.X,l.Color.Y,l.Color.Z,l.Color.W);d["Tiling"]=l.Tiling;return d;}
inline py::dict TerrainVegetationInfo(const TerrainVegetation& v,unsigned index){py::dict d;d["_Index"]=index;
#define F(Name) d[#Name]=v.Name;
    F(Name) F(Enabled) F(AlignToNormal) F(Density) F(MinHeight) F(MaxHeight) F(MinSlope) F(MaxSlope) F(MinScale) F(MaxScale) F(Spacing) F(Seed) F(MaxInstances)
#undef F
    d["Model"]=v.Model.ToString();d["Material"]=v.Material.ToString();return d;
}
inline void InspectTerrain(Entity e,py::list& components,py::dict& enabled,py::dict& data){const auto* t=e.TryGetComponent<Terrain>();if(!t)return;components.append("Terrain");enabled["Terrain"]=t->Enabled;py::dict d;d["Size"]=py::make_tuple(t->Size.X,t->Size.Y);
#define F(Name) d[#Name]=t->Name;
    F(VerticalSamples) F(Resolution) F(ChunkSize) F(MaxLOD) F(LODDistance) F(SkirtDepth) F(Visible) F(ShowGrid) F(CastShadows) F(ReceiveShadows)
#undef F
    d["VerticalRange"]=py::make_tuple(t->VerticalRange.X,t->VerticalRange.Y);
    py::list volumes;for(const auto& volume:t->Volumes){py::dict row;row["Name"]=volume.Name;row["Enabled"]=volume.Enabled;row["Operation"]=int(volume.Operation);row["Center"]=py::make_tuple(volume.Center.X,volume.Center.Y,volume.Center.Z);row["Radius"]=py::make_tuple(volume.Radius.X,volume.Radius.Y,volume.Radius.Z);volumes.append(row);}d["Volumes"]=volumes;
    py::list layers,vegetation,splines;for(unsigned i=0;i<t->Layers.size();++i)layers.append(TerrainLayerInfo(t->Layers[i],i));for(unsigned i=0;i<t->Vegetation.size();++i)vegetation.append(TerrainVegetationInfo(t->Vegetation[i],i));
    for(const auto& s:t->Splines){py::dict row;row["Name"]=s.Name;row["Width"]=s.Width;row["Shoulder"]=s.Shoulder;row["Depth"]=s.Depth;row["Strength"]=s.Strength;row["Closed"]=s.Closed;row["GenerateSurface"]=s.GenerateSurface;row["SurfaceCollision"]=s.SurfaceCollision;row["SurfaceOffset"]=s.SurfaceOffset;row["TextureScale"]=s.TextureScale;row["Material"]=s.Material.ToString();row["Color"]=py::make_tuple(s.Color.X,s.Color.Y,s.Color.Z,s.Color.W);py::list points;for(auto p:s.Points)points.append(py::make_tuple(p.X,p.Y,p.Z));row["Points"]=points;py::list incoming,outgoing;
        for(auto tangent:s.InTangents)incoming.append(py::make_tuple(tangent.X,tangent.Y,tangent.Z));
        for(auto tangent:s.OutTangents)outgoing.append(py::make_tuple(tangent.X,tangent.Y,tangent.Z));
        row["InTangents"]=incoming;row["OutTangents"]=outgoing;splines.append(row);}
    d["Layers"]=layers;d["Vegetation"]=vegetation;d["Splines"]=splines;data["Terrain"]=d;
}
inline UUID TerrainAsset(py::handle value,const std::set<std::string>& extensions){auto text=py::cast<std::string>(value);UUID id;if(text.empty()||text=="0")return id;if(!UUID::TryParse(text,id))throw std::invalid_argument("Invalid terrain asset UUID");if(id){auto asset=AssetManager::GetAsset(id);auto extension=asset?asset->SourcePath.extension().string():std::string{};std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});if(!asset||!extensions.contains(extension))throw std::invalid_argument("Wrong asset type for terrain field");}return id;}
template<class T> inline void TerrainRead(py::dict d,const char* name,T& value){if(d.contains(name))value=py::cast<T>(d[name]);}
inline bool SetTerrainProperty(Entity e,const std::string& name,py::handle input){auto* t=e.TryGetComponent<Terrain>();if(!t)return false;auto next=*t;
    try{
        if(name=="Size"){auto v=py::cast<std::array<float,2>>(input);next.Size={v[0],v[1]};}
        else if(name=="Resolution")next.Resize(py::cast<unsigned>(input));
#define F(Name) else if(name==#Name)next.Name=py::cast<decltype(next.Name)>(input);
        F(VerticalSamples) F(ChunkSize) F(MaxLOD) F(LODDistance) F(SkirtDepth) F(Visible) F(ShowGrid) F(CastShadows) F(ReceiveShadows)
#undef F
        else if(name=="VerticalRange"){auto range=py::cast<std::array<float,2>>(input);next.VerticalRange={range[0],range[1]};}
        else if(name=="Volumes"){
            auto rows=py::cast<py::list>(input);if(rows.size()>64)return false;
            next.Volumes.clear();
            for(auto row:rows){auto d=py::cast<py::dict>(row);TerrainVolume volume;
                TerrainRead(d,"Name",volume.Name);TerrainRead(d,"Enabled",volume.Enabled);
                if(d.contains("Operation"))volume.Operation=TerrainVolumeOperation(py::cast<int>(d["Operation"]));
                if(d.contains("Center")){auto v=py::cast<std::array<float,3>>(d["Center"]);volume.Center={v[0],v[1],v[2]};}
                if(d.contains("Radius")){auto v=py::cast<std::array<float,3>>(d["Radius"]);volume.Radius={v[0],v[1],v[2]};}
                next.Volumes.push_back(volume);
            }
        }
        else if(name=="Layers"){auto rows=py::cast<py::list>(input);if(rows.empty()||rows.size()>Terrain::MaxLayers)return false;std::vector<TerrainLayer> result;std::set<unsigned> seen;
            for(auto row:rows){auto d=py::cast<py::dict>(row);TerrainLayer l;if(d.contains("_Index")){auto index=py::cast<unsigned>(d["_Index"]);if(index>=next.Layers.size()||!seen.insert(index).second)return false;l=next.Layers[index];}
#define F(Name) TerrainRead(d,#Name,l.Name);
                F(Name) F(Enabled) F(Opacity) F(Tiling)
#undef F
                if(d.contains("Blend"))l.Blend=TerrainBlend(py::cast<int>(d["Blend"]));if(d.contains("Color")){auto c=py::cast<std::array<float,4>>(d["Color"]);l.Color={c[0],c[1],c[2],c[3]};}
                if(d.contains("Material"))l.Material=TerrainAsset(d["Material"],{".matinst"});if(d.contains("Heightmap"))l.Heightmap=TerrainAsset(d["Heightmap"],{".png",".jpg",".jpeg",".bmp",".tga",".hdr",".exr"});if(d.contains("Splatmap"))l.Splatmap=TerrainAsset(d["Splatmap"],{".png",".jpg",".jpeg",".bmp",".tga"});result.push_back(std::move(l));}next.Layers=std::move(result);
        }else if(name=="Vegetation"){auto rows=py::cast<py::list>(input);if(rows.size()>64)return false;std::vector<TerrainVegetation> result;std::map<unsigned,unsigned> remap;for(auto row:rows){auto d=py::cast<py::dict>(row);TerrainVegetation v;if(d.contains("_Index")){auto previous=py::cast<unsigned>(d["_Index"]);if(previous>=next.Vegetation.size()||remap.contains(previous))return false;remap[previous]=unsigned(result.size());}
#define F(Name) TerrainRead(d,#Name,v.Name);
                F(Name) F(Enabled) F(AlignToNormal) F(Density) F(MinHeight) F(MaxHeight) F(MinSlope) F(MaxSlope) F(MinScale) F(MaxScale) F(Spacing) F(Seed) F(MaxInstances)
#undef F
                if(d.contains("Model"))v.Model=TerrainAsset(d["Model"],{".gltf",".glb",".obj",".fbx",".filamesh"});if(d.contains("Material"))v.Material=TerrainAsset(d["Material"],{".matinst"});result.push_back(v);}next.Vegetation=std::move(result);std::erase_if(next.Instances,[&](const auto& instance){return !remap.contains(instance.Prototype);});for(auto& instance:next.Instances)instance.Prototype=remap.at(instance.Prototype);
        }else if(name=="Splines"){auto rows=py::cast<py::list>(input);if(rows.size()>128)return false;next.Splines.clear();for(auto row:rows){auto d=py::cast<py::dict>(row);TerrainSpline s;
#define F(Name) TerrainRead(d,#Name,s.Name);
                F(Name) F(Width) F(Shoulder) F(Depth) F(Strength) F(Closed) F(GenerateSurface) F(SurfaceCollision) F(SurfaceOffset) F(TextureScale)
#undef F
                if(d.contains("Material"))s.Material=TerrainAsset(d["Material"],{".matinst"});
                if(d.contains("Color")){auto c=py::cast<std::array<float,4>>(d["Color"]);s.Color={c[0],c[1],c[2],c[3]};}
                if(d.contains("Points")){auto points=py::cast<std::vector<std::array<float,3>>>(d["Points"]);if(points.size()>1024)return false;for(auto p:points)s.Points.push_back({p[0],p[1],p[2]});}
                for(auto entry:{std::pair{"InTangents",&s.InTangents},std::pair{"OutTangents",&s.OutTangents}}){
                    if(!d.contains(entry.first))continue;
                    auto values=py::cast<std::vector<std::array<float,3>>>(d[entry.first]);
                    if(values.size()>1024)return false;
                    for(auto value:values)entry.second->push_back({value[0],value[1],value[2]});
                }
                next.Splines.push_back(s);}
        }else return false;
        if(!next.IsValid())return false;next.MarkDirty();*t=std::move(next);return true;
    }catch(const std::exception&){return false;}
}
inline std::tuple<std::vector<float>,unsigned,unsigned> ReadTerrainImage(UUID id){auto asset=AssetManager::GetAsset(id);if(!asset)throw std::invalid_argument("Missing terrain image");std::ifstream file(asset->SourcePath,std::ios::binary|std::ios::ate);if(!file||file.tellg()<=0||file.tellg()>32*1024*1024)throw std::invalid_argument("Terrain image exceeds 32 MB or is unreadable");std::vector<unsigned char> bytes(std::size_t(file.tellg()));file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Could not read terrain image");int width=0,height=0,channels=0;if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels)||width<=0||height<=0||width>4096||height>4096)throw std::invalid_argument("Terrain image must be a supported image, at most 4096 x 4096");std::vector<float> samples(std::size_t(width)*height);
    if(stbi_is_hdr_from_memory(bytes.data(),int(bytes.size()))){auto* pixels=stbi_loadf_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,1);if(!pixels)throw std::runtime_error("Could not decode terrain image");std::copy(pixels,pixels+samples.size(),samples.begin());stbi_image_free(pixels);}
    else if(stbi_is_16_bit_from_memory(bytes.data(),int(bytes.size()))){auto* pixels=stbi_load_16_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,1);if(!pixels)throw std::runtime_error("Could not decode terrain image");for(std::size_t i=0;i<samples.size();++i)samples[i]=float(pixels[i])/65535.f;stbi_image_free(pixels);}
    else {auto* pixels=stbi_load_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,1);if(!pixels)throw std::runtime_error("Could not decode terrain image");for(std::size_t i=0;i<samples.size();++i)samples[i]=float(pixels[i])/255.f;stbi_image_free(pixels);}
    return {std::move(samples),unsigned(width),unsigned(height)};}
inline std::tuple<std::vector<float>,unsigned,unsigned> ReadTerrainStamp(UUID id){
    static UUID cached;static std::filesystem::file_time_type modified{};static std::vector<float> stamp;static unsigned width=0,height=0;
    auto asset=AssetManager::GetAsset(id);if(!asset)throw std::invalid_argument("Missing terrain brush texture");std::error_code error;auto time=std::filesystem::last_write_time(asset->SourcePath,error);if(error)throw std::runtime_error("Cannot inspect terrain brush texture");
    if(cached!=id||time!=modified){auto [samples,w,h]=ReadTerrainImage(id);width=std::min(w,512u);height=std::min(h,512u);stamp.resize(std::size_t(width)*height);for(unsigned z=0;z<height;++z)for(unsigned x=0;x<width;++x)stamp[z*width+x]=std::clamp(samples[(std::size_t(z)*h/height)*w+std::size_t(x)*w/width],0.f,1.f);cached=id;modified=time;}
    return {stamp,width,height};
}
inline py::dict EditTerrainSpline(Entity entity,const std::string& command,py::dict input){
    auto& terrain=entity.GetComponent<Terrain>();
    unsigned index=0;TerrainRead(input,"spline",index);
    if(index>=128)throw std::out_of_range("Terrain spline limit");
    auto world=entity.GetWorldMatrix();Mat4 inverse;
    if(!world.TryInverse(inverse))throw std::invalid_argument("Singular terrain transform");
    auto tuple=[](Vec3 value){return py::make_tuple(value.X,value.Y,value.Z);};
    py::dict result;
    if(command=="spline_probe"){
        auto origin=py::cast<std::array<float,3>>(input["origin"]),direction=py::cast<std::array<float,3>>(input["direction"]);
        auto hit=TerrainGeometry::Raycast(terrain,inverse.TransformPoint({origin[0],origin[1],origin[2]}),inverse.TransformDirection({direction[0],direction[1],direction[2]}));
        if(hit)result["position"]=tuple(world.TransformPoint(hit->Position));
        return result;
    }
    if(command=="spline_info"){
        py::list points,incoming,outgoing,path;
        if(index<terrain.Splines.size()){
            const auto& spline=terrain.Splines[index];
            for(unsigned point=0;point<spline.Points.size();++point){
                auto anchor=spline.Points[point];points.append(tuple(world.TransformPoint(anchor)));
                auto in=spline.InTangents.empty()?Vec3{}:spline.InTangents[point];
                auto out=spline.OutTangents.empty()?Vec3{}:spline.OutTangents[point];
                incoming.append(tuple(world.TransformPoint(anchor+in)));outgoing.append(tuple(world.TransformPoint(anchor+out)));
            }
            const unsigned subdivisions=std::min(16u,std::max(1u,1024u/unsigned(std::max<std::size_t>(1,spline.Points.size()))));
            for(auto sample:TerrainGeometry::SampleSpline(spline,subdivisions))path.append(tuple(world.TransformPoint(sample)));
            result["name"]=spline.Name;result["closed"]=spline.Closed;
        }
        result["points"]=points;result["incoming"]=incoming;result["outgoing"]=outgoing;result["path"]=path;
        result["count"]=terrain.Splines.size();return result;
    }
    if(command=="spline_new"){
        if(terrain.Splines.size()>=128)throw std::out_of_range("Terrain spline limit");
        result["spline"]=terrain.Splines.size();terrain.Splines.emplace_back();terrain.MarkDirty();result["changed"]=true;return result;
    }
    TerrainSpline spline=index<terrain.Splines.size()?terrain.Splines[index]:TerrainSpline{};
    if(command=="spline_add"){
        auto origin=py::cast<std::array<float,3>>(input["origin"]),direction=py::cast<std::array<float,3>>(input["direction"]);
        auto hit=TerrainGeometry::Raycast(terrain,inverse.TransformPoint({origin[0],origin[1],origin[2]}),inverse.TransformDirection({direction[0],direction[1],direction[2]}));
        result["hit"]=bool(hit);
        if(!hit)return result;
        result["position"]=tuple(hit->Position);
        if(spline.Points.size()>=1024)throw std::out_of_range("Terrain spline point limit");
        spline.Points.push_back(hit->Position);
        if(!spline.InTangents.empty())spline.InTangents.push_back({});
        if(!spline.OutTangents.empty())spline.OutTangents.push_back({});
        result["point"]=spline.Points.size()-1;
    }else{
        if(index>=terrain.Splines.size())throw std::out_of_range("Missing terrain spline");
        if(command=="spline_close")spline.Closed=py::cast<bool>(input["closed"]);
        else{
            unsigned point=py::cast<unsigned>(input["point"]);
            if(point>=spline.Points.size())throw std::out_of_range("Missing spline anchor");
            spline.InTangents.resize(spline.Points.size());spline.OutTangents.resize(spline.Points.size());
            if(command=="spline_move"){
                auto position=py::cast<std::array<float,3>>(input["position"]);
                auto local=inverse.TransformPoint({position[0],position[1],position[2]});
                const int part=py::cast<int>(input["part"]);
                if(part==0)spline.Points[point]=local;
                else{
                    if(part!=1&&part!=2)throw std::invalid_argument("Invalid spline handle");
                    auto offset=local-spline.Points[point];
                    auto& selected=part==1?spline.InTangents[point]:spline.OutTangents[point];selected=offset;
                    if(!input.contains("linked")||py::cast<bool>(input["linked"])){
                        auto& opposite=part==1?spline.OutTangents[point]:spline.InTangents[point];opposite=offset*-1;
                    }
                }
            }else if(command=="spline_mode"){
                Vec3 tangent{};
                if(py::cast<bool>(input["curved"])){
                    auto previous=point?point-1:spline.Closed?unsigned(spline.Points.size()-1):point;
                    auto next=point+1<spline.Points.size()?point+1:spline.Closed?0:point;
                    tangent=(spline.Points[next]-spline.Points[previous])*(1.f/6.f);
                    if(tangent.LengthSquared()<Epsilon)tangent={.5f,0,0};
                }
                spline.InTangents[point]=tangent*-1;spline.OutTangents[point]=tangent;
            }else if(command=="spline_remove"){
                spline.Points.erase(spline.Points.begin()+point);spline.InTangents.erase(spline.InTangents.begin()+point);spline.OutTangents.erase(spline.OutTangents.begin()+point);
            }else throw std::invalid_argument("Unknown spline edit");
        }
    }
    Terrain validation;validation.Splines.push_back(spline);
    if(!validation.IsValid())throw std::invalid_argument("Invalid spline edit");
    if(index>=terrain.Splines.size()){
        if(index!=terrain.Splines.size())throw std::out_of_range("Missing terrain spline");
        terrain.Splines.push_back(std::move(spline));
    }else terrain.Splines[index]=std::move(spline);
    terrain.MarkDirty();result["changed"]=true;return result;
}
inline py::dict TerrainCommand(Entity e,const std::string& command,py::dict d){auto* t=e.TryGetComponent<Terrain>();if(!t||!t->Enabled)throw std::invalid_argument("Select an enabled Terrain");py::dict result;
    if(command.starts_with("spline_"))return EditTerrainSpline(e,command=="spline_point"?"spline_add":command,d);
    unsigned layer=0;TerrainRead(d,"layer",layer);
    if(command=="hit"||command=="brush"||command=="vegetation"){
        auto from=py::cast<std::array<float,3>>(d["origin"]),direction=py::cast<std::array<float,3>>(d["direction"]);Mat4 inverse;if(!e.GetWorldMatrix().TryInverse(inverse))throw std::invalid_argument("Singular terrain transform");auto hit=TerrainGeometry::Raycast(*t,inverse.TransformPoint({from[0],from[1],from[2]}),inverse.TransformDirection({direction[0],direction[1],direction[2]}));if(!hit){result["hit"]=false;return result;}result["hit"]=true;result["position"]=py::make_tuple(hit->Position.X,hit->Position.Y,hit->Position.Z);if(command=="hit"){float radius=1;TerrainRead(d,"radius",radius);if(!std::isfinite(radius)||radius<=0)throw std::invalid_argument("Invalid brush radius");py::list lines;auto world=e.GetWorldMatrix();for(unsigned i=0;i<64;++i){float a=float(i)*2*Pi/64,b=float(i+1)*2*Pi/64;Vec3 p{hit->Position.X+std::cos(a)*radius,0,hit->Position.Z+std::sin(a)*radius},q{hit->Position.X+std::cos(b)*radius,0,hit->Position.Z+std::sin(b)*radius};p.Y=t->GetHeight(p.X,p.Z)+.015f;q.Y=t->GetHeight(q.X,q.Z)+.015f;p=world.TransformPoint(p);q=world.TransformPoint(q);lines.append(py::make_tuple(p.X,p.Y,p.Z,q.X,q.Y,q.Z,.35f,.7f,1.f,1.f));}result["guides"]=lines;return result;}
        if(command=="brush"){TerrainBrush b;b.Layer=layer;TerrainRead(d,"radius",b.Radius);TerrainRead(d,"strength",b.Strength);TerrainRead(d,"falloff",b.Falloff);TerrainRead(d,"height",b.TargetHeight);if(d.contains("stamp")){UUID stamp=TerrainAsset(d["stamp"],{".png",".jpg",".jpeg",".bmp",".tga",".hdr"});if(stamp){auto [data,w,h]=ReadTerrainStamp(stamp);b.Stamp=std::move(data);b.StampWidth=w;b.StampHeight=h;for(auto& value:b.Stamp)value=std::clamp(value,0.f,1.f);}}int tool=0;float seconds=.05f;TerrainRead(d,"tool",tool);TerrainRead(d,"seconds",seconds);t->ApplyBrush(TerrainTool(tool),{hit->Position.X,hit->Position.Z},b,seconds);}
        else if(command=="vegetation"){float radius=1;unsigned prototype=0,count=1;bool erase=false;TerrainRead(d,"radius",radius);TerrainRead(d,"prototype",prototype);TerrainRead(d,"count",count);TerrainRead(d,"erase",erase);t->PaintVegetation(prototype,{hit->Position.X,hit->Position.Z},radius,count,erase);}
    }else if(command=="import_height"||command=="import_splat"){if(layer>=t->Layers.size())throw std::out_of_range("Terrain layer");auto id=command=="import_height"?t->Layers[layer].Heightmap:t->Layers[layer].Splatmap;auto [data,w,h]=ReadTerrainImage(id);if(command=="import_height"){float scale=10;TerrainRead(d,"scale",scale);t->ImportHeightmap(layer,data,w,h,scale);}else t->ImportSplatmap(layer,data,w,h);}
    else if(command=="generate"){unsigned prototype=0;TerrainRead(d,"prototype",prototype);t->GenerateVegetation(prototype);}
    else if(command=="match_edge"){
        UUID source;
        if(!UUID::TryParse(py::cast<std::string>(d["source"]),source))throw std::invalid_argument("Invalid terrain neighbour");
        auto neighbour=e.GetScene()->GetEntity(source);
        if(!neighbour||!neighbour.HasComponent<Terrain>())throw std::invalid_argument("Missing terrain neighbour");
        Mat4 inverse;
        if(!e.GetWorldMatrix().TryInverse(inverse))throw std::invalid_argument("Singular terrain transform");
        unsigned edge=0;TerrainRead(d,"edge",edge);
        t->MatchEdge(neighbour.GetComponent<Terrain>(),TerrainEdge(edge),inverse*neighbour.GetWorldMatrix(),layer);
    }
    else if(command=="spline"){unsigned index=0;TerrainRead(d,"spline",index);t->ApplySpline(index,layer);}
    else if(command=="boolean"){UUID id;if(!UUID::TryParse(py::cast<std::string>(d["source"]),id))throw std::invalid_argument("Invalid boolean source");auto other=e.GetScene()->GetEntity(id);if(!other||!other.HasComponent<Terrain>())throw std::invalid_argument("Boolean source needs Terrain in the same scene");Mat4 inverse;if(!e.GetWorldMatrix().TryInverse(inverse))throw std::invalid_argument("Singular terrain transform");t->ApplyBoolean(other.GetComponent<Terrain>(),inverse*other.GetWorldMatrix(),TerrainBoolean(py::cast<int>(d["operation"])),layer);}
    else throw std::invalid_argument("Unknown terrain command");result["changed"]=true;return result;
}
}
