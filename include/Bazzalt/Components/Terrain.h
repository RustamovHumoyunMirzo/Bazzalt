#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Bazzalt/Export.h"
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {
enum class TerrainBlend { Add, Replace, Maximum, Minimum };
enum class TerrainTool { Raise, Lower, Smooth, Flatten, Paint, ErasePaint, Hole, FillHole };
enum class TerrainBoolean { Union, Intersection, Subtract };
struct TerrainLayer {
    std::string Name="Layer";
    bool Enabled=true;
    float Opacity=1;
    TerrainBlend Blend=TerrainBlend::Add;
    UUID Heightmap{}, Splatmap{}, Material{};
    Vec4 Color{1,1,1,1};
    float Tiling=1;
    // Metres and normalized weights, row-major. Empty means zero height/weight.
    std::vector<float> Heights, Weights;
};
struct TerrainBrush {
    float Radius=1, Strength=1, Falloff=.5f, TargetHeight=0;
    std::uint32_t Layer=0;
    // Optional normalized stamp. Asset decoding belongs to the importing host.
    std::uint32_t StampWidth=0, StampHeight=0;
    std::vector<float> Stamp;
};
struct TerrainVegetation {
    std::string Name="Vegetation";
    UUID Model{}, Material{};
    bool Enabled=true, AlignToNormal=true;
    float Density=.1f, MinHeight=-10000, MaxHeight=10000, MinSlope=0, MaxSlope=45;
    float MinScale=.8f, MaxScale=1.2f, Spacing=.5f;
    std::uint32_t Seed=1, MaxInstances=1000;
};
struct TerrainInstance {
    std::uint32_t Prototype=0;
    Vec3 Position{};
    Quaternion Rotation{};
    float Scale=1;
};
struct TerrainSpline {
    std::string Name="Road";
    std::vector<Vec3> Points;
    float Width=1, Shoulder=1, Depth=0, Strength=1;
    bool Closed=false;
};
// Heightfield surface in local XZ; dimensions are metres. No renderer types escape.
// Mutating sample arrays directly requires MarkDirty(). Prefer the validated APIs.
struct BAZZALT_API Terrain : Component {
    Terrain();
    Vec2 Size{10,10};
    std::uint32_t Resolution=65, ChunkSize=16, MaxLOD=3;
    float LODDistance=30, SkirtDepth=2;
    bool Visible=true, CastShadows=true, ReceiveShadows=true, ShowGrid=true;
    std::uint8_t LayerMask=0x7f;
    std::vector<TerrainLayer> Layers{TerrainLayer{}};
    std::vector<std::uint8_t> Holes;
    std::vector<TerrainVegetation> Vegetation;
    std::vector<TerrainInstance> Instances;
    std::vector<TerrainSpline> Splines;
    std::uint64_t Revision=0;
    static constexpr std::uint32_t MaxResolution=257;
    static constexpr std::uint32_t MaxLayers=8;
    static constexpr std::uint32_t MaxInstances=10000;

    bool IsValid() const;
    void MarkDirty();
    void Resize(std::uint32_t resolution); // Resamples layers and holes, preserving extent.
    std::uint32_t AddLayer(std::string name="Layer");
    void RemoveLayer(std::uint32_t index);
    float GetHeight(float x,float z) const;
    Vec3 GetNormal(float x,float z) const;
    float GetSlope(float x,float z) const; // Degrees from up.
    bool IsHole(std::uint32_t x,std::uint32_t z) const;
    void ApplyBrush(TerrainTool tool,Vec2 center,const TerrainBrush& brush,float seconds=1);
    void ImportHeightmap(std::uint32_t layer,const std::vector<float>& samples,std::uint32_t width,std::uint32_t height,float scale=1);
    void ImportSplatmap(std::uint32_t layer,const std::vector<float>& samples,std::uint32_t width,std::uint32_t height);
    void ApplySpline(std::uint32_t spline,std::uint32_t layer=0);
    void ApplyBoolean(const Terrain& other,const Mat4& otherToLocal,TerrainBoolean operation,std::uint32_t layer=0);
    void GenerateVegetation(std::uint32_t prototype);
    void PaintVegetation(std::uint32_t prototype,Vec2 center,float radius,std::uint32_t count,bool erase=false);
};
}
