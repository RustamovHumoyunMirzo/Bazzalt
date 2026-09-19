from PySide6.QtWidgets import QMainWindow
from ...gui import docking


class Editor(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("BAZZALT")
        self.resize(1024, 720)

        self.docking = docking.DockingSystem()
        self.setCentralWidget(self.docking)
