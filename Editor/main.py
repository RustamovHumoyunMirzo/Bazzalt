import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

if __package__ in {None, ""}:
    # Support `python editor/main.py` without relying on the current directory.
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from Editor.gui.application import Editor
    from Editor.localization import LocalizationManager
    from Editor.resources import ResourceManager
    from Editor.theme import ThemeManager
else:
    from .gui.application import Editor
    from .localization import LocalizationManager
    from .resources import ResourceManager
    from .theme import ThemeManager


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("BAZZALT Editor")
    app.setOrganizationName("BAZZALT")
    theme_manager = ThemeManager(app)
    resources = ResourceManager()
    localization = LocalizationManager(resources)
    editor_window = Editor(theme_manager, resources, localization)
    editor_window.showMaximized()

    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
