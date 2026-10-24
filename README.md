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
assets, with C++/Lua access and editor authoring/preview. See the
[GUI guide](docs/public/0.6.0/native/gui/en.html). Source builds also require the
pinned FreeType source (`scripts/get_freetype.ps1` / `scripts/get_freetype.sh`);
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

Issues and focused pull requests are welcome. Include tests for behavior
changes, keep third-party types out of `include/Bazzalt`, preserve UUID and YAML
forward compatibility, and run the native and editor test suites before
submitting changes.

Public gameplay API changes must update the Lua bindings, API coverage tests,
and documentation too. Lua names follow the same PascalCase conventions as C++.
See [Lua integration](docs/private/lua-scripting.md) for bindings, lifecycle,
bytecode assets, and DLL deployment rules.

## License

This project uses a modular multi-license structure:

Portions of this software use the FreeType Project (www.freetype.org), under the
FreeType License. Packaged runtimes include its license and bundled zlib notice
in `licenses/`. No system font is redistributed by default; projects must use
fonts they have permission to ship.

* **Core Engine:** Licensed under the **Apache License 2.0**. You can find the full terms in the root [LICENSE](LICENSE) file.
* **Editor (`/Editor`) and Launcher (`/Launcher`):** Licensed under the **GNU Lesser General Public License v3 (LGPLv3)**. The specific terms for the editor are located in the [Editor/LICENSE](Editor/LICENSE) and [Launcher/LICENSE](Launcher/LICENSE) files.

This structure allows the core engine to remain permissive under Apache 2.0 while ensuring full legal and technical compliance with the [PySide6 (Qt)](https://www.qt.io/qt-for-python) framework used by the editor and launcher.
