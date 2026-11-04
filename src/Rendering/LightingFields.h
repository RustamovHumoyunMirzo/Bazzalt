#pragma once
#include "Bazzalt/Components/LightProbe.h"
#include <optional>
#include "Bazzalt/Components/Lightmap.h"
#include "Bazzalt/Components/ReflectionProbe.h"
namespace Bazzalt {class Scene;}
namespace Bazzalt::Runtime {
template<class T,class F>void VisitLightProbe(T& v,F f){
    f("Radius",v.Radius);f("Intensity",v.Intensity);f("Baked",v.Baked);
    f("SH0",v.Coefficients[0]);f("SH1",v.Coefficients[1]);f("SH2",v.Coefficients[2]);
    f("SH3",v.Coefficients[3]);f("SH4",v.Coefficients[4]);f("SH5",v.Coefficients[5]);
    f("SH6",v.Coefficients[6]);f("SH7",v.Coefficients[7]);f("SH8",v.Coefficients[8]);
}
inline std::array<float,9> LightingBasis(Vec3 n){return {.282094792f,.488602512f*n.Y,.488602512f*n.Z,.488602512f*n.X,
    1.092548431f*n.X*n.Y,1.092548431f*n.Y*n.Z,.315391565f*(3*n.Z*n.Z-1),1.092548431f*n.X*n.Z,.546274215f*(n.X*n.X-n.Y*n.Y)};}
template<class T,class F>void VisitLightmap(T& v,F f){f("Resolution",v.Resolution);f("Padding",v.Padding);f("Baked",v.Baked);f("BakedResolution",v.BakedResolution);f("BakedPadding",v.BakedPadding);f("ChartGrid",v.ChartGrid);f("Geometry",v.Geometry);f("Texels",v.Texels);f("TransformSignature",v.TransformSignature);}
template<class T,class F>void VisitReflectionProbe(T& v,F f){f("Resolution",v.Resolution);f("Radius",v.Radius);f("Intensity",v.Intensity);f("BlendDistance",v.BlendDistance);f("BoxProjection",v.BoxProjection);f("BoxSize",v.BoxSize);f("Baked",v.Baked);f("CapturePosition",v.CapturePosition);f("BakedResolution",v.BakedResolution);f("Radiance",v.Radiance);}
std::uint64_t LightingRevision(Scene& scene);
Vec3 LightingCubeDirection(unsigned face,float u,float v);
std::string EncodeLightingVectors(const std::vector<Vec3>& values);
bool DecodeLightingVectors(const std::string& text,std::vector<Vec3>& values);
std::string LightingTransformSignature(const Mat4& world);
std::optional<std::pair<Vec3,float>> SampleLightmapIrradiance(const Lightmap& map,Vec3 position,Vec3 normal);
Vec3 SampleReflectionRadiance(const ReflectionProbe& probe,Vec3 direction,float roughness);
Vec3 ReflectionDirection(const ReflectionProbe& probe,Vec3 position,Vec3 direction);
}
