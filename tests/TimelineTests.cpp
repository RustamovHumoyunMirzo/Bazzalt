#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/TimelineDirector.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Serialization.h"
#include "Bazzalt/Timeline.h"
#include "Runtime/Engine.h"
#include <cassert>
#include <cmath>
#include <fstream>
using namespace Bazzalt;
int main() {
    Scene scene;
    auto parent = scene.CreateEntity("Parent"), child = scene.CreateEntity("Child");
    child.SetParent(parent, false);
    parent.GetComponent<Transform>().Position = {10, 0, 0};
    child.GetComponent<Transform>().Position = {2, 0, 0};
    auto camera = scene.CreateEntity("Camera");
    camera.AddComponent<Camera>();
    TimelineSequence sequence;
    sequence.Duration = 2;
    sequence.Name = "Test";
    TimelineTransformTrack track;
    track.Target = parent.GetUUID();
    TimelineTransformKey a, b;
    b.Time = 2;
    b.Value.Position = {20, 0, 0};
    track.Keys = {a, b};
    sequence.Tracks.push_back(track);
    sequence.Cameras.push_back({"Shot", camera.GetUUID(), 0, 2, 0});
    sequence.Signals.push_back({0, "Start", {}, {}});
    sequence.Signals.push_back({2, "End", {}, {}});
    auto roundtrip = TimelineSequence::FromSource(sequence.ToSource());
    assert(roundtrip.Tracks.size() == 1 && roundtrip.Cameras.size() == 1);
    {
        TimelinePlayer player(scene, roundtrip);
        player.Seek(1);
        assert(std::abs(parent.GetWorldTransform().Position.X - 10) < .001);
        assert(!camera.GetComponent<Camera>().Active);
        player.Seek(2);
        assert(std::abs(child.GetWorldTransform().Position.X - 22) < .001);
        player.Stop();
        assert(camera.GetComponent<Camera>().Active);
        assert(parent.GetComponent<Transform>().Position.X == 10);
        player.Play(true);
        player.Advance(2.5);
        assert(std::abs(player.GetStatus().Time - .5) < .001);
        auto events = player.ConsumeEvents();
        unsigned signals = 0;
        for (auto &e : events)
            if (e.Type == TimelineEventType::Signal)
                ++signals;
        assert(signals == 3);
        player.Pause();
        player.Advance(1);
        assert(player.GetStatus().Time == .5);
        player.Resume();
        player.Advance(.5);
        assert(player.GetStatus().Time == 1);
        bool conflict = false;
        try {
            TimelinePlayer other(scene, sequence);
            other.Seek(0);
        } catch (const std::logic_error &) {
            conflict = true;
        }
        assert(conflict);
    }
    assert(parent.GetComponent<Transform>().Position.X == 10 && scene.GetEntityCount() == 4);
    auto owner = scene.CreateEntity("Director");
    owner.AddComponent<TimelineDirector>();
    Cinematics::SetSequence(owner, sequence);
    Cinematics::Play(owner);
    Cinematics::Seek(owner, 1);
    assert(Cinematics::GetStatus(owner).Time == 1);
    Cinematics::StopAll(scene);
    auto folder =
        std::filesystem::temp_directory_path() / ("timeline-" + UUID::Generate().ToString());
    std::filesystem::create_directories(folder / "Assets");
    struct Cleanup {
        std::filesystem::path Path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }
    } cleanup{folder};
    {
        std::ofstream file(folder / "Test.bproject");
        file << "FormatVersion: 1\nProjectUUID: \"00000000-0000-0001-0000-000000000005\"\nName: "
                "Timeline\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties: {}\n";
    }
    {
        std::ofstream file(folder / "Assets/Cue.event");
        file << "Version: 1\nName: Cue\nArguments:\n - {Name: Value, Type: Number}\n";
    }
    {
        Runtime::Engine engine;
        assert(engine.LoadProject(folder / "Test.bproject", false));
        auto asset = AssetManager::GetAsset(folder / "Assets/Cue.event");
        assert(asset);
        auto event = GlobalEvent::Load(asset->Id);
        int calls = 0;
        auto subscription = event.Subscribe([&](const EventInvocation &value) {
            assert(value.Get<double>("Value") == 42);
            ++calls;
        });
        auto cues = sequence;
        cues.Signals = {{1, "Cue", asset->Id, {42.0}}};
        TimelinePlayer player(scene, cues);
        player.Seek(2);
        assert(calls == 0);
        player.Play();
        player.Advance(1);
        assert(calls == 1);
        player.Advance(.5);
        assert(calls == 1);
        event.Unsubscribe(subscription);
    }
    auto invalid = sequence;
    invalid.Tracks.push_back(track);
    bool rejected = false;
    try {
        invalid.Validate();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert(rejected);
    auto orphan = std::make_unique<Scene>();
    auto target = orphan->CreateEntity("Target");
    auto isolated = sequence;
    isolated.Cameras.clear();
    isolated.Tracks[0].Target = target.GetUUID();
    TimelinePlayer survivor(*orphan, isolated);
    survivor.Seek(1);
    orphan.reset();
    survivor.Stop();

    Scene reused;
    auto previous = reused.CreateEntity("Previous");
    isolated.Tracks[0].Target = previous.GetUUID();
    TimelinePlayer stale(reused, isolated);
    stale.Play();
    reused.Clear();
    auto replacement = reused.CreateEntity("Replacement");
    replacement.GetComponent<Transform>().Position = {7, 8, 9};
    stale.Advance(1);
    stale.Stop();
    assert(!stale.GetStatus().Playing);
    assert(replacement.GetComponent<Transform>().Position.X == 7);
}
