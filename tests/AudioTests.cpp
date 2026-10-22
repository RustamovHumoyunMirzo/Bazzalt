#include "Bazzalt/Audio.h"
#include "Runtime/Engine.h"
#include "Audio/AudioRuntime.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <thread>
#include <chrono>
using namespace Bazzalt;
namespace {
void Wave(const std::filesystem::path& path){
    std::ofstream f(path,std::ios::binary);const auto u16=[&](unsigned v){char b[]={char(v),char(v>>8)};f.write(b,2);};const auto u32=[&](unsigned v){char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(b,4);};
    f.write("RIFF",4);u32(36+48000*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(48000);u32(96000);u16(2);u16(16);f.write("data",4);u32(96000);
    for(int i=0;i<48000;++i)u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::sin(i*2*Pi*440/48000)*10000)));
}
float Render(Runtime::AudioRuntime& runtime,int frames=4096){std::vector<float> pcm(frames*2);assert(runtime.ReadFrames(pcm.data(),frames));float peak=0;for(auto v:pcm){assert(std::isfinite(v));peak=std::max(peak,std::abs(v));}return peak;}
}
int main(){
    AudioSource invalid;assert(invalid.IsValid());invalid.Volume=2;assert(!invalid.IsValid());invalid.Volume=1;invalid.CustomRolloff={{0,1},{0,.5f},{1,0}};assert(!invalid.IsValid());
    assert(!Audio::PlayOneShot({}).IsValid());assert(!AudioVoice{}.Pause());
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-audio-"+UUID::Generate().ToString())/std::filesystem::u8path("Документы");std::filesystem::create_directories(folder/"Assets");
    struct Cleanup{std::filesystem::path Folder;~Cleanup(){std::error_code error;std::filesystem::remove_all(Folder.parent_path(),error);}} cleanup{folder};
    auto source=folder/"Assets"/std::filesystem::u8path("Музыка.wav");Wave(source);
    auto project=folder/"Test.bproject";{std::ofstream f(project);f<<"FormatVersion: 1\nProjectUUID: \""<<UUID::Generate().ToString()<<"\"\nName: Audio\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties:\n";}
    Runtime::Engine engine;assert(engine.LoadProject(project,false));auto asset=AssetManager::GetAsset(source);assert(asset&&asset->Importer=="Bazzalt.AudioClip");auto clip=AudioClip::Load(asset->Id);assert(clip.IsValid());auto info=clip.GetInfo();assert(info.FrameCount==48000&&info.Channels==1&&info.SampleRate==48000&&info.Duration==1);
    auto& scene=engine.GetScene();auto listener=scene.CreateEntity("Listener");listener.AddComponent<AudioListener>();auto entity=scene.CreateEntity("Source");auto& settings=entity.AddComponent<AudioSource>();settings.Clip=clip;settings.Loop=true;settings.SpatialBlend=0;settings.CustomRolloff={{0,1},{.25f,.8f},{1,0}};
    auto file=folder/"Scene.bscene";assert(engine.SaveScene(file));auto restored=engine.LoadSceneAsset(file);assert(restored);const auto& restoredSource=restored->GetEntity(entity.GetUUID()).GetComponent<AudioSource>();assert(restoredSource.Clip.GetAssetUUID()==asset->Id&&restoredSource.CustomRolloff.size()==3&&restored->GetEntity(listener.GetUUID()).HasComponent<AudioListener>());
    auto& runtime=engine.GetAudioRuntime();runtime.Start(scene,true);assert(Audio::IsAvailable());auto voice=Audio::GetVoice(entity);assert(voice.IsValid());
    float peak=0;for(int i=0;i<100&&peak<.01f;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));runtime.Update(.01f);peak=Render(runtime);}assert(peak>.1f);
    assert(voice.Pause());assert(voice.GetState()==AudioPlaybackState::Paused);Render(runtime);assert(Render(runtime)<.0001f);assert(voice.Resume());runtime.Update(.01f);assert(Render(runtime)>.1f);
    const auto sfx=AudioMixer::GetSFXBus();assert(sfx.IsValid());assert(sfx.SetMuted(true));Render(runtime);assert(Render(runtime)<.0001f);assert(sfx.SetMuted(false));assert(Render(runtime)>.1f);
    listener.GetComponent<AudioListener>().Enabled=false;runtime.Update(.01f);assert(!Audio::GetActiveListener());Render(runtime);assert(Render(runtime)<.0001f);listener.GetComponent<AudioListener>().Enabled=true;
    settings.SpatialBlend=1;settings.MinDistance=1;settings.MaxDistance=10;settings.RolloffCurve=AudioRolloff::Linear;entity.GetComponent<Transform>().Position={0,0,-20};runtime.Update(.01f);Render(runtime);assert(Render(runtime)<.0001f);
    entity.GetComponent<Transform>().Position={0,0,-1};runtime.Update(.01f);assert(Render(runtime)>.1f);
    settings.Muted=true;runtime.Update(.01f);Render(runtime);assert(Render(runtime)<.0001f);settings.Muted=false;
    auto custom=AudioMixer::CreateBus("Impacts",sfx);assert(custom.IsValid());auto busSettings=sfx.GetSettings();busSettings.Parent=custom.GetUUID();assert(!sfx.SetSettings(busSettings));assert(!AudioMixer::RemoveBus({}));assert(AudioMixer::RemoveBus(custom));
    auto buses=AudioMixer::GetBuses();std::string error;auto mixerPath=folder/"Assets"/std::filesystem::u8path("Микшер.bmixer");assert(Runtime::SaveAudioMixer(mixerPath,buses,error));std::vector<AudioBusSettings> parsed;assert(Runtime::ReadAudioMixer(mixerPath,parsed,error)&&parsed.size()==3);assert(engine.RefreshTextureAsset(mixerPath));auto mixerAsset=AssetManager::GetAsset(mixerPath);assert(mixerAsset&&mixerAsset->Importer=="Bazzalt.AudioMixer");assert(AudioMixer::Load(mixerAsset->Id));
    const auto id=voice.GetId();assert(Audio::SetMaxVoices(1));auto one=Audio::PlayOneShot(clip);assert(one.IsValid()&&!voice.IsValid());assert(one.GetId()!=id);assert(!Audio::SetMaxVoices(0));
    Audio::PauseAll(true);assert(one.GetState()==AudioPlaybackState::Paused);Audio::PauseAll(false);assert(one.FadeTo(0,.1));runtime.Update(.2f);Render(runtime);assert(Render(runtime)<.0001f);
    Audio::StopAll();assert(!one.IsValid()&&Audio::GetActiveVoiceCount()==0);settings.Enabled=false;runtime.Update(.01f);settings.Enabled=true;runtime.Update(.01f);assert(Audio::GetVoice(entity).IsValid());
    scene.DestroyEntity(entity);runtime.Update(.01f);assert(Audio::GetActiveVoiceCount()==0);
    for(const auto name:{"16-44100-stereo.flac","48000-stereo.ogg"}){
        auto imported=folder/"Assets"/std::filesystem::u8path("Тест-");imported+=std::filesystem::path(name);
        std::filesystem::copy_file(std::filesystem::path(BAZZALT_TEST_AUDIO_DIR)/name,imported);
        assert(engine.RefreshTextureAsset(imported));auto compressed=AssetManager::GetAsset(imported);assert(compressed);auto decoded=AudioClip::Load(compressed->Id).GetInfo();assert(decoded.Channels==2&&decoded.FrameCount>0);
        auto speaker=scene.CreateEntity(name);auto& stream=speaker.AddComponent<AudioSource>();stream.Clip=AudioClip::Load(compressed->Id);stream.Streaming=true;stream.SpatialBlend=0;stream.Loop=true;stream.PlayOnAwake=false;
        auto playback=Audio::Play(speaker);assert(playback.IsValid());float output=0;for(int i=0;i<200&&output<.001f;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));output=Render(runtime);}assert(output>.001f);assert(playback.GetDuration()>0);assert(playback.Seek(.5));playback.Stop();scene.DestroyEntity(speaker);
    }
    runtime.Stop();assert(!Audio::IsAvailable());
    {std::ofstream f(folder/"Broken.wav");f<<"not audio";}assert(!Runtime::InspectAudioFile(folder/"Broken.wav",info,error));
}
