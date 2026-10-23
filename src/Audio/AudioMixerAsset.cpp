#include "Audio/AudioRuntime.h"
#include <fstream>
#include <iomanip>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
namespace Bazzalt::Runtime {
bool SaveAudioMixer(const std::filesystem::path& path,const std::vector<AudioBusSettings>& buses,std::string& error,const std::string& editorState){
    if(!AudioMixer::Validate(buses,error))return false;
    if(editorState.size()>512*1024){error="Audio Mixer editor state exceeds 512 KiB";return false;}
    auto quote=[](const std::string& value){std::string result="\"";for(unsigned char c:value){if(c=='\\'||c=='\"'){result+='\\';result+=char(c);}else if(c<32){constexpr char hex[]="0123456789abcdef";result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}else result+=char(c);}return result+'\"';};
    // Legacy callers editing buses must not discard snapshots/views.
    std::string metadata=editorState;if(metadata.empty()&&std::filesystem::exists(path)){std::vector<AudioBusSettings> previous;std::string ignored;ReadAudioMixer(path,previous,ignored,&metadata);}
    auto temporary=path;temporary+=".tmp-"+UUID::Generate().ToString();std::error_code ec;
    {
        std::ofstream file(temporary,std::ios::binary);file<<std::setprecision(9)<<"Version: 2\nEditorState: "<<quote(metadata)<<"\nBuses:\n";
        for(const auto& b:buses){
            // Quoted YAML strings use JSON-compatible escaping, including controls.
            std::string name="\"";for(unsigned char c:b.Name){if(c=='\\'||c=='\"'){name+='\\';name+=char(c);}else if(c<32){constexpr char hex[]="0123456789abcdef";name+="\\u00";name+=hex[c>>4];name+=hex[c&15];}else name+=char(c);}name+='\"';
            file<<"  - Id: \""<<b.Id.ToString()<<"\"\n    Parent: \""<<b.Parent.ToString()<<"\"\n    Name: "<<name<<"\n    Volume: "<<b.Volume<<"\n    Pitch: "<<b.Pitch<<"\n    Muted: "<<(b.Muted?"true":"false")<<"\n    Solo: "<<(b.Solo?"true":"false")<<"\n    Paused: "<<(b.Paused?"true":"false")<<'\n';
            file<<"    Bypass: "<<(b.Bypass?"true":"false")<<"\n    Effects:"<<(b.Effects.empty()?" []\n":"\n");
            const char* types[]={"Lowpass","Highpass","Echo","Distortion","Reverb","Equalizer"};
            for(const auto& e:b.Effects){file<<"      - Id: "<<quote(e.Id.ToString())<<"\n        Type: "<<types[int(e.Type)]<<"\n        Enabled: "<<(e.Enabled?"true":"false")<<"\n        Parameters:"<<(e.Parameters.empty()?" {}\n":"\n");for(const auto& [key,v]:e.Parameters)file<<"          "<<quote(key)<<": "<<v<<'\n';}
        }
        file.flush();bool ok=bool(file);file.close();if(!ok){error="Cannot write Audio Mixer asset";std::filesystem::remove(temporary,ec);return false;}
    }
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error="Cannot replace Audio Mixer asset";std::filesystem::remove(temporary,ec);return false;}
#else
    std::filesystem::rename(temporary,path,ec);if(ec){error=ec.message();std::filesystem::remove(temporary,ec);return false;}
#endif
    error.clear();return true;
}
}
