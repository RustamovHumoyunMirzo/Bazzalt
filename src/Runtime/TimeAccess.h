#pragma once
#include "Bazzalt/Time.h"
#include <algorithm>
#include <limits>

namespace Bazzalt::Runtime {
class TimeAccess final {
public:
    static void Reset() { Time::State() = {}; }
    static Detail::TimeState* GetState() { return &Time::State(); }
    class FixedScope final {
    public:
        FixedScope() { auto& state=Time::State();state.InFixedStep=true;state.CurrentFixedDelta=state.FixedDelta;state.CurrentFixedUnscaledDelta=Time::GetFixedUnscaledDeltaTime();state.FixedElapsed+=state.CurrentFixedDelta;state.FixedUnscaledElapsed+=state.CurrentFixedUnscaledDelta; }
        ~FixedScope() { Time::State().InFixedStep=false; }
        FixedScope(const FixedScope&)=delete;
        FixedScope& operator=(const FixedScope&)=delete;
    };
    static void Advance(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
        auto& state = Time::State();
        state.UnscaledDelta = static_cast<float>(std::min(seconds, double(std::numeric_limits<float>::max())));
        state.Delta = static_cast<float>(std::min(seconds * double(state.Scale), double(state.MaximumDelta)));
        state.SmoothDelta = state.Delta == 0.0f ? 0.0f : state.Frames == 0 ? state.Delta : state.SmoothDelta + (state.Delta - state.SmoothDelta) * 0.1f;
        state.Elapsed += state.Delta;
        state.UnscaledElapsed += seconds;
        ++state.Frames;
    }
};
}
