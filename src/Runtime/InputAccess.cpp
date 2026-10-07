#include "Runtime/InputAccess.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Bazzalt::Runtime {
namespace { unsigned references=0; }
namespace { std::string pendingText; }
bool InputAccess::Initialize(){
    if(references++>0)return true;
    if(!SDL_InitSubSystem(SDL_INIT_EVENTS)){references=0;return false;}
    Input::Bind(GetState());SetActive(false);return true;
}
void InputAccess::Shutdown(){if(!references||--references)return;SetActive(false);SDL_QuitSubSystem(SDL_INIT_EVENTS);}
void InputAccess::SetActive(bool active){
    auto& state=*GetState();if(state.Active==active&&active)return;
    state={};state.Active=active;pendingText.clear();
    // Discard queued presses on focus loss, so none can leak into a later Play.
    if(references)SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_MOUSE_WHEEL);
}
void InputAccess::KeyEvent(int key,bool pressed,bool repeat){
    if(!Input::IsActive()||key<=0||key>=512||repeat)return;
    SDL_Event event{};event.type=pressed?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;event.key.scancode=static_cast<SDL_Scancode>(key);event.key.down=pressed;SDL_PushEvent(&event);
}
void InputAccess::ButtonEvent(int button,bool pressed){
    if(!Input::IsActive()||button<1||button>5)return;
    SDL_Event event{};event.type=pressed?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;event.button.button=static_cast<Uint8>(button);event.button.down=pressed;SDL_PushEvent(&event);
}
void InputAccess::MotionEvent(float x,float y,float dx,float dy){
    if(!Input::IsActive()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(dx)||!std::isfinite(dy))return;
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=x;event.motion.y=y;event.motion.xrel=dx;event.motion.yrel=dy;SDL_PushEvent(&event);
}
void InputAccess::ScrollEvent(float x,float y){
    if(!Input::IsActive()||!std::isfinite(x)||!std::isfinite(y))return;
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_WHEEL;event.wheel.x=x;event.wheel.y=y;SDL_PushEvent(&event);
}
void InputAccess::BeginFrame(){
    auto& state=*GetState();state.KeysDown.fill(0);state.KeysUp.fill(0);state.ButtonsDown.fill(0);state.ButtonsUp.fill(0);state.MouseDelta={};state.ScrollDelta={};
    state.TextInput=std::move(pendingText);pendingText.clear();
    if(!references)return;
    SDL_Event event{};while(SDL_PollEvent(&event)){
        if(!state.Active)continue;
        if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP){
            const int key=event.key.scancode;if(key<=0||key>=512||event.key.repeat)continue;
            const bool down=event.type==SDL_EVENT_KEY_DOWN;
            if(down&&!state.Keys[key])state.KeysDown[key]=1;
            if(!down&&state.Keys[key])state.KeysUp[key]=1;
            state.Keys[key]=down;
        }else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP){
            const int button=event.button.button;if(button<1||button>5)continue;
            const bool down=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
            if(down&&!state.Buttons[button])state.ButtonsDown[button]=1;
            if(!down&&state.Buttons[button])state.ButtonsUp[button]=1;
            state.Buttons[button]=down;
        }else if(event.type==SDL_EVENT_MOUSE_MOTION){state.MousePosition={event.motion.x,event.motion.y};state.MouseDelta.X+=event.motion.xrel;state.MouseDelta.Y+=event.motion.yrel;}
        else if(event.type==SDL_EVENT_MOUSE_WHEEL){state.ScrollDelta.X+=event.wheel.x;state.ScrollDelta.Y+=event.wheel.y;}
        else if(event.type==SDL_EVENT_TEXT_INPUT&&event.text.text&&state.TextInput.size()<65536){state.TextInput.append(event.text.text,0,std::min<std::size_t>(std::strlen(event.text.text),65536-state.TextInput.size()));}
    }
}
void InputAccess::TextEvent(const std::string& text){if(Input::IsActive()&&pendingText.size()<65536)pendingText.append(text,0,std::min<std::size_t>(text.size(),65536-pendingText.size()));}
}
