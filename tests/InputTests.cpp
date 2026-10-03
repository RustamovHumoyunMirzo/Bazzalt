#include <cassert>
#include "Bazzalt/Input.h"
#include "Runtime/InputAccess.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/UUID.h"
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc,char** argv){
    using namespace Bazzalt;using Runtime::InputAccess;
    assert(InputAccess::Initialize());assert(!Input::IsActive());
    InputAccess::KeyEvent(4,true);InputAccess::BeginFrame();assert(!Input::GetAnyKey());
    InputAccess::SetActive(true);InputAccess::KeyEvent(26,true);InputAccess::KeyEvent(26,true,true);
    InputAccess::BeginFrame();assert(Input::GetKey(KeyCode::W)&&Input::GetKeyDown(KeyCode::W));assert(Input::GetAxis(InputAxis::Vertical)==1);
    InputAccess::BeginFrame();assert(Input::GetKey(KeyCode::W)&&!Input::GetKeyDown(KeyCode::W));
    InputAccess::KeyEvent(26,false);InputAccess::BeginFrame();assert(!Input::GetKey(KeyCode::W)&&Input::GetKeyUp(KeyCode::W));
    InputAccess::BeginFrame();assert(!Input::GetKeyUp(KeyCode::W));
    InputAccess::KeyEvent(4,true);InputAccess::KeyEvent(4,false);InputAccess::ButtonEvent(1,true);
    InputAccess::MotionEvent(20,30,2,3);InputAccess::MotionEvent(25,32,5,2);InputAccess::ScrollEvent(0,1);
    InputAccess::BeginFrame();assert(Input::GetKeyDown(KeyCode::A)&&Input::GetKeyUp(KeyCode::A)&&!Input::GetKey(KeyCode::A));
    assert(Input::GetMouseButtonDown(MouseButton::Left)&&Input::GetMouseButton(MouseButton::Left));
    assert(Input::GetMousePosition().X==25&&Input::GetMouseDelta().X==7&&Input::GetMouseDelta().Y==5&&Input::GetScrollDelta().Y==1);
    InputAccess::BeginFrame();assert(Input::GetMouseDelta().X==0&&Input::GetScrollDelta().Y==0);
    InputAccess::KeyEvent(4,true);InputAccess::SetActive(false);InputAccess::SetActive(true);InputAccess::BeginFrame();assert(!Input::GetAnyKey());
    InputAccess::KeyEvent(-1,true);InputAccess::KeyEvent(512,true);InputAccess::ButtonEvent(255,true);InputAccess::BeginFrame();assert(!Input::GetAnyKey());
    assert(!Input::GetKey(static_cast<KeyCode>(65535))&&!Input::GetMouseButton(static_cast<MouseButton>(255)));
    if(argc>1){
        const auto log=std::filesystem::temp_directory_path()/("bazzalt-input-"+UUID::Generate().ToString()+".log");
        Runtime::NativeScriptRuntime scripts;Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(argv[1]);binding.TypeName="LifecycleProbe";binding.Entity=UUID{1,1}.ToString();binding.Properties["LogPath"]=log.string();std::string error;
        assert(scripts.Configure({binding},error));assert(scripts.Start(error));
        InputAccess::KeyEvent(26,true);InputAccess::BeginFrame();scripts.Update(Time::GetDeltaTime());scripts.Stop();
        std::ifstream stream(log);const std::string content((std::istreambuf_iterator<char>(stream)),{});assert(content.find("input_w:")!=std::string::npos);stream.close();std::filesystem::remove(log);
    }
    InputAccess::SetActive(false);assert(!Input::GetMouseButton(MouseButton::Left));InputAccess::Shutdown();assert(!Input::IsActive());
}
