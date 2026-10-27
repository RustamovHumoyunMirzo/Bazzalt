#include "Bazzalt/Extension.h"
#include "Bazzalt/Scene.h"
#include "Runtime/ExtensionAccess.h"
#include "Runtime/LuaRuntime.h"
#include "Runtime/Engine.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace Bazzalt;
int main(int argc,char** argv){
    assert(argc==3);auto root=std::filesystem::temp_directory_path()/("bazzalt-extension-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root/"ext");
    struct Cleanup{std::filesystem::path Path;~Cleanup(){Runtime::ExtensionAccess::Clear();std::filesystem::remove_all(Path);}} cleanup{root};
    auto companion=std::filesystem::path(argv[2]);std::filesystem::create_directories(root/"ext/libs");std::filesystem::copy_file(companion,root/"ext/libs"/companion.filename());
    auto binary=std::filesystem::path(argv[1]);std::filesystem::copy_file(binary,root/"ext"/binary.filename());
    auto source=root/"module.lua";{std::ofstream f(source);f<<"local M = {}; function M.Value() return Bazzalt.require('fixture.Helper').Value() end;return M";}
    std::string error;assert(Runtime::LuaRuntime::Import(source,root/"ext/module.blua",error));
    {std::ofstream f(root/"helper.lua");f<<"return {Value=function() return Bazzalt.require('FixtureMath').Double(3) end}";}assert(Runtime::LuaRuntime::Import(root/"helper.lua",root/"ext/helper.blua",error));
    {std::ofstream f(root/"ext/runtime.json");f<<"{\"FormatVersion\":1,\"NativeDirectories\":[\"libs\"],\"NativeLibraries\":[\"libs/"<<companion.filename().string()<<"\"],\"Assets\":{\"fixture:Data\":\"12345678-1234-4234-8234-123456789abc\"},\"Modules\":[{\"Name\":\"FixtureMath\",\"Owner\":\"fixture\",\"Public\":true,\"Language\":\"cpp\",\"Path\":\""<<binary.filename().string()<<"\"},{\"Name\":\"FixtureLua\",\"Owner\":\"fixture\",\"Public\":true,\"Language\":\"lua\",\"Path\":\"module.blua\"},{\"Name\":\"fixture.Helper\",\"Owner\":\"fixture\",\"Public\":false,\"Language\":\"lua\",\"Path\":\"helper.blua\"}]}";}

    Runtime::ExtensionAccess::Configure(root);assert(Extensions::HasModule("FixtureMath"));assert(Extensions::HasModule("FixtureLua"));assert(Extensions::GetModules().size()==2);assert(!Extensions::HasModule("fixture.Helper"));assert(Extensions::GetAsset("fixture","Data"));assert(std::visit([](const auto& v){if constexpr(std::is_arithmetic_v<std::decay_t<decltype(v)>>)return double(v)==6;else return false;},Extensions::Call("FixtureLua","Value")));
    assert(std::get<double>(Extensions::Call("FixtureMath","Double",{double(4)}))==8);
    auto vector=std::get<Vec3>(Extensions::Call("FixtureMath","Offset",{Vec3{1,2,3},Vec3{2,3,4}}));assert(vector.X==3&&vector.Y==5&&vector.Z==7);
    bool rejected=false;try{Extensions::Call("FixtureMath","Missing");}catch(...){rejected=true;}assert(rejected);
    rejected=false;try{Extensions::Call("FixtureMath","Double",{});}catch(...){rejected=true;}assert(rejected);
    {std::ofstream f(source);f<<"return {OnEnable=function(self) assert(not pcall(function() Bazzalt.require('fixture.Helper') end));local M=Bazzalt:require('FixtureLua');assert(M.Value()==6);assert(Bazzalt.require('FixtureMath').Double(5)==10);Editor:Log('loaded');Editor:SetEnabled('button',true) end}";}
    assert(Runtime::LuaRuntime::Import(source,root/"controller.blua",error));int called=0;
    {Runtime::ExtensionScript host(root/"controller.blua","Editor",{"Log","SetEnabled"},[&](const std::string& method,const ExtensionArguments& args)->ExtensionValue{if(method=="Log"){assert(std::get<std::string>(args[0])=="loaded");++called;}if(method=="SetEnabled"){assert(std::get<std::string>(args[0])=="button"&&std::get<bool>(args[1]));++called;}return {};});host.Call("OnEnable");assert(called==2);host.Call("OnDisable");}
    Runtime::Engine engine;assert(engine.GetScene().SetExtensionProperty("fixture","Speed","3.5"));auto scene=root/"extension.bscene";assert(engine.SaveScene(scene));assert(engine.GetScene().SetExtensionProperty("fixture","Speed","9"));assert(engine.LoadScene(scene));assert(engine.GetScene().GetExtensionProperty("fixture","Speed")=="3.5");
    Runtime::ExtensionAccess::Clear();assert(!Extensions::HasModule("FixtureMath"));
}
