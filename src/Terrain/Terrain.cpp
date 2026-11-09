#include "Bazzalt/Terrain.h"
#include "Terrain/TerrainGeometryInternal.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace Bazzalt {
namespace {
std::atomic<std::uint64_t> NextRevision{1};
bool Finite(float v){return std::isfinite(v);}
bool Finite(Vec3 v){return Finite(v.X)&&Finite(v.Y)&&Finite(v.Z);}
float Sample(const std::vector<float>& values,unsigned width,unsigned height,float u,float v,float fallback=0){
    if(values.empty())return fallback;
    if(!width||!height||values.size()!=std::size_t(width)*height)throw std::invalid_argument("Invalid terrain sample storage");
    float x=std::clamp(u,0.f,1.f)*(width-1),z=std::clamp(v,0.f,1.f)*(height-1);
    auto a=unsigned(x),b=unsigned(z),c=std::min(a+1,width-1),d=std::min(b+1,height-1);
    return Lerp(Lerp(values[b*width+a],values[b*width+c],x-a),Lerp(values[d*width+a],values[d*width+c],x-a),z-b);
}
float Height(const Terrain& t,float u,float v){float result=0;for(const auto& layer:t.Layers){if(!layer.Enabled)continue;float h=Sample(layer.Heights,t.Resolution,t.Resolution,u,v),a=layer.Opacity;
    switch(layer.Blend){case TerrainBlend::Add:result+=h*a;break;case TerrainBlend::Replace:result=Lerp(result,h,a);break;case TerrainBlend::Maximum:result=Lerp(result,std::max(result,h),a);break;case TerrainBlend::Minimum:result=Lerp(result,std::min(result,h),a);break;}}
    return result;
}
void Require(const Terrain& t){if(!t.IsValid())throw std::invalid_argument("Invalid terrain data");}
TerrainLayer& Layer(Terrain& t,unsigned index){if(index>=t.Layers.size())throw std::out_of_range("Terrain layer");auto& layer=t.Layers[index];if(!layer.Enabled||layer.Opacity<=Epsilon)throw std::invalid_argument("Terrain layer is disabled or transparent");return layer;}
void Allocate(std::vector<float>& v,unsigned resolution,float value=0){if(v.empty())v.assign(std::size_t(resolution)*resolution,value);}
void PaintStorage(Terrain& t){for(std::size_t i=0;i<t.Layers.size();++i)Allocate(t.Layers[i].Weights,t.Resolution,i==0?1.f:0.f);}
void Redistribute(Terrain& t,unsigned layer,std::size_t at){if(t.Layers.size()==1){t.Layers.front().Weights[at]=1;return;}float sum=0;for(unsigned i=0;i<t.Layers.size();++i)if(i!=layer)sum+=t.Layers[i].Weights[at];float remaining=1-t.Layers[layer].Weights[at];if(sum>Epsilon){for(unsigned i=0;i<t.Layers.size();++i)if(i!=layer)t.Layers[i].Weights[at]=std::clamp(t.Layers[i].Weights[at]*remaining/sum,0.f,1.f);}else{for(unsigned i=0;i<t.Layers.size();++i)if(i!=layer)t.Layers[i].Weights[at]=i==(layer==0?1:0)?remaining:0;}}
float Random(std::uint32_t& state){state=state*1664525u+1013904223u;return float(state>>8)*(1.f/16777216.f);}
Quaternion Align(Vec3 normal){auto axis=Vec3::Cross({0,1,0},normal);float dot=std::clamp(normal.Y,-1.f,1.f);return axis.LengthSquared()<Epsilon?Quaternion{}:Quaternion::FromAxisAngle(axis.Normalized(),std::acos(dot));}
void Import(Terrain& t,unsigned layerIndex,const std::vector<float>& data,unsigned width,unsigned height,bool splat,float scale){Require(t);if(!width||!height||width>8192||height>8192||std::size_t(width)*height!=data.size()||!Finite(scale)||!std::all_of(data.begin(),data.end(),[](float f){return Finite(f);}))throw std::invalid_argument("Invalid terrain image");auto& layer=Layer(t,layerIndex);auto& target=splat?layer.Weights:layer.Heights;target.resize(std::size_t(t.Resolution)*t.Resolution);for(unsigned z=0;z<t.Resolution;++z)for(unsigned x=0;x<t.Resolution;++x){float value=Sample(data,width,height,float(x)/(t.Resolution-1),float(z)/(t.Resolution-1))*scale;target[z*t.Resolution+x]=splat?std::clamp(value,0.f,1.f):std::clamp(value,-100000.f,100000.f);}t.MarkDirty();}
bool VolumeCell(const Terrain& terrain,unsigned x,unsigned z){
    float left=(float(x)/(terrain.Resolution-1)-.5f)*terrain.Size.X;
    float front=(float(z)/(terrain.Resolution-1)-.5f)*terrain.Size.Y;
    float right=left+terrain.Size.X/(terrain.Resolution-1);
    float back=front+terrain.Size.Y/(terrain.Resolution-1);
    for(const auto& volume:terrain.Volumes){
        if(volume.Enabled&&left<=volume.Center.X+volume.Radius.X&&right>=volume.Center.X-volume.Radius.X&&front<=volume.Center.Z+volume.Radius.Z&&back>=volume.Center.Z-volume.Radius.Z)return true;
    }
    return false;
}
float Density(const Terrain& terrain,Vec3 point){
    float density=terrain.GetHeight(point.X,point.Z)-point.Y;
    for(const auto& volume:terrain.Volumes){
        if(!volume.Enabled)continue;
        auto delta=point-volume.Center;
        Vec3 scaled{delta.X/volume.Radius.X,delta.Y/volume.Radius.Y,delta.Z/volume.Radius.Z};
        float shape=(1-scaled.Length())*std::min({volume.Radius.X,volume.Radius.Y,volume.Radius.Z});
        density=volume.Operation==TerrainVolumeOperation::Add?std::max(density,shape):std::min(density,-shape);
    }
    return density;
}
Vec3 DensityNormal(const Terrain& terrain,Vec3 point){
    float e=std::min({terrain.Size.X/(terrain.Resolution-1),terrain.Size.Y/(terrain.Resolution-1),(terrain.VerticalRange.Y-terrain.VerticalRange.X)/terrain.VerticalSamples})*.01f;
    return Vec3{Density(terrain,point-Vec3{e,0,0})-Density(terrain,point+Vec3{e,0,0}),Density(terrain,point-Vec3{0,e,0})-Density(terrain,point+Vec3{0,e,0}),Density(terrain,point-Vec3{0,0,e})-Density(terrain,point+Vec3{0,0,e})}.Normalized();
}
Vec4 SurfaceColor(const Terrain& terrain,Vec3 point){
    Vec4 color{};
    float total=0;
    for(std::size_t i=0;i<terrain.Layers.size();++i){
        const auto& layer=terrain.Layers[i];
        if(!layer.Enabled)continue;
        float weight=Sample(layer.Weights,terrain.Resolution,terrain.Resolution,point.X/terrain.Size.X+.5f,point.Z/terrain.Size.Y+.5f,i==0?1.f:0.f)*layer.Opacity;
        color=color+layer.Color*weight;
        total+=weight;
    }
    return total>Epsilon?color*(1/total):Vec4{1,1,1,1};
}
void VolumeMesh(const Terrain& terrain,unsigned x,unsigned z,TerrainMesh& mesh){
    const float dx=terrain.Size.X/(terrain.Resolution-1),dz=terrain.Size.Y/(terrain.Resolution-1);
    const float dy=(terrain.VerticalRange.Y-terrain.VerticalRange.X)/terrain.VerticalSamples;
    const Vec3 base{float(x)*dx-terrain.Size.X*.5f,terrain.VerticalRange.X,float(z)*dz-terrain.Size.Y*.5f};
    constexpr unsigned tetrahedra[6][4]={{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
    const auto triangle=[&](Vec3 a,Vec3 b,Vec3 c){
        auto normal=DensityNormal(terrain,(a+b+c)/3);
        if(Vec3::Dot(Vec3::Cross(b-a,c-a),normal)<0)std::swap(b,c);
        if(Vec3::Cross(b-a,c-a).LengthSquared()<1e-16f)return;
        auto index=unsigned(mesh.Vertices.size());
        for(auto point:{a,b,c}){
            auto vertexNormal=DensityNormal(terrain,point);
            if(vertexNormal.LengthSquared()<Epsilon)vertexNormal=Vec3::Cross(b-a,c-a).Normalized();
            mesh.Vertices.push_back({point,vertexNormal,{point.X,point.Z},SurfaceColor(terrain,point)});
        }
        mesh.Indices.insert(mesh.Indices.end(),{index,index+1,index+2});
    };
    for(unsigned y=0;y<terrain.VerticalSamples;++y){
        Vec3 points[8];float values[8];
        for(unsigned i=0;i<8;++i){points[i]=base+Vec3{float(i&1)*dx,float(y+((i>>2)&1))*dy,float((i>>1)&1)*dz};values[i]=Density(terrain,points[i]);}
        for(const auto& tetra:tetrahedra){
            unsigned inside[4],outside[4],count=0,empty=0;
            for(auto corner:tetra){if(values[corner]>=0)inside[count++]=corner;else outside[empty++]=corner;}
            if(count==0||count==4)continue;
            const auto crossing=[&](unsigned a,unsigned b){float blend=values[a]/(values[a]-values[b]);return points[a]+(points[b]-points[a])*blend;};
            if(count==1)triangle(crossing(inside[0],outside[0]),crossing(inside[0],outside[1]),crossing(inside[0],outside[2]));
            else if(count==3)triangle(crossing(outside[0],inside[0]),crossing(outside[0],inside[1]),crossing(outside[0],inside[2]));
            else{
                auto a=crossing(inside[0],outside[0]),b=crossing(inside[0],outside[1]),c=crossing(inside[1],outside[0]),d=crossing(inside[1],outside[1]);
                triangle(a,b,d);triangle(a,d,c);
            }
        }
    }
}
}
Terrain::Terrain(){MarkDirty();}
bool Terrain::IsValid() const {
    if(!Finite(Size.X)||!Finite(Size.Y)||Size.X<=0||Size.Y<=0||Size.X>100000||Size.Y>100000||Resolution<3||Resolution>MaxResolution||((Resolution-1)&(Resolution-2))||ChunkSize<2||ChunkSize>64||(ChunkSize&(ChunkSize-1))||ChunkSize>Resolution-1||MaxLOD>6||!Finite(LODDistance)||LODDistance<=0||!Finite(SkirtDepth)||SkirtDepth<0||Layers.empty()||Layers.size()>MaxLayers||Vegetation.size()>64||Splines.size()>128||Instances.size()>MaxInstances)return false;
    auto count=std::size_t(Resolution)*Resolution;
    if(count*Layers.size()*2>MaxSampleValues)return false;
    if(Volumes.size()>64||VerticalSamples<4||VerticalSamples>128||!Finite(VerticalRange.X)||!Finite(VerticalRange.Y)||VerticalRange.Y-VerticalRange.X<.001f||std::abs(VerticalRange.X)>1000000||std::abs(VerticalRange.Y)>1000000)return false;
    for(const auto& volume:Volumes){
        if(volume.Name.size()>128||unsigned(volume.Operation)>1||!Finite(volume.Center)||!Finite(volume.Radius)||volume.Radius.X<=0||volume.Radius.Y<=0||volume.Radius.Z<=0)return false;
        if(volume.Enabled&&(volume.Center.Y-volume.Radius.Y<=VerticalRange.X||volume.Center.Y+volume.Radius.Y>=VerticalRange.Y))return false;
    }
    if(!Holes.empty()&&(Holes.size()!=std::size_t(Resolution-1)*(Resolution-1)||std::any_of(Holes.begin(),Holes.end(),[](auto v){return v>1;})))return false;
    for(const auto& layer:Layers){if(layer.Name.size()>128||unsigned(layer.Blend)>3||!Finite(layer.Opacity)||layer.Opacity<0||layer.Opacity>1||!Finite(layer.Tiling)||layer.Tiling<=0||!Finite(layer.Color.X)||!Finite(layer.Color.Y)||!Finite(layer.Color.Z)||!Finite(layer.Color.W))return false;
        if(!layer.Heights.empty()&&(layer.Heights.size()!=count||!std::all_of(layer.Heights.begin(),layer.Heights.end(),[](float h){return Finite(h)&&std::abs(h)<=100000;})))return false;
        if(!layer.Weights.empty()&&(layer.Weights.size()!=count||!std::all_of(layer.Weights.begin(),layer.Weights.end(),[](float h){return Finite(h)&&h>=0&&h<=1;})))return false;}
    if(!Volumes.empty()){
        // Bound extraction work and ensure that replacing a column retains its top.
        std::size_t volumeCells=0;
        for(unsigned z=0;z<Resolution-1;++z)for(unsigned x=0;x<Resolution-1;++x)if(VolumeCell(*this,x,z)){
            if(++volumeCells*VerticalSamples>4u*1024u*1024u)return false;
            for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){float height=GetHeight((float(x+i)/(Resolution-1)-.5f)*Size.X,(float(z+j)/(Resolution-1)-.5f)*Size.Y);if(height<=VerticalRange.X||height>=VerticalRange.Y)return false;}
        }
    }
    for(const auto& v:Vegetation)if(v.Name.size()>128||!Finite(v.Density)||v.Density<0||v.Density>1000||!Finite(v.Spacing)||v.Spacing<.01f||!Finite(v.MinHeight)||!Finite(v.MaxHeight)||v.MinHeight>v.MaxHeight||!Finite(v.MinSlope)||!Finite(v.MaxSlope)||v.MinSlope<0||v.MaxSlope>90||v.MinSlope>v.MaxSlope||!Finite(v.MinScale)||!Finite(v.MaxScale)||v.MinScale<=0||v.MinScale>v.MaxScale||v.MaxScale>1000||v.MaxInstances>MaxInstances)return false;
    for(const auto& i:Instances)if(i.Prototype>=Vegetation.size()||!Finite(i.Position)||std::abs(i.Position.X)>1000000||std::abs(i.Position.Y)>1000000||std::abs(i.Position.Z)>1000000||!Finite(i.Scale)||i.Scale<=0||i.Scale>1000||!Finite(i.Rotation.X)||!Finite(i.Rotation.Y)||!Finite(i.Rotation.Z)||!Finite(i.Rotation.W))return false;
    for(const auto& s:Splines)if(s.Name.size()>128||s.Points.size()>1024||!Finite(s.Width)||s.Width<=0||s.Width>100000||!Finite(s.Shoulder)||s.Shoulder<0||s.Shoulder>100000||!Finite(s.Depth)||std::abs(s.Depth)>100000||!Finite(s.Strength)||s.Strength<0||s.Strength>1||!std::all_of(s.Points.begin(),s.Points.end(),[](Vec3 p){return Finite(p)&&std::abs(p.X)<=1000000&&std::abs(p.Y)<=1000000&&std::abs(p.Z)<=1000000;}))return false;
    for(const auto& spline:Splines)if(!Finite(spline.SurfaceOffset)||!Finite(spline.TextureScale)||spline.TextureScale<=0||!Finite(spline.Color.X)||!Finite(spline.Color.Y)||!Finite(spline.Color.Z)||!Finite(spline.Color.W))return false;
    return true;
}
void Terrain::MarkDirty(){Revision=NextRevision.fetch_add(1,std::memory_order_relaxed);if(!Revision)Revision=NextRevision.fetch_add(1,std::memory_order_relaxed);}
void Terrain::Resize(unsigned resolution){Require(*this);if(resolution<3||resolution>MaxResolution||((resolution-1)&(resolution-2)))throw std::invalid_argument("Terrain resolution must be 2^n + 1, at most 2049");if(resolution==Resolution)return;
    if(std::size_t(resolution)*resolution*Layers.size()*2>MaxSampleValues)throw std::invalid_argument("Terrain sample memory budget exceeded");
    auto next=*this;next.Resolution=resolution;next.ChunkSize=std::min(ChunkSize,resolution-1);
    for(std::size_t i=0;i<Layers.size();++i)for(bool splat:{false,true}){const auto& from=splat?Layers[i].Weights:Layers[i].Heights;auto& to=splat?next.Layers[i].Weights:next.Layers[i].Heights;if(from.empty())continue;to.resize(std::size_t(resolution)*resolution);for(unsigned z=0;z<resolution;++z)for(unsigned x=0;x<resolution;++x)to[z*resolution+x]=Sample(from,Resolution,Resolution,float(x)/(resolution-1),float(z)/(resolution-1));}
    if(!Holes.empty()){next.Holes.resize(std::size_t(resolution-1)*(resolution-1));for(unsigned z=0;z<resolution-1;++z)for(unsigned x=0;x<resolution-1;++x)next.Holes[z*(resolution-1)+x]=Holes[(std::uint64_t(z)*(Resolution-1)/(resolution-1))*(Resolution-1)+std::uint64_t(x)*(Resolution-1)/(resolution-1)];}
    if(!next.IsValid())throw std::invalid_argument("Terrain sample memory budget exceeded");
    *this=std::move(next);MarkDirty();
}
unsigned Terrain::AddLayer(std::string name){Require(*this);if(Layers.size()>=MaxLayers||name.size()>128||std::size_t(Resolution)*Resolution*(Layers.size()+1)*2>MaxSampleValues)throw std::invalid_argument("Terrain layer limit or invalid name");TerrainLayer layer;layer.Name=std::move(name);Layers.push_back(std::move(layer));MarkDirty();return unsigned(Layers.size()-1);}
void Terrain::RemoveLayer(unsigned index){Require(*this);if(index>=Layers.size()||Layers.size()==1)throw std::invalid_argument("Terrain must retain at least one layer");Layers.erase(Layers.begin()+index);bool painted=std::any_of(Layers.begin(),Layers.end(),[](const auto& l){return !l.Weights.empty();});if(painted){PaintStorage(*this);for(std::size_t at=0;at<std::size_t(Resolution)*Resolution;++at){float sum=0;for(const auto& l:Layers)sum+=l.Weights[at];for(std::size_t i=0;i<Layers.size();++i)Layers[i].Weights[at]=sum>Epsilon?std::clamp(Layers[i].Weights[at]/sum,0.f,1.f):(i==0?1.f:0.f);}}MarkDirty();}
float Terrain::GetHeight(float x,float z)const{if(!Finite(x)||!Finite(z))throw std::invalid_argument("Non-finite terrain position");return Height(*this,x/Size.X+.5f,z/Size.Y+.5f);}
Vec3 Terrain::GetNormal(float x,float z)const{float dx=Size.X/(Resolution-1),dz=Size.Y/(Resolution-1),a=std::max(x-dx,-Size.X*.5f),b=std::min(x+dx,Size.X*.5f),c=std::max(z-dz,-Size.Y*.5f),d=std::min(z+dz,Size.Y*.5f);return Vec3{(GetHeight(a,z)-GetHeight(b,z))/std::max(b-a,Epsilon),1,(GetHeight(x,c)-GetHeight(x,d))/std::max(d-c,Epsilon)}.Normalized();}
float Terrain::GetSlope(float x,float z)const{return ToDegrees(std::acos(std::clamp(GetNormal(x,z).Y,-1.f,1.f)));}
bool Terrain::IsHole(unsigned x,unsigned z)const{return !Holes.empty()&&x<Resolution-1&&z<Resolution-1&&Holes[z*(Resolution-1)+x]!=0;}
void Terrain::ApplyBrush(TerrainTool tool,Vec2 center,const TerrainBrush& brush,float seconds){Require(*this);
    if(!Volumes.empty()){
        auto next=*this;
        auto volumes=std::move(next.Volumes);
        next.Volumes.clear();
        next.ApplyBrush(tool,center,brush,seconds);
        next.Volumes=std::move(volumes);
        if(!next.IsValid())throw std::invalid_argument("Terrain edit exceeds the volume extraction budget or height range");
        *this=std::move(next);
        return;
    }
if(unsigned(tool)>unsigned(TerrainTool::FillHole)||!Finite(center.X)||!Finite(center.Y)||!Finite(brush.Radius)||brush.Radius<=0||brush.Radius>1000000||!Finite(brush.Strength)||brush.Strength<0||brush.Strength>1000000||!Finite(seconds)||seconds<0||seconds>10||!Finite(brush.Falloff)||brush.Falloff<0||brush.Falloff>1||(!Finite(brush.TargetHeight)||std::abs(brush.TargetHeight)>100000))throw std::invalid_argument("Invalid terrain brush");
    if(!brush.Stamp.empty()&&(!brush.StampWidth||!brush.StampHeight||brush.StampWidth>8192||brush.StampHeight>8192||std::size_t(brush.StampWidth)*brush.StampHeight!=brush.Stamp.size()||!std::all_of(brush.Stamp.begin(),brush.Stamp.end(),[](float v){return Finite(v)&&v>=0&&v<=1;})))throw std::invalid_argument("Invalid terrain stamp");
    auto& layer=Layer(*this,brush.Layer);auto n=Resolution;bool paint=tool==TerrainTool::Paint||tool==TerrainTool::ErasePaint;if(paint)PaintStorage(*this);auto& target=paint?layer.Weights:layer.Heights;Allocate(target,n);auto before=target;
    if(tool==TerrainTool::Hole||tool==TerrainTool::FillHole)if(Holes.empty())Holes.assign(std::size_t(n-1)*(n-1),0);
    for(unsigned z=0;z<n;++z)for(unsigned x=0;x<n;++x){float px=float(x)/(n-1)*Size.X-Size.X*.5f,pz=float(z)/(n-1)*Size.Y-Size.Y*.5f;float dx=px-center.X,dz=pz-center.Y,d=std::sqrt(dx*dx+dz*dz)/brush.Radius;if(d>1)continue;float fall=brush.Falloff<=Epsilon?1:std::clamp((1-d)/brush.Falloff,0.f,1.f);fall=fall*fall*(3-2*fall);float stamp=brush.Stamp.empty()?1:Sample(brush.Stamp,brush.StampWidth,brush.StampHeight,dx/(2*brush.Radius)+.5f,dz/(2*brush.Radius)+.5f);float a=fall*stamp*brush.Strength*seconds;auto at=z*n+x;if(a<=0)continue;
        if(tool==TerrainTool::Hole||tool==TerrainTool::FillHole){if(x<n-1&&z<n-1)Holes[z*(n-1)+x]=tool==TerrainTool::Hole;continue;}
        if(paint){target[at]=std::clamp(target[at]+(tool==TerrainTool::Paint?a:-a),0.f,1.f);Redistribute(*this,brush.Layer,at);continue;}
        if(tool==TerrainTool::Smooth){float sum=0;unsigned count=0;for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i){auto sx=unsigned(std::clamp(int(x)+i,0,int(n)-1)),sz=unsigned(std::clamp(int(z)+j,0,int(n)-1));sum+=before[sz*n+sx];++count;}target[at]=Lerp(before[at],sum/count,std::min(a,1.f));}
        else if(tool==TerrainTool::Flatten){float delta=brush.TargetHeight-GetHeight(px,pz);target[at]+=delta*std::min(a,1.f)/layer.Opacity;}
        else target[at]+=a*(tool==TerrainTool::Lower?-1:1)/layer.Opacity;
        target[at]=std::clamp(target[at],-100000.f,100000.f);
    }MarkDirty();
}
void Terrain::ImportHeightmap(unsigned layer,const std::vector<float>& data,unsigned w,unsigned h,float scale){
    auto next=*this;
    Import(next,layer,data,w,h,false,scale);
    if(!next.IsValid())throw std::invalid_argument("Heightmap exceeds the volume height range");
    *this=std::move(next);
}
void Terrain::ImportSplatmap(unsigned layer,const std::vector<float>& data,unsigned w,unsigned h){Require(*this);if(layer>=Layers.size())throw std::out_of_range("Terrain layer");Import(*this,layer,data,w,h,true,1);PaintStorage(*this);for(std::size_t i=0;i<Layers[layer].Weights.size();++i)Redistribute(*this,layer,i);MarkDirty();}
void Terrain::ApplySpline(unsigned index,unsigned layerIndex){Require(*this);
    if(!Volumes.empty()){
        auto next=*this;
        auto volumes=std::move(next.Volumes);
        next.Volumes.clear();
        next.ApplySpline(index,layerIndex);
        next.Volumes=std::move(volumes);
        if(!next.IsValid())throw std::invalid_argument("Terrain edit exceeds the volume extraction budget or height range");
        *this=std::move(next);
        return;
    }
if(index>=Splines.size()||Splines[index].Points.size()<2)throw std::invalid_argument("A terrain spline needs at least two points");auto& layer=Layer(*this,layerIndex);Allocate(layer.Heights,Resolution);const auto& s=Splines[index];auto segments=s.Points.size()-(s.Closed?0:1);
    for(unsigned z=0;z<Resolution;++z)for(unsigned x=0;x<Resolution;++x){Vec3 p{float(x)/(Resolution-1)*Size.X-Size.X*.5f,0,float(z)/(Resolution-1)*Size.Y-Size.Y*.5f};float closest=std::numeric_limits<float>::max(),height=0;for(std::size_t i=0;i<segments;++i){auto a=s.Points[i],b=s.Points[(i+1)%s.Points.size()],d=b-a;float length=d.X*d.X+d.Z*d.Z,t=length<Epsilon?0:std::clamp(((p.X-a.X)*d.X+(p.Z-a.Z)*d.Z)/length,0.f,1.f);auto q=a+d*t;float distance=std::hypot(p.X-q.X,p.Z-q.Z);if(distance<closest){closest=distance;height=q.Y-s.Depth;}}
        float blend=closest<=s.Width*.5f?1:s.Shoulder<=Epsilon?0:std::clamp(1-(closest-s.Width*.5f)/s.Shoulder,0.f,1.f);blend=blend*blend*(3-2*blend)*s.Strength;layer.Heights[z*Resolution+x]=std::clamp(layer.Heights[z*Resolution+x]+(height-GetHeight(p.X,p.Z))*blend/layer.Opacity,-100000.f,100000.f);
    }MarkDirty();
}
void Terrain::ApplyBoolean(const Terrain& other,const Mat4& otherToLocal,TerrainBoolean op,unsigned layerIndex){Require(*this);if(this==&other)throw std::invalid_argument("Terrain boolean needs a separate source");
    if(!Volumes.empty()){
        auto next=*this;
        auto volumes=std::move(next.Volumes);
        next.Volumes.clear();
        next.ApplyBoolean(other,otherToLocal,op,layerIndex);
        next.Volumes=std::move(volumes);
        if(!next.IsValid())throw std::invalid_argument("Terrain edit exceeds the volume extraction budget or height range");
        *this=std::move(next);
        return;
    }
Require(other);if(unsigned(op)>2)throw std::invalid_argument("Invalid terrain boolean");for(float v:otherToLocal.Values)if(!Finite(v))throw std::invalid_argument("Non-finite terrain boolean transform");if(std::abs(otherToLocal(0,1))>Epsilon||std::abs(otherToLocal(2,1))>Epsilon||std::abs(otherToLocal(1,0))>Epsilon||std::abs(otherToLocal(1,2))>Epsilon||otherToLocal(1,1)<=0)throw std::invalid_argument("Heightfield booleans require upright terrain transforms");Mat4 inverse;if(!otherToLocal.TryInverse(inverse))throw std::invalid_argument("Singular terrain boolean transform");if(this==&other)throw std::invalid_argument("Terrain boolean needs a separate source");auto& layer=Layer(*this,layerIndex);Allocate(layer.Heights,Resolution);
    for(unsigned z=0;z<Resolution;++z)for(unsigned x=0;x<Resolution;++x){float px=float(x)/(Resolution-1)*Size.X-Size.X*.5f,pz=float(z)/(Resolution-1)*Size.Y-Size.Y*.5f,from=GetHeight(px,pz);auto q=inverse.TransformPoint({px,from,pz});if(std::abs(q.X)>other.Size.X*.5f||std::abs(q.Z)>other.Size.Y*.5f)continue;float to=otherToLocal.TransformPoint({q.X,other.GetHeight(q.X,q.Z),q.Z}).Y;float value=op==TerrainBoolean::Union?std::max(from,to):op==TerrainBoolean::Intersection?std::min(from,to):from-std::max(0.f,to);layer.Heights[z*Resolution+x]=std::clamp(layer.Heights[z*Resolution+x]+(value-from)/layer.Opacity,-100000.f,100000.f);
    }MarkDirty();
}
void Terrain::PaintVegetation(unsigned prototype,Vec2 center,float radius,unsigned count,bool erase){Require(*this);if(prototype>=Vegetation.size()||!Finite(center.X)||!Finite(center.Y)||!Finite(radius)||radius<=0||radius>1000000||count>MaxInstances)throw std::invalid_argument("Invalid vegetation brush");if(erase){std::erase_if(Instances,[&](const auto& i){return i.Prototype==prototype&&std::hypot(i.Position.X-center.X,i.Position.Z-center.Y)<=radius;});MarkDirty();return;}
    const auto& v=Vegetation[prototype];if(!v.Enabled||!v.Model)return;std::uint32_t seed=v.Seed+std::uint32_t(Instances.size());auto current=unsigned(std::count_if(Instances.begin(),Instances.end(),[&](const auto& i){return i.Prototype==prototype;}));
    std::unordered_map<std::uint64_t,std::vector<std::size_t>> cells;
    const auto cellKey=[](int x,int z){return (std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(z);};
    const auto cell=[&](Vec3 p){return std::pair{int(std::floor(p.X/v.Spacing)),int(std::floor(p.Z/v.Spacing))};};
    for(std::size_t i=0;i<Instances.size();++i){auto [x,z]=cell(Instances[i].Position);cells[cellKey(x,z)].push_back(i);}
    for(unsigned attempt=0,added=0;attempt<count*20&&added<count&&Instances.size()<MaxInstances&&current<v.MaxInstances;++attempt){float angle=Random(seed)*2*Pi,r=std::sqrt(Random(seed))*radius;Vec3 p{center.X+std::cos(angle)*r,0,center.Y+std::sin(angle)*r};if(std::abs(p.X)>Size.X*.5f||std::abs(p.Z)>Size.Y*.5f)continue;auto cx=std::min(unsigned((p.X/Size.X+.5f)*(Resolution-1)),Resolution-2),cz=std::min(unsigned((p.Z/Size.Y+.5f)*(Resolution-1)),Resolution-2);if(IsHole(cx,cz))continue;p.Y=GetHeight(p.X,p.Z);float slope=GetSlope(p.X,p.Z);if(p.Y<v.MinHeight||p.Y>v.MaxHeight||slope<v.MinSlope||slope>v.MaxSlope)continue;auto [cxCell,czCell]=cell(p);bool occupied=false;for(int z=-1;z<=1&&!occupied;++z)for(int x=-1;x<=1&&!occupied;++x){auto found=cells.find(cellKey(cxCell+x,czCell+z));if(found==cells.end())continue;for(auto index:found->second){const auto& i=Instances[index];if(std::hypot(i.Position.X-p.X,i.Position.Z-p.Z)<v.Spacing){occupied=true;break;}}}if(occupied)continue;auto rotation=Quaternion::FromAxisAngle({0,1,0},Random(seed)*2*Pi);if(v.AlignToNormal)rotation=Align(GetNormal(p.X,p.Z))*rotation;Instances.push_back({prototype,p,rotation,Lerp(v.MinScale,v.MaxScale,Random(seed))});cells[cellKey(cxCell,czCell)].push_back(Instances.size()-1);++added;++current;}
    MarkDirty();
}
void Terrain::GenerateVegetation(unsigned prototype){Require(*this);if(prototype>=Vegetation.size())throw std::out_of_range("Terrain vegetation prototype");std::erase_if(Instances,[&](const auto& i){return i.Prototype==prototype;});auto& v=Vegetation[prototype];auto count=unsigned(std::min(double(v.MaxInstances),std::ceil(double(Size.X)*Size.Y*v.Density)));PaintVegetation(prototype,{},std::hypot(Size.X,Size.Y)*.5f,count);MarkDirty();}

void Terrain::MatchEdge(const Terrain& neighbour,TerrainEdge edge,const Mat4& neighbourToLocal,unsigned layerIndex){
    Require(*this);
    Require(neighbour);
    if(this==&neighbour||unsigned(edge)>3)throw std::invalid_argument("Invalid terrain neighbour");
    for(float value:neighbourToLocal.Values)if(!Finite(value))throw std::invalid_argument("Invalid terrain neighbour transform");
    Mat4 inverse;
    if(!neighbourToLocal.TryInverse(inverse))throw std::invalid_argument("Singular terrain neighbour transform");
    // Upright affine transforms preserve the heightfield's single-valued surface.
    if(std::abs(neighbourToLocal(0,1))>Epsilon||std::abs(neighbourToLocal(2,1))>Epsilon||std::abs(neighbourToLocal(1,0))>Epsilon||std::abs(neighbourToLocal(1,2))>Epsilon||neighbourToLocal(1,1)<=0)throw std::invalid_argument("Terrain neighbours require upright transforms");
    auto next=*this;
    if(next.Resolution<neighbour.Resolution)next.Resize(neighbour.Resolution);
    const auto resolution=next.Resolution;
    auto& layer=Layer(next,layerIndex);
    if(layer.Blend!=TerrainBlend::Add&&layer.Blend!=TerrainBlend::Replace)throw std::invalid_argument("Match edges using an additive or replacement layer");
    Allocate(layer.Heights,resolution);
    for(unsigned sample=0;sample<resolution;++sample){
        const bool vertical=edge==TerrainEdge::Left||edge==TerrainEdge::Right;
        unsigned x=vertical?(edge==TerrainEdge::Left?0:resolution-1):sample;
        unsigned z=vertical?sample:(edge==TerrainEdge::Front?0:resolution-1);
        float px=float(x)/(resolution-1)*Size.X-Size.X*.5f;
        float pz=float(z)/(resolution-1)*Size.Y-Size.Y*.5f;
        auto local=inverse.TransformPoint({px,0,pz});
        float tolerance=std::max(neighbour.Size.X,neighbour.Size.Y)*1e-5f;
        if(std::abs(local.X)>neighbour.Size.X*.5f+tolerance||std::abs(local.Z)>neighbour.Size.Y*.5f+tolerance)throw std::invalid_argument("Neighbour does not cover the complete terrain edge");
        if(std::min(std::abs(std::abs(local.X)-neighbour.Size.X*.5f),std::abs(std::abs(local.Z)-neighbour.Size.Y*.5f))>tolerance)throw std::invalid_argument("Terrain edges are not adjacent");
        float target=neighbourToLocal.TransformPoint({local.X,neighbour.GetHeight(local.X,local.Z),local.Z}).Y;
        // Determine this layer's effective contribution through all later layers.
        auto at=z*resolution+x;
        layer.Heights[at]=0;
        float zero=next.GetHeight(px,pz);
        layer.Heights[at]=1;
        float influence=next.GetHeight(px,pz)-zero;
        if(influence<=Epsilon)throw std::invalid_argument("Later terrain layers mask the edge layer");
        layer.Heights[at]=(target-zero)/influence;
        if(!Finite(layer.Heights[at])||std::abs(layer.Heights[at])>100000||std::abs(next.GetHeight(px,pz)-target)>.01f)throw std::invalid_argument("Terrain layers cannot represent the matched edge");
    }
    if(!next.IsValid())throw std::invalid_argument("Matched edge exceeds terrain volume limits");
    next.MarkDirty();
    *this=std::move(next);
}

TerrainMesh TerrainGeometry::BuildSpline(const Terrain& terrain,unsigned index){Require(terrain);return Runtime::BuildTerrainSpline(terrain,index);}
TerrainMesh Runtime::BuildTerrainSpline(const Terrain& terrain,unsigned index){
    if(index>=terrain.Splines.size())throw std::out_of_range("Terrain spline");
    const auto& spline=terrain.Splines[index];
    TerrainMesh mesh;
    if(!spline.GenerateSurface||spline.Points.size()<2)return mesh;
    std::vector<Vec3> points;
    for(auto point:spline.Points){
        if(points.empty()||std::hypot(point.X-points.back().X,point.Z-points.back().Z)>Epsilon)points.push_back(point);
    }
    if(spline.Closed&&points.size()>1&&std::hypot(points.front().X-points.back().X,points.front().Z-points.back().Z)<=Epsilon)points.pop_back();
    if(points.size()<2)return mesh;
    const auto count=points.size()+(spline.Closed?1:0);
    float distance=0;
    for(std::size_t row=0;row<count;++row){
        auto i=row%points.size();
        auto point=points[i];
        auto previous=points[i==0?(spline.Closed?points.size()-1:0):i-1];
        auto next=points[i+1==points.size()?(spline.Closed?0:i):i+1];
        auto incoming=(point-previous).Normalized(),outgoing=(next-point).Normalized();
        if(incoming.LengthSquared()<Epsilon)incoming=outgoing;
        if(outgoing.LengthSquared()<Epsilon)outgoing=incoming;
        auto incomingSide=Vec3::Cross({0,1,0},incoming).Normalized();
        auto outgoingSide=Vec3::Cross({0,1,0},outgoing).Normalized();
        auto side=(incomingSide+outgoingSide).Normalized();
        if(side.LengthSquared()<Epsilon)side=outgoingSide;
        float miter=std::max(.25f,std::abs(Vec3::Dot(side,outgoingSide)));
        auto offset=side*(spline.Width*.5f/miter);
        auto tangent=(incoming+outgoing).Normalized();
        if(tangent.LengthSquared()<Epsilon)tangent=outgoing;
        auto normal=Vec3::Cross(tangent,side).Normalized();
        if(row)distance+=(point-points[(row-1)%points.size()]).Length();
        point.Y+=spline.SurfaceOffset;
        float v=distance/spline.TextureScale;
        mesh.Vertices.push_back({point-offset,normal,{0,v},spline.Color});
        mesh.Vertices.push_back({point+offset,normal,{spline.Width/spline.TextureScale,v},spline.Color});
        if(row){
            auto base=unsigned((row-1)*2);
            mesh.Indices.insert(mesh.Indices.end(),{base,base+2,base+3,base,base+3,base+1});
        }
    }
    return mesh;
}

TerrainMesh TerrainGeometry::BuildChunk(const Terrain& t,unsigned x,unsigned z,unsigned lod){Require(t);return Runtime::BuildTerrainChunk(t,x,z,lod);}
TerrainMesh Runtime::BuildTerrainChunk(const Terrain& t,unsigned chunkX,unsigned chunkZ,unsigned lod){TerrainMesh mesh;unsigned cells=t.Resolution-1,startX=chunkX*t.ChunkSize,startZ=chunkZ*t.ChunkSize;if(chunkX>=cells/t.ChunkSize||chunkZ>=cells/t.ChunkSize)throw std::out_of_range("Terrain chunk");unsigned step=1u<<std::min({lod,t.MaxLOD,unsigned(std::log2(t.ChunkSize))});unsigned endX=startX+t.ChunkSize,endZ=startZ+t.ChunkSize;
    // Cells containing holes stay at full detail, preserving their exact boundary.
    for(unsigned z=startZ;z<endZ;++z)for(unsigned x=startX;x<endX;++x)if(t.IsHole(x,z)||VolumeCell(t,x,z))step=1;
    auto vertex=[&](unsigned x,unsigned z){float u=float(x)/cells,v=float(z)/cells;TerrainVertex a;a.Position={u*t.Size.X-t.Size.X*.5f,Height(t,u,v),v*t.Size.Y-t.Size.Y*.5f};a.Normal=t.GetNormal(a.Position.X,a.Position.Z);a.UV={u*t.Size.X,v*t.Size.Y};Vec4 color{};float total=0;for(std::size_t i=0;i<t.Layers.size();++i){const auto& l=t.Layers[i];if(!l.Enabled)continue;float weight=Sample(l.Weights,t.Resolution,t.Resolution,u,v,i==0?1.f:0.f)*l.Opacity;color=color+l.Color*weight;total+=weight;}a.Color=total>Epsilon?color*(1/total):Vec4{1,1,1,1};mesh.Vertices.push_back(a);};
    unsigned row=t.ChunkSize/step+1;for(unsigned z=startZ;z<=endZ;z+=step)for(unsigned x=startX;x<=endX;x+=step)vertex(x,z);
    for(unsigned z=0;z<row-1;++z)for(unsigned x=0;x<row-1;++x){if(t.IsHole(startX+x*step,startZ+z*step)||VolumeCell(t,startX+x*step,startZ+z*step))continue;auto a=z*row+x,b=a+1,c=a+row+1,d=a+row;mesh.Indices.insert(mesh.Indices.end(),{a,d,c,a,c,b});}
    for(unsigned z=startZ;z<endZ;++z)for(unsigned x=startX;x<endX;++x)if(!t.IsHole(x,z)&&VolumeCell(t,x,z))VolumeMesh(t,x,z,mesh);
    if(t.SkirtDepth>0){
        auto skirt=[&](unsigned a,unsigned b){auto topA=mesh.Vertices[a],topB=mesh.Vertices[b],bottomA=topA,bottomB=topB;bottomA.Position.Y-=t.SkirtDepth;bottomB.Position.Y-=t.SkirtDepth;
            auto normal=Vec3::Cross(topB.Position-topA.Position,bottomA.Position-topA.Position).Normalized();topA.Normal=topB.Normal=bottomA.Normal=bottomB.Normal=normal;
            auto base=unsigned(mesh.Vertices.size());mesh.Vertices.insert(mesh.Vertices.end(),{topA,topB,bottomB,bottomA});mesh.Indices.insert(mesh.Indices.end(),{base,base+1,base+2,base,base+2,base+3});};
        for(unsigned i=0;i<row-1;++i){if(!t.IsHole(startX+i*step,startZ)&&!VolumeCell(t,startX+i*step,startZ))skirt(i,i+1);if(!t.IsHole(startX+i*step,endZ-1)&&!VolumeCell(t,startX+i*step,endZ-1))skirt((row-1)*row+i+1,(row-1)*row+i);if(!t.IsHole(startX,startZ+i*step)&&!VolumeCell(t,startX,startZ+i*step))skirt((i+1)*row,i*row);if(!t.IsHole(endX-1,startZ+i*step)&&!VolumeCell(t,endX-1,startZ+i*step))skirt(i*row+row-1,(i+1)*row+row-1);}
    }
    return mesh;
}
std::optional<TerrainHit> TerrainGeometry::Raycast(const Terrain& t,Vec3 origin,Vec3 direction,float maximum){Require(t);if(!Finite(origin)||!Finite(direction)||direction.LengthSquared()<Epsilon||!Finite(maximum)||maximum<0)throw std::invalid_argument("Invalid terrain ray");direction=direction.Normalized();std::optional<TerrainHit> result;float closest=maximum;unsigned n=t.Resolution;
    // Authoritative full-resolution surface, independent of camera LOD and skirts.
    auto point=[&](unsigned x,unsigned z){float px=float(x)/(n-1)*t.Size.X-t.Size.X*.5f,pz=float(z)/(n-1)*t.Size.Y-t.Size.Y*.5f;return Vec3{px,t.GetHeight(px,pz),pz};};
    auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,bool heightfield=true){auto e=b-a,f=c-a,p=Vec3::Cross(direction,f);float det=Vec3::Dot(e,p);if(std::abs(det)<1e-8f)return;float inv=1/det;auto s=origin-a;float u=Vec3::Dot(s,p)*inv;if(u<0||u>1)return;auto q=Vec3::Cross(s,e);float v=Vec3::Dot(direction,q)*inv;if(v<0||u+v>1)return;float d=Vec3::Dot(f,q)*inv;if(d>=0&&d<=closest){closest=d;auto pos=origin+direction*d;result=TerrainHit{pos,heightfield?t.GetNormal(pos.X,pos.Z):Vec3::Cross(e,f).Normalized(),d};}};
    for(unsigned index=0;index<t.Splines.size();++index){
        auto surface=Runtime::BuildTerrainSpline(t,index);
        for(std::size_t i=0;i<surface.Indices.size();i+=3)triangle(surface.Vertices[surface.Indices[i]].Position,surface.Vertices[surface.Indices[i+1]].Position,surface.Vertices[surface.Indices[i+2]].Position,false);
    }
    float enter=0,exit=maximum;
    auto slab=[&](float o,float d,float half){if(std::abs(d)<1e-12f)return o>=-half&&o<=half;float a=(-half-o)/d,b=(half-o)/d;if(a>b)std::swap(a,b);enter=std::max(enter,a);exit=std::min(exit,b);return enter<=exit;};
    if(!slab(origin.X,direction.X,t.Size.X*.5f)||!slab(origin.Z,direction.Z,t.Size.Y*.5f))return result;
    auto start=origin+direction*enter;float cellX=t.Size.X/(n-1),cellZ=t.Size.Y/(n-1);
    int x=std::clamp(int(std::floor((start.X+t.Size.X*.5f)/cellX)),0,int(n)-2),z=std::clamp(int(std::floor((start.Z+t.Size.Y*.5f)/cellZ)),0,int(n)-2);
    int sx=direction.X>0?1:direction.X<0?-1:0,sz=direction.Z>0?1:direction.Z<0?-1:0;
    const float infinity=std::numeric_limits<float>::infinity();float deltaX=sx?cellX/std::abs(direction.X):infinity,deltaZ=sz?cellZ/std::abs(direction.Z):infinity;
    float nextX=sx?((float(x+(sx>0))*cellX-t.Size.X*.5f)-origin.X)/direction.X:infinity,nextZ=sz?((float(z+(sz>0))*cellZ-t.Size.Y*.5f)-origin.Z)/direction.Z:infinity;
    for(unsigned visited=0;visited<2*n&&x>=0&&z>=0&&x<int(n)-1&&z<int(n)-1;++visited){
        if(!t.IsHole(unsigned(x),unsigned(z))&&VolumeCell(t,unsigned(x),unsigned(z))){
            TerrainMesh volume;VolumeMesh(t,unsigned(x),unsigned(z),volume);
            for(std::size_t i=0;i<volume.Indices.size();i+=3)triangle(volume.Vertices[volume.Indices[i]].Position,volume.Vertices[volume.Indices[i+1]].Position,volume.Vertices[volume.Indices[i+2]].Position,false);
        }else if(!t.IsHole(unsigned(x),unsigned(z))){auto a=point(x,z),b=point(x+1,z),c=point(x+1,z+1),d=point(x,z+1);triangle(a,d,c);triangle(a,c,b);}
        float boundary=std::min(nextX,nextZ);if((result&&closest<=boundary+1e-5f)||boundary>exit||(!sx&&!sz))break;
        if(nextX<nextZ){x+=sx;nextX+=deltaX;}else if(nextZ<nextX){z+=sz;nextZ+=deltaZ;}else{x+=sx;z+=sz;nextX+=deltaX;nextZ+=deltaZ;}
    }return result;
}
}
