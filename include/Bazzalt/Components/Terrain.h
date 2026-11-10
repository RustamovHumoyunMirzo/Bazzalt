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
enum class TerrainEdge { Left, Right, Front, Back };
enum class TerrainVolumeOperation { Add, Subtract };
struct TerrainVolume {
    std::string Name="Volume";
    bool Enabled=true;
    TerrainVolumeOperation Operation=TerrainVolumeOperation::Subtract;
    Vec3 Center{0,-2,0}, Radius{2,2,2};
};
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
    // Local offsets from each anchor. Empty arrays describe a linear spline.
    std::vector<Vec3> InTangents, OutTangents;
    float Width=1, Shoulder=1, Depth=0, Strength=1;
    bool Closed=false;
    bool GenerateSurface=false, SurfaceCollision=true;
    UUID Material{};
    Vec4 Color{1,1,1,1};
    float SurfaceOffset=.02f, TextureScale=1;
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
    // Ellipsoidal CSG volumes produce caves and overhangs in affected chunks.
    std::vector<TerrainVolume> Volumes;
    // Lower bound is the solid terrain base; both bounds limit CSG extraction.
    Vec2 VerticalRange{-20,20};
    std::uint32_t VerticalSamples=32;
    std::uint64_t Revision=0;
    static constexpr std::uint32_t MaxResolution=2049;
    static constexpr std::uint32_t MaxLayers=32;
    static constexpr std::size_t MaxSampleValues=64u*1024u*1024u;
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
    // Copies a neighbour's composed edge into this terrain, resampling as needed.
    // Transform maps neighbour-local coordinates into this terrain's space.
    void MatchEdge(const Terrain& neighbour,TerrainEdge edge,const Mat4& neighbourToLocal,std::uint32_t layer=0);
    void ApplyBoolean(const Terrain& other,const Mat4& otherToLocal,TerrainBoolean operation,std::uint32_t layer=0);
    void GenerateVegetation(std::uint32_t prototype);
    void PaintVegetation(std::uint32_t prototype,Vec2 center,float radius,std::uint32_t count,bool erase=false);
};
}
