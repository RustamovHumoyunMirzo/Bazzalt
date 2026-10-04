# BAZZALT

[![Windows 32-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-32-bit.yml)
[![Windows 64-bit](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml/badge.svg?event=push)](https://github.com/RustamovHumoyunMirzo/Bazzalt/actions/workflows/windows-64-bit.yml)

BAZZALT is an editor-first C++20 game engine in active alpha development. It
combines a native runtime with a PySide6 editor and Hub while keeping low-level
engine lifetime, rendering, importing, and persistence away from game code.

> **Alpha status:** core workflows are functional, but APIs and file formats may
> still change. BAZZALT is not yet recommended for production projects.

## Contributing

Issues and focused pull requests are welcome. Include tests for behavior
changes, keep third-party types out of `include/Bazzalt`, preserve UUID and YAML
forward compatibility, and run the native and editor test suites before
submitting changes.

## License

This project uses a modular multi-license structure:

* **Core Engine:** Licensed under the **Apache License 2.0**. You can find the full terms in the root [LICENSE](LICENSE) file.
* **Editor (`/Editor`) and Launcher (`/Launcher`):** Licensed under the **GNU Lesser General Public License v3 (LGPLv3)**. The specific terms for the editor are located in the [Editor/LICENSE](Editor/LICENSE) and [Launcher/LICENSE](Launcher/LICENSE) files.

This structure allows the core engine to remain permissive under Apache 2.0 while ensuring full legal and technical compliance with the [PySide6 (Qt)](https://www.qt.io/qt-for-python) framework used by the editor.
