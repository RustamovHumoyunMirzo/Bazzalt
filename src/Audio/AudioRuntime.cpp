#include "Audio/AudioRuntime.h"
#include "Utils/NumberParsing.h"
#include "Bazzalt/Detail/SubsystemAccess.h"
#include "Audio/AudioEffects.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Console.h"
#include <miniaudio.h>
#include <ryml.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_set>

extern "C" void BazzaltInitializeAudioCodecs(void);
namespace Bazzalt {
namespace {
void InitializeCodecs(){static std::once_flag once;std::call_once(once,BazzaltInitializeAudioCodecs);}
constexpr UUID SFX{0x42415a5a414c5400,1}, Music{0x42415a5a414c5400,2};
std::vector<Runtime::AudioRuntime*> Runtimes;
std::atomic_uint64_t NextVoice{1}; // Never recycle handles across sessions/runtimes.
bool Range(float v,float a,float b){return std::isfinite(v)&&v>=a&&v<=b;}
bool Finite(Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);}
std::string Utf8(const std::filesystem::path& p){const auto v=p.u8string();return {reinterpret_cast<const char*>(v.data()),v.size()};}
std::optional<std::filesystem::path> ClipPath(UUID id){
    const auto asset=AssetManager::GetAsset(id);
    if(!asset||asset->Importer!="Bazzalt.AudioClip"||asset->State!=AssetState::Ready)return {};
    return asset->CachePath;
}
ma_result OpenSound(ma_engine* engine,const std::filesystem::path& p,ma_uint32 flags,ma_sound_group* group,ma_sound* sound){
#ifdef _WIN32
    return ma_sound_init_from_file_w(engine,p.c_str(),flags,group,nullptr,sound);
#else
    return ma_sound_init_from_file(engine,p.c_str(),flags,group,nullptr,sound);
#endif
}
std::vector<AudioBusSettings> DefaultBuses(){
    AudioBusSettings master;master.Name="Master";
    AudioBusSettings sfx;sfx.Id=SFX;sfx.Name="SFX";
    AudioBusSettings music;music.Id=Music;music.Name="Music";
    return {master,sfx,music};
}
bool IsDescendant(UUID id,UUID ancestor,const std::map<UUID,AudioBusSettings>& buses){
    for(std::size_t count=0;count<=buses.size();++count){
        if(id==ancestor)return true;if(!id)return false;
        auto found=buses.find(id);if(found==buses.end())return false;id=found->second.Parent;
    }return false;
}
float Attenuation(const AudioSource& s,float distance){
    if(distance<=s.MinDistance)return 1;if(distance>=s.MaxDistance)return 0;
    const float t=(distance-s.MinDistance)/(s.MaxDistance-s.MinDistance);
    if(s.RolloffCurve==AudioRolloff::Linear)return 1-t;
    if(s.RolloffCurve==AudioRolloff::Logarithmic){
        const float edge=s.MinDistance/s.MaxDistance;
        return std::clamp((s.MinDistance/distance-edge)/(1-edge),0.0f,1.0f);
    }
    for(std::size_t i=1;i<s.CustomRolloff.size();++i)if(t<=s.CustomRolloff[i].X){
        const auto a=s.CustomRolloff[i-1],b=s.CustomRolloff[i];return a.Y+(b.Y-a.Y)*(t-a.X)/(b.X-a.X);
    }return 0;
}
}
bool AudioSource::IsValid() const {
    if(!Range(Volume,0,1)||!Range(Pitch,.5f,2)||!Range(SpatialBlend,0,1)||!Range(MinDistance,.001f,1e8f)||!Range(MaxDistance,MinDistance+.001f,1e9f)||MaxDistance<=MinDistance||
       Priority<0||Priority>256||!Range(Pan,-1,1)||!Range(DopplerLevel,0,5)||!Range(ConeInnerAngle,0,360)||!Range(ConeOuterAngle,ConeInnerAngle,360)||!Range(ConeOuterGain,0,1)||
       int(RolloffCurve)<0||int(RolloffCurve)>2||CustomRolloff.size()<2||CustomRolloff.size()>64)return false;
    if(CustomRolloff.front().X!=0||CustomRolloff.back().X!=1)return false;
    float previous=-1;for(auto p:CustomRolloff){if(!Range(p.X,0,1)||!Range(p.Y,0,1)||p.X<=previous)return false;previous=p.X;}return true;
}
namespace Runtime {
bool InspectAudioFile(const std::filesystem::path& path,AudioClipInfo& info,std::string& error){
    InitializeCodecs();
    info={};std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec||size>2ull*1024*1024*1024){error="Audio Clip is unreadable or exceeds 2 GiB";return false;}
    ma_decoder decoder{};auto config=ma_decoder_config_init(ma_format_f32,0,0);
#ifdef _WIN32
    const auto result=ma_decoder_init_file_w(path.c_str(),&config,&decoder);
#else
    const auto result=ma_decoder_init_file(path.c_str(),&config,&decoder);
#endif
    if(result!=MA_SUCCESS){
        // Extensions are hints, not proof of the encoded payload. In particular
        // browser downloads are sometimes MP4 audio saved with an .mp3 suffix.
        std::ifstream input(path,std::ios::binary);std::array<char,12> header{};input.read(header.data(),header.size());
        if(input.gcount()>=12&&std::string_view(header.data()+4,4)=="ftyp"){
            error="Audio Clip contains an unsupported MP4/M4A container, not MP3 audio. Renaming the extension does not convert it. Export or convert to MP3, WAV, FLAC or Ogg Vorbis.";return false;
        }
        const std::string description=ma_result_description(result);
        error="Unsupported or invalid Audio Clip: "+(description=="Unknown error"?"decoder could not recognize the audio payload":description)+" (decoder code "+std::to_string(result)+")";return false;
    }
    struct Cleanup{ma_decoder* Value;~Cleanup(){ma_decoder_uninit(Value);}} cleanup{&decoder};
    ma_uint64 frames=0;ma_format format;ma_uint32 channels=0,rate=0;
    if(ma_decoder_get_data_format(&decoder,&format,&channels,&rate,nullptr,0)!=MA_SUCCESS||ma_decoder_get_length_in_pcm_frames(&decoder,&frames)!=MA_SUCCESS||!frames||!channels||channels>8||rate<8000||rate>384000||frames>ma_uint64(rate)*86400){error="Audio Clip has invalid duration, channels or sample rate (maximum 8 channels, 24 hours)";return false;}
    // Validate actual PCM, not just the container header.
    std::vector<float> probe(256*channels);ma_uint64 read=0;
    if(ma_decoder_read_pcm_frames(&decoder,probe.data(),256,&read)!=MA_SUCCESS&&read==0){error="Audio Clip cannot decode its first frames";return false;}
    info={double(frames)/rate,frames,channels,rate};error.clear();return true;
}
std::vector<std::array<float,2>> BuildAudioWaveform(const std::filesystem::path& path,unsigned bins){
    if(bins<32||bins>2048)return {};InitializeCodecs();ma_decoder decoder{};auto config=ma_decoder_config_init(ma_format_f32,2,48000);
#ifdef _WIN32
    const auto opened=ma_decoder_init_file_w(path.c_str(),&config,&decoder);
#else
    const auto opened=ma_decoder_init_file(path.c_str(),&config,&decoder);
#endif
    if(opened!=MA_SUCCESS)return {};struct Cleanup{ma_decoder* Value;~Cleanup(){ma_decoder_uninit(Value);}} cleanup{&decoder};
    ma_uint64 frames=0;if(ma_decoder_get_length_in_pcm_frames(&decoder,&frames)!=MA_SUCCESS||!frames||frames>48000ull*86400)return {};
    std::vector<std::array<float,2>> result(bins);std::vector<float> pcm(32768*2);ma_uint64 cursor=0;
    for(unsigned bin=0;bin<bins;++bin){const auto start=frames*bin/bins,end=frames*(bin+1)/bins;const auto count=std::min<ma_uint64>(pcm.size()/2,end-start);
        if(!count)continue;if(cursor!=start&&ma_decoder_seek_to_pcm_frame(&decoder,start)!=MA_SUCCESS)return {};
        ma_uint64 read=0;ma_decoder_read_pcm_frames(&decoder,pcm.data(),count,&read);if(!read)return {};cursor=start+read;
        for(ma_uint64 i=0;i<read*2;++i)if(std::isfinite(pcm[i])){result[bin][0]=std::min(result[bin][0],pcm[i]);result[bin][1]=std::max(result[bin][1],pcm[i]);}
    }return result;
}
bool ReadAudioMixer(const std::filesystem::path& path,std::vector<AudioBusSettings>& buses,std::string& error,std::string* editorState){
    std::error_code ec;if(std::filesystem::file_size(path,ec)>1024*1024||ec){error="Could not read Audio Mixer (1 MiB maximum)";return false;}
    std::ifstream stream(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(stream)),{});
    try{
        auto tree=ryml::parse_in_arena(ryml::csubstr(text.data(),text.size()));auto root=tree.rootref();
        if(!root.is_map()||!root.has_child("Version")||(root["Version"].val()!="1"&&root["Version"].val()!="2")||!root.has_child("Buses")||!root["Buses"].is_seq())throw std::invalid_argument("Expected supported Version and Buses sequence");
        std::vector<AudioBusSettings> parsed;
        auto value=[](ryml::ConstNodeRef n){if(!n.has_val())throw std::invalid_argument("Expected scalar mixer value");auto v=n.val();return std::string(v.str,v.len);};
        for(auto n:root["Buses"].children()){
            if(parsed.size()>=256||!n.is_map())throw std::invalid_argument("Invalid mixer bus list");
            AudioBusSettings bus;
            if(!n.has_child("Id")||!n.has_child("Parent")||!n.has_child("Name")||!UUID::TryParse(value(n["Id"]),bus.Id)||!UUID::TryParse(value(n["Parent"]),bus.Parent))throw std::invalid_argument("Invalid bus UUID/name");
            bus.Name=value(n["Name"]);
            auto number=[&](const char* key,float& v){if(!n.has_child(key))return;if(!Detail::ParseNumber(value(n[key]),v))throw std::invalid_argument("Invalid bus number");};
            number("Volume",bus.Volume);number("Pitch",bus.Pitch);
            auto boolean=[&](const char* key,bool& v){if(!n.has_child(key))return;auto t=value(n[key]);if(t!="true"&&t!="false")throw std::invalid_argument("Invalid bus boolean");v=t=="true";};
            boolean("Muted",bus.Muted);boolean("Solo",bus.Solo);boolean("Paused",bus.Paused);boolean("Bypass",bus.Bypass);
            if(n.has_child("Effects")){
                if(!n["Effects"].is_seq())throw std::invalid_argument("Expected effect sequence");
                for(auto fx:n["Effects"].children()){
                    if(bus.Effects.size()>=16||!fx.is_map())throw std::invalid_argument("Too many effects");
                    AudioEffectSettings e;if(!fx.has_child("Id")||!UUID::TryParse(value(fx["Id"]),e.Id)||!fx.has_child("Type"))throw std::invalid_argument("Invalid effect ID/type");
                    const auto type=value(fx["Type"]);const char* types[]={"Lowpass","Highpass","Echo","Distortion","Reverb","Equalizer"};int index=0;for(;index<6&&type!=types[index];++index){}if(index==6)throw std::invalid_argument("Unknown audio effect");e.Type=AudioEffectType(index);
                    if(fx.has_child("Enabled")){auto enabled=value(fx["Enabled"]);if(enabled!="true"&&enabled!="false")throw std::invalid_argument("Invalid effect enabled flag");e.Enabled=enabled=="true";}
                    if(fx.has_child("Parameters")){if(!fx["Parameters"].is_map())throw std::invalid_argument("Invalid effect parameters");for(auto p:fx["Parameters"].children()){float v;if(!Detail::ParseNumber(value(p),v))throw std::invalid_argument("Invalid effect parameter number");auto key=p.key();e.Parameters.emplace(std::string(key.str,key.len),v);}}
                    bus.Effects.push_back(std::move(e));
                }
            }
            parsed.push_back(std::move(bus));
        }
        if(!AudioMixer::Validate(parsed,error))return false;buses=std::move(parsed);if(editorState)*editorState=root.has_child("EditorState")?value(root["EditorState"]):"";error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
struct AudioRuntime::Impl {
    struct Voice {
        ma_sound Wet{},Dry{};
        bool WetReady=false,DryReady=false,UserPaused=false,TransportPaused=false,Preview=false,Stopped=false;
        std::uint64_t Id=0;UUID Owner{},Clip{};AudioSource Settings{};Vec3 Position{},Previous{};
        float FadeStart=1,FadeEnd=1,FadeElapsed=0,FadeDuration=0,FadeGain=1;
        ma_sound_group* RoutedGroup=nullptr;
        ~Voice(){if(DryReady)ma_sound_uninit(&Dry);if(WetReady)ma_sound_uninit(&Wet);}
    };
    struct Bus {ma_sound_group Group{};Runtime::BusProcessor Processor;bool Ready=false;~Bus(){if(Ready)ma_sound_group_uninit(&Group);}};
    ma_engine Engine{};bool Ready=false,NoDevice=false,Running=false,Paused=false,HostPaused=false;
    Scene* World=nullptr;
    std::string Error;
    std::map<std::uint64_t,std::unique_ptr<Voice>> Voices;
    std::map<UUID,AudioBusSettings> Buses;
    std::map<UUID,std::unique_ptr<Bus>> Groups;
    std::unordered_set<UUID> Seen;
    std::unordered_map<UUID,UUID> SourceClips;
    std::unordered_map<UUID,AudioClipInfo> ClipInfos;
    std::uint32_t MaxVoices=64;
    UUID Listener{},Mixer{},FailedMixer{};
    UUID MixerListener{},RequestedMixer{};bool MixerObserved=false;
    Vec3 ListenerPosition{},PreviousListener{};AudioListener ListenerSettings{};
    bool HasListener=false;
    Impl(){for(auto b:DefaultBuses())Buses[b.Id]=b;}
    ~Impl(){Shutdown();}
    void Shutdown(){Voices.clear();ClearGroups();if(Ready)ma_engine_uninit(&Engine);Ready=false;}
    void ClearGroups(){
        // Detach first: map/UUID ordering is not parent-before-child ordering.
        for(auto& [id,g]:Groups)if(g->Ready)ma_node_detach_output_bus(&g->Group,0);
        Groups.clear();
    }
    bool Initialize(){
        if(Ready)return true;
        InitializeCodecs();
        auto config=ma_engine_config_init();config.listenerCount=1;config.channels=2;config.sampleRate=48000;config.noDevice=NoDevice;
        auto result=ma_engine_init(&config,&Engine);
        if(result!=MA_SUCCESS){Error="Audio device initialization failed: "+std::string(ma_result_description(result));return false;}
        Ready=true;if(!BuildGroups()){Shutdown();return false;}SelectListener(0);Error.clear();return true;
    }
    bool BuildGroups(){
        ClearGroups();
        for(const auto& [id,b]:Buses){auto g=std::make_unique<Bus>();if(ma_sound_group_init(&Engine,MA_SOUND_FLAG_NO_SPATIALIZATION,nullptr,&g->Group)!=MA_SUCCESS){Error="Could not create audio mixer bus";ClearGroups();return false;}g->Ready=true;if(!g->Processor.Initialize(&Engine,b)){Error="Could not create audio effect processor";ClearGroups();return false;}Groups[id]=std::move(g);}
        for(const auto& [id,b]:Buses){auto& g=*Groups.at(id);ma_node_attach_output_bus(&g.Group,0,&g.Processor.Node,0);ma_node_attach_output_bus(&g.Processor.Node,0,id?static_cast<ma_node*>(&Groups.at(b.Parent)->Group):ma_node_graph_get_endpoint(ma_engine_get_node_graph(&Engine)),0);}
        ApplyBuses();return true;
    }
    bool ReplaceBuses(const std::vector<AudioBusSettings>& values){
        std::map<UUID,AudioBusSettings> replacement;for(auto b:values)replacement[b.Id]=b;
        bool reusable=replacement.size()==Buses.size();
        if(reusable)for(const auto& [id,b]:replacement)if(!Buses.contains(id)||Buses.at(id).Effects!=b.Effects||Buses.at(id).Bypass!=b.Bypass){reusable=false;break;}
        if(reusable){Buses=std::move(replacement);if(Ready){for(const auto& [id,b]:Buses)if(id)ma_node_attach_output_bus(&Groups.at(id)->Processor.Node,0,&Groups.at(b.Parent)->Group,0);ApplyBuses();for(auto& [id,v]:Voices)Apply(*v,0);}return true;}
        auto previous=std::move(Buses);Buses=std::move(replacement);auto oldGroups=std::move(Groups);
        if(Ready&&!BuildGroups()){Buses=std::move(previous);Groups=std::move(oldGroups);return false;}
        for(auto& [id,v]:Voices)Apply(*v,0);
        for(auto& [id,g]:oldGroups)if(g->Ready)ma_node_detach_output_bus(&g->Group,0);
        return true;
    }
    void ApplyBuses(){
        if(!Ready)return;
        bool solo=false;for(const auto& [id,b]:Buses)solo|=b.Solo;
        for(const auto& [id,b]:Buses){
            bool allowed=!solo||!id;
            if(solo)for(const auto& [sid,s]:Buses)if(s.Solo&&(IsDescendant(id,sid,Buses)||IsDescendant(sid,id,Buses)))allowed=true;
            auto& group=Groups.at(id)->Group;ma_sound_group_set_volume(&group,b.Muted||!allowed?0:b.Volume);ma_sound_group_set_pitch(&group,b.Pitch);
        }
    }
    bool BusPaused(UUID id) const {for(std::size_t n=0;n<=Buses.size();++n){auto f=Buses.find(id);if(f==Buses.end())return false;if(f->second.Paused)return true;if(!id)return false;id=f->second.Parent;}return false;}
    Voice* Find(std::uint64_t id){auto f=Voices.find(id);return f==Voices.end()?nullptr:f->second.get();}
    Voice* Source(UUID id){for(auto& [n,v]:Voices)if(!v->Preview&&v->Owner==id)return v.get();return nullptr;}
    void SelectListener(float dt){
        HasListener=false;UUID best{};AudioListener settings;Vec3 position,forward{0,0,-1},up{0,1,0};
        if(World)for(auto h:World->GetRegistry().view<AudioListener>()){
            const auto& candidate=World->GetRegistry().get<AudioListener>(h);if(!candidate.IsEnabled())continue;
            auto e=World->GetEntity(static_cast<Entity::Id>(h));auto id=e.GetUUID();
            if(!HasListener||candidate.Priority>settings.Priority||(candidate.Priority==settings.Priority&&id<best)){
                HasListener=true;best=id;settings=candidate;const auto m=e.GetWorldMatrix();position=m.TransformPoint({});forward=m.TransformDirection({0,0,-1}).Normalized();up=m.TransformDirection({0,1,0}).Normalized();
            }
        }
        if(!HasListener){Listener={};return;}
        if(!Finite(position)||!Finite(forward)||!Finite(up)){HasListener=false;return;}
        const auto velocity=best==Listener&&dt>0&&dt<.25f?(position-PreviousListener)/dt:Vec3{};
        Listener=best;ListenerSettings=settings;ListenerPosition=position;PreviousListener=position;
        if(Ready){ma_engine_listener_set_position(&Engine,0,position.X,position.Y,position.Z);ma_engine_listener_set_direction(&Engine,0,forward.X,forward.Y,forward.Z);ma_engine_listener_set_world_up(&Engine,0,up.X,up.Y,up.Z);ma_engine_listener_set_velocity(&Engine,0,velocity.X,velocity.Y,velocity.Z);ma_spatializer_listener_set_speed_of_sound(&Engine.listeners[0],Range(settings.SpeedOfSound,1,10000)?settings.SpeedOfSound:343.3f);}
    }
    void Apply(Voice& v,float dt){
        auto& s=v.Settings;const auto p=v.Position;
        Vec3 direction{0,0,-1};
        if(v.Owner&&World){auto entity=World->GetEntity(v.Owner);if(entity)direction=entity.GetWorldMatrix().TransformDirection(direction).Normalized();}
        const auto velocity=dt>0&&dt<.25f?(p-v.Previous)/dt:Vec3{};v.Previous=p;
        ma_sound_set_position(&v.Wet,p.X,p.Y,p.Z);ma_sound_set_direction(&v.Wet,direction.X,direction.Y,direction.Z);ma_sound_set_velocity(&v.Wet,velocity.X,velocity.Y,velocity.Z);
        // `none` bypasses the entire spatializer (including gain/cones/Doppler).
        // A zero-rolloff linear model keeps spatial processing but neutralizes
        // native distance attenuation; our blended/custom gain is applied below.
        ma_sound_set_attenuation_model(&v.Wet,ma_attenuation_model_linear);
        ma_sound_set_rolloff(&v.Wet,0);
        ma_sound_set_doppler_factor(&v.Wet,s.DopplerLevel*(Range(ListenerSettings.DopplerFactor,0,5)?ListenerSettings.DopplerFactor:1));
        ma_sound_set_cone(&v.Wet,ToRadians(s.ConeInnerAngle),ToRadians(s.ConeOuterAngle),s.ConeOuterGain);
        ma_sound_set_pan(&v.Dry,s.Pan);ma_sound_set_pan(&v.Wet,s.Pan);
        ma_sound_set_pitch(&v.Dry,s.Pitch);ma_sound_set_pitch(&v.Wet,s.Pitch);
        ma_sound_set_looping(&v.Dry,s.Loop);ma_sound_set_looping(&v.Wet,s.Loop);
        if(v.FadeDuration>0){v.FadeElapsed=std::min(v.FadeDuration,v.FadeElapsed+std::max(0.0f,dt));v.FadeGain=v.FadeStart+(v.FadeEnd-v.FadeStart)*(v.FadeElapsed/v.FadeDuration);if(v.FadeElapsed>=v.FadeDuration)v.FadeDuration=0;}
        float gain=s.Muted?0:s.Volume*v.FadeGain;
        if(!v.Preview)gain*=HasListener&&!ListenerSettings.Muted&&Range(ListenerSettings.MasterVolume,0,1)?ListenerSettings.MasterVolume:0;
        ma_sound_set_volume(&v.Dry,gain*(1-s.SpatialBlend));
        ma_sound_set_volume(&v.Wet,gain*s.SpatialBlend*(v.Preview?1:Attenuation(s,(p-ListenerPosition).Length())));
        const UUID bus=Buses.contains(s.OutputBus.GetUUID())?s.OutputBus.GetUUID():UUID{};
        auto group=&Groups.at(bus)->Group;
        if(v.RoutedGroup!=group){ma_node_attach_output_bus(&v.Dry,0,group,0);ma_node_attach_output_bus(&v.Wet,0,group,0);v.RoutedGroup=group;}
        const bool pause=v.UserPaused||BusPaused(bus)||(!v.Preview&&(Paused||HostPaused||(!s.IgnoreListenerPause&&HasListener&&ListenerSettings.Paused)));
        if(pause!=v.TransportPaused&&!v.Stopped){if(pause){ma_sound_stop(&v.Wet);ma_sound_stop(&v.Dry);}else{ma_sound_start(&v.Wet);ma_sound_start(&v.Dry);}v.TransportPaused=pause;}
    }
    AudioVoice Play(AudioSource settings,UUID owner,Vec3 position,bool preview=false){
        if(!settings.IsValid()||!Finite(position)){Error="Invalid AudioSource settings";return {};}
        auto path=ClipPath(settings.Clip.GetAssetUUID());if(!path){Error="Audio Clip is missing or not imported";return {};}
        if(!Initialize())return {};
        // Retire completed voices before deciding which active voice to steal.
        std::erase_if(Voices,[](const auto& entry){auto& v=*entry.second;return v.Stopped||(!v.TransportPaused&&(ma_sound_at_end(&v.Dry)&&ma_sound_at_end(&v.Wet)));});
        if(Voices.size()>=MaxVoices){
            auto worst=Voices.end();std::pair<int,float> score{-1,0};
            for(auto i=Voices.begin();i!=Voices.end();++i){const auto& v=*i->second;if(v.Preview)continue;const std::pair<int,float> priority{v.Settings.Priority,(v.Position-ListenerPosition).Length()};if(priority>score){score=priority;worst=i;}}
            if(worst==Voices.end()||(!preview&&worst->second->Settings.Priority<settings.Priority)){Error="Audio voice limit reached";return {};}
            Voices.erase(worst);
        }
        auto v=std::make_unique<Voice>();v->Id=NextVoice++;v->Owner=owner;v->Clip=settings.Clip.GetAssetUUID();v->Settings=settings;v->Position=v->Previous=position;v->Preview=preview;
        auto clipInfo=ClipInfos.find(v->Clip);
        if(clipInfo==ClipInfos.end()){AudioClipInfo info;if(!InspectAudioFile(*path,info,Error))return {};clipInfo=ClipInfos.emplace(v->Clip,info).first;}
        const bool stream=settings.Streaming||clipInfo->second.FrameCount*clipInfo->second.Channels*sizeof(float)>64u*1024u*1024u;
        const auto flags=MA_SOUND_FLAG_ASYNC|(stream?MA_SOUND_FLAG_STREAM:MA_SOUND_FLAG_DECODE);
        const UUID bus=Buses.contains(settings.OutputBus.GetUUID())?settings.OutputBus.GetUUID():UUID{};auto group=&Groups.at(bus)->Group;
        auto result=OpenSound(&Engine,*path,flags,group,&v->Wet);if(result!=MA_SUCCESS){Error="Could not play Audio Clip: "+std::string(ma_result_description(result));return {};}
        v->WetReady=true;
        result=OpenSound(&Engine,*path,flags|MA_SOUND_FLAG_NO_SPATIALIZATION,group,&v->Dry);if(result!=MA_SUCCESS){Error="Could not create blended audio voice";return {};}
        v->DryReady=true;Apply(*v,0);
        // Schedule both branches at the same engine frame, avoiding phase drift at start.
        const auto start=ma_engine_get_time_in_pcm_frames(&Engine)+ma_engine_get_sample_rate(&Engine)/100;
        ma_sound_set_start_time_in_pcm_frames(&v->Wet,start);ma_sound_set_start_time_in_pcm_frames(&v->Dry,start);
        if(!v->TransportPaused){ma_sound_start(&v->Wet);ma_sound_start(&v->Dry);}
        auto id=v->Id;Voices.emplace(id,std::move(v));Error.clear();return AudioVoice(id);
    }
};
AudioRuntime::AudioRuntime():Data(std::make_unique<Impl>()){Runtimes.push_back(this);}
AudioRuntime::~AudioRuntime(){std::erase(Runtimes,this);}
AudioRuntime* AudioRuntime::Current(){return Runtimes.empty()?nullptr:Runtimes.back();}
void AudioRuntime::Start(Scene& scene,bool noDevice){Stop();Data->World=&scene;Data->Running=true;Data->NoDevice=noDevice;Data->SelectListener(0);Update(0);}
void AudioRuntime::Stop(){Data->Shutdown();Data->Running=false;Data->World=nullptr;Data->Seen.clear();Data->SourceClips.clear();Data->Buses.clear();for(auto b:DefaultBuses())Data->Buses[b.Id]=b;Data->Mixer={};Data->FailedMixer={};Data->HasListener=false;Data->Listener={};Data->Paused=false;Data->HostPaused=false;Data->ClipInfos.clear();Data->MixerObserved=false;Data->MixerListener={};Data->RequestedMixer={};}
void AudioRuntime::SetPaused(bool paused){Data->Paused=paused;for(auto& [id,v]:Data->Voices)Data->Apply(*v,0);}
void AudioRuntime::SetHostPaused(bool paused){if(Data->HostPaused==paused)return;Data->HostPaused=paused;for(auto& [id,v]:Data->Voices)Data->Apply(*v,0);}
bool AudioRuntime::ReadFrames(float* stereo,std::uint32_t frames){return stereo&&Data->Ready&&Data->NoDevice&&ma_engine_read_pcm_frames(&Data->Engine,stereo,frames,nullptr)==MA_SUCCESS;}
void AudioRuntime::StopPreview(){std::erase_if(Data->Voices,[](const auto& e){return e.second->Preview;});}
UUID AudioRuntime::GetMixerAsset() const{return Data->Mixer;}
AudioVoice AudioRuntime::Preview(UUID id,AudioBus bus){StopPreview();AudioSource s;s.Clip=AudioClip::Load(id);s.SpatialBlend=0;s.OutputBus=bus;return Data->Play(s,{}, {},true);}
void AudioRuntime::Update(float dt){
    auto& d=*Data;
    if(d.Running&&d.World){
        d.SelectListener(dt);
        if(d.HasListener&&(!d.MixerObserved||d.MixerListener!=d.Listener||d.RequestedMixer!=d.ListenerSettings.MixerAsset)){
            d.MixerObserved=true;d.MixerListener=d.Listener;d.RequestedMixer=d.ListenerSettings.MixerAsset;
            const auto id=d.ListenerSettings.MixerAsset;
            if(id){if(!AudioMixer::Load(id)){d.FailedMixer=id;Console::Error(d.Error,"Audio");}}
            else if(d.Mixer&&d.ReplaceBuses(DefaultBuses())){d.Mixer={};d.FailedMixer={};}
        }
        std::unordered_set<UUID> live;
        for(auto h:d.World->GetRegistry().view<AudioSource>()){
            auto entity=d.World->GetEntity(static_cast<Entity::Id>(h));auto id=entity.GetUUID();const auto& s=entity.GetComponent<AudioSource>();
            if(!s.IsEnabled()){if(auto v=d.Source(id))d.Voices.erase(v->Id);d.Seen.erase(id);d.SourceClips.erase(id);continue;}
            live.insert(id);const bool first=d.Seen.insert(id).second;
            auto voice=d.Source(id);
            if(voice&&voice->Clip!=s.Clip.GetAssetUUID()){d.Voices.erase(voice->Id);voice=nullptr;d.Seen.erase(id);}
            if(!voice&&(first||!d.Seen.contains(id))&&s.PlayOnAwake&&s.Clip.GetAssetUUID()){
                const auto result=d.Play(s,id,entity.GetWorldTransform().Position);d.Seen.insert(id);
                if(!result.IsValid())Console::Warning(d.Error,"Audio");voice=d.Source(id);
            }
            if(voice&&s.IsValid()){voice->Settings=s;voice->Position=entity.GetWorldTransform().Position;}
        }
        std::erase_if(d.Seen,[&](UUID id){return !live.contains(id);});
        std::erase_if(d.Voices,[&](const auto& e){return e.second->Owner&&!live.contains(e.second->Owner);});
    }
    for(auto& [id,v]:d.Voices)d.Apply(*v,dt);
    std::erase_if(d.Voices,[](const auto& e){auto& v=*e.second;return v.Stopped||(!v.TransportPaused&&(ma_sound_at_end(&v.Dry)&&ma_sound_at_end(&v.Wet)));});
}
}
namespace {
Runtime::AudioRuntime::Impl* Data(){Detail::RequireSubsystemService(Detail::SubsystemService::Audio,"Audio");auto current=Runtime::AudioRuntime::Current();return current?current->Data.get():nullptr;}
Runtime::AudioRuntime::Impl::Voice* Voice(std::uint64_t id){auto d=Data();return d?d->Find(id):nullptr;}
}
bool AudioClip::IsValid() const{return ClipPath(m_id).has_value();}
AudioClipInfo AudioClip::GetInfo() const{AudioClipInfo info;std::string error;if(auto path=ClipPath(m_id))Runtime::InspectAudioFile(*path,info,error);return info;}
AudioBus AudioMixer::GetMasterBus(){return {};}
AudioBus AudioMixer::GetMusicBus(){return AudioBus::Load(Music);}
AudioBus AudioMixer::GetSFXBus(){return AudioBus::Load(SFX);}
bool AudioMixer::Validate(const std::vector<AudioBusSettings>& values,std::string& error){
    std::map<UUID,AudioBusSettings> buses;
    if(values.empty()||values.size()>256){error="Audio Mixer requires 1–256 buses";return false;}
    std::set<UUID> effects;float delayBudget=0;
    for(const auto& b:values){if(b.Id==UUID{~0ull,~0ull}||!buses.emplace(b.Id,b).second||b.Name.empty()||b.Name.size()>128||!Range(b.Volume,0,10)||!Range(b.Pitch,.25f,4)||b.Effects.size()>16){error="Invalid/duplicate audio bus";return false;}
        for(const auto& e:b.Effects){if(!e.Id||!effects.insert(e.Id).second||effects.size()>512||int(e.Type)<0||int(e.Type)>5){error="Invalid/duplicate audio effect";return false;}
            std::map<std::string,std::pair<float,float>> bounds;
            switch(e.Type){
                case AudioEffectType::Lowpass:case AudioEffectType::Highpass:bounds={{"Cutoff Freq",{10,22000}},{"Resonance",{1,10}}};break;
                case AudioEffectType::Echo:bounds={{"Delay",{10,5000}},{"Decay Ratio",{0,1}},{"Dry Mix",{0,1}},{"Wet Mix",{0,1}}};delayBudget+=e.Parameters.contains("Delay")?e.Parameters.at("Delay")/1000:.5f;break;
                case AudioEffectType::Distortion:bounds={{"Level",{0,1}}};break;
                case AudioEffectType::Reverb:bounds={{"Dry Level",{-80,0}},{"Room",{-80,0}},{"Decay Time",{.1f,20}},{"Density",{0,100}}};delayBudget+=.18f;break;
                case AudioEffectType::Equalizer:bounds={{"Center Freq",{20,22000}},{"Octave Range",{.2f,5}},{"Frequency Gain",{.05f,3}}};break;
            }
            for(const auto& [key,v]:e.Parameters)if(!bounds.contains(key)||!Range(v,bounds.at(key).first,bounds.at(key).second)){error="Invalid audio effect parameter: "+key;return false;}
        }
    }
    if(!std::isfinite(delayBudget)||delayBudget>64){error="Mixer delay memory budget exceeded (64 stereo seconds)";return false;}
    if(!buses.contains(UUID{})||buses.at(UUID{}).Parent){error="Audio Mixer requires the root Master bus";return false;}
    for(const auto& [id,b]:buses){if(!buses.contains(b.Parent)){error="Audio bus parent does not exist";return false;}if(!id)continue;std::set<UUID> visited;UUID p=id;while(p){if(!visited.insert(p).second){error="Audio bus routing cycle";return false;}p=buses.at(p).Parent;}}
    error.clear();return true;
}
std::vector<AudioBusSettings> AudioMixer::GetBuses(){auto d=Data();if(!d)return DefaultBuses();std::vector<AudioBusSettings> result;for(const auto& [id,b]:d->Buses)result.push_back(b);return result;}
AudioMixerSnapshot AudioMixer::CaptureSnapshot(){return {GetBuses()};}
bool AudioMixer::ApplySnapshot(const AudioMixerSnapshot& snapshot){
    auto d=Data();if(!d)return false;auto values=GetBuses();
    for(auto& b:values)for(const auto& stored:snapshot.Buses)if(b.Id==stored.Id){b.Volume=stored.Volume;b.Pitch=stored.Pitch;b.Muted=stored.Muted;b.Bypass=stored.Bypass;
        for(auto& fx:b.Effects)for(const auto& saved:stored.Effects)if(fx.Id==saved.Id&&fx.Type==saved.Type){fx.Enabled=saved.Enabled;fx.Parameters=saved.Parameters;}}
    if(!Validate(values,d->Error))return false;return d->ReplaceBuses(values);
}
AudioBus AudioMixer::CreateBus(const std::string& name,AudioBus parent,UUID id){
    auto d=Data();if(!d||!id||id==UUID{~0ull,~0ull}||d->Buses.contains(id)||!d->Buses.contains(parent.GetUUID())||name.empty()||name.size()>128||d->Buses.size()>=256)return AudioBus::Load(UUID{~0ull,~0ull});
    AudioBusSettings b;b.Id=id;b.Parent=parent.GetUUID();b.Name=name;
    auto all=AudioMixer::GetBuses();all.push_back(b);if(!d->ReplaceBuses(all))return AudioBus::Load(UUID{~0ull,~0ull});return AudioBus::Load(id);
}
bool AudioMixer::RemoveBus(AudioBus bus){
    auto d=Data();auto id=bus.GetUUID();if(!d||!id||!d->Buses.contains(id))return false;const auto parent=d->Buses.at(id).Parent;
    for(auto& [child,b]:d->Buses)if(child&&b.Parent==id){b.Parent=parent;if(d->Ready)ma_node_attach_output_bus(&d->Groups.at(child)->Processor.Node,0,&d->Groups.at(parent)->Group,0);}
    for(auto& [n,v]:d->Voices)if(v->Settings.OutputBus.GetUUID()==id){v->Settings.OutputBus=AudioBus::Load(parent);d->Apply(*v,0);}
    d->Groups.erase(id);d->Buses.erase(id);d->ApplyBuses();return true;
}
bool AudioMixer::Load(UUID id){
    auto d=Data();if(!d)return false;auto asset=AssetManager::GetAsset(id);if(!asset||asset->Importer!="Bazzalt.AudioMixer"||asset->State!=AssetState::Ready){d->Error="Audio Mixer asset is missing";return false;}
    std::vector<AudioBusSettings> buses;if(!Runtime::ReadAudioMixer(asset->CachePath,buses,d->Error))return false;
    if(!d->ReplaceBuses(buses))return false;
    d->Mixer=id;d->FailedMixer={};return true;
}
bool AudioBus::IsValid() const{auto d=Data();return d&&d->Buses.contains(m_id);}
AudioBusSettings AudioBus::GetSettings() const{auto d=Data();if(d&&d->Buses.contains(m_id))return d->Buses.at(m_id);return {};}
bool AudioBus::SetSettings(const AudioBusSettings& value) const{
    auto d=Data();if(!d||value.Id!=m_id||!d->Buses.contains(m_id))return false;
    auto all=AudioMixer::GetBuses();for(auto& b:all)if(b.Id==m_id)b=value;std::string error;if(!AudioMixer::Validate(all,error))return false;
    if(d->Buses.at(m_id).Effects==value.Effects&&d->Buses.at(m_id).Bypass==value.Bypass){
        d->Buses[m_id]=value;if(d->Ready&&m_id)ma_node_attach_output_bus(&d->Groups.at(m_id)->Processor.Node,0,&d->Groups.at(value.Parent)->Group,0);
        d->ApplyBuses();for(auto& [id,v]:d->Voices)d->Apply(*v,0);return true;
    }
    return d->ReplaceBuses(all);
}
bool AudioBus::SetVolume(float v) const{auto s=GetSettings();s.Volume=v;return SetSettings(s);}
bool AudioBus::SetVolumeDecibels(float v) const{if(!Range(v,-96,20))return false;return SetVolume(std::pow(10.0f,v/20));}
bool AudioBus::SetPitch(float v) const{auto s=GetSettings();s.Pitch=v;return SetSettings(s);}
bool AudioBus::SetMuted(bool v) const{auto s=GetSettings();s.Muted=v;return SetSettings(s);}
bool AudioBus::SetSolo(bool v) const{auto s=GetSettings();s.Solo=v;return SetSettings(s);}
bool AudioBus::SetPaused(bool v) const{auto s=GetSettings();s.Paused=v;return SetSettings(s);}
bool AudioBus::SetBypass(bool v) const{auto s=GetSettings();s.Bypass=v;return SetSettings(s);}
bool AudioBus::SetEffects(const std::vector<AudioEffectSettings>& v) const{auto s=GetSettings();s.Effects=v;return SetSettings(s);}
std::array<float,2> AudioBus::GetPeakLevels() const{auto d=Data();if(!d||!d->Groups.contains(m_id))return {};auto& p=d->Groups.at(m_id)->Processor;return {p.Peaks[0].exchange(0),p.Peaks[1].exchange(0)};}
AudioVoice Audio::Play(Entity source){auto d=Data();if(!d||!d->Running||!source||source.GetScene()!=d->World)return {};auto s=source.TryGetComponent<AudioSource>();if(!s||!s->IsEnabled())return {};if(auto v=d->Source(source.GetUUID()))d->Voices.erase(v->Id);d->Seen.insert(source.GetUUID());return d->Play(*s,source.GetUUID(),source.GetWorldTransform().Position);}
AudioVoice Audio::GetVoice(Entity e){auto d=Data();if(!d||!e||e.GetScene()!=d->World)return {};auto v=d->Source(e.GetUUID());return v?AudioVoice(v->Id):AudioVoice{};}
Entity Audio::GetActiveListener(){auto d=Data();return d&&d->World&&d->HasListener?d->World->GetEntity(d->Listener):Entity{};}
AudioVoice Audio::PlayOneShot(AudioClip clip,Vec3 p,float volume,AudioBus bus,float blend){auto d=Data();if(!d||!d->Running)return {};AudioSource s;s.Clip=clip;s.Volume=volume;s.OutputBus=bus;s.SpatialBlend=blend;return d->Play(s,{},p);}
void Audio::StopAll(){if(auto d=Data())d->Voices.clear();}
void Audio::PauseAll(bool paused){if(auto r=Runtime::AudioRuntime::Current())r->SetPaused(paused);}
bool Audio::SetMaxVoices(std::uint32_t count){auto d=Data();if(!d||count<1||count>1024)return false;d->MaxVoices=count;while(d->Voices.size()>count){auto worst=std::max_element(d->Voices.begin(),d->Voices.end(),[](const auto& a,const auto& b){return a.second->Settings.Priority<b.second->Settings.Priority;});d->Voices.erase(worst);}return true;}
std::uint32_t Audio::GetMaxVoices(){auto d=Data();return d?d->MaxVoices:64;}
std::uint32_t Audio::GetActiveVoiceCount(){auto d=Data();return d?static_cast<std::uint32_t>(d->Voices.size()):0;}
bool Audio::IsAvailable(){auto d=Data();return d&&d->Ready;}
std::string Audio::GetLastError(){auto d=Data();return d?d->Error:"Audio runtime is not active";}
bool AudioVoice::IsValid() const{return Voice(m_id)!=nullptr;}
AudioPlaybackState AudioVoice::GetState() const{auto v=Voice(m_id);return !v||v->Stopped?AudioPlaybackState::Stopped:v->TransportPaused?AudioPlaybackState::Paused:(ma_sound_at_end(&v->Dry)&&ma_sound_at_end(&v->Wet))?AudioPlaybackState::Stopped:AudioPlaybackState::Playing;}
bool AudioVoice::Pause() const{auto v=Voice(m_id);if(!v)return false;v->UserPaused=true;Data()->Apply(*v,0);return true;}
bool AudioVoice::Resume() const{auto v=Voice(m_id);if(!v||v->Stopped)return false;v->UserPaused=false;Data()->Apply(*v,0);return true;}
bool AudioVoice::Stop() const{auto d=Data();return d&&d->Voices.erase(m_id)>0;}
bool AudioVoice::Seek(double seconds) const{auto v=Voice(m_id);if(!v||!std::isfinite(seconds)||seconds<0||seconds>GetDuration())return false;ma_format format;ma_uint32 channels,rate;if(ma_sound_get_data_format(&v->Dry,&format,&channels,&rate,nullptr,0)!=MA_SUCCESS)return false;const auto frame=static_cast<ma_uint64>(seconds*rate);return ma_sound_seek_to_pcm_frame(&v->Dry,frame)==MA_SUCCESS&&ma_sound_seek_to_pcm_frame(&v->Wet,frame)==MA_SUCCESS;}
double AudioVoice::GetTime() const{auto v=Voice(m_id);float seconds=0;if(v)ma_sound_get_cursor_in_seconds(v->Settings.SpatialBlend>=.5f?&v->Wet:&v->Dry,&seconds);return seconds;}
double AudioVoice::GetDuration() const{auto v=Voice(m_id);float seconds=0;if(v)ma_sound_get_length_in_seconds(&v->Dry,&seconds);return seconds;}
bool AudioVoice::SetVolume(float volume) const{auto v=Voice(m_id);if(!v||!Range(volume,0,1))return false;v->Settings.Volume=volume;Data()->Apply(*v,0);return true;}
bool AudioVoice::SetPitch(float pitch) const{auto v=Voice(m_id);if(!v||!Range(pitch,.5f,2))return false;v->Settings.Pitch=pitch;Data()->Apply(*v,0);return true;}
bool AudioVoice::FadeTo(float volume,double seconds) const{auto v=Voice(m_id);if(!v||!Range(volume,0,1)||!std::isfinite(seconds)||seconds<0||seconds>86400)return false;v->FadeStart=v->FadeGain;v->FadeEnd=volume;v->FadeElapsed=0;v->FadeDuration=float(seconds);if(seconds==0){v->FadeGain=volume;Data()->Apply(*v,0);}return true;}
}
