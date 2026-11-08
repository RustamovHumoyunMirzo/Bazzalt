#include "Script/LuaBindings.h"
#include "Bazzalt/Terrain.h"
namespace Bazzalt::Runtime {
void BindLuaTerrain(sol::table api){
    api.new_enum("TerrainBlend","Add",TerrainBlend::Add,"Replace",TerrainBlend::Replace,"Maximum",TerrainBlend::Maximum,"Minimum",TerrainBlend::Minimum);
    api.new_enum("TerrainTool","Raise",TerrainTool::Raise,"Lower",TerrainTool::Lower,"Smooth",TerrainTool::Smooth,"Flatten",TerrainTool::Flatten,"Paint",TerrainTool::Paint,"ErasePaint",TerrainTool::ErasePaint,"Hole",TerrainTool::Hole,"FillHole",TerrainTool::FillHole);
    api.new_enum("TerrainBoolean","Union",TerrainBoolean::Union,"Intersection",TerrainBoolean::Intersection,"Subtract",TerrainBoolean::Subtract);
    auto layer=api.new_usertype<TerrainLayer>("TerrainLayer",sol::constructors<TerrainLayer()>());
#define FIELD(Type,Value,Name) Value[#Name]=&Type::Name;
    FIELD(TerrainLayer,layer,Name) FIELD(TerrainLayer,layer,Enabled) FIELD(TerrainLayer,layer,Opacity) FIELD(TerrainLayer,layer,Blend) FIELD(TerrainLayer,layer,Heightmap) FIELD(TerrainLayer,layer,Splatmap) FIELD(TerrainLayer,layer,Material) FIELD(TerrainLayer,layer,Color) FIELD(TerrainLayer,layer,Tiling) FIELD(TerrainLayer,layer,Heights) FIELD(TerrainLayer,layer,Weights)
    auto brush=api.new_usertype<TerrainBrush>("TerrainBrush",sol::constructors<TerrainBrush()>());
    FIELD(TerrainBrush,brush,Radius) FIELD(TerrainBrush,brush,Strength) FIELD(TerrainBrush,brush,Falloff) FIELD(TerrainBrush,brush,TargetHeight) FIELD(TerrainBrush,brush,Layer) FIELD(TerrainBrush,brush,StampWidth) FIELD(TerrainBrush,brush,StampHeight) FIELD(TerrainBrush,brush,Stamp)
    auto vegetation=api.new_usertype<TerrainVegetation>("TerrainVegetation",sol::constructors<TerrainVegetation()>());
    FIELD(TerrainVegetation,vegetation,Name) FIELD(TerrainVegetation,vegetation,Model) FIELD(TerrainVegetation,vegetation,Material) FIELD(TerrainVegetation,vegetation,Enabled) FIELD(TerrainVegetation,vegetation,AlignToNormal) FIELD(TerrainVegetation,vegetation,Density) FIELD(TerrainVegetation,vegetation,MinHeight) FIELD(TerrainVegetation,vegetation,MaxHeight) FIELD(TerrainVegetation,vegetation,MinSlope) FIELD(TerrainVegetation,vegetation,MaxSlope) FIELD(TerrainVegetation,vegetation,MinScale) FIELD(TerrainVegetation,vegetation,MaxScale) FIELD(TerrainVegetation,vegetation,Spacing) FIELD(TerrainVegetation,vegetation,Seed) FIELD(TerrainVegetation,vegetation,MaxInstances)
    auto instance=api.new_usertype<TerrainInstance>("TerrainInstance",sol::constructors<TerrainInstance()>());
    FIELD(TerrainInstance,instance,Prototype) FIELD(TerrainInstance,instance,Position) FIELD(TerrainInstance,instance,Rotation) FIELD(TerrainInstance,instance,Scale)
    auto spline=api.new_usertype<TerrainSpline>("TerrainSpline",sol::constructors<TerrainSpline()>());
    FIELD(TerrainSpline,spline,Name) FIELD(TerrainSpline,spline,Points) FIELD(TerrainSpline,spline,Width) FIELD(TerrainSpline,spline,Shoulder) FIELD(TerrainSpline,spline,Depth) FIELD(TerrainSpline,spline,Strength) FIELD(TerrainSpline,spline,Closed)
    auto terrain=api.new_usertype<Terrain>("Terrain",sol::constructors<Terrain()>(),sol::base_classes,sol::bases<Component>());
    FIELD(Terrain,terrain,Size) FIELD(Terrain,terrain,Resolution) FIELD(Terrain,terrain,ChunkSize) FIELD(Terrain,terrain,MaxLOD) FIELD(Terrain,terrain,LODDistance) FIELD(Terrain,terrain,SkirtDepth) FIELD(Terrain,terrain,Visible) FIELD(Terrain,terrain,CastShadows) FIELD(Terrain,terrain,ReceiveShadows) FIELD(Terrain,terrain,ShowGrid) FIELD(Terrain,terrain,LayerMask) FIELD(Terrain,terrain,Layers) FIELD(Terrain,terrain,Holes) FIELD(Terrain,terrain,Vegetation) FIELD(Terrain,terrain,Instances) FIELD(Terrain,terrain,Splines)
#undef FIELD
    terrain["IsValid"]=&Terrain::IsValid;terrain["MarkDirty"]=&Terrain::MarkDirty;terrain["Resize"]=&Terrain::Resize;
    terrain["AddLayer"]=[](Terrain& t,sol::optional<std::string> name){return t.AddLayer(name.value_or("Layer"));};terrain["RemoveLayer"]=&Terrain::RemoveLayer;
    terrain["GetHeight"]=&Terrain::GetHeight;terrain["GetNormal"]=&Terrain::GetNormal;terrain["GetSlope"]=&Terrain::GetSlope;terrain["IsHole"]=&Terrain::IsHole;
    terrain["ApplyBrush"]=[](Terrain& t,TerrainTool tool,Vec2 center,const TerrainBrush& brush,sol::optional<float> seconds){t.ApplyBrush(tool,center,brush,seconds.value_or(1));};
    terrain["ImportHeightmap"]=[](Terrain& t,unsigned layer,sol::as_table_t<std::vector<float>> values,unsigned width,unsigned height,sol::optional<float> scale){t.ImportHeightmap(layer,values.value(),width,height,scale.value_or(1));};
    terrain["ImportSplatmap"]=[](Terrain& t,unsigned layer,sol::as_table_t<std::vector<float>> values,unsigned width,unsigned height){t.ImportSplatmap(layer,values.value(),width,height);};
    terrain["ApplySpline"]=[](Terrain& t,unsigned spline,sol::optional<unsigned> layer){t.ApplySpline(spline,layer.value_or(0));};
    terrain["ApplyBoolean"]=[](Terrain& t,const Terrain& other,const Mat4& transform,TerrainBoolean operation,sol::optional<unsigned> layer){t.ApplyBoolean(other,transform,operation,layer.value_or(0));};
    terrain["GenerateVegetation"]=&Terrain::GenerateVegetation;
    terrain["PaintVegetation"]=[](Terrain& t,unsigned prototype,Vec2 center,float radius,unsigned count,sol::optional<bool> erase){t.PaintVegetation(prototype,center,radius,count,erase.value_or(false));};
    auto hit=api.new_usertype<TerrainHit>("TerrainHit",sol::no_constructor);hit["Position"]=&TerrainHit::Position;hit["Normal"]=&TerrainHit::Normal;hit["Distance"]=&TerrainHit::Distance;
    auto vertex=api.new_usertype<TerrainVertex>("TerrainVertex",sol::no_constructor);vertex["Position"]=&TerrainVertex::Position;vertex["Normal"]=&TerrainVertex::Normal;vertex["UV"]=&TerrainVertex::UV;vertex["Color"]=&TerrainVertex::Color;
    auto mesh=api.new_usertype<TerrainMesh>("TerrainMesh",sol::no_constructor);mesh["Vertices"]=&TerrainMesh::Vertices;mesh["Indices"]=&TerrainMesh::Indices;
    auto geometry=api.create_named("TerrainGeometry");geometry["BuildChunk"]=[](const Terrain& t,unsigned x,unsigned z,sol::optional<unsigned> lod){return TerrainGeometry::BuildChunk(t,x,z,lod.value_or(0));};geometry["Raycast"]=[](const Terrain& t,Vec3 origin,Vec3 direction,sol::optional<float> distance){return TerrainGeometry::Raycast(t,origin,direction,distance.value_or(100000));};
    sol::table operations=api["_ComponentOperations"];auto op=operations.create_named("Terrain");
    op["Has"]=[](Entity e){return e.HasComponent<Terrain>();};op["Get"]=[](Entity e){return Terrain(e.GetComponent<Terrain>());};op["Add"]=[](Entity e){return Terrain(e.AddComponent<Terrain>());};
    op["Set"]=[](Entity e,Terrain value){if(!value.IsValid())throw std::invalid_argument("Invalid terrain");value.Revision=e.GetComponent<Terrain>().Revision;value.MarkDirty();e.GetComponent<Terrain>()=std::move(value);};op["Remove"]=[](Entity e){e.RemoveComponent<Terrain>();};
    op["Inherited"]=[](Entity e)->std::optional<Terrain>{if(!e)return {};auto* p=e.GetScene()->TryGetInheritedComponent<Terrain>(e);return p?std::optional<Terrain>(*p):std::nullopt;};op["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<Terrain>();};op["SetEnabled"]=[](Entity e,bool enabled){e.SetComponentEnabled<Terrain>(enabled);};
}
}
