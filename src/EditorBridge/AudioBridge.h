#pragma once
#include "Audio/AudioRuntime.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace Bazzalt::EditorBridge {
namespace py=pybind11;
template<class F> bool AudioDispatch(Entity e,const std::string& name,F function){
    if(name=="Audio Source")function.template operator()<AudioSource>(e);
    else if(name=="Audio Listener")function.template operator()<AudioListener>(e);
    else return false;return true;
}
inline void InspectAudio(Entity e,py::list& components,py::dict& enabled,py::dict& data){
    if(auto s=e.TryGetComponent<AudioSource>()){
        components.append("Audio Source");enabled["Audio Source"]=s->Enabled;py::dict v;
        v["Clip"]=s->Clip.GetAssetUUID().ToString();v["Output Bus"]=s->OutputBus.GetUUID().ToString();
#define AF(Label,Field) v[Label]=s->Field;
        AF("Volume",Volume) AF("Pitch",Pitch) AF("Loop",Loop) AF("Play On Awake",PlayOnAwake) AF("Streaming",Streaming) AF("Spatial Blend",SpatialBlend) AF("Min Distance",MinDistance) AF("Max Distance",MaxDistance) AF("Priority",Priority) AF("Pan",Pan) AF("Doppler Level",DopplerLevel) AF("Cone Inner Angle",ConeInnerAngle) AF("Cone Outer Angle",ConeOuterAngle) AF("Cone Outer Gain",ConeOuterGain) AF("Muted",Muted) AF("Ignore Listener Pause",IgnoreListenerPause)
#undef AF
        v["Rolloff Curve"]=int(s->RolloffCurve);
        if(s->RolloffCurve==AudioRolloff::Custom){py::list points;for(auto p:s->CustomRolloff)points.append(py::make_tuple(p.X,p.Y));v["Custom Rolloff"]=points;}
        data["Audio Source"]=v;
    }
    if(auto s=e.TryGetComponent<AudioListener>()){
        components.append("Audio Listener");enabled["Audio Listener"]=s->Enabled;py::dict v;
        v["Master Volume"]=s->MasterVolume;v["Muted"]=s->Muted;v["Paused"]=s->Paused;v["Priority"]=s->Priority;v["Doppler Factor"]=s->DopplerFactor;v["Speed Of Sound"]=s->SpeedOfSound;v["Mixer Asset"]=s->MixerAsset.ToString();data["Audio Listener"]=v;
    }
}
inline bool SetAudioProperty(Entity e,const std::string& type,const std::string& property,py::object value){
    if(type=="Audio Source"&&e.HasComponent<AudioSource>()){
        auto s=e.GetComponent<AudioSource>();
        if(property=="Clip"){UUID id;if(!UUID::TryParse(value.cast<std::string>(),id)||(id&&!AudioClip::Load(id).IsValid()))return false;s.Clip=AudioClip::Load(id);}
        else if(property=="Output Bus"){UUID id;if(!UUID::TryParse(value.cast<std::string>(),id))return false;s.OutputBus=AudioBus::Load(id);}
        else if(property=="Rolloff Curve"){auto v=value.cast<int>();if(v<0||v>2)return false;s.RolloffCurve=AudioRolloff(v);}
        else if(property=="Custom Rolloff"){s.CustomRolloff.clear();for(auto p:value.cast<std::vector<std::array<float,2>>>())s.CustomRolloff.push_back({p[0],p[1]});}
#define AF(Label,Field,T) else if(property==Label)s.Field=value.cast<T>();
        AF("Volume",Volume,float) AF("Pitch",Pitch,float) AF("Loop",Loop,bool) AF("Play On Awake",PlayOnAwake,bool) AF("Streaming",Streaming,bool) AF("Spatial Blend",SpatialBlend,float) AF("Min Distance",MinDistance,float) AF("Max Distance",MaxDistance,float) AF("Priority",Priority,int) AF("Pan",Pan,float) AF("Doppler Level",DopplerLevel,float) AF("Cone Inner Angle",ConeInnerAngle,float) AF("Cone Outer Angle",ConeOuterAngle,float) AF("Cone Outer Gain",ConeOuterGain,float) AF("Muted",Muted,bool) AF("Ignore Listener Pause",IgnoreListenerPause,bool)
#undef AF
        else return false;
        if(!s.IsValid())return false;e.GetComponent<AudioSource>()=std::move(s);return true;
    }
    if(type=="Audio Listener"&&e.HasComponent<AudioListener>()){
        auto s=e.GetComponent<AudioListener>();
        if(property=="Mixer Asset"){UUID id;if(!UUID::TryParse(value.cast<std::string>(),id))return false;if(id){auto a=AssetManager::GetAsset(id);if(!a||a->Importer!="Bazzalt.AudioMixer"||a->State!=AssetState::Ready)return false;}s.MixerAsset=id;}
        else if(property=="Master Volume")s.MasterVolume=value.cast<float>();else if(property=="Muted")s.Muted=value.cast<bool>();else if(property=="Paused")s.Paused=value.cast<bool>();else if(property=="Priority")s.Priority=value.cast<int>();else if(property=="Doppler Factor")s.DopplerFactor=value.cast<float>();else if(property=="Speed Of Sound")s.SpeedOfSound=value.cast<float>();else return false;
        if(!std::isfinite(s.MasterVolume)||s.MasterVolume<0||s.MasterVolume>1||!std::isfinite(s.DopplerFactor)||s.DopplerFactor<0||s.DopplerFactor>5||!std::isfinite(s.SpeedOfSound)||s.SpeedOfSound<1||s.SpeedOfSound>10000)return false;e.GetComponent<AudioListener>()=s;return true;
    }return false;
}
}
