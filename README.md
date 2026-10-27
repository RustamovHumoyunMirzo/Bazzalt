# BAZZALT

[![Windows 32-bit Game Runtime](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml)
[![Windows 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml)
[![Linux 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-64-bit.yml)
[![Linux 32-bit Game Runtime](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-32-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/linux-32-bit.yml)

BAZZALT is an editor-first C++20 game engine in active alpha development. It
combines a native runtime with a PySide6 editor and Hub while keeping low-level
engine lifetime, rendering, importing, and persistence away from game code.

Version 0.7.0 supports Windows and Linux x64 Hub, Editor, game compiler and runtime.
Linux presentation uses X11/XWayland; see the [Linux guide](docs/public/0.7.0/manual/linux/en.html).
Windows and Linux also support x86 game output; Hub, Editor and game compiler always require x64.
Build games on their target OS with a matching prebuilt runtime SDK.
See [what is new in 0.7.0](docs/public/0.7.0/whats-new/en.html).

> **Alpha status:** core workflows are functional, but APIs and file formats may
> still change. BAZZALT is not yet recommended for production projects.

## Supported platforms

| Platform | Editor / Hub | Standalone game output |
| --- | --- | --- |
| Windows | Supported: x64 | Supported: x64, x86 |
| Linux | Supported: x64, X11/XWayland | Supported: x64, x86, X11/XWayland |
| macOS | Not supported | Not supported |
| iOS / iPadOS | Not supported | Not supported |
| Android | Not supported | Not supported |
| Web / WebAssembly | Not supported | Not supported |
| Xbox consoles | Not supported | Not supported |
| PlayStation consoles | Not supported | Not supported |
| Nintendo consoles | Not supported | Not supported |
| FreeBSD / other BSD systems | Not supported | Not supported |
| Other operating systems / devices | Not supported | Not supported |

Hub, Editor and game compiler require a 64-bit machine. x86 is a game-output
option only. Build on the target OS with its matching prebuilt runtime SDK;
cross-OS game compilation and ARM targets are not supported. Linux uses X11 or
XWayland, not native Wayland surfaces. These are alpha support targets, not a
promise of production readiness.

## Rendering backends

| Backend / mode | Windows | Linux | Notes |
| --- | --- | --- | --- |
| Automatic | Supported | Supported | Selects the renderer default; not a separate graphics API. |
| Vulkan | Supported | Supported | Requires a matching SDK backend and compatible GPU driver. |
| OpenGL | Supported | Supported | Requires a matching SDK backend and compatible GPU driver. |
| Direct3D 11 / 12 | Not supported | Not supported | No BAZZALT rendering backend. |
| Metal | Not supported | Not supported | No supported Apple platform distribution. |
| OpenGL ES | Not a supported target | Not a supported target | No supported mobile distribution. |
| WebGPU / WebGL | Not a supported release backend | Not a supported release backend | No supported browser target. |
| Console graphics APIs | Not supported | Not supported | No console platform integration. |
| NOOP | Tests only | Tests only | No pixels; internal headless validation, not game presentation. |

Backend choices depend on the backends included in the installed runtime SDK;
Preferences lists those available in that build. A renderer dependency's support
for another platform does not make it a supported BAZZALT target. See the
[platform and backend guide](docs/public/0.7.0/manual/platforms/en.html).

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
