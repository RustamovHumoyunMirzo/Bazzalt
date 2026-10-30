![BAZZALT](./bazzalt/assets/branding/BazzaltLogo.svg)

[![Windows 32-bit Game Runtime](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml)
[![Windows 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml)
[![Linux 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-64-bit.yml)
[![Linux 32-bit Game Runtime](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-32-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-32-bit.yml)
[![macOS 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/macos-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/macos-64-bit.yml)

BAZZALT is an editor-first C++20 game engine in active alpha development. It
combines a native runtime with a PySide6 editor and Hub while keeping low-level
engine lifetime, rendering, importing, and persistence away from game code.

See [what is new in 0.8.0](docs/public/0.8.0/whats-new/en.html).

> **Alpha status:** core workflows are functional, but APIs and file formats may
> still change. BAZZALT is not yet recommended for production projects.

macOS installation, native menus, signing and player Keychain provisioning are
covered in the [macOS guide](docs/public/0.8.0/manual/macos/en.html).

## Supported platforms & backends

### Platforms

| Platform | Editor / Hub | Standalone game output |
| --- | --- | --- |
| Windows | ✅ x64 | ✅ x64, x86, ARM64 |
| Linux | ✅ x64; X11, Wayland | ✅ x64, x86, ARM64; X11, Wayland |
| macOS 12+ | ✅ Intel x64, Apple silicon | ✅ x64, ARM64 |
| iOS / iPadOS | Not supported | Not supported |
| Android | Not supported | Not supported |
| Web / WebAssembly | Not supported | Not supported |
| Xbox consoles | Not supported | Not supported |
| PlayStation consoles | Not supported | Not supported |
| Nintendo consoles | Not supported | Not supported |
| FreeBSD / other BSD systems | Not supported | Not supported |

Hub, Editor and game compiler require a 64-bit machine. x86 is a game-output
option only. ARM64 Core/game runtime artifacts are built on native Windows/Linux CI
runners. macOS supports Intel and Apple silicon. Modern macOS 32-bit output and
ARM32 do not have runtime targets. Native Wayland uses Vulkan with bounded
asynchronous readback into Qt/SDL composition; X11 retains direct swapchain presentation.

Build Settings supports native builds, WSL, user-supplied Zig/MinGW toolchains,
content/Lua-only foreign packaging, and SSH workers. C++ cross builds require a
matching toolchain-built SDK; shipped MSVC SDKs are not GNU/Zig ABI-compatible.
See [cross-build requirements and the host/target table](docs/public/0.8.0/manual/cross-builds/en.html)
and [remote workers](docs/public/0.8.0/manual/remote-builds/en.html).

### Rendering backends

| Backend | Windows | Linux | macOS | Notes |
| --- | --- | --- | --- | --- |
| Automatic | Supported | Supported | Metal | Selects an available SDK backend. |
| Vulkan | Supported | Supported | Unsupported | Compatible GPU/driver required. |
| OpenGL | Supported | Supported | Unsupported | Compatible GPU/driver required. |
| Metal | Unsupported | Unsupported | Supported | Native Cocoa / Metal presentation. |
| Direct3D 11 / 12 | Unsupported | Unsupported | Unsupported | No backend. |
| OpenGL ES / WebGPU / WebGL | Unsupported | Unsupported | Unsupported | No mobile/browser distribution. |
| NOOP | Tests only | Tests only | Tests only | No presentation. |

Backend choices depend on the backends included in the installed runtime SDK;
Preferences lists those available in that build. A renderer dependency's support
for another platform does not make it a supported BAZZALT target. See the
[platform and backend guide](docs/public/0.8.0/manual/platforms/en.html).

## Documentation

Read the [public guides](docs/public/index.html) for scene editing, C++ and Lua
gameplay, assets, and materials. The [Shader Graph guide](docs/public/0.8.0/manual/panels/shader-graph/en.html)
covers node-based shader authoring and previews. Open the downloaded HTML in a browser for
offline use. Documentation authoring and version/language setup are described
in [docs/public/README.md](docs/public/README.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines, API/Lua parity,
testing and dependency maintenance.

## License

Core: [Apache-2.0](LICENSE). Editor and Hub:
[LGPLv3](Editor/LICENSE) / [LGPLv3](Launcher/LICENSE).
Dependency licenses, attribution and distribution notices are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
