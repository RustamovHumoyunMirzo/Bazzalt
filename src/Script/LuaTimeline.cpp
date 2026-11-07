#include "Bazzalt/Components/TimelineDirector.h"
#include "Bazzalt/Timeline.h"
#include "Script/LuaBindings.h"
namespace Bazzalt::Runtime {
void BindLuaTimeline(sol::table api) {
    api.new_enum("TimelineInterpolation", "Step", TimelineInterpolation::Step, "Linear",
                 TimelineInterpolation::Linear, "Smooth", TimelineInterpolation::Smooth);
    api.new_enum("TimelineEventType", "Started", TimelineEventType::Started, "Paused",
                 TimelineEventType::Paused, "Resumed", TimelineEventType::Resumed, "Stopped",
                 TimelineEventType::Stopped, "Completed", TimelineEventType::Completed, "Looped",
                 TimelineEventType::Looped, "Signal", TimelineEventType::Signal);
    auto key = api.new_usertype<TimelineTransformKey>("TimelineTransformKey",
                                                      sol::constructors<TimelineTransformKey()>());
    key["Time"] = &TimelineTransformKey::Time;
    key["Value"] = &TimelineTransformKey::Value;
    key["Interpolation"] = &TimelineTransformKey::Interpolation;
    auto track = api.new_usertype<TimelineTransformTrack>(
        "TimelineTransformTrack", sol::constructors<TimelineTransformTrack()>());
    track["Name"] = &TimelineTransformTrack::Name;
    track["Target"] = &TimelineTransformTrack::Target;
    track["WorldSpace"] = &TimelineTransformTrack::WorldSpace;
    track["Muted"] = &TimelineTransformTrack::Muted;
    track["Keys"] = &TimelineTransformTrack::Keys;
    auto shot = api.new_usertype<TimelineCameraShot>("TimelineCameraShot",
                                                     sol::constructors<TimelineCameraShot()>());
    shot["Name"] = &TimelineCameraShot::Name;
    shot["Camera"] = &TimelineCameraShot::Camera;
    shot["Start"] = &TimelineCameraShot::Start;
    shot["End"] = &TimelineCameraShot::End;
    shot["BlendIn"] = &TimelineCameraShot::BlendIn;
    auto signal =
        api.new_usertype<TimelineSignal>("TimelineSignal", sol::constructors<TimelineSignal()>());
    signal["Time"] = &TimelineSignal::Time;
    signal["Name"] = &TimelineSignal::Name;
    signal["Event"] = &TimelineSignal::Event;
    signal["Arguments"] = &TimelineSignal::Arguments;
    auto sequence = api.new_usertype<TimelineSequence>("TimelineSequence",
                                                       sol::constructors<TimelineSequence()>());
    sequence["Name"] = &TimelineSequence::Name;
    sequence["Duration"] = &TimelineSequence::Duration;
    sequence["FrameRate"] = &TimelineSequence::FrameRate;
    sequence["Tracks"] = &TimelineSequence::Tracks;
    sequence["Cameras"] = &TimelineSequence::Cameras;
    sequence["Signals"] = &TimelineSequence::Signals;
    sequence["Validate"] = &TimelineSequence::Validate;
    sequence["FromSource"] = &TimelineSequence::FromSource;
    sequence["ToSource"] = &TimelineSequence::ToSource;
    sequence["Load"] = &TimelineSequence::Load;
    auto status = api.new_usertype<TimelineStatus>("TimelineStatus", sol::no_constructor);
    status["Time"] = &TimelineStatus::Time;
    status["Duration"] = &TimelineStatus::Duration;
    status["Playing"] = &TimelineStatus::Playing;
    status["Paused"] = &TimelineStatus::Paused;
    status["Error"] = &TimelineStatus::Error;
    auto event = api.new_usertype<TimelineEvent>("TimelineEvent", sol::no_constructor);
    event["Type"] = &TimelineEvent::Type;
    event["Time"] = &TimelineEvent::Time;
    event["Name"] = &TimelineEvent::Name;
    auto director = api.new_usertype<TimelineDirector>("TimelineDirector",
                                                       sol::constructors<TimelineDirector()>(),
                                                       sol::base_classes, sol::bases<Component>());
    director["Sequence"] = &TimelineDirector::Sequence;
    director["Speed"] = &TimelineDirector::Speed;
    director["PlayOnAwake"] = &TimelineDirector::PlayOnAwake;
    director["Loop"] = &TimelineDirector::Loop;
    director["RestoreOnStop"] = &TimelineDirector::RestoreOnStop;
    director["RestoreOnCompletion"] = &TimelineDirector::RestoreOnCompletion;
    director["UseUnscaledTime"] = &TimelineDirector::UseUnscaledTime;
    director["IsValid"] = &TimelineDirector::IsValid;
    sol::table operations = api["_ComponentOperations"];
    auto op = operations.create_named("TimelineDirector");
    op["Has"] = [](Entity e) { return e.HasComponent<TimelineDirector>(); };
    op["Get"] = [](Entity e) { return TimelineDirector(e.GetComponent<TimelineDirector>()); };
    op["Add"] = [](Entity e) { return TimelineDirector(e.AddComponent<TimelineDirector>()); };
    op["Set"] = [](Entity e, TimelineDirector value) {
        if (!value.IsValid())
            throw std::invalid_argument("Invalid Timeline Director");
        e.GetComponent<TimelineDirector>() = value;
    };
    op["Remove"] = [](Entity e) { e.RemoveComponent<TimelineDirector>(); };
    op["IsEnabled"] = [](Entity e) { return e.IsComponentEnabled<TimelineDirector>(); };
    op["SetEnabled"] = [](Entity e, bool enabled) {
        e.SetComponentEnabled<TimelineDirector>(enabled);
    };
    op["Inherited"] = [](Entity) -> std::optional<TimelineDirector> { return {}; };
    auto cinematic = api.create_named("Cinematics");
    cinematic["SetSequence"] = &Cinematics::SetSequence;
    cinematic["Play"] = &Cinematics::Play;
    cinematic["Pause"] = &Cinematics::Pause;
    cinematic["Resume"] = &Cinematics::Resume;
    cinematic["Stop"] = &Cinematics::Stop;
    cinematic["Seek"] = [](Entity e, double time, sol::optional<bool> fire) {
        Cinematics::Seek(e, time, fire.value_or(false));
    };
    cinematic["GetOutputCamera"] = &Cinematics::GetOutputCamera;
    cinematic["GetStatus"] = &Cinematics::GetStatus;
    cinematic["ConsumeEvents"] = [](Entity e) {
        return sol::as_table(Cinematics::ConsumeEvents(e));
    };
    cinematic["StopAll"] = &Cinematics::StopAll;
}
} // namespace Bazzalt::Runtime
