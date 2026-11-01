#include "Bazzalt/Detail/ModuleLifetime.h"
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Profiler.h"
#include <atomic>
#include <cassert>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace Bazzalt;
int main() {
    JobHandle{}.Wait();
    assert(JobHandle{}.IsComplete());
    bool threw=false;
    try {Jobs::Schedule({});}catch(const std::invalid_argument&) {threw=true;}
    assert(threw);
    std::vector<std::atomic<int>> visits(10003);
    auto lease=std::make_shared<int>(7);
    std::weak_ptr<void> weak=lease;
    Detail::SetCurrentGameplayModule(lease);
    auto retained=Jobs::Schedule([weak] {
        assert(Detail::GetCurrentGameplayModule()==weak.lock());
        Jobs::Schedule([weak]{assert(Detail::GetCurrentGameplayModule()==weak.lock());}).Wait();
    });
    Detail::SetCurrentGameplayModule({});lease.reset();
    retained.Wait();retained={};Jobs::WaitAll();assert(weak.expired());
    Profiler::BeginCapture(1000);
    auto loop=Jobs::ParallelFor(visits.size(),127,[&](auto begin,auto end) {
        for(auto i=begin;i<end;++i) ++visits[i];
    });
    loop.Wait(); loop.Wait();
    for(auto& value:visits) assert(value==1);
    std::vector<JobHandle> nested;
    std::atomic<unsigned> done=0;
    for(unsigned i=0;i<Jobs::GetWorkerCount()*2;++i)
        nested.push_back(Jobs::Schedule([&]{Jobs::Schedule([&]{++done;}).Wait();}));
    for(auto& job:nested) job.Wait();
    assert(done==nested.size());
    auto failure=Jobs::ParallelFor(200,1,[](auto begin,auto){if(begin==3)throw std::runtime_error("expected");});
    threw=false;try{failure.Wait();}catch(const std::runtime_error&) {threw=true;}assert(threw);
    Jobs::ParallelFor(0,1,[](auto,auto){assert(false);}).Wait();
    // Overflow-safe end calculation for the final range.
    Jobs::ParallelFor(std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max(),[](auto begin,auto end){assert(begin==0);assert(end==std::numeric_limits<std::size_t>::max());}).Wait();
    Jobs::WaitAll();
    std::atomic<bool> barrier=false;
    auto pending=Jobs::Schedule([&]{barrier=true;});
    Jobs::WaitAll();assert(barrier);pending.Wait();
    threw=false;
    try{Jobs::Schedule([]{Jobs::WaitAll();}).Wait();}catch(const std::logic_error&){threw=true;}
    assert(threw);
    Profiler::EndCapture();
    auto samples=Profiler::GetSamples();assert(!samples.empty());
    for(auto& sample:samples) {assert(sample.DurationMicroseconds>=0);assert(sample.StartMicroseconds>=0);}
    Profiler::BeginCapture(1);
    {ProfileScope scope("first\"\n");}
    {ProfileScope scope("dropped");}
    assert(Profiler::GetDroppedSampleCount()==1);
    auto path=std::filesystem::temp_directory_path()/"bazzalt-profiler-test.json";
    Profiler::ExportTrace(path);
    std::ifstream file(path);std::string json((std::istreambuf_iterator<char>(file)),{});
    assert(json.find("first\\\"\\u000a")!=std::string::npos);file.close();std::filesystem::remove(path);
    {ProfileScope old("old");Profiler::BeginCapture(5);}
    assert(Profiler::GetSamples().empty());
    Profiler::EndCapture();
}
