#include "PosixIntegrity.h"
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <iostream>
int main(int argc,char** argv){
    bool quiet=false;for(int i=1;i<argc;++i)quiet|=std::string(argv[i])=="--verify-package"||std::string(argv[i])=="--smoke-test";
    try{
        std::array<char,32768> path{};auto size=readlink("/proc/self/exe",path.data(),path.size()-1);Bazzalt::Game::Require(size>0&&std::size_t(size)<path.size()-1,"Cannot locate player");std::filesystem::path executable(std::string(path.data(),size));
        // Validate libraries before their ELF constructors can execute. The
        // bootstrap has no dependency on the engine or game-owned shared code.
        auto verified=Bazzalt::Game::VerifyInventory(executable);
        auto* library=dlopen((executable.parent_path()/"libbazzalt_player.so").c_str(),RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(dlerror());
        auto entry=reinterpret_cast<int(*)(int,char**)>(dlsym(library,"BazzaltRunLinuxPlayer"));Bazzalt::Game::Require(entry,"Missing Linux player entry point");return entry(argc,argv);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(!quiet)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Bazzalt game error",error.what(),nullptr);return 1;}
}
