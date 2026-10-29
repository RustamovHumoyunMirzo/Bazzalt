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
| Windows | ✅ Supported: x64 | ✅ Supported: x64, x86 |
| Linux | ✅ Supported: x64, X11/XWayland | ✅ Supported: x64, x86, X11/XWayland |
| macOS 12+ | Alpha: Intel x64 and Apple silicon | Alpha: x64 and arm64, Metal |
| iOS / iPadOS | Not supported | Not supported |
| Android | Not supported | Not supported |
| Web / WebAssembly | Not supported | Not supported |
| Xbox consoles | Not supported | Not supported |
| PlayStation consoles | Not supported | Not supported |
| Nintendo consoles | Not supported | Not supported |
| FreeBSD / other BSD systems | Not supported | Not supported |

Hub, Editor and game compiler require a 64-bit machine. x86 is a game-output
option only. Build on the target OS with its matching prebuilt runtime SDK;
cross-OS builds are not supported. macOS uses a matching native x64 or arm64 SDK;
Windows/Linux ARM and 32-bit macOS are not supported. Linux uses X11 or
XWayland, not native Wayland surfaces. These are alpha support targets, not a
promise of production readiness.

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
gameplay, assets, and materials. Open the downloaded HTML in a browser for
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
