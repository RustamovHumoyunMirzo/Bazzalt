#include "Rendering/ModelGeometry.h"
#include "Assets/GltfDocument.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <ryml.hpp>
#include <sstream>
#include <locale>
#include <map>

namespace Bazzalt::Runtime {
namespace {
using Ref=ryml::ConstNodeRef;
std::size_t Number(Ref node,std::size_t fallback=0) {
    if(!node.valid()||!node.has_val())return fallback;
    const auto text=node.val();std::size_t value=0;
    auto result=std::from_chars(text.str,text.str+text.len,value);
    return result.ec==std::errc{}&&result.ptr==text.str+text.len?value:fallback;
}
std::size_t Field(Ref node,const char* name,std::size_t fallback=0){return node.has_child(name)?Number(node[name],fallback):fallback;}
bool Finite(Vec3 p){return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z);}
float Axis(Vec3 v,int axis){return axis==0?v.X:axis==1?v.Y:v.Z;}
Vec3 Minimum(Vec3 a,Vec3 b){return {std::min(a.X,b.X),std::min(a.Y,b.Y),std::min(a.Z,b.Z)};}
Vec3 Maximum(Vec3 a,Vec3 b){return {std::max(a.X,b.X),std::max(a.Y,b.Y),std::max(a.Z,b.Z)};}
bool Box(Vec3 o,Vec3 d,Vec3 minimum,Vec3 maximum,float limit){
    float near=0,far=limit;
    for(int axis=0;axis<3;++axis){float p=Axis(o,axis),v=Axis(d,axis),a=Axis(minimum,axis),b=Axis(maximum,axis);
        if(std::abs(v)<1e-12f){if(p<a||p>b)return false;continue;}
        float x=(a-p)/v,y=(b-p)/v;if(x>y)std::swap(x,y);near=std::max(near,x);far=std::min(far,y);if(near>far)return false;
    }return true;
}
}

std::shared_ptr<ModelGeometryAsset> ReadModelGeometry(const std::vector<std::uint8_t>& bytes,const std::filesystem::path& source){
    auto result=std::make_shared<ModelGeometryAsset>();
    if(bytes.empty())return result;
    GltfDocument doc(bytes,source);auto root=doc.Tree.crootref();
    if(!root.has_child("nodes")||!root.has_child("meshes"))return result;
    std::vector<std::shared_ptr<ModelGeometry>> meshes;
    for(auto mesh:root["meshes"].children()){
        auto geometry=std::make_shared<ModelGeometry>();
        std::uint32_t slot=0;
        if(mesh.has_child("primitives"))for(auto primitive:mesh["primitives"].children()){
            const auto primitiveSlot=slot++;
            if(!primitive.has_child("attributes")||!primitive["attributes"].has_child("POSITION"))continue;
            auto attributes=primitive["attributes"];
            auto read=[&](Ref n,unsigned dimensions){return doc.Accessor(unsigned(Number(n)),dimensions);};
            auto positions=read(attributes["POSITION"],3);unsigned vertexCount=unsigned(positions.size()/3);
            auto optional=[&](const char* name,unsigned dimensions){auto out=attributes.has_child(name)?read(attributes[name],dimensions):std::vector<float>{};if(!out.empty()&&out.size()!=vertexCount*dimensions)throw std::invalid_argument("Mesh attribute count mismatch");return out;};
            auto uv=optional("TEXCOORD_0",2),normals=optional("NORMAL",3),joints=optional("JOINTS_0",4),weights=optional("WEIGHTS_0",4),joints1=optional("JOINTS_1",4),weights1=optional("WEIGHTS_1",4);
            if(joints.empty()!=weights.empty()||joints1.empty()!=weights1.empty())throw std::invalid_argument("Incomplete skin attributes");
            std::vector<std::vector<float>> morphs;if(primitive.has_child("targets"))for(auto target:primitive["targets"].children()){auto values=target.has_child("POSITION")?read(target["POSITION"],3):std::vector<float>(positions.size());if(values.size()!=positions.size())throw std::invalid_argument("Morph count mismatch");morphs.push_back(std::move(values));}if(morphs.size()>256)throw std::invalid_argument("Morph target budget exceeded");if(geometry->MorphPositions.size()<morphs.size())geometry->MorphPositions.resize(morphs.size(),std::vector<std::array<Vec3,3>>(geometry->Triangles.size()));
            std::vector<float> indices;if(primitive.has_child("indices"))indices=read(primitive["indices"],1);
            auto count=indices.empty()?vertexCount:indices.size();auto mode=Field(primitive,"mode",4);if(mode<4||mode>6)continue;
            auto vertex=[&](std::size_t i){float value=indices.empty()?float(i):indices[i];if(value<0||value>=vertexCount||std::floor(value)!=value)throw std::invalid_argument("Mesh index out of range");return unsigned(value);};
            auto position=[&](const std::vector<float>& stream,unsigned i){return Vec3{stream[i*3],stream[i*3+1],stream[i*3+2]};};
            auto texcoord=[&](unsigned i){return uv.empty()?Vec2{}:Vec2{uv[i*2],uv[i*2+1]};};
            for(std::size_t i=mode==4?0:2;mode==4?i+2<count:i<count; i+=mode==4?3:1){
                std::uint32_t a,b,c;
                if(mode==4){a=vertex(i);b=vertex(i+1);c=vertex(i+2);}
                else if(mode==5){a=vertex(i-2);b=vertex(i-1);c=vertex(i);if(i%2)std::swap(a,b);}
                else{a=vertex(0);b=vertex(i-1);c=vertex(i);}

                auto va=position(positions,a),vb=position(positions,b),vc=position(positions,c);
                if(Finite(va)&&Finite(vb)&&Finite(vc)&&Vec3::Cross(vb-va,vc-va).LengthSquared()>1e-18f){auto n=Vec3::Cross(vb-va,vc-va).Normalized();const auto normal=[&](unsigned index){auto v=!normals.empty()?position(normals,index):n;return Finite(v)&&v.LengthSquared()>1e-12f?v.Normalized():n;};geometry->Triangles.push_back({va,vb,vc,normal(a),normal(b),normal(c),texcoord(a),texcoord(b),texcoord(c),primitiveSlot});
                std::array<ModelGeometry::SkinVertex,3> skin{};unsigned vertices[]={a,b,c};for(unsigned v=0;v<3;++v){float total=0;for(unsigned j=0;j<8;++j){auto& js=j<4?joints:joints1;auto& ws=j<4?weights:weights1;if(js.empty())continue;float joint=js[vertices[v]*4+j%4],weight=ws[vertices[v]*4+j%4];if(joint<0||joint>65535||std::floor(joint)!=joint||weight<0)throw std::invalid_argument("Invalid skin influence");skin[v].Joints[j]=unsigned(joint);skin[v].Weights[j]=weight;total+=weight;}if(total>0)for(auto& w:skin[v].Weights)w/=total;}if(!joints.empty()||!geometry->Skin.empty()){if(geometry->Skin.empty())geometry->Skin.resize(geometry->Triangles.size()-1);geometry->Skin.push_back(skin);}
                for(unsigned t=0;t<geometry->MorphPositions.size();++t)geometry->MorphPositions[t].push_back(t<morphs.size()?std::array<Vec3,3>{position(morphs[t],a),position(morphs[t],b),position(morphs[t],c)}:std::array<Vec3,3>{});}
            }
        }
        geometry->Build();meshes.push_back(std::move(geometry));
    }
    if(root.has_child("skins"))for(auto sourceSkin:root["skins"].children()){ModelGeometryAsset::Skin skin;if(!sourceSkin.has_child("joints"))throw std::invalid_argument("Missing skin joints");for(auto joint:sourceSkin["joints"].children()){auto index=Number(joint);if(index>=root["nodes"].num_children())throw std::invalid_argument("Skin joint out of range");skin.Joints.push_back(unsigned(index));}if(skin.Joints.empty()||skin.Joints.size()>4096)throw std::invalid_argument("Invalid skin size");skin.InverseBind.resize(skin.Joints.size());if(sourceSkin.has_child("inverseBindMatrices")){auto matrices=doc.Accessor(unsigned(Number(sourceSkin["inverseBindMatrices"])),16);if(matrices.size()!=skin.Joints.size()*16)throw std::invalid_argument("Invalid inverse bind count");for(unsigned i=0;i<skin.Joints.size();++i)for(unsigned j=0;j<16;++j)skin.InverseBind[i](j%4,j/4)=matrices[i*16+j];}result->Skins.push_back(std::move(skin));}
    for(auto node:root["nodes"].children()){auto mesh=Field(node,"mesh",meshes.size());result->SourceNodes.push_back(mesh<meshes.size()?meshes[mesh]:nullptr);int skin=node.has_child("skin")?int(Number(node["skin"])):-1;if(skin>=int(result->Skins.size()))throw std::invalid_argument("Missing skin");result->NodeSkins.push_back(skin);std::vector<float> weights;if(mesh<meshes.size()){weights.resize(meshes[mesh]->MorphPositions.size());auto m=root["meshes"][mesh];auto values=node.has_child("weights")?node["weights"]:m.has_child("weights")?m["weights"]:Ref{};if(!values.invalid()){if(values.num_children()!=weights.size())throw std::invalid_argument("Invalid default morph weights");for(unsigned j=0;j<weights.size();++j){auto text=GltfDocument::Text(values[j]);std::istringstream in(text);in.imbue(std::locale::classic());if(!(in>>weights[j])||!std::isfinite(weights[j]))throw std::invalid_argument("Invalid morph weight");}}}result->NodeWeights.push_back(std::move(weights));}
    return result;
}

void ModelGeometry::Build(){
    Influences.clear();std::map<unsigned,InfluenceBounds> clusters;if(!Skin.empty()||!MorphPositions.empty())for(unsigned i=0;i<Triangles.size();++i){Vec3 vertices[]={Triangles[i].A,Triangles[i].B,Triangles[i].C};for(unsigned v=0;v<3;++v){std::vector<unsigned> joints;if(i<Skin.size())for(unsigned j=0;j<8;++j)if(Skin[i][v].Weights[j]>0)joints.push_back(Skin[i][v].Joints[j]);if(joints.empty())joints.push_back(~0u);for(auto joint:joints){auto [it,inserted]=clusters.try_emplace(joint);auto& cluster=it->second;if(inserted){cluster.Joint=joint;cluster.Min=cluster.Max=vertices[v];cluster.Morph.resize(MorphPositions.size());for(unsigned t=0;t<MorphPositions.size();++t)cluster.Morph[t]={MorphPositions[t][i][v],MorphPositions[t][i][v]};}else{cluster.Min=Minimum(cluster.Min,vertices[v]);cluster.Max=Maximum(cluster.Max,vertices[v]);for(unsigned t=0;t<MorphPositions.size();++t){cluster.Morph[t].first=Minimum(cluster.Morph[t].first,MorphPositions[t][i][v]);cluster.Morph[t].second=Maximum(cluster.Morph[t].second,MorphPositions[t][i][v]);}}}}}for(auto& [_,cluster]:clusters)Influences.push_back(std::move(cluster));
    Nodes.clear();Order.resize(Triangles.size());std::iota(Order.begin(),Order.end(),0u);
    if(Order.empty())return;
    const auto build=[&](auto&& self,std::uint32_t begin,std::uint32_t count)->std::uint32_t{
        const auto index=static_cast<std::uint32_t>(Nodes.size());Nodes.emplace_back();
        Vec3 minimum{std::numeric_limits<float>::max()},maximum{-std::numeric_limits<float>::max()};
        for(std::size_t i=begin;i<begin+count;++i){auto& t=Triangles[Order[i]];minimum=Minimum(minimum,Minimum(t.A,Minimum(t.B,t.C)));maximum=Maximum(maximum,Maximum(t.A,Maximum(t.B,t.C)));}
        Nodes[index]={minimum,maximum,begin,count,0,0};if(count<=8)return index;
        auto extent=maximum-minimum;int axis=extent.X>extent.Y?0:1;if(extent.Z>Axis(extent,axis))axis=2;
        auto middle=begin+count/2;
        std::nth_element(Order.begin()+begin,Order.begin()+middle,Order.begin()+begin+count,[&](auto a,auto b){auto& x=Triangles[a];auto& y=Triangles[b];return Axis(x.A+x.B+x.C,axis)<Axis(y.A+y.B+y.C,axis);});
        auto left=self(self,begin,middle-begin),right=self(self,middle,begin+count-middle);Nodes[index].Count=0;Nodes[index].Left=left;Nodes[index].Right=right;return index;
    };build(build,0,static_cast<std::uint32_t>(Order.size()));
}

std::pair<Vec3,Vec3> ModelGeometry::DeformedBounds(const std::vector<Mat4>& palette,const std::vector<float>& weights)const{Vec3 minimum{std::numeric_limits<float>::max()},maximum{-std::numeric_limits<float>::max()};for(auto& cluster:Influences){auto low=cluster.Min,high=cluster.Max;for(unsigned t=0;t<std::min(weights.size(),cluster.Morph.size());++t){float w=weights[t];low+=(w>=0?cluster.Morph[t].first:cluster.Morph[t].second)*w;high+=(w>=0?cluster.Morph[t].second:cluster.Morph[t].first)*w;}auto matrix=cluster.Joint==~0u||palette.empty()?Mat4{}:palette.at(cluster.Joint);for(unsigned mask=0;mask<8;++mask){auto p=matrix.TransformPoint({mask&1?high.X:low.X,mask&2?high.Y:low.Y,mask&4?high.Z:low.Z});minimum=Minimum(minimum,p);maximum=Maximum(maximum,p);}}if(Influences.empty())return Nodes.empty()?std::pair<Vec3,Vec3>{}:std::pair<Vec3,Vec3>{Nodes[0].Min,Nodes[0].Max};return {minimum,maximum};}

void ModelGeometry::Refit(){for(auto it=Nodes.rbegin();it!=Nodes.rend();++it){if(it->Count){it->Min=Vec3{std::numeric_limits<float>::max()};it->Max=Vec3{-std::numeric_limits<float>::max()};for(unsigned i=it->Begin;i<it->Begin+it->Count;++i){auto& t=Triangles[Order[i]];it->Min=Minimum(it->Min,Minimum(t.A,Minimum(t.B,t.C)));it->Max=Maximum(it->Max,Maximum(t.A,Maximum(t.B,t.C)));}}else{it->Min=Minimum(Nodes[it->Left].Min,Nodes[it->Right].Min);it->Max=Maximum(Nodes[it->Left].Max,Nodes[it->Right].Max);}}}
void ModelGeometry::Deform(const ModelGeometry& source,const std::vector<Mat4>& palette,const std::vector<float>& weights){if(Triangles.size()!=source.Triangles.size()){Triangles=source.Triangles;Order=source.Order;Nodes=source.Nodes;}for(unsigned i=0;i<Triangles.size();++i){Triangles[i]=source.Triangles[i];Vec3* vertices[]={&Triangles[i].A,&Triangles[i].B,&Triangles[i].C};for(unsigned v=0;v<3;++v){auto point=*vertices[v];for(unsigned t=0;t<std::min(weights.size(),source.MorphPositions.size());++t)point+=source.MorphPositions[t][i][v]*weights[t];if(!palette.empty()&&i<source.Skin.size()){auto& skin=source.Skin[i][v];Vec3 sum{};float total=0;for(unsigned j=0;j<8;++j)if(skin.Weights[j]>0){if(skin.Joints[j]>=palette.size())throw std::invalid_argument("Skin influence out of range");sum+=palette[skin.Joints[j]].TransformPoint(point)*skin.Weights[j];total+=skin.Weights[j];}if(total>0)point=sum/total;}*vertices[v]=point;}auto n=Vec3::Cross(Triangles[i].B-Triangles[i].A,Triangles[i].C-Triangles[i].A).Normalized();Triangles[i].NA=Triangles[i].NB=Triangles[i].NC=n;}Refit();}

bool ModelGeometry::Raycast(Vec3 origin,Vec3 direction,float& distance,float determinantEpsilon) const{
    if(Nodes.empty()||!Finite(origin)||!Finite(direction))return false;bool hit=false;
    std::array<std::uint32_t,64> stack{};std::size_t pending=1;
    while(pending){auto index=stack[--pending];const auto& node=Nodes[index];if(!Box(origin,direction,node.Min,node.Max,distance))continue;
        if(!node.Count){if(pending+2>stack.size())throw std::runtime_error("Invalid geometry BVH depth");stack[pending++]=node.Left;stack[pending++]=node.Right;continue;}
        for(std::size_t i=node.Begin;i<node.Begin+node.Count;++i){const auto& t=Triangles[Order[i]];auto e1=t.B-t.A,e2=t.C-t.A,p=Vec3::Cross(direction,e2);float determinant=Vec3::Dot(e1,p);if(std::abs(determinant)<determinantEpsilon)continue;
            float reciprocal=1/determinant;auto offset=origin-t.A;float u=Vec3::Dot(offset,p)*reciprocal;if(u<0||u>1)continue;auto q=Vec3::Cross(offset,e1);float v=Vec3::Dot(direction,q)*reciprocal;if(v<0||u+v>1)continue;float d=Vec3::Dot(e2,q)*reciprocal;if(d>=0&&d<distance){distance=d;hit=true;}}
    }return hit;
}
}
