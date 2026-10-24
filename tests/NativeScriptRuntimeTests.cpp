#include "Runtime/NativeScriptRuntime.h"
#include "Runtime/Engine.h"
#include "Bazzalt/Script.h"
#include "Runtime/TimeAccess.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

int main(int argc,char** argv){
    assert(argc==2);const auto log=std::filesystem::temp_directory_path()/"bazzalt-script-lifecycle.txt";std::error_code error;std::filesystem::remove(log,error);
    Bazzalt::Runtime::Engine engine;auto entity=engine.GetScene().CreateEntity("Probe");
    Bazzalt::Runtime::NativeScriptRuntime runtime;Bazzalt::Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(argv[1]);binding.Entity=entity.GetUUID().ToString();binding.TypeName="LifecycleProbe";binding.Properties["LogPath"]=log.string();std::string message;
    Bazzalt::Runtime::TimeAccess::Reset();
    assert(!runtime.Configure({binding,binding},message));assert(message.find("Duplicate")!=std::string::npos);
    auto conflicting=binding;conflicting.Entity="another-entity";conflicting.Module="another-module.dll";
    assert(!runtime.Configure({binding,conflicting},message));assert(message.find("Ambiguous")!=std::string::npos);
    assert(runtime.Configure({binding},message));assert(runtime.Start(message));
    assert(Bazzalt::Scripts::Find(entity,"LifecycleProbe"));assert(Bazzalt::Scripts::Get<Bazzalt::Behavior>(entity,"LifecycleProbe").GetEntityUUID()==binding.Entity);
    assert(Bazzalt::Time::GetTimeScale()==0.25f);
    Bazzalt::Runtime::TimeAccess::Advance(0.064);
    {Bazzalt::Runtime::TimeAccess::FixedScope fixed;runtime.FixedUpdate(Bazzalt::Time::GetDeltaTime());}
    runtime.Update(Bazzalt::Time::GetDeltaTime());runtime.LateUpdate(Bazzalt::Time::GetDeltaTime());runtime.Stop();
    std::ifstream stream(log);std::string contents((std::istreambuf_iterator<char>(stream)),{});stream.close();std::string expected;for(const auto* action:{"create","fixed","update","late","destroy"})expected+=std::string(action)+":"+binding.Entity+"\n";assert(contents==expected);assert(!Bazzalt::Scripts::Find(entity,"LifecycleProbe"));
    std::filesystem::remove(log,error);return 0;
}
