#pragma once
#include <string>
#include <vector>
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
struct AudioBusSettings {
    UUID Id{}, Parent{};
    std::string Name="Bus";
    float Volume=1, Pitch=1;
    bool Muted=false, Solo=false, Paused=false;
};
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
};
}
