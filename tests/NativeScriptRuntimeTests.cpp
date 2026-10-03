#include "Runtime/NativeScriptRuntime.h"
#include "Runtime/TimeAccess.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

int main(int argc,char** argv){
    assert(argc==2);const auto log=std::filesystem::temp_directory_path()/"bazzalt-script-lifecycle.txt";std::error_code error;std::filesystem::remove(log,error);
    Bazzalt::Runtime::NativeScriptRuntime runtime;Bazzalt::Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(argv[1]);binding.Entity="entity-uuid";binding.TypeName="LifecycleProbe";binding.Properties["LogPath"]=log.string();std::string message;
    Bazzalt::Runtime::TimeAccess::Reset();
    assert(!runtime.Configure({binding,binding},message));assert(message.find("Duplicate")!=std::string::npos);
    auto conflicting=binding;conflicting.Entity="another-entity";conflicting.Module="another-module.dll";
    assert(!runtime.Configure({binding,conflicting},message));assert(message.find("Ambiguous")!=std::string::npos);
    assert(runtime.Configure({binding},message));assert(runtime.Start(message));
    assert(Bazzalt::Time::GetTimeScale()==0.25f);
    Bazzalt::Runtime::TimeAccess::Advance(0.064);
    {Bazzalt::Runtime::TimeAccess::FixedScope fixed;runtime.FixedUpdate(Bazzalt::Time::GetDeltaTime());}
    runtime.Update(Bazzalt::Time::GetDeltaTime());runtime.Stop();
    std::ifstream stream(log);std::string contents((std::istreambuf_iterator<char>(stream)),{});assert(contents=="create:entity-uuid\nfixed:entity-uuid\nupdate:entity-uuid\ndestroy:entity-uuid\n");std::filesystem::remove(log,error);return 0;
}
