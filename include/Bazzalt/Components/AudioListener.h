#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
struct AudioListener : Component {
    float MasterVolume=1;
    bool Muted=false, Paused=false;
    int Priority=0; // Higher wins; UUID breaks ties deterministically.
    float DopplerFactor=1, SpeedOfSound=343.3f;
    UUID MixerAsset{}; // Optional .bmixer asset; selected listener owns routing.
};
}
