# Gameplay time

Include `Bazzalt/Time.h`. `Bazzalt::Time` is a static, PascalCase, main-thread API.
The runtime advances the clock; gameplay never creates an engine or advances
time manually. A new Play session resets elapsed time, frame count, scale, and
timing configuration to their defaults.

## Slow motion and pause

```cpp
#include <Bazzalt/Time.h>

Bazzalt::Time::SetTimeScale(0.25f); // Quarter-speed gameplay.
Bazzalt::Time::SetSlowMotion(0.5f); // Convenience helper; default is 0.5.
Bazzalt::Time::Pause();            // Scale zero, not an editor pause.
Bazzalt::Time::Resume();           // Restore the last positive scale.
Bazzalt::Time::RestoreTimeScale(); // Normal speed, scale 1.
```

`SetTimeScale` accepts finite nonnegative values, including values above one for
fast-forward. `SetSlowMotion` accepts `(0, 1]`. Invalid configuration throws
`std::invalid_argument` without changing the previous setting. Repeated `Pause`
calls preserve the remembered resume scale.

Changing scale affects the next sampled gameplay frame; it does not rewrite an
already-running callback's frame delta. At scale zero, regular `OnUpdate`
callbacks still run with zero scaled delta. Unscaled time continues advancing,
while fixed updates stop. Editor navigation and presentation are not scaled.
The editor's Pause button suspends gameplay updates altogether.

## API reference

| Method | Meaning |
| --- | --- |
| `GetDeltaTime()` | Scaled, capped frame delta, or the current fixed step inside fixed callbacks. |
| `GetUnscaledDeltaTime()` | Uncapped real frame delta, or the current fixed step divided by scale inside fixed callbacks. |
| `GetSmoothDeltaTime()` | Exponentially smoothed scaled frame delta; zero on paused gameplay frames. |
| `GetTime()` | Accumulated scaled frame time; fixed time inside fixed callbacks. |
| `GetUnscaledTime()` | Accumulated unscaled frame time; fixed unscaled time inside fixed callbacks. |
| `GetRealtimeSinceStartup()` | Wall-clock seconds since this gameplay clock was initialized/reset, including editor pauses. |
| `GetFrameCount()` | Sampled gameplay frames, not presentation frames or fixed steps. |
| `GetTimeScale()`, `SetTimeScale(float)` | Gameplay speed multiplier; default `1`. |
| `SetSlowMotion(float = 0.5f)` | Set a positive slow-motion multiplier. |
| `RestoreTimeScale()` | Restore scale `1`. |
| `Pause()`, `Resume()`, `IsPaused()` | Scale-based pause controls. |
| `GetMaximumDeltaTime()`, `SetMaximumDeltaTime(float)` | Cap scaled frame delta, default `1/3` second; finite and positive. |
| `GetFixedDeltaTime()`, `SetFixedDeltaTime(float)` | Fixed simulation step in gameplay seconds, default `0.02`; minimum `0.0001`. |
| `GetFixedUnscaledDeltaTime()` | Configured fixed step divided by scale, or zero when paused. |
| `GetFixedTime()`, `GetFixedUnscaledTime()` | Accumulated fixed-step clocks. |
| `IsInFixedTimeStep()` | Whether the current callback is executing in a fixed step. |

Elapsed-time methods return `double`; deltas/configuration return `float`; frame
count returns `std::uint64_t`. Clock state is not serialized scene metadata.

## Fixed updates

`Behavior` and `System` support optional `OnFixedUpdate` callbacks, run before
regular updates. A frame can have zero or several fixed updates, using scaled
time accumulated from frame deltas. Catch-up is limited to 64 steps per frame;
excess whole-step backlog is dropped to prevent unbounded stalls. The maximum
frame delta also bounds catch-up after a hitch.

```cpp
#include <Bazzalt/Script.h>

COMPONENT(ClockExample) {
public:
    void OnUpdate(float deltaTime) override {
        // deltaTime == Bazzalt::Time::GetDeltaTime().
        // GetUnscaledDeltaTime() is useful for UI that ignores slow motion.
    }
    void OnFixedUpdate(float fixedDeltaTime) override {
        // GetDeltaTime() returns this fixed step in this callback.
        // Custom simulation and future physics belong here.
    }
};
```

For systems, override `OnFixedUpdate(Scene&, float)` alongside `OnUpdate`.
Disabled systems receive neither callback. Render-only editor ticks invoke no
gameplay callbacks and refresh the frame timestamp so resuming an editor pause
does not generate a large elapsed delta.

## Native script modules

Generated modules export optional versioned time-binding and fixed-update entry
points. The loader binds the runtime's shared clock before construction and
`OnCreate`; changing scale in a script affects systems and other scripts too.
Existing V1 modules without these optional exports remain loadable. Compiler
cache keys incorporate the wrapper version and `Time.h` to rebuild old wrappers.

`Detail::TimeState`, `ScriptRuntimeAccess`, and `src/Runtime/TimeAccess.h` are
service-binding details, not supported gameplay loop APIs.
