#pragma once
#include "Bazzalt/Export.h"
#include <array>
#include <cstdint>
#include <string>
#include "Bazzalt/Math.h"

namespace Bazzalt {
namespace Runtime { class InputAccess; }
class ScriptRuntimeAccess;

// Physical key identifiers, independent of the platform's virtual-key values.
enum class KeyCode : std::uint16_t {
    Unknown=0,A=4,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    Digit1=30,Digit2,Digit3,Digit4,Digit5,Digit6,Digit7,Digit8,Digit9,Digit0,
    Enter=40,Escape,Backspace,Tab,Space,Minus,Equals,LeftBracket,RightBracket,Backslash,
    Semicolon=51,Apostrophe,Grave,Comma,Period,Slash,CapsLock,
    F1=58,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,PrintScreen,ScrollLock,Pause,
    Insert=73,Home,PageUp,Delete,End,PageDown,Right,Left,Down,Up,
    NumLock=83,KeypadDivide,KeypadMultiply,KeypadMinus,KeypadPlus,KeypadEnter,
    Keypad1,Keypad2,Keypad3,Keypad4,Keypad5,Keypad6,Keypad7,Keypad8,Keypad9,Keypad0,KeypadPeriod,
    F13=104,F14,F15,F16,F17,F18,F19,F20,F21,F22,F23,F24,
    LeftControl=224,LeftShift,LeftAlt,LeftSuper,RightControl,RightShift,RightAlt,RightSuper
};
enum class MouseButton : std::uint8_t { Left=1,Middle,Right,Back,Forward };
enum class InputAxis : std::uint8_t { Horizontal,Vertical,MouseX,MouseY,ScrollX,ScrollY };

namespace Detail {
struct InputState {
    bool Active=false;
    std::array<std::uint8_t,512> Keys{},KeysDown{},KeysUp{};
    std::array<std::uint8_t,6> Buttons{},ButtonsDown{},ButtonsUp{};
    Vec2 MousePosition{},MouseDelta{},ScrollDelta{};
    std::string TextInput;
};
}

// Read-only, main-thread gameplay input. The runtime owns focus and frame flow.
class Input final {
public:
    Input()=delete;
    [[nodiscard]] static bool IsActive(){return State().Active;}
    [[nodiscard]] static bool GetKey(KeyCode key){return Key(State().Keys,key);}
    [[nodiscard]] static bool GetKeyDown(KeyCode key){return Key(State().KeysDown,key);}
    [[nodiscard]] static bool GetKeyUp(KeyCode key){return Key(State().KeysUp,key);}
    [[nodiscard]] static bool GetMouseButton(MouseButton button){return Button(State().Buttons,button);}
    [[nodiscard]] static bool GetMouseButtonDown(MouseButton button){return Button(State().ButtonsDown,button);}
    [[nodiscard]] static bool GetMouseButtonUp(MouseButton button){return Button(State().ButtonsUp,button);}
    [[nodiscard]] static Vec2 GetMousePosition(){return IsActive()?State().MousePosition:Vec2{};}
    [[nodiscard]] static Vec2 GetMouseDelta(){return IsActive()?State().MouseDelta:Vec2{};}
    [[nodiscard]] static Vec2 GetScrollDelta(){return IsActive()?State().ScrollDelta:Vec2{};}
    [[nodiscard]] static const std::string& GetTextInput(){return IsActive()?State().TextInput:s_default.TextInput;}
    [[nodiscard]] static bool GetAnyKey(){if(!IsActive())return false;for(auto key:State().Keys)if(key)return true;for(auto button:State().Buttons)if(button)return true;return false;}
    [[nodiscard]] static float GetAxis(InputAxis axis){
        switch(axis){
        case InputAxis::Horizontal:return float(GetKey(KeyCode::D)||GetKey(KeyCode::Right))-float(GetKey(KeyCode::A)||GetKey(KeyCode::Left));
        case InputAxis::Vertical:return float(GetKey(KeyCode::W)||GetKey(KeyCode::Up))-float(GetKey(KeyCode::S)||GetKey(KeyCode::Down));
        case InputAxis::MouseX:return GetMouseDelta().X;case InputAxis::MouseY:return GetMouseDelta().Y;
        case InputAxis::ScrollX:return GetScrollDelta().X;case InputAxis::ScrollY:return GetScrollDelta().Y;
        }return 0;
    }
private:
    friend class Runtime::InputAccess;
    friend class ScriptRuntimeAccess;
    static bool Key(const std::array<std::uint8_t,512>& values,KeyCode key){const auto index=static_cast<std::size_t>(key);return IsActive()&&index>0&&index<values.size()&&values[index];}
    static bool Button(const std::array<std::uint8_t,6>& values,MouseButton button){const auto index=static_cast<std::size_t>(button);return IsActive()&&index>0&&index<values.size()&&values[index];}
    BAZZALT_API static Detail::InputState& State();
    BAZZALT_API static void Bind(Detail::InputState* state);
    inline static Detail::InputState s_default{};
    inline static Detail::InputState* s_state=&s_default;
};
}
