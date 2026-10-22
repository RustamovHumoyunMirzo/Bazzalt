#pragma once
#include <cstdint>
#include <string>
#include "Bazzalt/Entity.h"
#include "Bazzalt/Components/AudioSource.h"
#include "Bazzalt/Components/AudioListener.h"
namespace Bazzalt {
enum class AudioPlaybackState { Stopped, Playing, Paused };
class BAZZALT_API AudioVoice final {
public:
    AudioVoice()=default;
    explicit AudioVoice(std::uint64_t id):m_id(id){}
    std::uint64_t GetId() const { return m_id; }
    bool IsValid() const;
    AudioPlaybackState GetState() const;
    bool Pause() const;
    bool Resume() const;
    bool Stop() const;
    bool Seek(double seconds) const;
    double GetTime() const;
    double GetDuration() const;
    bool SetVolume(float volume) const;
    bool SetPitch(float pitch) const;
    bool FadeTo(float volume, double seconds) const;
private:
    std::uint64_t m_id=0;
};
class BAZZALT_API Audio final {
public:
    static AudioVoice Play(Entity source); // Restarts this source, never duplicates it.
    static AudioVoice GetVoice(Entity source);
    static Entity GetActiveListener();
    static AudioVoice PlayOneShot(AudioClip clip, Vec3 position={}, float volume=1, AudioBus bus={}, float spatialBlend=0);
    static void StopAll();
    static void PauseAll(bool paused);
    static bool SetMaxVoices(std::uint32_t count); // [1,1024], lower-priority voices are stolen.
    static std::uint32_t GetMaxVoices();
    static std::uint32_t GetActiveVoiceCount();
    static bool IsAvailable();
    static std::string GetLastError();
};
}
