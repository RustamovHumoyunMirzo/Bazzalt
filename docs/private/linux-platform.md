# Linux platform and release contract

0.7.0 targets Ubuntu 24.04 x86-64. Hub/Editor/Compiler are x64-only. Windows and Linux
x86 game outputs use separate SDKs; native Wayland and implicit cross-OS
compilation are rejected. bazzalt/platforms.py is the shared capability policy;
Build Settings warns, GameBuildController logs and aborts, and Compiler.Build
enforces the policy independently before opening the build lock or cooking.

Linux PlayerLinux.cpp uses SDL3 for its X11 window, events and native error
dialog; an X11 child cover keeps the startup black/logo visible until a rendered
frame is ready. It uses the same private Engine lifecycle as the Win32 player.
The editor selects Qt xcb. Do not pass Wayland surface IDs to the X11 Filament
platform. Linux CI includes offscreen widget tests and Xvfb GPU/player tests.

PosixCrypto/Package/Integrity implement the existing version-3 signed inventory,
key wrapping, authenticated manifests, bounded extraction and decoded hashes.
OpenSSL is a distribution dependency, not embedded into core. Zstandard uses
the Filament SDK static library and a BPAK compression codec flag (bit 3 plus
the compressed flag). The Windows reader accepts this codec as well as legacy
XPRESS. POSIX leases use O_NOFOLLOW/fstat/flock and are advisory, not equivalent
to Windows deny-write handles. Private extracted directories use 0700. DPAPI
is explicitly unsupported on Linux; never silently substitute weaker storage.

ELF inspection, artifact metadata and installation permissions are shared by
scripts/release_artifacts.py and Hub validation. Packages contain regular files,
including copies of Lua SONAME aliases; ZIP installation rejects links and
strips special permission bits. ScriptSDK uses libBazzalt.so and public headers;
scripts find clang++ through PATH and generate .so modules. Export/cook must
accept the native module suffix rather than require .dll.

build_linux_release.py consumes an existing native build, runs Nuitka only for
the three Python-side programs and stages native game templates independently.
Game output never contains Python/Qt. The Windows release workflow calls the
Linux reusable release workflow, waits for its artifacts and publishes a single
cross-platform Hub catalog. Dependency builds stay on CI, not contributors'
machines unless they explicitly choose a source build.

The Linux executable is a small integrity bootstrap. Only after verifying the
signed inventory does it dlopen libbazzalt_player.so and its engine dependencies.
Do not add a direct Bazzalt dependency to the bootstrap: ELF constructors would
otherwise execute before validation. The worker remains resident until exit.

The official x64 Filament SDK uses libc++; its native targets and generated C++
script modules must compile with Clang -stdlib=libc++. Install libc++-dev and
libc++abi-dev on build hosts; shipped games require the matching runtime libs.

Linux x86 uses a CI-built Filament 1.77.0 SDK pinned to the same source commit
as Win32, with -m32, USE_STATIC_LIBCXX=OFF and libstdc++. Its native library
layout is lib/x86; no x64 archive can be substituted. Engine CMake and the
script compiler select the same ABI. Editor/Hub/compiler stay x64. The release
workflow embeds the x86 runtime template and publishes a separate x86 Core ZIP.
The host needs IA32 execution and target libraries for package verification.
Linux x86 targets require SSE2. The SDK, engine and generated script modules
compile with -msse2 -mfpmath=sse; this also enables Filament's forced four-wide
culling loops. The real Culler.cpp is compiled as an optimized precheck before
the full renderer build, with failed requested optimizations still treated as errors.

## Pinned dependency compatibility

Sol2 3.3.1 hits LLVM's Clang 18 dependent-noexcept callback regression
(ThePhD/sol2#1581, llvm/llvm-project#91362). Fetch scripts apply
patch_sol2_clang18.py; CMake also applies it for existing Clang 18 checkouts.
Only two member-variable callback entry points change, and only Clang 18 sees
the omitted noexcept. Other compilers retain the upstream specification.
Linux CI compiles Sol2CompatibilityProbe.cpp before building the engine.
All UNIX static dependencies must be compiled with PIC before linking them
into shared core/GUI libraries. CMAKE_POSITION_INDEPENDENT_CODE is set before
dependency targets are created, including rapidyaml and c4core. Linux x86 CI
also needs amd64 EGL/GL/XKB libraries for the host Python/Qt SVG generators;
these are build-host tools, not 32-bit game dependencies. Run those generators
with QT_QPA_PLATFORM=offscreen, even though the native SDL smoke uses Xvfb.

Filament's Linux universal BlueGL assembly selects x64 for every non-AArch64
target. patch_filament_linux_x86.py adds a dedicated __i386__ branch and emits
PIC tail-call stubs from every upstream entry point. EAX is scratch; the
argument stack, callee-saved registers and x87/SSE argument state are preserved.
test_bluegl_linux_x86.py builds a small shared dispatch library and PIE caller
and exercises integer, float, double and pointer calls 10,000 times before the
large renderer build. Both patch scripts are idempotent and reject unfamiliar
source layouts. The Filament cache key includes patches and the runtime probe.

The x86 source patch also requires _LIBCPP_VERSION before enabling libc++'s
mutex annotations. Defining _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS alone does
not annotate libstdc++'s std::mutex. Locking code remains unchanged; other
compiler diagnostics remain enabled.

libc++ 18 lacks floating-point std::from_chars. Private Utils/NumberParsing.h
uses native from_chars when available and otherwise parses strict decimal
numbers through strtof_l/strtod_l and an owned C locale. Streams can reject
valid subnormals when libc sets ERANGE, so do not use num_get here. Both paths reject trailing data,
nonfinite values and out-of-range results without changing the destination.
Audio, scenes, GUI fields and CSS use this helper. NumberParsingTests executes
both paths even on Windows and runs before the full Linux builds.

Scene system keys compare owned RTTI type names, not RTTI addresses: hidden
type_info objects in the Python bridge and shared libraries are distinct under
libc++. Cached names avoid per-frame temporary allocations. SystemIdentityTests
exercises add/get/remove through a separate module with hidden RTTI.

Each native host owns its OS event pump (Qt, Win32 or SDL). InputAccess queues
forwarded SDL-shaped input privately and drains it exactly once in BeginFrame.
It must not push forwarded events back into SDL's native queue, poll window
events from another host, or depend on SDL poll-sentinel timing. Focus changes
clear the private queue. Linux x86 core links libatomic for 64-bit atomics;
target systems need the corresponding libatomic1 package.

The editor/compiler host remains x64. A Linux multilib host may run either an
x64 or x86 native content cooker; the x86 CI pipeline uses its real x86 cooker
for the encrypted-package/player integration test. Validate the tool's ELF
platform, supported machine/class and executable permission before invoking it.
Cooker word size does not determine game architecture: runtime-template and
script-module checks still enforce the selected target architecture separately.
