# Gameplay input

Include `Bazzalt/Input.h`. `Input` is a static, read-only gameplay API: no SDL
types, native handles, initialization, event polling, or application loop are
public. SDL3 3.2.30 is pinned by commit, built statically, and restricted to its
event subsystem. Qt owns the editor's windows and forwards Game-surface events
privately. Filament remains the only renderer. No SDL window is created.

## Keyboard and mouse API

All calls use PascalCase. Input is main-thread only.

| Method | Result |
| --- | --- |
| `IsActive()` | Play is running and the Game surface owns keyboard focus |
| `GetKey(KeyCode)` | Key is held |
| `GetKeyDown(KeyCode)` | Key transitioned to pressed in this gameplay frame |
| `GetKeyUp(KeyCode)` | Key transitioned to released in this gameplay frame |
| `GetAnyKey()` | Any keyboard key or mouse button is held |
| `GetMouseButton(MouseButton)` | Mouse button is held |
| `GetMouseButtonDown(MouseButton)` | Button pressed in this frame |
| `GetMouseButtonUp(MouseButton)` | Button released in this frame |
| `GetMousePosition()` | Position in Game-surface logical pixels, top-left origin |
| `GetMouseDelta()` | Accumulated pointer movement this frame, positive Y downward |
| `GetScrollDelta()` | Accumulated wheel steps this frame; supports fractional steps |
| `GetAxis(InputAxis)` | Digital Horizontal/Vertical or mouse/scroll delta |

`KeyCode` covers A–Z, digits, F1–F24, navigation, punctuation, keypad, and left/right
modifiers. Windows native make codes preserve physical letter keys on non-Latin
layouts; other platforms use Qt key identities. Invalid identifiers return false.
Mouse buttons are Left, Middle, Right, Back, and Forward. Horizontal uses A/D and
Left/Right; Vertical uses W/S and Up/Down. Opposing directions cancel. MouseX,
MouseY, ScrollX, and ScrollY expose their respective delta components without
smoothing. Axis mapping/rebinding, text entry, and gamepads are not provided yet.

```cpp
#include <Bazzalt/Input.h>
#include <Bazzalt/Script.h>

COMPONENT(PlayerControls) {
public:
    void OnUpdate(float deltaTime) override {
        const float horizontal = Bazzalt::Input::GetAxis(Bazzalt::InputAxis::Horizontal);
        if (Bazzalt::Input::GetKeyDown(Bazzalt::KeyCode::Space)) {
            // Begin a jump once, not once per frame while held.
        }
        (void)horizontal;
        (void)deltaTime;
    }
};
```

Held values persist across frames; Down/Up, motion, and wheel values reset at the
next gameplay frame. A press and release between ticks exposes both edges even
though the held value is false. OS key-repeat does not generate extra edges.
Input is updated before fixed and regular gameplay callbacks. Multiple fixed
updates in one frame see the same frame edges, so consume one-shot commands in
OnUpdate if they must execute only once.

## Focus and Play behavior

Play opens/activates Output Game and focuses its surface. Input is inactive in
Edit mode, while paused, when another panel or application is focused, when Game
is hidden, or while a modal dialog is active. Losing focus clears held states and
queued events immediately. Returning to Game requires fresh presses; it never
replays keys pressed elsewhere. Stop clears input before gameplay teardown.
Game keys do not trigger editor shortcuts while the surface owns focus.

Shift+Space on the focused, playing Game surface toggles Game maximization and
restores the previous docking layout on the second press. This editor shortcut
is not sent to game code. Maximized layouts are transient, not saved as the
workspace default.

Scene and Game share the active scene. Gameplay transform changes update Scene
geometry, selection outlines, icons, and gizmos in the same rendering frame.
Play snapshots active and inactive loaded scenes. Stop restores their authoring
values, the original dirty indicators, and authoring undo/redo history; runtime
edits are discarded. Do not save temporary Play values as authoring data.

## Gameplay DLL boundary

Generated modules export the optional `BazzaltBindInputV1` service binding. The
runtime binds its shared `Detail::InputState` before constructing behavior
instances, so each DLL observes the same input frame rather than a private copy.
Older V1 modules remain loadable. Wrapper generation and Input header changes
invalidate the compilation cache. `Detail` types and `ScriptRuntimeAccess` are
ABI implementation details, not APIs for gameplay code to initialize input.

Native tests cover edge/hold semantics, mouse accumulation, invalid keys, focus
reset, and sharing input through a real loaded module. Editor tests cover Play,
Stop restoration, focus gating, maximization, and loaded-scene folder renaming.
