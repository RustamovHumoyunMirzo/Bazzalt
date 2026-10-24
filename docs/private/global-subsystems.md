# Global subsystems implementation

The public service facade is Application.h/ApplicationControl.h. Subsystem.h is
the independent C++ registration API. No Component/Behavior inheritance or entity
UUID is involved. Global application events require a subsystem scope; ordinary
C++/Lua behaviors retain application controls and their own update lifecycle.

`Project.Properties["application.subsystems"]` is a JSON string containing up to
128 `{asset: UUID, level: 0..4}` registrations. Editor/subsystems.py is shared by
the compiler and modal editor registry. It checks UUID uniqueness, source type,
saved scene attachments and the legacy attachment manifest. Native startup
revalidates active scene attachments. Deliberately edited scenes fail Play/build
rather than executing a subsystem twice. `application.script` is rejected with a
migration diagnostic; the old project-settings picker is removed.

Compilation has a separate BazzaltGetSubsystemModuleV1 factory table, not the
entity script ABI. Registered COMPONENT sources are compiled with COMPONENT
mapped to the Subsystem base; entity methods fail compilation. SUBSYSTEM sources
are excluded from Add Component and rejected by attachment APIs. Cooked metadata
remaps registrations to the copied module asset UUIDs. Content filtering always
retains registered modules and fails when explicitly excluded. SDK/project header
fingerprints invalidate cached modules, including transitive project includes.

SubsystemRuntime stable-sorts by level and invokes OnRegister under a module
lease, object owner and thread-local subscription level. The event bus retains
that level on each connection and restores it during dispatch. The public
Subscribe path requires a subsystem owner and rejects event levels below its
load level. Only private host code can mutate capability/phase state. Registry
removal is reverse order, disconnecting listeners before object/module deletion.
Subsystems::Find/Get provide optional/checked cross-service interfaces through
public project headers; callers must not retain raw pointers after shutdown.
Native behavior interop uses optional BazzaltGetBehaviorV1 and a core-owned
Scripts::Find/Get registry, cleared before object/module disposal. Older modules
without that optional export can still execute but cannot be queried by this API.

Boot, BeforeWindowCreate, Init, Load, and PostWindowCreate trigger levels 0..4.
Repeat scene loads never instantiate modules twice. The player uses actual host
boundaries; editor Play simulates the game startup stages on the existing host.
Scene/driver guards depend on actual readiness flags, not merely chosen rank.
Audio readiness begins with gameplay. Initial focus notifications are deferred
until OnPlay (when audio is available), and initial resize notifications until
the rendering surface is ready. Ready listeners cannot subscribe to the
Content-level OnSceneLoad, even on later loads; use OnSceneLoaded instead.

Dispatch logs callback failures and records a pending fatal error rather than
throwing across a Win32 window procedure. Engine/host safe boundaries consume it.
The editor stops the failed session with Console diagnostics. The native player
catches exceptions in wWinMain and uses MessageBoxW, returning failure. Shutdown
must preserve pending errors until the player can report them. No callback is
guaranteed after a native memory fault, OS kill, or unsafe static initialization.

Application controls are shared across native modules and Lua (dot/colon calls).
Pacing is host-owned: steady-clock sleep in the player, nonblocking Qt timer in
the editor. 0 means uncapped; background caps combine with the foreground cap.
Quit stops editor Play, not the editor. Background suspension resets the frame
clock; minimized background simulation skips rendering/presentation.

Security boundary: native C++ remains trusted in-process code. SDK staging and
include validation prevent accidental internal dependencies, not arbitrary code
execution or forged declarations. Do not advertise this as a sandbox, anti-cheat
mechanism, or memory-safety guarantee. No private runtime headers are staged.
