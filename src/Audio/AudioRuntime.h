#pragma once
#include <filesystem>
#include <memory>
#include "Bazzalt/Audio.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Serialization.h"
namespace Bazzalt::Runtime {
bool InspectAudioFile(const std::filesystem::path& path, AudioClipInfo& info, std::string& error);
std::vector<std::array<float,2>> BuildAudioWaveform(const std::filesystem::path& path,unsigned bins);
bool ReadAudioMixer(const std::filesystem::path& path, std::vector<AudioBusSettings>& buses, std::string& error, std::string* editorState=nullptr);
bool SaveAudioMixer(const std::filesystem::path& path, const std::vector<AudioBusSettings>& buses, std::string& error, const std::string& editorState="");
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
    AudioVoice Preview(UUID clip, AudioBus bus={});
    void StopPreview();
    UUID GetMixerAsset() const;
    static AudioRuntime* Current();
    struct Impl;
    std::unique_ptr<Impl> Data;
};
void RegisterAudioComponents(ComponentSerializationRegistry& registry);
}
