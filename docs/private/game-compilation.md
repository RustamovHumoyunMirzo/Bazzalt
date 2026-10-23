# Game compiler and native player

The root `Compiler/` package is a development tool, packaged as an independent x64 `BazzaltCompiler.exe` using Nuitka. Editor integration uses asynchronous `QProcess` and ASCII-escaped newline-delimited JSON stage events (lossless Unicode through Windows legacy pipes), displayed in a modal progress dialog. The final completion event is emitted after publication/cleanup and before launching; game stdout/stderr cannot inherit the compiler protocol pipe. No compiler implementation, editor module, Python binary, Qt dependency, or source script is shipped in the game payload.

## Boundaries

- `Compiler/settings.py`: versioned per-project schema, validation and security presets. Never add a raw secret field here.
- `Compiler/build.py`: project build lock, matching prebuilt runtime validation, target-specific native script compilation, external cooker, custom shader cooking, PE production resources, final output ownership and publication.
- `Compiler/native/Cooker.cpp`: x64 headless host for the existing native import pipeline. It never initializes Filament. All invalid imports are fatal to a production cook, unlike editor project opening.
- `AssetDatabase::ExportCooked` and `Engine::ExportCookedProject`: immutable UUID index, asset products, scenes rewritten to stable script references, native module registration. Source projects are not rewritten.
- `Compiler/native/Player.cpp`: Win32 window, native input events forwarded into SDL-backed InputAccess, game viewport (never Scene view), resize, rendering, scripts, audio, teardown. `--verify-package` validates content without GPU initialization; `--smoke-test` exercises three hidden game frames.

`BuildSettings.json` is beside the project file. Work is under `.bazzalt/Cache/Build`; final outputs are in `bin`. Settings contain production/version/license metadata, x86/x64 target, startup scene, runtime directory, window options, ZIP output, BPAK/loose format, chunk size, padding, encryption and key delivery. Output ownership markers contain a path hash, not the developer's absolute project path.

## Native startup and windowing

Window modes are windowed, maximized, borderless-sized, and fullscreen desktop on the primary monitor. The player paints black from its first native erase/paint messages. An owned native cover (not a Filament child surface) displays the centered brand over the client area while extraction, initialization and warm-up occur. The renderer records successful viewport submissions; a bounded `flushAndWait` gates cover removal. Editor-only frames warm the scene without running gameplay scripts. Intro controls are enabled, minimum duration, logo size and allowing input to skip; skipping cannot bypass frame readiness. The SVG source is `Launcher/BazzaltLogo.svg`, rasterized once by `embed_game_logo.py` and embedded into the executable as BGRX pixels; Qt is build-time only. Smoke tests bypass the minimum timer, not the readiness gate.

## BPAK v1

Each chunk starts with eight bytes `BZPAK001`. Repeated records use little-endian `<IIQQB`: UTF-8 path byte count, segment byte count, file offset, total file size, encryption flag. Next are the path, 12-byte nonce, 16-byte GCM tag, 32-byte plaintext SHA-256 digest and segment bytes. Segment size is bounded to 4 MiB. AES-256-GCM AAD is the exact 25-byte header plus UTF-8 path. There is no compression within BPAK v1; ZIP distribution uses deflate separately. Never reinterpret this as a standard ZIP or generic encrypted archive.

The Windows player uses system BCrypt AES-GCM/SHA-256. The tool uses cryptography's [AESGCM API](https://cryptography.io/en/latest/hazmat/primitives/aead/). Nonces are independently random; keys are 32 random bytes per build except environment-key delivery. `BZKEY001` plus 32 bytes at the end of the PE executable implements explicitly recoverable overlay delivery. Sibling delivery is `game.key`; external delivery reads a named environment variable containing 64 hex digits. No encryption algorithm is hand-written.

The player checks framing, safe relative paths, case-folded duplicates, segment order, file sizes, integrity and complete files. Bounds: 4 MiB segments, 4096-byte paths, 1 million records, 8 GiB per extracted file, 32 GiB total extraction. Extraction is a unique per-process temporary directory; engine destruction precedes directory cleanup. Normal shutdown deletes plaintext; crashes can leave it. A VFS/streaming reader and signed manifests are future changes requiring a new format/ABI review, not silent additions to this protocol.

Presets are relative configurations, not secrecy guarantees. Embedded/sibling keys are recoverable; players can inspect decrypted memory and temporary files. Padding is not authentication. Unencrypted SHA-256 is corruption detection only. No online key server, code signing, DRM, sandbox, or anti-cheat is implemented. Document these limits when extending settings.

## Releases and verification

Cook workspaces are disposable and removed on success or failure. Incremental scripts remain separate; ZIP Build and Launch retains one owned launch image. Older GUID staging directories are not automatically deleted. Cooking exports current import products and explicit glTF dependencies rather than whole UUID folders with obsolete generations. Source copies are still retained for runtime path consumers. BPAK v1 has no compression; 64 MiB is a chunk ceiling, not a promised reduction in size.

Plaintext extraction directories have a protected Windows ACL granting access only to the launching account and SYSTEM, inherited by extracted files. Control characters in package paths and zero-progress nonempty segments are rejected. These changes do not implement a VFS, publisher signatures or DRM, and do not hide plaintext from the launching account or administrators. The settings UI omits the explanatory paragraph; the security limits above still apply.

CI builds the native player for both Windows architectures, and the cooker only for x64. `stage_game_runtime.py` copies binaries and the target ScriptSDK into `RuntimeTemplates/windows-{x64,x86}` and checks PE machine types. Core releases carry their runtime template; x64 Editor releases include x64 runtime and standalone compiler/cooker tools. Install the x86 Core template separately and select it in Build Settings. Large dependencies are built by existing CI workflows, never by the content compiler.

Run `python -m unittest discover -s Compiler/tests -v`, editor tests, and CTest. Native integration tests consume the already-built runtime, build and verify encrypted/unprotected games and ZIPs, and reject tampered packages. `game-compiler.yml` runs tool unit tests independently. Test changes in clean packaged editor distributions too: Nuitka tool dependencies must remain outside game outputs.
