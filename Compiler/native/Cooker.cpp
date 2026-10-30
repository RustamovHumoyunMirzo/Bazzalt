#include "Runtime/Engine.h"
#include "Runtime/NativeScriptRuntime.h"
#include <ryml.hpp>
#include <fstream>
#include <iostream>

std::string Text(ryml::ConstNodeRef n){auto v=n.val();return {v.str,v.len};}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
#else
int main(int argc,char** argv){
#endif
    try{
        if(argc==2&&std::filesystem::path(argv[1])=="--version"){std::cout<<"{\"core_version\":\""<<BAZZALT_CORE_VERSION<<"\"}\n";return 0;}
        if(argc!=4)throw std::runtime_error("Usage: BazzaltCooker project.bproject staging bindings.json");
        Bazzalt::Runtime::Engine engine;if(!engine.LoadProject(argv[1],false))throw std::runtime_error(engine.GetLastError());
        std::ifstream f(std::filesystem::path(argv[3]),std::ios::binary);std::string json((std::istreambuf_iterator<char>(f)),{});
        auto tree=ryml::parse_in_arena(ryml::csubstr(json.data(),json.size()));auto root=tree.rootref();
        if(root.has_child("startup")&&!Text(root["startup"]).empty())engine.GetProject().StartupScene=std::filesystem::u8path(Text(root["startup"]));
        auto startup=engine.GetProject().StartupScene;
        if(startup.empty()||startup.is_absolute()||*startup.lexically_normal().begin()==".."||startup.extension()!=".bscene"||!std::filesystem::is_regular_file(std::filesystem::path(argv[1]).parent_path()/startup))throw std::runtime_error("Select a valid project-relative startup scene in Build Settings");
        auto startupAsset=Bazzalt::AssetManager::GetAsset(std::filesystem::path(argv[1]).parent_path()/startup);
        if(!startupAsset||startupAsset->State!=Bazzalt::AssetState::Ready)throw std::runtime_error("Startup scene must be a ready asset inside the project's asset directory");
        std::vector<Bazzalt::Runtime::ScriptBinding> bindings;
        for(auto node:root["bindings"].children()){
            Bazzalt::Runtime::ScriptBinding value;value.Module=std::filesystem::u8path(Text(node["module"]));value.Entity=Text(node["entity"]);value.TypeName=Text(node["type"]);
            for(auto prop:node["properties"].children()){auto k=prop.key();value.Properties[{k.str,k.len}]=Text(prop);}bindings.push_back(std::move(value));
        }
        if(!engine.ExportCookedProject(argv[2],bindings))throw std::runtime_error(engine.GetLastError());
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
