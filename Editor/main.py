from PySide6.QtWidgets import QApplication
from edtr.gui.Editor import Editor
import sys


def main():
    app = QApplication(sys.argv)
    editorWin = Editor()
    editorWin.showMaximized()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
