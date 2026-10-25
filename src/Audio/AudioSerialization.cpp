#include "Audio/AudioRuntime.h"
#include "Utils/NumberParsing.h"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace Bazzalt::Runtime {
namespace {
std::string Number(float v){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(9)<<v;return s.str();}
template<class T> bool Number(const PropertyMap& p,const std::string& key,T& v){auto i=p.find(key);return i!=p.end()&&Detail::ParseNumber(i->second,v);}
bool Boolean(const PropertyMap& p,const char* key,bool& v){auto i=p.find(key);if(i==p.end()||(i->second!="true"&&i->second!="false"))return false;v=i->second=="true";return true;}
bool Id(const PropertyMap& p,const char* key,UUID& v){auto i=p.find(key);return i!=p.end()&&UUID::TryParse(i->second,v);}
}
void RegisterAudioComponents(ComponentSerializationRegistry& registry){
    registry.Register<AudioSource>("Bazzalt.AudioSource",1,[](const AudioSource& v,PropertyMap& p){
        p["Clip"]=v.Clip.GetAssetUUID().ToString();p["OutputBus"]=v.OutputBus.GetUUID().ToString();
#define AUDIO_FLOAT(F) p[#F]=Number(v.F);
        AUDIO_FLOAT(Volume) AUDIO_FLOAT(Pitch) AUDIO_FLOAT(SpatialBlend) AUDIO_FLOAT(MinDistance) AUDIO_FLOAT(MaxDistance) AUDIO_FLOAT(Pan) AUDIO_FLOAT(DopplerLevel) AUDIO_FLOAT(ConeInnerAngle) AUDIO_FLOAT(ConeOuterAngle) AUDIO_FLOAT(ConeOuterGain)
#undef AUDIO_FLOAT
#define AUDIO_BOOL(F) p[#F]=v.F?"true":"false";
        AUDIO_BOOL(Enabled) AUDIO_BOOL(Loop) AUDIO_BOOL(PlayOnAwake) AUDIO_BOOL(Streaming) AUDIO_BOOL(Muted) AUDIO_BOOL(IgnoreListenerPause)
#undef AUDIO_BOOL
        p["Priority"]=std::to_string(v.Priority);p["RolloffCurve"]=std::to_string(int(v.RolloffCurve));p["CurveCount"]=std::to_string(v.CustomRolloff.size());
        for(std::size_t i=0;i<v.CustomRolloff.size();++i){auto key="Curve."+std::to_string(i);p[key+".X"]=Number(v.CustomRolloff[i].X);p[key+".Y"]=Number(v.CustomRolloff[i].Y);}
    },[](AudioSource& value,const PropertyMap& p,std::uint32_t version){
        if(version!=1)return false;AudioSource v;UUID clip,bus;
        if(!Id(p,"Clip",clip)||!Id(p,"OutputBus",bus))return false;v.Clip=AudioClip::Load(clip);v.OutputBus=AudioBus::Load(bus);
#define AUDIO_FLOAT(F) if(!Number(p,#F,v.F))return false;
        AUDIO_FLOAT(Volume) AUDIO_FLOAT(Pitch) AUDIO_FLOAT(SpatialBlend) AUDIO_FLOAT(MinDistance) AUDIO_FLOAT(MaxDistance) AUDIO_FLOAT(Pan) AUDIO_FLOAT(DopplerLevel) AUDIO_FLOAT(ConeInnerAngle) AUDIO_FLOAT(ConeOuterAngle) AUDIO_FLOAT(ConeOuterGain)
#undef AUDIO_FLOAT
#define AUDIO_BOOL(F) if(!Boolean(p,#F,v.F))return false;
        AUDIO_BOOL(Enabled) AUDIO_BOOL(Loop) AUDIO_BOOL(PlayOnAwake) AUDIO_BOOL(Streaming) AUDIO_BOOL(Muted) AUDIO_BOOL(IgnoreListenerPause)
#undef AUDIO_BOOL
        int rolloff=0,count=0;if(!Number(p,"Priority",v.Priority)||!Number(p,"RolloffCurve",rolloff)||rolloff<0||rolloff>2||!Number(p,"CurveCount",count)||count<2||count>64)return false;
        v.RolloffCurve=AudioRolloff(rolloff);v.CustomRolloff.resize(count);
        for(int i=0;i<count;++i){auto key="Curve."+std::to_string(i);if(!Number(p,key+".X",v.CustomRolloff[i].X)||!Number(p,key+".Y",v.CustomRolloff[i].Y))return false;}
        if(!v.IsValid())return false;value=std::move(v);return true;
    });
    registry.Register<AudioListener>("Bazzalt.AudioListener",1,[](const AudioListener& v,PropertyMap& p){
        p["Enabled"]=v.Enabled?"true":"false";p["Muted"]=v.Muted?"true":"false";p["Paused"]=v.Paused?"true":"false";
        p["MasterVolume"]=Number(v.MasterVolume);p["DopplerFactor"]=Number(v.DopplerFactor);p["SpeedOfSound"]=Number(v.SpeedOfSound);p["Priority"]=std::to_string(v.Priority);p["MixerAsset"]=v.MixerAsset.ToString();
    },[](AudioListener& value,const PropertyMap& p,std::uint32_t version){
        AudioListener v;if(version!=1||!Boolean(p,"Enabled",v.Enabled)||!Boolean(p,"Muted",v.Muted)||!Boolean(p,"Paused",v.Paused)||!Number(p,"MasterVolume",v.MasterVolume)||!Number(p,"DopplerFactor",v.DopplerFactor)||!Number(p,"SpeedOfSound",v.SpeedOfSound)||!Number(p,"Priority",v.Priority)||!Id(p,"MixerAsset",v.MixerAsset))return false;
        if(v.MasterVolume<0||v.MasterVolume>1||v.DopplerFactor<0||v.DopplerFactor>5||v.SpeedOfSound<1||v.SpeedOfSound>10000)return false;value=v;return true;
    });
}
}
