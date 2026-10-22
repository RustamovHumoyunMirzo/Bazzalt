#pragma once
#include "Bazzalt/Components/Camera.h"
#include <filament/View.h>
namespace Bazzalt::Runtime {
struct AntiAliasingSettings {bool Fxaa=false,Temporal=false;std::uint8_t Samples=1;};
inline constexpr AntiAliasingSettings ResolveAntiAliasing(AntiAliasing mode){
    switch(mode){
        case AntiAliasing::FXAA:return {true,false,1};
        case AntiAliasing::TAA:return {false,true,1};
        case AntiAliasing::MSAA2x:return {false,false,2};
        case AntiAliasing::MSAA4x:return {false,false,4};
        case AntiAliasing::TAAFXAA:return {true,true,1};
        case AntiAliasing::MSAA2xFXAA:return {true,false,2};
        case AntiAliasing::MSAA4xFXAA:return {true,false,4};
        default:return {};
    }
}
inline void ApplyAntiAliasing(filament::View& view,AntiAliasing mode,bool enabled,std::uint8_t destinationSamples=1){
    const auto options=ResolveAntiAliasing(mode);
    view.setAntiAliasing(enabled&&options.Fxaa?filament::View::AntiAliasing::FXAA:filament::View::AntiAliasing::NONE);
    view.setTemporalAntiAliasingOptions({.enabled=enabled&&options.Temporal});
    // The integrated view-MSAA path is verified up to 4x. Do not reintroduce
    // unsupported 8x view sampling through a low-level texture descriptor.
    const auto minimum=static_cast<std::uint8_t>(destinationSamples>4?4:destinationSamples);
    const auto samples=options.Samples>1&&minimum>options.Samples?minimum:options.Samples;
    view.setMultiSampleAntiAliasingOptions({.enabled=enabled&&samples>1,.sampleCount=static_cast<std::uint8_t>(enabled?samples:1)});
}
}
