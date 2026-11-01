#include "Bazzalt/Profiler.h"
#include <atomic>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace Bazzalt {
namespace {
struct Capture {
    std::mutex Mutex;
    std::atomic<std::uint64_t> Active{0};
    std::uint64_t Generation=0, Dropped=0;
    std::size_t Capacity=0;
    std::chrono::steady_clock::time_point Start;
    std::vector<ProfileSample> Samples;
};
Capture& State() {static Capture state; return state;}
void String(std::ostream& stream,const std::string& value) {
    stream << '"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') stream << '\\' << char(c);
        else if(c<32) stream << "\\u00" << "0123456789abcdef"[c>>4] << "0123456789abcdef"[c&15];
        else stream << char(c);
    }
    stream << '"';
}
}
void Profiler::BeginCapture(std::size_t capacity) {
    if(!capacity || capacity>10000000) throw std::invalid_argument("Profile capacity must be 1..10000000");
    auto& state=State(); std::lock_guard lock(state.Mutex);
    state.Active=0; state.Samples.clear(); state.Samples.reserve(capacity);
    state.Capacity=capacity; state.Dropped=0; state.Start=std::chrono::steady_clock::now();
    state.Active=++state.Generation;
}
void Profiler::EndCapture() {auto& s=State();std::lock_guard lock(s.Mutex);s.Active=0;}
bool Profiler::IsCapturing() {return State().Active.load()!=0;}
std::vector<ProfileSample> Profiler::GetSamples() {auto& s=State();std::lock_guard lock(s.Mutex);return s.Samples;}
std::uint64_t Profiler::GetDroppedSampleCount() {auto& s=State();std::lock_guard lock(s.Mutex);return s.Dropped;}
ProfileScope::ProfileScope(const char* name) {
    m_generation=State().Active.load();
    if(m_generation) {m_name=name?name:"Unnamed";m_start=std::chrono::steady_clock::now();}
}
ProfileScope::~ProfileScope() noexcept {
    if(!m_generation) return;
    try {
        const auto end=std::chrono::steady_clock::now();auto& s=State();std::lock_guard lock(s.Mutex);
        if(s.Active!=m_generation) return;
        if(s.Samples.size()>=s.Capacity) {++s.Dropped;return;}
        s.Samples.push_back({std::move(m_name),std::chrono::duration<double,std::micro>(m_start-s.Start).count(),
            std::chrono::duration<double,std::micro>(end-m_start).count(),
            static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))});
    } catch(...) {}
}
void Profiler::ExportTrace(const std::filesystem::path& path) {
    const auto samples=GetSamples();
    std::ofstream stream(path,std::ios::binary);stream.exceptions(std::ios::failbit|std::ios::badbit);
    stream.imbue(std::locale::classic());stream<<std::setprecision(17)<<"{\"traceEvents\":[";
    bool first=true;
    for(auto& s:samples) {
        if(!first) stream<<',';first=false;
        stream<<"{\"name\":";String(stream,s.Name);
        stream<<",\"ph\":\"X\",\"pid\":1,\"tid\":"<<s.Thread<<",\"ts\":"<<s.StartMicroseconds<<",\"dur\":"<<s.DurationMicroseconds<<'}';
    }
    stream<<"]}";stream.close();
}
}
