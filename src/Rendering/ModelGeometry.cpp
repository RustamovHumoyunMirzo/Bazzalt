#include "Rendering/ModelGeometry.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <ryml.hpp>

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
std::vector<std::uint8_t> Decode(ryml::csubstr uri) {
    std::string_view text(uri.str,uri.len);auto comma=text.find(',');
    if(comma==text.npos||!text.substr(0,comma).ends_with(";base64"))return {};
    std::vector<std::uint8_t> result;std::uint32_t value=0;int bits=0;
    for(char c:text.substr(comma+1)) {
        if(c=='=')break;
        const int n=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;
        if(n<0)return {};value=(value<<6)|n;bits+=6;
        if(bits>=8){bits-=8;result.push_back(static_cast<std::uint8_t>(value>>bits));}
    }return result;
}
struct Accessor {
    const std::vector<std::uint8_t>* Buffer=nullptr;
    std::size_t Offset=0,Stride=0,Count=0,Element=0;
    std::uint32_t Type=0;
};
Accessor ReadAccessor(Ref root,std::size_t index,const std::vector<std::vector<std::uint8_t>>& buffers,bool positions) {
    Accessor result;
    if(!root.has_child("accessors")||index>=root["accessors"].num_children()||!root.has_child("bufferViews"))return result;
    auto accessor=root["accessors"][index];if(!accessor.has_child("bufferView")||accessor.has_child("sparse"))return result;
    auto vi=Field(accessor,"bufferView");if(vi>=root["bufferViews"].num_children())return result;
    auto view=root["bufferViews"][vi];auto bi=Field(view,"buffer");if(bi>=buffers.size())return result;
    auto type=Field(accessor,"componentType");auto elements=positions?3u:1u;
    if(positions && (type!=5126||!accessor.has_child("type")||accessor["type"].val()!=ryml::csubstr("VEC3")))return result;
    if(!positions && (type!=5121&&type!=5123&&type!=5125))return result;
    const auto element=(type==5121?1u:type==5123?2u:4u)*elements;
    const auto offset=Field(accessor,"byteOffset"),viewOffset=Field(view,"byteOffset"),viewSize=Field(view,"byteLength");
    const auto stride=Field(view,"byteStride",element),count=Field(accessor,"count");
    if(stride<element||!count||offset>viewSize||element>viewSize-offset||
        count-1>(viewSize-offset-element)/stride||viewOffset>buffers[bi].size()||viewSize>buffers[bi].size()-viewOffset)return result;
    result={&buffers[bi],viewOffset+offset,stride,count,element,static_cast<std::uint32_t>(type)};return result;
}
Vec3 Position(const Accessor& a,std::size_t index){Vec3 value;std::memcpy(&value,a.Buffer->data()+a.Offset+index*a.Stride,12);return value;}
std::uint32_t Index(const Accessor& a,std::size_t index){std::uint32_t value=0;std::memcpy(&value,a.Buffer->data()+a.Offset+index*a.Stride,a.Element);return value;}
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

std::shared_ptr<ModelGeometryAsset> ReadModelGeometry(const std::vector<std::uint8_t>& bytes){
    auto result=std::make_shared<ModelGeometryAsset>();
    if(bytes.empty())return result;
    std::size_t start=0,length=bytes.size();std::vector<std::uint8_t> binary;
    std::uint32_t magic=0;if(bytes.size()>=4)std::memcpy(&magic,bytes.data(),4);
    if(magic==0x46546c67){
        if(bytes.size()<20)return result;std::uint32_t size=0;std::memcpy(&size,bytes.data()+12,4);
        if(size>bytes.size()-20)return result;start=20;length=size;
        auto tail=start+length;
        if(tail+8<=bytes.size()){std::uint32_t binSize=0,type=0;std::memcpy(&binSize,bytes.data()+tail,4);std::memcpy(&type,bytes.data()+tail+4,4);
            if(type==0x004e4942&&binSize<=bytes.size()-tail-8)binary.assign(bytes.begin()+tail+8,bytes.begin()+tail+8+binSize);}
    }
    auto tree=ryml::parse_in_arena(ryml::csubstr(reinterpret_cast<const char*>(bytes.data()+start),length));auto root=tree.crootref();
    if(!root.has_child("nodes")||!root.has_child("buffers")||!root.has_child("meshes"))return result;
    std::vector<std::vector<std::uint8_t>> buffers;
    for(auto buffer:root["buffers"].children())buffers.push_back(buffer.has_child("uri")?Decode(buffer["uri"].val()):binary);
    std::vector<std::shared_ptr<ModelGeometry>> meshes;
    for(auto mesh:root["meshes"].children()){
        auto geometry=std::make_shared<ModelGeometry>();
        if(mesh.has_child("primitives"))for(auto primitive:mesh["primitives"].children()){
            if(!primitive.has_child("attributes")||!primitive["attributes"].has_child("POSITION"))continue;
            auto positions=ReadAccessor(root,Number(primitive["attributes"]["POSITION"]),buffers,true);if(!positions.Buffer)continue;
            Accessor indices;if(primitive.has_child("indices")){indices=ReadAccessor(root,Number(primitive["indices"]),buffers,false);if(!indices.Buffer)continue;}
            auto count=indices.Buffer?indices.Count:positions.Count;auto mode=Field(primitive,"mode",4);if(mode<4||mode>6)continue;
            const auto vertex=[&](std::size_t i){return indices.Buffer?Index(indices,i):static_cast<std::uint32_t>(i);};
            for(std::size_t i=mode==4?0:2;mode==4?i+2<count:i<count; i+=mode==4?3:1){
                std::uint32_t a,b,c;
                if(mode==4){a=vertex(i);b=vertex(i+1);c=vertex(i+2);}
                else if(mode==5){a=vertex(i-2);b=vertex(i-1);c=vertex(i);if(i%2)std::swap(a,b);}
                else{a=vertex(0);b=vertex(i-1);c=vertex(i);}
                if(a>=positions.Count||b>=positions.Count||c>=positions.Count)continue;
                auto va=Position(positions,a),vb=Position(positions,b),vc=Position(positions,c);
                if(Finite(va)&&Finite(vb)&&Finite(vc)&&Vec3::Cross(vb-va,vc-va).LengthSquared()>1e-18f)geometry->Triangles.push_back({va,vb,vc});
            }
        }
        geometry->Build();meshes.push_back(std::move(geometry));
    }
    for(auto node:root["nodes"].children()){auto mesh=Field(node,"mesh",meshes.size());result->SourceNodes.push_back(mesh<meshes.size()?meshes[mesh]:nullptr);}
    return result;
}

void ModelGeometry::Build(){
    Order.resize(Triangles.size());std::iota(Order.begin(),Order.end(),0u);
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
    using Point=std::tuple<float,float,float>;using Key=std::pair<Point,Point>;
    std::map<Key,std::uint32_t> edges;
    for(std::uint32_t i=0;i<Triangles.size();++i){const auto& t=Triangles[i];const std::array<Vec3,3> points{t.A,t.B,t.C};
        for(int j=0;j<3;++j){auto a=points[j],b=points[(j+1)%3];Point pa{a.X,a.Y,a.Z},pb{b.X,b.Y,b.Z};if(pb<pa){std::swap(pa,pb);std::swap(a,b);}auto [it,inserted]=edges.emplace(Key{pa,pb},static_cast<std::uint32_t>(Edges.size()));if(inserted)Edges.push_back({a,b,{}});Edges[it->second].Faces.push_back(i);}
    }
}

bool ModelGeometry::Raycast(Vec3 origin,Vec3 direction,float& distance) const{
    if(Nodes.empty()||!Finite(origin)||!Finite(direction))return false;bool hit=false;
    std::vector<std::uint32_t> stack{0};
    while(!stack.empty()){auto index=stack.back();stack.pop_back();const auto& node=Nodes[index];if(!Box(origin,direction,node.Min,node.Max,distance))continue;
        if(!node.Count){stack.push_back(node.Left);stack.push_back(node.Right);continue;}
        for(std::size_t i=node.Begin;i<node.Begin+node.Count;++i){const auto& t=Triangles[Order[i]];auto e1=t.B-t.A,e2=t.C-t.A,p=Vec3::Cross(direction,e2);float determinant=Vec3::Dot(e1,p);if(std::abs(determinant)<1e-10f)continue;
            float reciprocal=1/determinant;auto offset=origin-t.A;float u=Vec3::Dot(offset,p)*reciprocal;if(u<0||u>1)continue;auto q=Vec3::Cross(offset,e1);float v=Vec3::Dot(direction,q)*reciprocal;if(v<0||u+v>1)continue;float d=Vec3::Dot(e2,q)*reciprocal;if(d>=0&&d<distance){distance=d;hit=true;}}
    }return hit;
}
}
