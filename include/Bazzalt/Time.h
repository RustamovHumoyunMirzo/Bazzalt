#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Bazzalt {
namespace Runtime { class TimeAccess; }
class ScriptRuntimeAccess;

namespace Detail {
// Shared with dynamically loaded gameplay modules, not an application loop API.
struct TimeState {
    float Scale = 1.0f, ResumeScale = 1.0f;
    float Delta = 0.0f, UnscaledDelta = 0.0f, SmoothDelta = 0.0f;
    float MaximumDelta = 1.0f / 3.0f;
    float FixedDelta = 0.02f;
    float CurrentFixedDelta = 0.02f, CurrentFixedUnscaledDelta = 0.02f;
    double FixedElapsed = 0.0, FixedUnscaledElapsed = 0.0;
    bool InFixedStep = false;
    double Elapsed = 0.0, UnscaledElapsed = 0.0;
    std::uint64_t Frames = 0;
    std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now();
};
}

/// Main-thread gameplay clock, advanced exclusively by the runtime.
class Time final {
public:
    Time() = delete;
    [[nodiscard]] static float GetDeltaTime() { return State().InFixedStep ? State().CurrentFixedDelta : State().Delta; }
    [[nodiscard]] static float GetUnscaledDeltaTime() { return State().InFixedStep ? State().CurrentFixedUnscaledDelta : State().UnscaledDelta; }
    [[nodiscard]] static float GetSmoothDeltaTime() { return State().SmoothDelta; }
    [[nodiscard]] static double GetTime() { return State().InFixedStep ? State().FixedElapsed : State().Elapsed; }
    [[nodiscard]] static double GetUnscaledTime() { return State().InFixedStep ? State().FixedUnscaledElapsed : State().UnscaledElapsed; }
    [[nodiscard]] static double GetRealtimeSinceStartup() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - State().Started).count();
    }
    [[nodiscard]] static std::uint64_t GetFrameCount() { return State().Frames; }
    [[nodiscard]] static float GetTimeScale() { return State().Scale; }
    [[nodiscard]] static float GetFixedDeltaTime() { return State().FixedDelta; }
    [[nodiscard]] static float GetFixedUnscaledDeltaTime() { return State().Scale > 0.0f ? State().FixedDelta / State().Scale : 0.0f; }
    [[nodiscard]] static double GetFixedTime() { return State().FixedElapsed; }
    [[nodiscard]] static double GetFixedUnscaledTime() { return State().FixedUnscaledElapsed; }
    [[nodiscard]] static bool IsInFixedTimeStep() { return State().InFixedStep; }
    static void SetFixedDeltaTime(float seconds) {
        if (!std::isfinite(seconds) || seconds < 0.0001f) throw std::invalid_argument("Fixed delta time must be finite and at least 0.0001 seconds");
        State().FixedDelta = seconds;
    }
    static void SetTimeScale(float scale) {
        if (!std::isfinite(scale) || scale < 0.0f) throw std::invalid_argument("Time scale must be finite and nonnegative");
        auto& state = State();
        if (scale == 0.0f && state.Scale > 0.0f) state.ResumeScale = state.Scale;
        if (scale > 0.0f) state.ResumeScale = scale;
        state.Scale = scale;
    }
    static void SetSlowMotion(float scale = 0.5f) {
        if (!std::isfinite(scale) || scale <= 0.0f || scale > 1.0f) throw std::invalid_argument("Slow motion scale must be in (0, 1]");
        SetTimeScale(scale);
    }
    static void RestoreTimeScale() { SetTimeScale(1.0f); }
    static void Pause() { SetTimeScale(0.0f); }
    static void Resume() { SetTimeScale(State().ResumeScale); }
    [[nodiscard]] static bool IsPaused() { return State().Scale == 0.0f; }
    [[nodiscard]] static float GetMaximumDeltaTime() { return State().MaximumDelta; }
    static void SetMaximumDeltaTime(float seconds) {
        if (!std::isfinite(seconds) || seconds <= 0.0f) throw std::invalid_argument("Maximum delta time must be finite and positive");
        State().MaximumDelta = seconds;
    }
private:
    friend class Runtime::TimeAccess;
    friend class ScriptRuntimeAccess;
    static Detail::TimeState& State() { return *s_state; }
    static void Bind(Detail::TimeState* state) { s_state = state ? state : &s_default; }
    inline static Detail::TimeState s_default{};
    inline static Detail::TimeState* s_state = &s_default;
};
} // namespace Bazzalt
