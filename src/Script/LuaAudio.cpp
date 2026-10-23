#include "Script/LuaBindings.h"
#include "Bazzalt/Audio.h"
namespace Bazzalt::Runtime {
namespace {
template<class T> void AudioOperations(sol::table api,const char* name){
    sol::table operations=api["_ComponentOperations"];auto type=operations.create_named(name);
    type["Has"]=[](Entity e){return e.HasComponent<T>();};
    type["Get"]=[](Entity e){return T(e.GetComponent<T>());};
    type["Add"]=[](Entity e){return T(e.AddComponent<T>());};
    type["Set"]=[](Entity e,T v){e.GetComponent<T>()=std::move(v);};
    type["Remove"]=[](Entity e){e.RemoveComponent<T>();};
    type["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<T>();};
    type["SetEnabled"]=[](Entity e,bool v){e.SetComponentEnabled<T>(v);};
    type["Inherited"]=[](Entity e)->std::optional<T>{auto v=e.GetScene()->TryGetInheritedComponent<T>(e);return v?std::optional<T>(*v):std::nullopt;};
}
}
void BindLuaAudio(sol::table api){
    api.new_enum("AudioEffectType","Lowpass",AudioEffectType::Lowpass,"Highpass",AudioEffectType::Highpass,"Echo",AudioEffectType::Echo,"Distortion",AudioEffectType::Distortion,"Reverb",AudioEffectType::Reverb,"Equalizer",AudioEffectType::Equalizer);
    auto effect=api.new_usertype<AudioEffectSettings>("AudioEffectSettings",sol::constructors<AudioEffectSettings()>());
    effect["Id"]=&AudioEffectSettings::Id;effect["Type"]=&AudioEffectSettings::Type;effect["Enabled"]=&AudioEffectSettings::Enabled;
    effect["Parameters"]=sol::property([](const AudioEffectSettings& e){return sol::as_table(e.Parameters);},[](AudioEffectSettings& e,sol::as_table_t<std::map<std::string,float>> v){e.Parameters=v.value();});
    api.new_enum("AudioRolloff","Linear",AudioRolloff::Linear,"Logarithmic",AudioRolloff::Logarithmic,"Custom",AudioRolloff::Custom);
    api.new_enum("AudioPlaybackState","Stopped",AudioPlaybackState::Stopped,"Playing",AudioPlaybackState::Playing,"Paused",AudioPlaybackState::Paused);
    auto info=api.new_usertype<AudioClipInfo>("AudioClipInfo",sol::constructors<AudioClipInfo()>());
    info["Duration"]=&AudioClipInfo::Duration;info["FrameCount"]=&AudioClipInfo::FrameCount;info["Channels"]=&AudioClipInfo::Channels;info["SampleRate"]=&AudioClipInfo::SampleRate;
    auto clip=api.new_usertype<AudioClip>("AudioClip",sol::constructors<AudioClip()>());
    clip["Load"]=&AudioClip::Load;clip["GetAssetUUID"]=&AudioClip::GetAssetUUID;clip["IsValid"]=&AudioClip::IsValid;clip["GetInfo"]=&AudioClip::GetInfo;
    auto settings=api.new_usertype<AudioBusSettings>("AudioBusSettings",sol::constructors<AudioBusSettings()>());
#define AUDIO_FIELD(T,F) settings[#F]=&T::F;
    AUDIO_FIELD(AudioBusSettings,Id) AUDIO_FIELD(AudioBusSettings,Parent) AUDIO_FIELD(AudioBusSettings,Name) AUDIO_FIELD(AudioBusSettings,Volume) AUDIO_FIELD(AudioBusSettings,Pitch) AUDIO_FIELD(AudioBusSettings,Muted) AUDIO_FIELD(AudioBusSettings,Solo) AUDIO_FIELD(AudioBusSettings,Paused)
#undef AUDIO_FIELD
    settings["Bypass"]=&AudioBusSettings::Bypass;
    settings["Effects"]=sol::property([](const AudioBusSettings& s){return sol::as_table(s.Effects);},[](AudioBusSettings& s,sol::as_table_t<std::vector<AudioEffectSettings>> v){s.Effects=v.value();});
    auto bus=api.new_usertype<AudioBus>("AudioBus",sol::constructors<AudioBus()>());
    bus["Load"]=&AudioBus::Load;bus["GetUUID"]=&AudioBus::GetUUID;bus["IsValid"]=&AudioBus::IsValid;bus["GetSettings"]=&AudioBus::GetSettings;bus["SetSettings"]=&AudioBus::SetSettings;
    bus["SetVolume"]=&AudioBus::SetVolume;bus["SetVolumeDecibels"]=&AudioBus::SetVolumeDecibels;bus["SetPitch"]=&AudioBus::SetPitch;bus["SetMuted"]=&AudioBus::SetMuted;bus["SetSolo"]=&AudioBus::SetSolo;bus["SetPaused"]=&AudioBus::SetPaused;
    bus["SetBypass"]=&AudioBus::SetBypass;bus["SetEffects"]=[](AudioBus b,sol::as_table_t<std::vector<AudioEffectSettings>> effects){return b.SetEffects(effects.value());};
    bus["GetPeakLevels"]=[](AudioBus b){auto peak=b.GetPeakLevels();return sol::as_table(std::vector<float>{peak[0],peak[1]});};
    auto mixer=api.create_named("AudioMixer");
    auto snapshot=api.new_usertype<AudioMixerSnapshot>("AudioMixerSnapshot",sol::constructors<AudioMixerSnapshot()>());
    snapshot["Buses"]=sol::property([](const AudioMixerSnapshot& s){return sol::as_table(s.Buses);},[](AudioMixerSnapshot& s,sol::as_table_t<std::vector<AudioBusSettings>> v){s.Buses=v.value();});
    mixer["CaptureSnapshot"]=&AudioMixer::CaptureSnapshot;mixer["ApplySnapshot"]=&AudioMixer::ApplySnapshot;
    mixer["GetMasterBus"]=&AudioMixer::GetMasterBus;mixer["GetMusicBus"]=&AudioMixer::GetMusicBus;mixer["GetSFXBus"]=&AudioMixer::GetSFXBus;
    mixer["GetBuses"]=[](){return sol::as_table(AudioMixer::GetBuses());};
    mixer["CreateBus"]=[](const std::string& name,sol::optional<AudioBus> parent,sol::optional<UUID> id){return AudioMixer::CreateBus(name,parent.value_or(AudioBus{}),id.value_or(UUID::Generate()));};
    mixer["RemoveBus"]=&AudioMixer::RemoveBus;mixer["Load"]=&AudioMixer::Load;
    mixer["Validate"]=[](sol::as_table_t<std::vector<AudioBusSettings>> buses){std::string error;bool valid=AudioMixer::Validate(buses.value(),error);return std::make_tuple(valid,error);};
    auto source=api.new_usertype<AudioSource>("AudioSource",sol::constructors<AudioSource()>(),sol::base_classes,sol::bases<Component>());
    source["Clip"]=&AudioSource::Clip;source["Volume"]=&AudioSource::Volume;source["Pitch"]=&AudioSource::Pitch;
    source["Loop"]=&AudioSource::Loop;source["PlayOnAwake"]=&AudioSource::PlayOnAwake;source["Streaming"]=&AudioSource::Streaming;
    source["SpatialBlend"]=&AudioSource::SpatialBlend;source["MinDistance"]=&AudioSource::MinDistance;source["MaxDistance"]=&AudioSource::MaxDistance;
    source["RolloffCurve"]=&AudioSource::RolloffCurve;source["OutputBus"]=&AudioSource::OutputBus;source["Priority"]=&AudioSource::Priority;
    source["Pan"]=&AudioSource::Pan;source["DopplerLevel"]=&AudioSource::DopplerLevel;source["ConeInnerAngle"]=&AudioSource::ConeInnerAngle;
    source["ConeOuterAngle"]=&AudioSource::ConeOuterAngle;source["ConeOuterGain"]=&AudioSource::ConeOuterGain;source["Muted"]=&AudioSource::Muted;source["IgnoreListenerPause"]=&AudioSource::IgnoreListenerPause;
    source["CustomRolloff"]=sol::property([](const AudioSource& s){return sol::as_table(s.CustomRolloff);},[](AudioSource& s,sol::as_table_t<AudioCurve> points){s.CustomRolloff=points.value();});
    source["IsValid"]=&AudioSource::IsValid;
    auto listener=api.new_usertype<AudioListener>("AudioListener",sol::constructors<AudioListener()>(),sol::base_classes,sol::bases<Component>());
    listener["MasterVolume"]=&AudioListener::MasterVolume;listener["Muted"]=&AudioListener::Muted;listener["Paused"]=&AudioListener::Paused;
    listener["Priority"]=&AudioListener::Priority;listener["DopplerFactor"]=&AudioListener::DopplerFactor;listener["SpeedOfSound"]=&AudioListener::SpeedOfSound;listener["MixerAsset"]=&AudioListener::MixerAsset;
    AudioOperations<AudioSource>(api,"AudioSource");AudioOperations<AudioListener>(api,"AudioListener");
    auto voice=api.new_usertype<AudioVoice>("AudioVoice",sol::constructors<AudioVoice()>());
#define AUDIO_METHOD(F) voice[#F]=&AudioVoice::F;
    AUDIO_METHOD(GetId) AUDIO_METHOD(IsValid) AUDIO_METHOD(GetState) AUDIO_METHOD(Pause) AUDIO_METHOD(Resume) AUDIO_METHOD(Stop) AUDIO_METHOD(Seek) AUDIO_METHOD(GetTime) AUDIO_METHOD(GetDuration) AUDIO_METHOD(SetVolume) AUDIO_METHOD(SetPitch) AUDIO_METHOD(FadeTo)
#undef AUDIO_METHOD
    auto audio=api.create_named("Audio");audio["Play"]=&Audio::Play;audio["GetVoice"]=&Audio::GetVoice;audio["GetActiveListener"]=&Audio::GetActiveListener;
    audio["PlayOneShot"]=[](AudioClip c,sol::optional<Vec3> p,sol::optional<float> v,sol::optional<AudioBus> b,sol::optional<float> blend){return Audio::PlayOneShot(c,p.value_or(Vec3{}),v.value_or(1),b.value_or(AudioBus{}),blend.value_or(0));};
    audio["StopAll"]=&Audio::StopAll;audio["PauseAll"]=&Audio::PauseAll;audio["SetMaxVoices"]=&Audio::SetMaxVoices;audio["GetMaxVoices"]=&Audio::GetMaxVoices;audio["GetActiveVoiceCount"]=&Audio::GetActiveVoiceCount;audio["IsAvailable"]=&Audio::IsAvailable;audio["GetLastError"]=&Audio::GetLastError;
}
}
