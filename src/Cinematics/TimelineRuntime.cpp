#include "Bazzalt/Time.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Rigidbody.h"
#include "Bazzalt/Components/TimelineDirector.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Profiler.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Systems/TimelineSystem.h"
#include "Bazzalt/Timeline.h"
#include <algorithm>
#include <map>
#include <set>
namespace Bazzalt {
namespace {
Scene &DirectorScene(Entity e) {
    if (!e || !e.HasComponent<TimelineDirector>() || !e.GetScene()->HasSystem<TimelineSystem>())
        throw std::invalid_argument("A scene-owned Timeline Director is required");
    return *e.GetScene();
}
Transform Blend(const Transform &a, const Transform &b, float weight) {
    Transform out;
    out.Position = Vec3::Lerp(a.Position, b.Position, weight);
    out.Rotation = Quaternion::Slerp(a.Rotation, b.Rotation, weight);
    out.Scale = Vec3::Lerp(a.Scale, b.Scale, weight);
    return out;
}
Transform Normalized(Transform pose) {
    pose.Rotation = pose.Rotation.Normalized();
    return pose;
}
Transform Sample(const TimelineTransformTrack &track, double time) {
    auto upper = std::upper_bound(track.Keys.begin(), track.Keys.end(), time,
                                  [](double time, const auto &key) { return time < key.Time; });
    if (upper == track.Keys.begin())
        return Normalized(upper->Value);
    if (upper == track.Keys.end())
        return Normalized(track.Keys.back().Value);
    const auto &a = *(upper - 1);
    if (a.Interpolation == TimelineInterpolation::Step)
        return Normalized(a.Value);
    float t = float((time - a.Time) / (upper->Time - a.Time));
    if (a.Interpolation == TimelineInterpolation::Smooth)
        t = t * t * (3 - 2 * t);
    return Blend(a.Value, upper->Value, t);
}
struct Lease {
    void *Owner;
    std::set<UUID> Targets;
    bool Camera;
    std::weak_ptr<void> Lifetime;
    Entity Root;
};
std::map<Scene *, std::vector<Lease>> &Leases() {
    static std::map<Scene *, std::vector<Lease>> value;
    return value;
}
} // namespace
struct TimelinePlayer::Impl {
    Scene *World;
    Entity Root;
    std::weak_ptr<void> Lifetime;
    TimelineSequence Sequence;
    TimelineStatus Status;
    bool Loop = false, Captured = false, Initial = true;
    double Speed = 1;
    struct Original {
        Entity Object;
        Transform Local;
    };
    std::vector<Original> Transforms;
    struct OriginalCamera {
        Entity Object;
        bool Active;
    };
    std::vector<OriginalCamera> Cameras;
    Entity OutputCamera;
    std::vector<TimelineEvent> Events;
    Impl(Scene &scene, std::weak_ptr<void> lifetime, TimelineSequence sequence)
        : World(&scene), Root(scene.GetRootEntity()), Lifetime(lifetime),
          Sequence(std::move(sequence)) {
        Sequence.Validate();
        Status.Duration = Sequence.Duration;
    }
    void Event(TimelineEventType type, std::string name = "") {
        if (Events.size() < 4096)
            Events.push_back({type, Status.Time, std::move(name)});
    }
    void Release() {
        auto found = Leases().find(World);
        if (found != Leases().end()) {
            std::erase_if(found->second, [&](const auto &lease) { return lease.Owner == this; });
            if (found->second.empty())
                Leases().erase(found);
        }
    }
    void Capture() {
        if (Lifetime.expired() || !Root)
            throw std::logic_error("Timeline scene has been destroyed");
        if (Captured)
            return;
        Lease lease{this, {}, !Sequence.Cameras.empty(), Lifetime, Root};
        for (auto &track : Sequence.Tracks)
            if (!track.Muted)
                lease.Targets.insert(track.Target);
        if (auto it = Leases().find(World); it != Leases().end()) {
            std::erase_if(it->second, [](const auto &value) {
                return value.Lifetime.expired() || !value.Root;
            });
            for (auto &other : it->second) {
                if (lease.Camera && other.Camera)
                    throw std::logic_error("Another timeline controls this scene's cameras");
                for (auto id : lease.Targets)
                    if (other.Targets.contains(id))
                        throw std::logic_error("Another timeline controls this transform");
            }
        }
        for (auto &track : Sequence.Tracks)
            if (!track.Muted) {
                auto e = World->GetEntity(track.Target);
                if (!e)
                    throw std::invalid_argument("Missing timeline transform binding: " +
                                                track.Target.ToString());
                if (auto *body = e.TryGetComponent<Rigidbody>();
                    body && body->IsEnabled() && body->MotionType == BodyMotionType::Dynamic)
                    throw std::invalid_argument(
                        "Timeline transforms require a nondynamic rigidbody");
            }
        for (auto &shot : Sequence.Cameras) {
            auto e = World->GetEntity(shot.Camera);
            if (!e || !e.HasComponent<Camera>() || !e.IsComponentEnabled<Camera>())
                throw std::invalid_argument("Missing or disabled cinematic camera");
        }
        for (auto id : lease.Targets) {
            auto e = World->GetEntity(id);
            Transforms.push_back({e, e.GetComponent<Transform>()});
        }
        if (lease.Camera) {
            for (auto handle : World->GetRegistry().view<Camera>()) {
                auto e = World->GetEntity(static_cast<Entity::Id>(handle));
                Cameras.push_back({e, e.GetComponent<Camera>().Active});
            }
            OutputCamera = World->CreateEntity("Cinematic Camera");
            OutputCamera.AddComponent<Camera>();
            OutputCamera.GetComponent<Camera>().Active = false;
        }
        Leases()[World].push_back(std::move(lease));
        Captured = true;
    }
    void Evaluate(double delta = 0) {
        Capture();
        std::vector<std::pair<Entity, const TimelineTransformTrack *>> tracks;
        for (auto &track : Sequence.Tracks)
            if (!track.Muted) {
                auto e = World->GetEntity(track.Target);
                if (e)
                    tracks.push_back({e, &track});
            }
        const auto depth = [](Entity e) {
            unsigned value = 0;
            while (e.HasParent() && value < 65536) {
                e = e.GetParent();
                ++value;
            }
            return value;
        };
        std::stable_sort(tracks.begin(), tracks.end(), [&](const auto &a, const auto &b) {
            return depth(a.first) < depth(b.first);
        });
        for (auto &[e, track] : tracks) {
            auto pose = Sample(*track, Status.Time);
            if (track->WorldSpace) {
                if (!e.SetWorldTransform(pose))
                    throw std::runtime_error("Singular timeline parent transform");
            } else
                e.GetComponent<Transform>() = pose;
            if (auto *body = e.TryGetComponent<Rigidbody>(); body && body->IsEnabled()) {
                if (body->MotionType == BodyMotionType::Dynamic)
                    throw std::logic_error("Timeline transform became dynamic");
                if (World->GetPhysics().IsRunning()) {
                    auto world = e.GetWorldTransform();
                    if (body->MotionType == BodyMotionType::Kinematic && delta > 0)
                        World->GetPhysics().MoveKinematic(e, world.Position, world.Rotation,
                                                          float(delta));
                    else
                        World->GetPhysics().Teleport(e, world.Position, world.Rotation);
                }
            }
        }
        if (!OutputCamera)
            return;
        auto shot =
            std::find_if(Sequence.Cameras.begin(), Sequence.Cameras.end(), [&](const auto &s) {
                return Status.Time >= s.Start &&
                       (Status.Time < s.End ||
                        (Status.Time == Sequence.Duration && s.End == Sequence.Duration));
            });
        if (shot == Sequence.Cameras.end()) {
            OutputCamera.GetComponent<Camera>().Active = false;
            for (auto &value : Cameras)
                if (value.Object && value.Object.HasComponent<Camera>())
                    value.Object.GetComponent<Camera>().Active = value.Active;
            return;
        }
        auto camera = World->GetEntity(shot->Camera);
        if (!camera || !camera.HasComponent<Camera>() || !camera.IsComponentEnabled<Camera>())
            throw std::runtime_error("Cinematic camera binding was removed or disabled");
        auto settings = camera.GetComponent<Camera>();
        auto pose = camera.GetWorldTransform();
        if (shot != Sequence.Cameras.begin() && shot->BlendIn > 0 &&
            Status.Time < shot->Start + shot->BlendIn) {
            auto previous = World->GetEntity((shot - 1)->Camera);
            if (previous && previous.HasComponent<Camera>()) {
                float weight = float((Status.Time - shot->Start) / shot->BlendIn);
                weight = weight * weight * (3 - 2 * weight);
                pose = Blend(previous.GetWorldTransform(), pose, weight);
                auto before = previous.GetComponent<Camera>();
                if (before.Projection == settings.Projection) {
                    settings.VerticalFieldOfView =
                        Lerp(before.VerticalFieldOfView, settings.VerticalFieldOfView, weight);
                    settings.OrthographicSize =
                        Lerp(before.OrthographicSize, settings.OrthographicSize, weight);
                    settings.PostProcessing.DepthOfField.FocusDistance =
                        Lerp(before.PostProcessing.DepthOfField.FocusDistance,
                             settings.PostProcessing.DepthOfField.FocusDistance, weight);
                }
            }
        }
        for (auto handle : World->GetRegistry().view<Camera>()) {
            auto e = World->GetEntity(static_cast<Entity::Id>(handle));
            if (e != OutputCamera &&
                std::none_of(Cameras.begin(), Cameras.end(),
                             [&](const auto &value) { return value.Object == e; }))
                Cameras.push_back({e, e.GetComponent<Camera>().Active});
            e.GetComponent<Camera>().Active = false;
        }
        settings.Active = true;
        OutputCamera.GetComponent<Camera>() = settings;
        if (!OutputCamera.SetWorldTransform(pose))
            throw std::runtime_error("Cannot place cinematic camera");
    }
    std::vector<TimelineSignal> Cross(double before, double after, bool includeStart) {
        std::vector<TimelineSignal> out;
        for (auto &signal : Sequence.Signals)
            if ((signal.Time > before || (includeStart && signal.Time == before)) &&
                signal.Time <= after) {
                out.push_back(signal);
                if (Events.size() < 4096)
                    Events.push_back({TimelineEventType::Signal, signal.Time, signal.Name});
            }
        return out;
    }
    void Fire(const std::vector<TimelineSignal> &signals) {
        for (auto &signal : signals)
            if (signal.Event)
                GlobalEvent::Load(signal.Event).Fire(signal.Arguments);
    }
    void Restore(bool transforms) {
        if (!Lifetime.expired() && Root && Captured) {
            if (transforms)
                for (auto &original : Transforms)
                    if (original.Object)
                        original.Object.GetComponent<Transform>() = original.Local;
            if (transforms)
                for (auto &original : Transforms)
                    if (original.Object && original.Object.HasComponent<Rigidbody>() &&
                        World->GetPhysics().IsRunning()) {
                        auto world = original.Object.GetWorldTransform();
                        World->GetPhysics().Teleport(original.Object, world.Position,
                                                     world.Rotation, false);
                    }
            for (auto &original : Cameras)
                if (original.Object && original.Object.HasComponent<Camera>())
                    original.Object.GetComponent<Camera>().Active = original.Active;
            if (OutputCamera)
                World->DestroyEntity(OutputCamera);
        }
        Release();
        OutputCamera = {};
        Transforms.clear();
        Cameras.clear();
        Captured = false;
    }
};
TimelinePlayer::TimelinePlayer(Scene &scene, TimelineSequence sequence)
    : m_impl(std::make_unique<Impl>(scene, scene.m_lifetime, std::move(sequence))) {}
TimelinePlayer::~TimelinePlayer() {
    m_impl->Restore(true);
}
void TimelinePlayer::Play(bool loop, double speed) {
    if (!std::isfinite(speed) || speed <= 0 || speed > 100)
        throw std::invalid_argument("Timeline speed must be positive and at most 100");
    m_impl->Capture();
    m_impl->Loop = loop;
    m_impl->Speed = speed;
    m_impl->Status.Time = 0;
    m_impl->Status.Playing = true;
    m_impl->Status.Paused = false;
    m_impl->Initial = true;
    m_impl->Evaluate();
    m_impl->Event(TimelineEventType::Started);
}
void TimelinePlayer::SetSpeed(double speed) {
    if (!std::isfinite(speed) || speed <= 0 || speed > 100)
        throw std::invalid_argument("Invalid timeline speed");
    m_impl->Speed = speed;
}
void TimelinePlayer::SetLoop(bool loop) {
    m_impl->Loop = loop;
}
Entity TimelinePlayer::GetOutputCamera() const {
    return m_impl->Lifetime.expired() ? Entity{} : m_impl->OutputCamera;
}
void TimelinePlayer::Pause() {
    if (m_impl->Status.Playing && !m_impl->Status.Paused) {
        m_impl->Status.Paused = true;
        m_impl->Event(TimelineEventType::Paused);
    }
}
void TimelinePlayer::Resume() {
    if (m_impl->Status.Playing && m_impl->Status.Paused) {
        m_impl->Status.Paused = false;
        m_impl->Event(TimelineEventType::Resumed);
    }
}
void TimelinePlayer::Stop(bool restore) {
    m_impl->Restore(restore);
    m_impl->Status.Playing = false;
    m_impl->Status.Paused = false;
    m_impl->Event(TimelineEventType::Stopped);
}
void TimelinePlayer::Seek(double time, bool signals) {
    if (!std::isfinite(time) || time < 0 || time > m_impl->Sequence.Duration)
        throw std::invalid_argument("Timeline seek is outside its range");
    auto &p = *m_impl;
    auto before = p.Status.Time;
    p.Status.Time = time;
    p.Evaluate();
    auto crossed = signals && time >= before ? p.Cross(before, time, p.Initial)
                                             : std::vector<TimelineSignal>{};
    p.Initial = false;
    p.Fire(crossed);
}
void TimelinePlayer::Advance(double delta) {
    auto &p = *m_impl;
    if (!std::isfinite(delta) || delta < 0)
        throw std::invalid_argument("Timeline delta must be finite and nonnegative");
    if (!p.Status.Playing || p.Status.Paused)
        return;
    if (p.Lifetime.expired() || !p.Root) {
        p.Status.Playing = false;
        return;
    }
    double remaining = delta * p.Speed;
    if (remaining > p.Sequence.Duration * 256)
        throw std::length_error("Timeline loop budget exceeded");
    std::vector<TimelineSignal> signals;
    do {
        double before = p.Status.Time, step = std::min(remaining, p.Sequence.Duration - before);
        p.Status.Time += step;
        remaining -= step;
        auto crossed = p.Cross(before, p.Status.Time, p.Initial);
        signals.insert(signals.end(), crossed.begin(), crossed.end());
        p.Initial = false;
        if (signals.size() > 65536)
            throw std::length_error("Timeline signal budget exceeded");
        if (p.Status.Time >= p.Sequence.Duration) {
            if (p.Loop) {
                p.Status.Time = 0;
                p.Initial = true;
                p.Event(TimelineEventType::Looped);
            } else {
                p.Status.Playing = false;
                p.Event(TimelineEventType::Completed);
                break;
            }
        } else
            break;
    } while (remaining > 0);
    p.Evaluate(delta);
    p.Fire(signals);
}
TimelineStatus TimelinePlayer::GetStatus() const {
    return m_impl->Status;
}
std::vector<TimelineEvent> TimelinePlayer::ConsumeEvents() {
    auto out = std::move(m_impl->Events);
    m_impl->Events.clear();
    return out;
}
struct TimelineSystem::Impl {
    struct Entry {
        UUID Asset;
        std::shared_ptr<TimelinePlayer> Player;
        TimelineStatus Failure;
        bool Attempted = false;
    };
    std::map<UUID, Entry> Players;
    Entry &Get(Entity e) {
        if (!e || !e.HasComponent<TimelineDirector>() ||
            !e.GetComponent<TimelineDirector>().IsValid())
            throw std::invalid_argument("A valid Timeline Director is required");
        auto &component = e.GetComponent<TimelineDirector>();
        auto &entry = Players[e.GetUUID()];
        if (entry.Asset != component.Sequence) {
            entry = Entry{};
            entry.Asset = component.Sequence;
        }
        if (!entry.Player)
            entry.Player = std::make_shared<TimelinePlayer>(
                *e.GetScene(), TimelineSequence::Load(component.Sequence));
        return entry;
    }
};
TimelineSystem::TimelineSystem() : m_impl(std::make_unique<Impl>()) {}
TimelineSystem::~TimelineSystem() = default;
void TimelineSystem::OnUpdate(Scene &, float) {}
void TimelineSystem::OnLateUpdate(Scene &, float) {}
void TimelineSystem::Evaluate(Scene &scene, float delta) {
    ProfileScope profile("Timeline.Evaluate");
    std::vector<UUID> ids;
    for (auto handle : scene.GetRegistry().view<TimelineDirector>())
        ids.push_back(scene.GetEntity(static_cast<Entity::Id>(handle)).GetUUID());
    std::set<UUID> alive(ids.begin(), ids.end());
    std::erase_if(m_impl->Players, [&](const auto &row) { return !alive.contains(row.first); });
    for (auto id : ids) {
        auto e = scene.GetEntity(id);
        if (!e || !e.HasComponent<TimelineDirector>())
            continue;
        auto settings = e.GetComponent<TimelineDirector>();
        auto found = m_impl->Players.find(id);
        if (!settings.Enabled) {
            if (found != m_impl->Players.end()) {
                if (found->second.Player)
                    found->second.Player->Stop(settings.RestoreOnStop);
                m_impl->Players.erase(found);
            }
            continue;
        }
        auto &entry = m_impl->Players[id];
        if (entry.Asset != settings.Sequence) {
            entry = Impl::Entry{};
            entry.Asset = settings.Sequence;
        }
        try {
            if (!entry.Attempted) {
                entry.Attempted = true;
                if (settings.PlayOnAwake)
                    Cinematics::Play(e);
            }
            if (entry.Player) {
                auto player = entry.Player;
                player->SetLoop(settings.Loop);
                player->SetSpeed(settings.Speed);
                const bool wasPlaying = player->GetStatus().Playing;
                player->Advance(settings.UseUnscaledTime ? Time::GetUnscaledDeltaTime() : delta);
                if (wasPlaying && !player->GetStatus().Playing && !settings.Loop &&
                    settings.RestoreOnCompletion)
                    player->Stop(true);
            }
        } catch (const std::exception &error) {
            auto current = m_impl->Players.find(id);
            if (current != m_impl->Players.end()) {
                current->second.Failure.Error = error.what();
                if (current->second.Player)
                    current->second.Player->Stop(true);
            }
            Console::Error(error.what(), "Timeline");
        }
    }
}
void TimelineSystem::OnDestroy(Scene &) {
    m_impl->Players.clear();
}
void Cinematics::SetSequence(Entity e, TimelineSequence sequence) {
    auto &system = DirectorScene(e).GetSystem<TimelineSystem>();
    sequence.Validate();
    auto &entry = system.m_impl->Players[e.GetUUID()];
    if (entry.Player)
        entry.Player->Stop(true);
    entry.Player.reset();
    entry.Asset = e.GetComponent<TimelineDirector>().Sequence;
    entry.Player = std::make_shared<TimelinePlayer>(*e.GetScene(), std::move(sequence));
    entry.Attempted = true;
}
void Cinematics::Play(Entity e) {
    auto &system = DirectorScene(e).GetSystem<TimelineSystem>();
    auto &entry = system.m_impl->Get(e);
    entry.Attempted = true;
    auto settings = e.GetComponent<TimelineDirector>();
    entry.Player->Play(settings.Loop, settings.Speed);
}
void Cinematics::Pause(Entity e) {
    auto &entry = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Get(e);
    entry.Player->Pause();
}
void Cinematics::Resume(Entity e) {
    auto &entry = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Get(e);
    entry.Player->Resume();
}
void Cinematics::Stop(Entity e) {
    auto &entry = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Players[e.GetUUID()];
    entry.Asset = e.GetComponent<TimelineDirector>().Sequence;
    entry.Attempted = true;
    if (entry.Player)
        entry.Player->Stop(e.GetComponent<TimelineDirector>().RestoreOnStop);
}
void Cinematics::Seek(Entity e, double time, bool fire) {
    auto &entry = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Get(e);
    entry.Attempted = true;
    auto player = entry.Player;
    player->Seek(time, fire);
}
Entity Cinematics::GetOutputCamera(Entity e) {
    auto &players = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Players;
    auto found = players.find(e.GetUUID());
    return found != players.end() && found->second.Player ? found->second.Player->GetOutputCamera()
                                                          : Entity{};
}
TimelineStatus Cinematics::GetStatus(Entity e) {
    auto &players = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Players;
    auto found = players.find(e.GetUUID());
    if (found == players.end())
        return {};
    auto &entry = found->second;
    auto out = entry.Player ? entry.Player->GetStatus() : TimelineStatus{};
    out.Error = entry.Failure.Error;
    return out;
}
std::vector<TimelineEvent> Cinematics::ConsumeEvents(Entity e) {
    auto &players = DirectorScene(e).GetSystem<TimelineSystem>().m_impl->Players;
    auto found = players.find(e.GetUUID());
    return found != players.end() && found->second.Player ? found->second.Player->ConsumeEvents()
                                                          : std::vector<TimelineEvent>{};
}
void Cinematics::StopAll(Scene &scene) {
    if (!scene.HasSystem<TimelineSystem>())
        return;
    auto &players = scene.GetSystem<TimelineSystem>().m_impl->Players;
    for (auto &[id, entry] : players)
        if (entry.Player)
            entry.Player->Stop(true);
    players.clear();
}
} // namespace Bazzalt
