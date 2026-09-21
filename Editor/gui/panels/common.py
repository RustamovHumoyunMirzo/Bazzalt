"""Shared panel building blocks."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout

from ...localization import LocalizationManager


class ViewportPlaceholder(QFrame):
    def __init__(self, localization: LocalizationManager, text_key: str) -> None:
        super().__init__()
        self.setObjectName("ViewportPlaceholder")
        self.setStyleSheet("#ViewportPlaceholder { background: #000; border: 0; }")
        layout = QVBoxLayout(self)
        self.Label = QLabel(localization.Translate(text_key))
        self.Label.setObjectName("ViewportPlaceholderLabel")
        self.Label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.Label)
        localization.LocaleChanged.connect(
            lambda _: self.Label.setText(localization.Translate(text_key))
        )


__all__ = ["ViewportPlaceholder"]
