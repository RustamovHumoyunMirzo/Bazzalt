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
