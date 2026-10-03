#include "Bazzalt/Input.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/Shader.h"

namespace Bazzalt {
Detail::MaterialServices* Detail::BoundMaterialServices = nullptr;
Detail::InputState& Input::State() { return *s_state; }
void Input::Bind(Detail::InputState* state) { s_state = state ? state : &s_default; }
Detail::TimeState& Time::State() { return *s_state; }
void Time::Bind(Detail::TimeState* state) { s_state = state ? state : &s_default; }
}
