#pragma once
#include "Bazzalt/Input.h"
namespace Bazzalt::Runtime {
class InputAccess final {
public:
    static bool Initialize();
    static void Shutdown();
    static void SetActive(bool active);
    static void BeginFrame();
    static void KeyEvent(int key,bool pressed,bool repeat=false);
    static void ButtonEvent(int button,bool pressed);
    static void MotionEvent(float x,float y,float dx,float dy);
    static void ScrollEvent(float x,float y);
    static Detail::InputState* GetState(){return &Input::State();}
};
}
