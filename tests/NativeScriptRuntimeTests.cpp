#include "Runtime/NativeScriptRuntime.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

int main(int argc,char** argv){
    assert(argc==2);const auto log=std::filesystem::temp_directory_path()/"bazzalt-script-lifecycle.txt";std::error_code error;std::filesystem::remove(log,error);
    Bazzalt::Runtime::NativeScriptRuntime runtime;Bazzalt::Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(argv[1]);binding.Entity="entity-uuid";binding.TypeName="LifecycleProbe";binding.Properties["LogPath"]=log.string();std::string message;
    assert(runtime.Configure({binding},message));assert(runtime.Start(message));runtime.Update(0.016f);runtime.Stop();
    std::ifstream stream(log);std::string contents((std::istreambuf_iterator<char>(stream)),{});assert(contents=="create:entity-uuid\nupdate:entity-uuid\ndestroy:entity-uuid\n");std::filesystem::remove(log,error);return 0;
}
