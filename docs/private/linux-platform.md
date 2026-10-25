# Linux platform and release contract

0.7.0 targets Ubuntu 24.04 x86-64. Hub/Editor/Compiler are x64-only. Windows
x86 output remains separate; Linux x86, native Wayland and implicit cross-OS
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

The official Filament SDK uses libc++; all native targets and generated C++
script modules must compile with Clang -stdlib=libc++. Install libc++-dev and
libc++abi-dev on build hosts; shipped games require the matching runtime libs.
