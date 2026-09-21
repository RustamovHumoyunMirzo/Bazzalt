"""Traditional editor console panel with a small logging API."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QAction, QTextCursor
from PySide6.QtWidgets import QHBoxLayout, QMenu, QPlainTextEdit, QPushButton, QVBoxLayout, QWidget

from ...localization import LocalizationManager


class ConsoleLevel(Enum):
    Info = "info"
    Warning = "warning"
    Error = "error"


@dataclass(slots=True)
class ConsoleMessage:
    Text: str
    Level: ConsoleLevel = ConsoleLevel.Info


class ConsolePanel(QWidget):
    ContextMenuRequested = Signal(object, object)

    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__()
        self._localization = localization
        self._messages: list[ConsoleMessage] = []
        self._visible_levels = set(ConsoleLevel)
        layout = QVBoxLayout(self); layout.setContentsMargins(4, 4, 4, 4); layout.setSpacing(4)
        controls = QHBoxLayout()
        self.ClearButton = QPushButton()
        self.ClearButton.clicked.connect(self.Clear)
        controls.addWidget(self.ClearButton); controls.addStretch()
        self.View = QPlainTextEdit(); self.View.setReadOnly(True); self.View.setUndoRedoEnabled(False)
        self.View.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.View.customContextMenuRequested.connect(self._ShowContextMenu)
        layout.addLayout(controls); layout.addWidget(self.View, 1)
        localization.LocaleChanged.connect(lambda _: self._Retranslate())
        self._Retranslate()

    def AddMessage(self, text: str, level: ConsoleLevel = ConsoleLevel.Info) -> None:
        message = ConsoleMessage(str(text), level); self._messages.append(message)
        if level in self._visible_levels:
            self.View.appendHtml(self._Format(message))
            self.View.moveCursor(QTextCursor.MoveOperation.End)

    def Clear(self) -> None:
        self._messages.clear(); self.View.clear()

    def GetMessages(self) -> tuple[ConsoleMessage, ...]:
        return tuple(self._messages)

    def SetLevelVisible(self, level: ConsoleLevel, visible: bool) -> None:
        if visible: self._visible_levels.add(level)
        else: self._visible_levels.discard(level)
        self._Refresh()

    def _Refresh(self) -> None:
        self.View.clear()
        for message in self._messages:
            if message.Level in self._visible_levels: self.View.appendHtml(self._Format(message))

    @staticmethod
    def _Format(message: ConsoleMessage) -> str:
        color = {ConsoleLevel.Info: "#b8c0cc", ConsoleLevel.Warning: "#e8b85c",
                 ConsoleLevel.Error: "#ee6a6a"}[message.Level]
        import html
        return f'<span style="color:{color}">[{message.Level.name}] {html.escape(message.Text)}</span>'

    def _ShowContextMenu(self, position) -> None:  # type: ignore[no-untyped-def]
        menu = self.View.createStandardContextMenu()
        menu.addSeparator()
        clear = QAction(self._localization.Translate("console.clear"), menu)
        clear.triggered.connect(self.Clear); menu.addAction(clear)
        self.ContextMenuRequested.emit(menu, self.View.mapToGlobal(position))
        menu.exec(self.View.mapToGlobal(position))

    def _Retranslate(self) -> None:
        self.ClearButton.setText(self._localization.Translate("console.clear"))


__all__ = ["ConsoleLevel", "ConsoleMessage", "ConsolePanel"]
