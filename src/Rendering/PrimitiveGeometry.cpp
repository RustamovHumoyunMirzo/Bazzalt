#include "Rendering/PrimitiveGeometry.h"
#include <algorithm>
#include <cmath>
#include <math/mat3.h>
#include <math/quat.h>

namespace Bazzalt::Runtime { namespace {
constexpr float Pi=3.14159265358979323846f;
void Vertex(PrimitiveGeometry& g,Vec3 p,Vec3 n){n=n.Normalized();Vec3 helper=std::abs(n.Y)<.999f?Vec3{0,1,0}:Vec3{1,0,0};Vec3 tangent=Vec3::Cross(helper,n).Normalized(),bitangent=Vec3::Cross(n,tangent);auto q=filament::math::mat3f::packTangentFrame({filament::math::float3{tangent.X,tangent.Y,tangent.Z},filament::math::float3{bitangent.X,bitangent.Y,bitangent.Z},filament::math::float3{n.X,n.Y,n.Z}});g.Vertices.push_back({{p.X,p.Y,p.Z},{q.x,q.y,q.z,q.w},{p.X*.5f+.5f,p.Z*.5f+.5f}});}
void Quad(PrimitiveGeometry&g,Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 n){auto s=static_cast<std::uint32_t>(g.Vertices.size());Vertex(g,a,n);Vertex(g,b,n);Vertex(g,c,n);Vertex(g,d,n);const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};for(int i=0;i<4;++i){g.Vertices[s+i].UV[0]=uv[i][0];g.Vertices[s+i].UV[1]=uv[i][1];}g.Indices.insert(g.Indices.end(),{s,s+1,s+2,s,s+2,s+3});}
void GridSurface(PrimitiveGeometry&g,std::uint32_t segments,std::uint32_t rings,const auto& sample){
    segments=std::clamp(segments,3u,128u);rings=std::clamp(rings,2u,128u);
    for(std::uint32_t y=0;y<=rings;++y)for(std::uint32_t x=0;x<=segments;++x){auto [p,n]=sample(float(x)/segments,float(y)/rings);Vertex(g,p,n);g.Vertices.back().UV[0]=float(x)/segments;g.Vertices.back().UV[1]=float(y)/rings;}
    for(std::uint32_t y=0;y<rings;++y)for(std::uint32_t x=0;x<segments;++x){auto a=y*(segments+1)+x,b=a+1,c=a+segments+2,d=a+segments+1;g.Indices.insert(g.Indices.end(),{a,d,c,a,c,b});}
}
}
PrimitiveGeometry BuildPrimitiveGeometry(const PrimitiveObject&p){PrimitiveGeometry g;
    if(p.Shape==PrimitiveShape::Cube){Vec3 e{std::max(.001f,p.Size.X)*.5f,std::max(.001f,p.Size.Y)*.5f,std::max(.001f,p.Size.Z)*.5f};g.Extents=e;Quad(g,{-e.X,-e.Y,e.Z},{e.X,-e.Y,e.Z},{e.X,e.Y,e.Z},{-e.X,e.Y,e.Z},{0,0,1});Quad(g,{e.X,-e.Y,-e.Z},{-e.X,-e.Y,-e.Z},{-e.X,e.Y,-e.Z},{e.X,e.Y,-e.Z},{0,0,-1});Quad(g,{-e.X,e.Y,e.Z},{e.X,e.Y,e.Z},{e.X,e.Y,-e.Z},{-e.X,e.Y,-e.Z},{0,1,0});Quad(g,{-e.X,-e.Y,-e.Z},{e.X,-e.Y,-e.Z},{e.X,-e.Y,e.Z},{-e.X,-e.Y,e.Z},{0,-1,0});Quad(g,{e.X,-e.Y,e.Z},{e.X,-e.Y,-e.Z},{e.X,e.Y,-e.Z},{e.X,e.Y,e.Z},{1,0,0});Quad(g,{-e.X,-e.Y,-e.Z},{-e.X,-e.Y,e.Z},{-e.X,e.Y,e.Z},{-e.X,e.Y,-e.Z},{-1,0,0});}
    else if(p.Shape==PrimitiveShape::Plane){float x=std::max(.001f,p.Width)*.5f,z=std::max(.001f,p.Depth)*.5f;g.Extents={x,.001f,z};Quad(g,{-x,0,-z},{-x,0,z},{x,0,z},{x,0,-z},{0,1,0});}
    else if(p.Shape==PrimitiveShape::Sphere){float r=std::max(.001f,p.Radius);g.Extents={r,r,r};GridSurface(g,p.Segments,p.Rings,[=](float u,float v){float a=2*Pi*u,b=Pi*v-Pi*.5f;Vec3 n{std::cos(b)*std::cos(a),std::sin(b),std::cos(b)*std::sin(a)};return std::pair{n*r,n};});}
    else if(p.Shape==PrimitiveShape::Torus){float major=std::max(.001f,p.MajorRadius),minor=std::clamp(p.MinorRadius,.001f,major);g.Extents={major+minor,minor,major+minor};GridSurface(g,p.Segments,p.Rings,[=](float u,float v){float a=2*Pi*u,b=2*Pi*v;Vec3 n{std::cos(b)*std::cos(a),std::sin(b),std::cos(b)*std::sin(a)};return std::pair{Vec3{(major+minor*std::cos(b))*std::cos(a),minor*std::sin(b),(major+minor*std::cos(b))*std::sin(a)},n};});}
    else {float r=std::max(.001f,p.Radius),h=std::max(.001f,p.Height),half=h*.5f;g.Extents={r,half+(p.Shape==PrimitiveShape::Capsule?r:0),r};std::uint32_t rings=p.Shape==PrimitiveShape::Capsule?std::max(4u,p.Rings):2u;GridSurface(g,p.Segments,rings,[=](float u,float v){float a=2*Pi*u;if(p.Shape==PrimitiveShape::Capsule){float b=Pi*v-Pi*.5f;Vec3 n{std::cos(b)*std::cos(a),std::sin(b),std::cos(b)*std::sin(a)};return std::pair{Vec3{n.X*r,n.Y*r+(n.Y>=0?half:-half),n.Z*r},n};}float y=-half+h*v,rr=p.Shape==PrimitiveShape::Cone?r*(1-v):r;Vec3 n{std::cos(a),p.Shape==PrimitiveShape::Cone?r/h:0,std::sin(a)};n=n.Normalized();return std::pair{Vec3{rr*std::cos(a),y,rr*std::sin(a)},n};});
        // End caps for cylinders and cones.
        if(p.Shape!=PrimitiveShape::Capsule)for(int side=0;side<2;++side){float y=side?half:-half;Vec3 n{0,side?1.0f:-1.0f,0};auto center=static_cast<std::uint32_t>(g.Vertices.size());Vertex(g,{0,y,0},n);for(std::uint32_t i=0;i<=std::clamp(p.Segments,3u,128u);++i){float a=2*Pi*i/std::clamp(p.Segments,3u,128u),rr=(p.Shape==PrimitiveShape::Cone&&side)?0:r;Vertex(g,{rr*std::cos(a),y,rr*std::sin(a)},n);g.Vertices.back().UV[0]=.5f+rr*std::cos(a)/(2*r);g.Vertices.back().UV[1]=.5f+rr*std::sin(a)/(2*r);if(i){if(side)g.Indices.insert(g.Indices.end(),{center,center+i+1,center+i});else g.Indices.insert(g.Indices.end(),{center,center+i,center+i+1});}}}}
    return g;}
}
