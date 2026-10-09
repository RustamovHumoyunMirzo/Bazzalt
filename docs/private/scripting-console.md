# Editor-only scripting console

The public C++ API is Bazzalt/Console.h, with Lua bindings in LuaConsole.cpp.
ConsoleStore.cpp owns a thread-safe bounded store in core. It is inert unless
EditorHost acquires ConsoleAccess; the final host release clears stored data.
Only the private editor bridge enables it. Core knows nothing about Qt/Python.
Public methods catch failures, never print, and return absent optionals when no
editor is attached. Lua absent reads map to nil, not empty fabricated tables.

The private bridge returns revisioned immutable snapshots. ConsolePanel.BindRuntime
migrates early Python messages, then uses the same native store for all message
sources. Editor ticks poll revisions; unchanged stores return no snapshot. Simple
append changes add only new Qt rows; clearing/eviction rebuilds the filtered view.
Tick-driven UI polling is limited to 10 Hz to avoid per-frame Qt work under log
spam; explicit panel edits and reads refresh immediately. Script reads always
read current native data. UI clear-filtered and multi-selection operations remove stable IDs in one batch,
preserving Unicode casefold behavior of the visible panel filter. Public filters
instead use documented exact source/case-sensitive UTF-8 substring matching.

Maximum retention is 10,000 entries and 16 MiB of text/source payloads,
message text 64 KiB, source 256 bytes. Malformed UTF-8 is replaced only when
converting native text to Python for display; it never aborts an editor tick. IDs
are monotonic across clears and host lifetimes; timestamps are Unix seconds.
Indices are C++ zero-based and Lua one-based. Copies are never writable store
references. No callbacks/GIL/Qt objects enter the native store, so worker logging
does not touch UI widgets. Lua and C++ both link the same core Console methods;
native script SDK compilation requires the normal Bazzalt import/shared library,
not a mock Console implementation or separate inline DLL-local service.

Tests cover no-editor no-ops/null reads, threaded logging, filters, IDs, repeated
host acquire/release, Lua nil semantics, Lua editor read/clear, real ConsolePanel
snapshot synchronization and UTF-8 text. Gameplay must not depend on read results.
