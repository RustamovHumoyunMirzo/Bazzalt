#pragma once
#include <filesystem>
#include <memory>
#include "Bazzalt/Audio.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Serialization.h"
namespace Bazzalt::Runtime {
bool InspectAudioFile(const std::filesystem::path& path, AudioClipInfo& info, std::string& error);
bool ReadAudioMixer(const std::filesystem::path& path, std::vector<AudioBusSettings>& buses, std::string& error);
bool SaveAudioMixer(const std::filesystem::path& path, const std::vector<AudioBusSettings>& buses, std::string& error);
class AudioRuntime final {
public:
    AudioRuntime();
    ~AudioRuntime();
    void Start(Scene& scene, bool noDevice=false);
    void Update(float deltaTime);
    void SetPaused(bool paused);
    void SetHostPaused(bool paused);
    bool ReadFrames(float* stereo, std::uint32_t frames); // Offline/device-free verification only.
    void Stop();
    AudioVoice Preview(UUID clip);
    void StopPreview();
    static AudioRuntime* Current();
    struct Impl;
    std::unique_ptr<Impl> Data;
};
void RegisterAudioComponents(ComponentSerializationRegistry& registry);
}
