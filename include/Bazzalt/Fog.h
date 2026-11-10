#pragma once
#include "Bazzalt/Math.h"
namespace Bazzalt {
enum class FogMode { Exponential, Height, Volumetric };
struct FogSettings {
    bool Enabled=true, AffectSky=false;
    FogMode Mode=FogMode::Height;
    Vec3 Color{.55f,.62f,.7f};
    float Density=.002f, StartDistance=10, MaxDistance=1000, MaximumOpacity=1;
    float Height=0, HeightFalloff=.05f;
    float NoiseAmount=.5f, NoiseScale=.05f, Anisotropy=.2f;
    Vec3 Wind{};
    unsigned Steps=32;
    bool IsValid() const {
        const float values[]={Color.X,Color.Y,Color.Z,Density,StartDistance,MaxDistance,MaximumOpacity,Height,HeightFalloff,NoiseAmount,NoiseScale,Anisotropy,Wind.X,Wind.Y,Wind.Z};
        for(float value:values)if(!std::isfinite(value))return false;
        return unsigned(Mode)<=2&&Color.X>=0&&Color.Y>=0&&Color.Z>=0&&Density>=0&&Density<=10&&StartDistance>=0&&MaxDistance>StartDistance&&MaximumOpacity>=0&&MaximumOpacity<=1&&HeightFalloff>=0&&HeightFalloff<=10&&NoiseAmount>=0&&NoiseAmount<=1&&NoiseScale>0&&Steps>=4&&Steps<=128&&Anisotropy>-.99f&&Anisotropy<.99f;
    }
};
}
