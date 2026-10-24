# Application lifecycle integration

Public headers: `Bazzalt/ApplicationEvents.h`, `Bazzalt/Application.h`.
The host alone dispatches through private `ApplicationEventsAccess`. Gameplay
can subscribe, unsubscribe, or implement an application entry; it cannot emit
events, initialize subsystems, replace the loop, or destroy a host window.

## Bootstrap and trust

Project metadata `Properties.application.script` is an optional script asset UUID.
The Project Settings asset picker edits it. Lua returns a plain table; native
sources declare `APPLICATION(Name)` and include `Bazzalt/Application.h`. The
normal script compiler creates optional V1 event/late-update exports without
changing the existing module table layout. Old behavior modules still work;
an old module selected as application entry fails with a rebuild diagnostic.
Application scripts have no entity and are not scene components.

Player order: restrict DLL search, authenticate packages/executable, extract and
verify content, read project metadata/index, instantiate application entry,
OnBoot, OnBeforeWindowCreate, create the black native window, OnInit, initialize
renderer/input, OnLoad/OnSceneLoad, load startup scene, OnSceneLoaded, create
viewport/context, OnPostWindowCreate, start scene behaviors, OnPlay, frames.
OnInit marks the beginning of subsystem initialization, not a guarantee that
drivers are ready inside that callback. Audio starts with gameplay. No physics
driver exists yet; this notification does not invent one.

OnBoot is the first *lifecycle notification to trusted gameplay*, not the first
CPU instruction in the executable. OS/CRT initialization, package authentication,
project/index loading, native module constructors, Lua VM creation, and top-level
script declarations necessarily precede it. Do not run asset-dependent gameplay
in constructors/top-level declarations. Never move user execution ahead of
integrity verification. The current Windows player owns Win32 windows; SDL3 is
input-only. Replacing that window backend is independent of the public contract.

Editor Play owns an application session, not the editor process. Boot happens
at Play, and Stop ends the session. Already-completed editor initialization and
window creation are not replayed as fictitious startup events. OS window events
belong to the standalone player; the editor forwards Game viewport resize/focus.

## Frames, scenes, teardown

Input is prepared before OnPreUpdate. Fixed systems/behaviors run with fixed
Time state, with global OnFixedUpdate inside that scope. Normal systems and
behaviors run before global OnUpdate. Late systems, late native behaviors, late
Lua behaviors and global OnLateUpdate follow. Camera synchronization then sees
late camera changes. OnPreRender and OnRender notify immediately before backend
submission; OnPostRender means the backend call returned, **not a GPU fence**.
Editor-only paused rendering does not advance gameplay lifecycle events.

OnSceneLoad carries ScenePath before deserialization; OnSceneLoaded carries the
new SceneUUID after replacement/attachment. A failed load leaves the old scene
and its scripts alive and does not emit OnSceneLoaded. OnSceneUnload precedes
old scene script teardown; OnSceneUnloaded follows replacement. Inactive scenes
retained by the editor are not destructively unloaded through this API.

Normal player close requests stop the loop without prematurely destroying the
window. OnPreShutdown runs once while scene/services still exist, then scene
unload/OnStop, script destruction, scene destruction, graphics/input shutdown,
native window destruction, OnShutdown, and application script disposal.
Engine destructor performs best-effort cleanup on startup errors. Abrupt process
termination, fatal driver faults and OS kill cannot promise shutdown callbacks.
Private Engine::Shutdown(false) retains authoring entity data for offline/editor
inspection; it does not announce destructive scene unloading. The standalone
player and final engine disposal use Shutdown(true).

## Dispatch ownership and tests

Main thread only. Registration order is deterministic; dispatch snapshots IDs,
skips subscriptions removed mid-dispatch, and defers newly-added callbacks to
the next emission. Unsubscribe is idempotent. Exceptions are logged and do not
prevent remaining subscribers/teardown. Lua callbacks have instruction budgets
and the same 32-MiB VM/4-MiB source limits as existing scripts. VM subscriptions
are disconnected before lua_close. Native lifecycle scopes associate registered
callbacks with their behavior instance; Stop disconnects those before deletion.
Native module leases survive callback destruction. Native background-thread
subscriptions must be marshaled to the main thread and explicitly managed.

Tests: `BazzaltApplicationEventsTests`, native lifecycle fixture, editor scripting
and project settings tests, compiler content-filter tests. Keep new event names,
Lua enum bindings, Application dispatch, host emissions, docs, and tests in sync.
