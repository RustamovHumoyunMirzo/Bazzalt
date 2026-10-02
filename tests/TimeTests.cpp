#include "Bazzalt/Time.h"
#include "Runtime/TimeAccess.h"
#include <cassert>
#include <cmath>
#include <limits>

using Bazzalt::Time;
using Bazzalt::Runtime::TimeAccess;
int main() {
    TimeAccess::Reset();
    TimeAccess::Advance(0.1);
    assert(std::abs(Time::GetDeltaTime() - 0.1f) < 1e-6f);
    assert(Time::GetFrameCount() == 1);
    Time::SetSlowMotion(0.25f);
    TimeAccess::Advance(0.2);
    assert(std::abs(Time::GetDeltaTime() - 0.05f) < 1e-6f);
    assert(std::abs(Time::GetUnscaledDeltaTime() - 0.2f) < 1e-6f);
    assert(std::abs(Time::GetTime() - 0.15) < 1e-6);
    const double elapsed = Time::GetTime();
    Time::Pause();Time::Pause();TimeAccess::Advance(0.2);
    assert(Time::IsPaused() && Time::GetDeltaTime() == 0);
    assert(Time::GetTime() == elapsed);
    assert(std::abs(Time::GetUnscaledTime() - 0.5) < 1e-6);
    Time::Resume();assert(Time::GetTimeScale() == 0.25f);
    Time::SetFixedDeltaTime(0.01f);
    {
        TimeAccess::FixedScope fixed;
        assert(Time::IsInFixedTimeStep());
        assert(Time::GetDeltaTime() == 0.01f);
        assert(std::abs(Time::GetUnscaledDeltaTime() - 0.04f) < 1e-6f);
        assert(Time::GetTime() == Time::GetFixedTime());
        Time::SetFixedDeltaTime(0.02f);
        assert(Time::GetDeltaTime() == 0.01f);
    }
    assert(!Time::IsInFixedTimeStep());
    Time::RestoreTimeScale();Time::SetMaximumDeltaTime(0.1f);
    TimeAccess::Advance(5.0);
    assert(Time::GetDeltaTime() == 0.1f && Time::GetUnscaledDeltaTime() == 5.0f);
    for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        bool rejected = false;
        try { Time::SetTimeScale(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    bool rejected = false;
    try { Time::SetMaximumDeltaTime(0); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { Time::SetFixedDeltaTime(0); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    TimeAccess::Reset();assert(Time::GetFrameCount() == 0 && Time::GetTimeScale() == 1);
    TimeAccess::Advance(-1);assert(Time::GetDeltaTime() == 0);
    assert(Time::GetRealtimeSinceStartup() >= 0);
}
