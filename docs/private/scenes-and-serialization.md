# Scenes, hierarchy, and serialization

## UUID identity

`UUID` is a 128-bit value. `UUID::Generate()` creates RFC 4122 version-4-style
values, `ToString()` emits canonical hexadecimal text, and `TryParse()` accepts
32 hexadecimal digits with optional hyphens.

UUID zero is the permanent scene root:

```cpp
auto root = scene.GetRootEntity();
assert(root.GetUUID() == Bazzalt::UUID::Root());
```

The root cannot be destroyed or reparented. New entities are parented to it.
Explicit UUID creation exists for loaders and deterministic tools; callers must
keep IDs unique within a scene.

## Lookup and destruction

```cpp
auto named = scene.FindEntityByName("Camera");
auto stable = scene.GetEntity(savedUuid);
auto transient = scene.GetEntity(entityId);

scene.DestroyEntity(stable);
scene.Clear();
```

Name lookup returns the first match and is not identity. Destroying an entity
recursively destroys descendants. `Clear` preserves the root invariant.

## Hierarchy

```cpp
child.SetParent(parent);
parent.AddChild(child);

auto actualParent = child.GetParent();
auto children = parent.GetChildren();
bool ancestor = parent.IsAncestorOf(child);
Bazzalt::Mat4 world = child.GetWorldMatrix();

child.RemoveParent(); // reparent to root
```

Parenting rejects invalid entities, cross-scene links, self-parenting, and
cycles. `Transform` is local; `GetWorldMatrix` composes transforms through the
hierarchy.

## Scene files

`.bscene` files contain versioned YAML recording scene/entity UUIDs, hierarchy,
built-in components, registered custom components, and component format
versions.

Saving and loading are host-only. `SceneSerializer` intentionally has private
construction and I/O; game code registers formats during host/module setup
rather than initiating disk writes.

## Custom component serialization

```cpp
serializer.GetComponents().Register<Health>(
    "Game.Health", 1,
    [](const Health& health, Bazzalt::PropertyMap& output) {
        output["Value"] = std::to_string(health.Value);
    },
    [](Health& health, const Bazzalt::PropertyMap& input,
       std::uint32_t storedVersion) {
        if (storedVersion != 1) return false;
        const auto found = input.find("Value");
        if (found == input.end()) return false;
        health.Value = std::stof(found->second);
        return true;
    });
```

Use a globally namespaced type such as `Studio.Gameplay.Health`. Increment its
version when storage changes and migrate supported old versions in the reader.
A reader returns `false` for invalid or unsupported data.

Unavailable types become `UnresolvedComponents` and are saved back verbatim.
The editor can therefore round-trip a scene when a game module is absent or
newer than the editor.

## Projects

`.bproject` is versioned YAML. `ProjectMetadata` contains a stable
`ProjectUUID`, display `Name`, `AssetDirectory`, `StartupScene`, and namespaced
`Properties`. Opening and saving projects remain host-only operations.

## Game-facing scene loading

```cpp
#include <Bazzalt/SceneManager.h>

Bazzalt::SceneManager::LoadScene(levelAssetUuid);
// Or, when path loading is appropriate:
Bazzalt::SceneManager::LoadScene("Assets/Scenes/Level02.bscene");
```

Loading is deferred to the next safe frame boundary. `GetActiveScene()` returns
the host-owned scene or `nullptr`; `IsLoadPending()` reports a queued request;
`GetLastError()` reports the last failure. Game code cannot save scenes or
open/save projects.
