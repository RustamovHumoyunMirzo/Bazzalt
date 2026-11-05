# Runtime developer console / CVars

Public surfaces are CVar.h and DeveloperConsole.h; Console.h keeps its editor-only semantics.
DeveloperConsole.cpp owns a process-wide registry, bounded output and typed scalar values.
Registration and execution are engine-thread operations. Output snapshots/clearing use a mutex;
logging mirrors to the existing editor ConsoleStore without exposing Qt or renderer internals.

The interpreter tokenizes one bounded line with quoted/escaped arguments. It does not execute
shells, files, pipelines or environment substitutions. Name collisions are rejected across commands,
CVars and reserved built-ins. SetFromString uses classic-locale float parsing (not unavailable libc++
floating from_chars) and exact integer parsing. Flags are enforced before mutation. Archived input
is validated/copied before all values publish, and notifications occur after the entire commit.
RestartRequired writes a pending value; only explicit host ApplyPendingChanges commits it.

Every registered callback has native module lease, owner, subsystem level and asset-access context.
Leases outlive std::function destruction. Dispatch retains shared entries and restores the captured
context even after exceptions. ApplicationEventsAccess::DisconnectOwner removes console commands,
CVars and listeners before VM/module destruction; removal enumerates IDs first so capture destructors
cannot invalidate an active map traversal. GlobalEventsAccess::IsOwnerClosing blocks registration
from Lua finalizers while lua_close runs. Lua handlers use protected functions and an instruction
budget, restore prior hooks, and pass copied change events. Re-register callbacks in OnReload;
compatible values survive owner removal in a bounded orphan recovery map until re-registration.
Full application reset clears this map. Do not transfer callback userdata through reload snapshots.

Engine PrepareApplication initializes built-in tunables before subsystem Boot. Standalone command
execution/overlay are disabled unless trusted startup code opts in. Editor host setup enables the
registry outside Play too; leaving Play restores native editor tunables after removing game owners.
Application frame rates and Time scale setters receive actual change notifications. Direct changes
through those other APIs do not implicitly rewrite CVar values. There is no remote command endpoint.
This is development tooling, not an anti-cheat or sandbox boundary against trusted game code.

DeveloperConsoleOverlay owns an independent Scene and GuiRenderer; no console entities enter the
game scene, hierarchy, scene serializer, physics, world-cell streaming or camera render textures.
It allocates lazily when opened. Game viewport presentation prepares/renders it after game GUI,
including a black no-camera viewport. Input is processed before game systems/scripts, with the
InputState Active flag temporarily masked by an exception-safe stack guard. Physical key state is
retained, closing/toggle input is consumed, and gameplay resumes without synthesizing key presses.
Editor paused frames use the same overlay input path. Renderer teardown releases overlay resources
before the Filament engine. The fallback font is the GUI renderer's embedded diagnostic font;
Unicode text storage remains intact even when a glyph is unavailable in that font.

Editor ConsolePanel uses the shared QLineEdit/theme, native bridge Execute/Complete/History, and
locale text. Tab is intercepted at QWidget event dispatch to avoid focus traversal; Tab/Shift+Tab cycle completions without creating native popup windows. Existing
filtering, row selection and native logging remain in use. The standalone overlay never uses Qt.

Tests: DeveloperConsoleTests exercises type/bound/flag validation, command dispatch, exact quoting,
archive atomic publication, reentrant removal, owner cleanup/recovery, recursion bounds, threaded
logging, closing-owner safeguards and NOOP-renderer keyboard toggles. LuaRuntimeTests covers scalar
conversion, callback errors/budgets and teardown across restart. Editor tests exercise command input,
history/completion/clear with the actual native host. Keep full native, UI and docs checks enabled.
