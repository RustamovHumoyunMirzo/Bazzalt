# BAZZALT

[![Windows 32-bit Game Runtime](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml)
[![Windows 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml)

BAZZALT is an editor-first C++20 game engine in active alpha development. It
combines a native runtime with a PySide6 editor and Hub while keeping low-level
engine lifetime, rendering, importing, and persistence away from game code.

Hub and Editor require a 64-bit machine. The core/game runtime retains 32-bit
support for generating games; the Win32 CI badge covers that runtime only.

> **Alpha status:** core workflows are functional, but APIs and file formats may
> still change. BAZZALT is not yet recommended for production projects.

Version 0.6 adds native audio: Audio Clips with editor preview, positional
sources/listeners, distance curves, streaming, playback controls, and mixer
buses. C++ and Lua share the same audio API. See the
[audio guide](docs/public/0.6.0/manual/audio/en.html).

Runtime GUI supports responsive layouts, CSS stylesheet assets and FreeType font
assets with HarfBuzz shaping and bidirectional text, C++/Lua access and editor authoring/preview. See the
[GUI guide](docs/public/0.6.0/native/gui/en.html). Source builds also require the
pinned FreeType source (`scripts/get_freetype.ps1` / `scripts/get_freetype.sh`) and
text shaping sources (`scripts/get_text_shaping.ps1` / `.sh`);
large renderer dependencies remain supplied by the existing CI workflows.

Build standalone Windows x86/x64 games from **Build → Build Settings**. The
separate content compiler packages saved scenes, scripts, and assets with a
prebuilt native runtime; games do not include Python or the editor. See
[building a game](docs/public/0.6.0/manual/building-games/en.html), including
packaging, ZIP output, and the limits of asset encryption.

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
