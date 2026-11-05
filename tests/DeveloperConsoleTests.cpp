#include "Bazzalt/DeveloperConsole.h"
#include "Bazzalt/Application.h"
#include "Bazzalt/Time.h"
#include "Runtime/DeveloperConsoleAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/InputAccess.h"
#include "Rendering/RenderBackend.h"
#include "Runtime/Engine.h"
#include "Runtime/GlobalEventsAccess.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <thread>
#include "Rendering/DeveloperConsoleOverlay.h"
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <atomic>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <utils/EntityManager.h>
#include <iostream>
using namespace Bazzalt;
template<class F> bool Throws(F f){try{f();}catch(const std::exception&){return true;}return false;}
int Pixels(){
    Runtime::DeveloperConsoleAccess::BindThread();Runtime::DeveloperConsoleAccess::Initialize(true);
    Runtime::RenderBackend backend;assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(false));
    {
        Runtime::DeveloperConsoleOverlay overlay(backend);auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();auto* swap=engine.createSwapChain(320u,240u,filament::SwapChain::CONFIG_READABLE);
        auto cameraEntity=engine.getEntityManager().create();auto* camera=engine.createCamera(cameraEntity);auto* view=engine.createView();auto* scene=engine.createScene();view->setScene(scene);view->setCamera(camera);view->setDithering(filament::View::Dithering::NONE);view->setViewport({0,0,320,240});
        const auto capture=[&](){std::vector<std::uint8_t> image(320*240*4);std::atomic<bool> ready=false;for(int i=0;i<3;++i){overlay.Prepare({320,240});assert(renderer.beginFrame(swap));renderer.setClearOptions({.clearColor={0,0,0,1},.clear=true});renderer.render(view);overlay.Render({320,240});if(i==2)renderer.readPixels(0,0,320,240,{image.data(),image.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* context){static_cast<std::atomic<bool>*>(context)->store(true);},&ready});renderer.endFrame();engine.flushAndWait();engine.pumpMessageQueues();}assert(ready);return image;};
        auto blank=capture();DeveloperConsole::Log("GPU console ready");DeveloperConsole::SetVisible(true);auto shown=capture();DeveloperConsole::SetVisible(false);auto hidden=capture();unsigned changed=0,restored=0,significant=0,restoreSignificant=0,maxRestore=0;for(std::size_t i=0;i<blank.size();i+=4){unsigned d=0,r=0;for(unsigned c=0;c<3;++c){d=std::max(d,unsigned(std::abs(int(shown[i+c])-int(blank[i+c]))));r=std::max(r,unsigned(std::abs(int(hidden[i+c])-int(blank[i+c]))));}if(d>3)++significant;if(r>3)++restoreSignificant;maxRestore=std::max(maxRestore,r);if(shown[i]!=blank[i]||shown[i+1]!=blank[i+1]||shown[i+2]!=blank[i+2])++changed;if(hidden[i]!=blank[i]||hidden[i+1]!=blank[i+1]||hidden[i+2]!=blank[i+2])++restored;}std::cout<<"Console changed pixels="<<changed<<", hide differences="<<restored<<", significant="<<significant<<", restore significant="<<restoreSignificant<<", max restore="<<maxRestore<<std::endl;assert(changed>100&&changed<320*240/2);assert(restored==0);
        engine.destroy(view);engine.destroy(scene);engine.destroyCameraComponent(cameraEntity);engine.getEntityManager().destroy(cameraEntity);engine.destroy(swap);
    }backend.Shutdown();Runtime::DeveloperConsoleAccess::Reset();return 0;
}
int main(int argc,char**){
    if(argc>1)return Pixels();
    Runtime::DeveloperConsoleAccess::BindThread();Time::SetTimeScale(200);Runtime::DeveloperConsoleAccess::Initialize(false);
    assert(!DeveloperConsole::Execute("echo disabled").Success);assert(DeveloperConsole::GetHistory().empty());
    DeveloperConsole::SetEnabled(true);assert(DeveloperConsole::Execute("app.target_fps 144").Success);assert(Application::GetTargetFrameRate()==144);
    assert(!DeveloperConsole::Execute("time.scale 0.25").Success);DeveloperConsole::SetCheatsAllowed(true);assert(DeveloperConsole::Execute("time.scale 0.25").Success);assert(Time::GetTimeScale()==.25f);DeveloperConsole::SetCheatsAllowed(false);assert(Time::GetTimeScale()==1);
    CVar::Register({"game.speed","Speed",2.0,CVarFlags::Archive,0,10});assert(CVar::GetInfo("GAME.SPEED").Type==CVarType::Float);
    assert(Throws([]{CVar::Register({"Game.Speed","duplicate",true});}));assert(Throws([]{CVar::SetValue("game.speed",std::int64_t(3));}));assert(Throws([]{CVar::SetValue("game.speed",11.0);}));assert(Throws([]{CVar::SetValue("game.speed",std::numeric_limits<double>::infinity());}));
    for(auto text:{"nan","inf","1oops"," 1","1 ","1e99999"})assert(Throws([&]{CVar::SetFromString("game.speed",text);}));
    assert(DeveloperConsole::Execute("set game.speed 3.5").Success);assert(std::get<double>(CVar::GetValue("game.speed"))==3.5);
    CVar::Register({"game.title","Title",std::string("Default"),CVarFlags::Archive});assert(DeveloperConsole::Execute("game.title \"two words\"").Success);assert(std::get<std::string>(CVar::GetValue("game.title"))=="two words");assert(DeveloperConsole::Execute("game.title \"\"").Success);assert(std::get<std::string>(CVar::GetValue("game.title")).empty());
    assert(!DeveloperConsole::Execute("game.title \"unclosed").Success);assert(!DeveloperConsole::Execute("echo one\necho two").Success);assert(DeveloperConsole::Execute("echo hello;quit").Output=="hello;quit");assert(DeveloperConsole::Execute("echo C:\\Users\\Game").Output=="C:\\Users\\Game");
    std::string maximum="echo";for(unsigned i=0;i<64;++i)maximum+=" x";assert(DeveloperConsole::Execute(maximum).Success);assert(!DeveloperConsole::Execute(maximum+" x").Success);
    CVar::Register({"game.locked","Readonly",true,CVarFlags::ReadOnly});assert(Throws([]{CVar::Toggle("game.locked");}));
    int changes=0;auto subscription=CVar::Subscribe([&](const CVarChange& e){if(e.Name=="game.speed")++changes;});CVar::SetValue("game.speed",4.0);CVar::SetValue("game.speed",4.0);assert(changes==1);assert(CVar::Unsubscribe(subscription));
    CVar::Register({"game.restart","Latched",std::int64_t(1),CVarFlags::RestartRequired|CVarFlags::Archive,0,4});CVar::SetValue("game.restart",std::int64_t(2));assert(std::get<std::int64_t>(CVar::GetValue("game.restart"))==1);assert(CVar::GetInfo("game.restart").PendingValue);CVar::ApplyPendingChanges();assert(std::get<std::int64_t>(CVar::GetValue("game.restart"))==2);
    auto archived=CVar::ExportArchive();assert(archived.at("game.speed")=="4");auto invalid=archived;invalid["game.speed"]="8";invalid["game.title"] = std::string(5000,'x');assert(Throws([&]{CVar::ImportArchive(invalid);}));assert(std::get<double>(CVar::GetValue("game.speed"))==4);
    bool allPublished=false;subscription=CVar::Subscribe([&](const CVarChange& e){if(e.Name=="game.speed")allPublished=std::get<std::string>(CVar::GetValue("game.title"))=="Loaded";});CVar::ImportArchive({{"game.speed","5"},{"game.title","Loaded"}});assert(allPublished);CVar::Unsubscribe(subscription);
    std::uint64_t command=0;command=DeveloperConsole::RegisterCommand({"game.once","self removal","game.once",false},[&](const auto& args){assert(args.size()==1&&args[0]=="hello world");assert(DeveloperConsole::UnregisterCommand(command));return "done";});assert(DeveloperConsole::Execute("game.once 'hello world'").Output=="done");assert(!DeveloperConsole::Execute("game.once").Success);
    DeveloperConsole::RegisterCommand({"game.throw","exception","game.throw",false},[](const auto&)->std::string{throw std::runtime_error("expected failure");});assert(!DeveloperConsole::Execute("game.throw").Success);
    DeveloperConsole::RegisterCommand({"game.recursive","recursion","game.recursive",false},[](const auto&){return DeveloperConsole::Execute("game.recursive").Output;});assert(DeveloperConsole::Execute("game.recursive").Output.find("recursion")!=std::string::npos);
    assert(Throws([]{DeveloperConsole::RegisterCommand({"game.speed","collision","",false},[](auto&){return "";});}));assert(!DeveloperConsole::Complete("game.").empty());assert(DeveloperConsole::Complete("set game.s")==std::vector<std::string>{"set game.speed"});
    int owner=0;auto old=Runtime::ApplicationEventsAccess::SetOwner(&owner);int called=0;CVar::Register({"owned.value","",std::int64_t(1)},[&](auto&){++called;});DeveloperConsole::RegisterCommand({"owned.command","","",false},[](auto&){return "owned";});Runtime::ApplicationEventsAccess::SetOwner(old);CVar::SetValue("owned.value",std::int64_t(9));assert(called==1);Runtime::ApplicationEventsAccess::DisconnectOwner(&owner);assert(!CVar::Exists("owned.value"));assert(!DeveloperConsole::Execute("owned.command").Success);old=Runtime::ApplicationEventsAccess::SetOwner(&owner);CVar::Register({"owned.value","",std::int64_t(1)});Runtime::ApplicationEventsAccess::SetOwner(old);assert(std::get<std::int64_t>(CVar::GetValue("owned.value"))==9);Runtime::ApplicationEventsAccess::DisconnectOwner(&owner);
    DeveloperConsole::SetHistoryLimit(2);DeveloperConsole::Execute("echo first");DeveloperConsole::Execute("echo second");DeveloperConsole::Execute("echo third");auto history=DeveloperConsole::GetHistory();assert(history.size()==2&&history.back()=="echo third");
    DeveloperConsole::Clear();DeveloperConsole::SetMessageLimit(1000);for(int i=0;i<130;++i)DeveloperConsole::Log(std::string(65536,'x'),ConsoleLevel::Info,"Budget");assert(DeveloperConsole::GetMessages().size()<130);DeveloperConsole::Clear();DeveloperConsole::SetMessageLimit(4);std::thread worker([]{for(int i=0;i<20;++i)DeveloperConsole::Log("worker",ConsoleLevel::Warning,"Worker");assert(!DeveloperConsole::Execute("echo forbidden").Success);});worker.join();assert(DeveloperConsole::GetMessages().size()==4);ConsoleFilter filter;filter.Source="Worker";assert(!DeveloperConsole::GetMessages(filter).empty());DeveloperConsole::Clear(filter);assert(DeveloperConsole::GetMessages(filter).empty());
    DeveloperConsole::SetVisible(true);assert(DeveloperConsole::IsVisible());DeveloperConsole::SetEnabled(false);assert(!DeveloperConsole::IsVisible());
    Runtime::DeveloperConsoleAccess::Initialize(true);
    {Runtime::RenderBackend backend;assert(backend.Initialize(true));Runtime::InputAccess::Initialize();Runtime::InputAccess::SetActive(true);Runtime::InputAccess::KeyEvent(int(KeyCode::Grave),true);Runtime::InputAccess::BeginFrame();assert(backend.ProcessDeveloperConsoleInput());assert(DeveloperConsole::IsVisible());Runtime::InputAccess::KeyEvent(int(KeyCode::Escape),true);Runtime::InputAccess::BeginFrame();assert(backend.ProcessDeveloperConsoleInput());assert(!DeveloperConsole::IsVisible());Runtime::InputAccess::BeginFrame();assert(!backend.ProcessDeveloperConsoleInput());Runtime::InputAccess::Shutdown();backend.Shutdown();}
    auto closing=Runtime::GlobalEventsAccess::SetClosingOwner(&owner);old=Runtime::ApplicationEventsAccess::SetOwner(&owner);
    assert(Throws([]{CVar::Register({"closing.value","",true});}));assert(Throws([]{CVar::Subscribe([](auto&){});}));assert(Throws([]{DeveloperConsole::RegisterCommand({"closing.command","","",false},[](auto&){return "";});}));Runtime::ApplicationEventsAccess::SetOwner(old);Runtime::GlobalEventsAccess::SetClosingOwner(closing);
    {Runtime::Engine engine;engine.SetEditorHost(true);assert(engine.Init(true));engine.StopScripts();engine.EndApplication();assert(DeveloperConsole::Execute("echo editor after stop").Success);engine.Shutdown();}
    Runtime::DeveloperConsoleAccess::Reset();assert(CVar::GetAll().empty());
}
