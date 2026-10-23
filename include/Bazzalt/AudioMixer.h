#pragma once
#include <string>
#include <vector>
#include <map>
#include <array>
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
enum class AudioEffectType { Lowpass, Highpass, Echo, Distortion, Reverb, Equalizer };
struct AudioEffectSettings {
    UUID Id=UUID::Generate();
    AudioEffectType Type=AudioEffectType::Lowpass;
    bool Enabled=true;
    // Missing parameters use the documented effect defaults.
    std::map<std::string,float> Parameters;
    bool operator==(const AudioEffectSettings&) const=default;
};
struct AudioBusSettings {
    UUID Id{}, Parent{};
    std::string Name="Bus";
    float Volume=1, Pitch=1;
    bool Muted=false, Solo=false, Paused=false, Bypass=false;
    std::vector<AudioEffectSettings> Effects;
};
struct AudioMixerSnapshot { std::vector<AudioBusSettings> Buses; };
// UUID zero is the master bus, not a missing bus.
class BAZZALT_API AudioBus final {
public:
    AudioBus()=default;
    static AudioBus Load(UUID id) { AudioBus result;result.m_id=id;return result; }
    UUID GetUUID() const { return m_id; }
    bool IsValid() const;
    AudioBusSettings GetSettings() const;
    bool SetSettings(const AudioBusSettings& settings) const;
    bool SetVolume(float volume) const;
    bool SetVolumeDecibels(float decibels) const;
    bool SetPitch(float pitch) const;
    bool SetMuted(bool muted) const;
    bool SetSolo(bool solo) const;
    bool SetPaused(bool paused) const;
    bool SetBypass(bool bypass) const;
    bool SetEffects(const std::vector<AudioEffectSettings>& effects) const;
    std::array<float,2> GetPeakLevels() const; // Linear post-effect stereo peaks since last poll.
private:
    UUID m_id{};
};
class BAZZALT_API AudioMixer final {
public:
    static AudioBus GetMasterBus();
    static AudioBus GetMusicBus();
    static AudioBus GetSFXBus();
    static std::vector<AudioBusSettings> GetBuses();
    static AudioBus CreateBus(const std::string& name, AudioBus parent={}, UUID id=UUID::Generate());
    static bool RemoveBus(AudioBus bus); // Children and sources are rerouted to its parent.
    static bool Load(UUID mixerAsset); // .bmixer YAML; transactional replacement.
    static bool Validate(const std::vector<AudioBusSettings>& buses, std::string& error);
    static AudioMixerSnapshot CaptureSnapshot();
    static bool ApplySnapshot(const AudioMixerSnapshot& snapshot); // Mix state only; retains current routing.
};
}
