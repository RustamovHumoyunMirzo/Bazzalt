#pragma once
#include <vector>
#include "Bazzalt/Component.h"
#include "Bazzalt/AudioClip.h"
#include "Bazzalt/AudioMixer.h"
#include "Bazzalt/Math.h"
namespace Bazzalt {
enum class AudioRolloff { Linear, Logarithmic, Custom };
// X: normalized distance between MinDistance and MaxDistance; Y: gain [0,1].
using AudioCurve=std::vector<Vec2>;
struct AudioSource : Component {
    AudioClip Clip{};
    float Volume=1, Pitch=1;
    bool Loop=false, PlayOnAwake=true;
    bool Streaming=false;
    float SpatialBlend=1, MinDistance=1, MaxDistance=100;
    AudioRolloff RolloffCurve=AudioRolloff::Logarithmic;
    AudioCurve CustomRolloff{{0,1},{1,0}};
    AudioBus OutputBus=AudioMixer::GetSFXBus();
    int Priority=128; // 0 highest, 256 lowest.
    float Pan=0, DopplerLevel=1;
    float ConeInnerAngle=360, ConeOuterAngle=360, ConeOuterGain=0;
    bool Muted=false, IgnoreListenerPause=false;
    [[nodiscard]] BAZZALT_API bool IsValid() const;
};
}
