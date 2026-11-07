#pragma once
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Entity.h"
#include "Bazzalt/GlobalEvent.h"
#include <memory>
#include <string>
#include <vector>
namespace Bazzalt {
enum class TimelineInterpolation { Step, Linear, Smooth };
struct TimelineTransformKey {
    double Time = 0;
    Transform Value{};
    TimelineInterpolation Interpolation = TimelineInterpolation::Linear;
};
struct TimelineTransformTrack {
    std::string Name;
    UUID Target{};
    bool WorldSpace = false, Muted = false;
    std::vector<TimelineTransformKey> Keys;
};
struct TimelineCameraShot {
    std::string Name;
    UUID Camera{};
    double Start = 0, End = 1, BlendIn = 0;
};
struct TimelineSignal {
    double Time = 0;
    std::string Name;
    UUID Event{};
    std::vector<EventValue> Arguments;
};
struct BAZZALT_API TimelineSequence {
    std::string Name = "Timeline";
    double Duration = 10;
    unsigned FrameRate = 30;
    std::vector<TimelineTransformTrack> Tracks;
    std::vector<TimelineCameraShot> Cameras;
    std::vector<TimelineSignal> Signals;
    void Validate() const;
    static TimelineSequence FromSource(const std::string &source);
    static TimelineSequence Load(UUID asset);
    std::string ToSource() const;
};
enum class TimelineEventType { Started, Paused, Resumed, Stopped, Completed, Looped, Signal };
struct TimelineEvent {
    TimelineEventType Type;
    double Time = 0;
    std::string Name;
};
struct TimelineStatus {
    double Time = 0, Duration = 0;
    bool Playing = false, Paused = false;
    std::string Error;
};
// A restorable scene evaluation session. Seeking is silent unless explicitly requested.
class BAZZALT_API TimelinePlayer final {
  public:
    TimelinePlayer(Scene &scene, TimelineSequence sequence);
    ~TimelinePlayer();
    TimelinePlayer(const TimelinePlayer &) = delete;
    TimelinePlayer &operator=(const TimelinePlayer &) = delete;
    void Play(bool loop = false, double speed = 1);
    void SetSpeed(double speed);
    void SetLoop(bool loop);
    Entity GetOutputCamera() const;
    void Pause();
    void Resume();
    void Stop(bool restore = true);
    void Seek(double time, bool fireSignals = false);
    void Advance(double deltaTime);
    TimelineStatus GetStatus() const;
    std::vector<TimelineEvent> ConsumeEvents();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
class BAZZALT_API Cinematics final {
  public:
    static void SetSequence(Entity director, TimelineSequence sequence);
    static void Play(Entity director);
    static void Pause(Entity director);
    static void Resume(Entity director);
    static void Stop(Entity director);
    static void Seek(Entity director, double time, bool fireSignals = false);
    static Entity GetOutputCamera(Entity director);
    static TimelineStatus GetStatus(Entity director);
    static std::vector<TimelineEvent> ConsumeEvents(Entity director);
    static void StopAll(Scene &scene);
};
} // namespace Bazzalt
