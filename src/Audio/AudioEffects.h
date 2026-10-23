#pragma once
#include "Bazzalt/AudioMixer.h"
#include <miniaudio.h>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace Bazzalt::Runtime {
// Immutable configuration and preallocated buffers. Only the audio thread owns
// histories. Destruction is synchronized by ma_node_uninit, never by a DSP lock.
struct EffectProcessor {
    AudioEffectSettings Settings;
    double B0=1,B1=0,B2=0,A1=0,A2=0,Z1[2]{},Z2[2]{};
    std::vector<float> Delay;
    std::size_t Cursor=0;
    float Feedback=0,Dry=1,Wet=0,Drive=1,Mix=1;
    explicit EffectProcessor(const AudioEffectSettings& settings):Settings(settings){
        const auto get=[&](const char* k,float fallback){auto i=Settings.Parameters.find(k);return i==Settings.Parameters.end()?fallback:i->second;};
        if(Settings.Type==AudioEffectType::Echo){Delay.resize(static_cast<std::size_t>(get("Delay",500)*48)*2);Feedback=std::min(.999f,get("Decay Ratio",.5f));Dry=get("Dry Mix",1);Wet=get("Wet Mix",1);}
        else if(Settings.Type==AudioEffectType::Reverb){
            // Four decorrelated feedback taps form a compact diffuse room.
            Delay.resize(8192*2);Feedback=std::pow(.001f,.08f/get("Decay Time",1));Dry=std::pow(10.f,get("Dry Level",0)/20);Wet=std::pow(10.f,get("Room",-10)/20)*(get("Density",100)/100);
        }else if(Settings.Type==AudioEffectType::Distortion){Mix=get("Level",.5f);Drive=1+Mix*40;}
        else {
            const bool eq=Settings.Type==AudioEffectType::Equalizer;
            const double frequency=std::clamp(double(get(eq?"Center Freq":"Cutoff Freq",eq?8000:Settings.Type==AudioEffectType::Lowpass?5000:10)),10.,22000.);
            const double w=2*3.141592653589793*frequency/48000,c=std::cos(w),s=std::sin(w);
            double alpha=s/(2*get("Resonance",1)),a0=1;
            if(eq){const double gain=std::sqrt(get("Frequency Gain",1));alpha=s*std::sinh(std::log(2.)*.5*get("Octave Range",1)*w/s);B0=1+alpha*gain;B1=-2*c;B2=1-alpha*gain;a0=1+alpha/gain;A1=-2*c;A2=1-alpha/gain;}
            else{const bool low=Settings.Type==AudioEffectType::Lowpass;B0=(1+(low?-c:c))*.5;B1=low?1-c:-(1+c);B2=B0;a0=1+alpha;A1=-2*c;A2=1-alpha;}
            B0/=a0;B1/=a0;B2/=a0;A1/=a0;A2/=a0;
        }
    }
    float Process(float sample,unsigned channel){
        if(!Settings.Enabled)return sample;
        if(Settings.Type==AudioEffectType::Distortion)return sample*(1-Mix)+std::tanh(sample*Drive)/std::tanh(Drive)*Mix;
        if(!Delay.empty()){
            const auto index=Cursor*2+channel;float delayed=Delay[index];
            if(Settings.Type==AudioEffectType::Reverb){delayed=0;for(auto tap:{1559u,2081u,2909u,4093u})delayed+=Delay[((Cursor+8192-tap)%8192)*2+channel]*.25f;}
            Delay[index]=std::clamp(sample+delayed*Feedback,-32.f,32.f);
            if(channel==1)Cursor=(Cursor+1)%(Delay.size()/2);
            return sample*Dry+delayed*Wet;
        }
        const double out=B0*sample+Z1[channel];Z1[channel]=B1*sample-A1*out+Z2[channel];Z2[channel]=B2*sample-A2*out;
        if(std::abs(Z1[channel])<1e-25)Z1[channel]=0;if(std::abs(Z2[channel])<1e-25)Z2[channel]=0;
        return static_cast<float>(out);
    }
};
struct BusProcessor {
    ma_node_base Node{}; // First member: miniaudio callback points to this object.
    std::vector<EffectProcessor> Effects;
    std::atomic<float> Peaks[2]{};
    bool Ready=false,Bypass=false;
    static void Process(ma_node* node,const float** in,ma_uint32* countIn,float** out,ma_uint32* countOut){
        auto& self=*reinterpret_cast<BusProcessor*>(node);const auto frames=std::min(*countIn,*countOut);float peak[2]{};
        for(ma_uint32 frame=0;frame<frames;++frame)for(unsigned channel=0;channel<2;++channel){float v=in&&in[0]?in[0][frame*2+channel]:0;
            if(!self.Bypass)for(auto& effect:self.Effects)v=effect.Process(v,channel);
            if(!std::isfinite(v))v=0;out[0][frame*2+channel]=v;peak[channel]=std::max(peak[channel],std::abs(v));}
        for(unsigned c=0;c<2;++c){float previous=self.Peaks[c].load(std::memory_order_relaxed);while(previous<peak[c]&&!self.Peaks[c].compare_exchange_weak(previous,peak[c],std::memory_order_relaxed)){} }
        *countIn=*countOut=frames;
    }
    bool Initialize(ma_engine* engine,const AudioBusSettings& settings){
        Bypass=settings.Bypass;for(const auto& e:settings.Effects)Effects.emplace_back(e);
        static const ma_node_vtable table={Process,nullptr,1,1,MA_NODE_FLAG_CONTINUOUS_PROCESSING};ma_uint32 channels=2;auto config=ma_node_config_init();config.vtable=&table;config.pInputChannels=&channels;config.pOutputChannels=&channels;
        Ready=ma_node_init(ma_engine_get_node_graph(engine),&config,nullptr,&Node)==MA_SUCCESS;return Ready;
    }
    ~BusProcessor(){if(Ready)ma_node_uninit(&Node,nullptr);}
};
}
