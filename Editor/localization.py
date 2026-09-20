"""Runtime-switchable editor localization backed by JSON catalogs."""

from __future__ import annotations

import json
from typing import Any

from PySide6.QtCore import QObject, Signal

from .resources import ResourceManager


class LocalizationManager(QObject):
    LocaleChanged = Signal(str)

    def __init__(self, resources: ResourceManager, locale: str = "en") -> None:
        super().__init__()
        self._resources = resources
        self._locale = ""
        self._messages: dict[str, str] = {}
        self.SetLocale(locale)

    def GetLocale(self) -> str:
        return self._locale

    def SetLocale(self, locale: str) -> None:
        normalized = locale.strip().replace("-", "_")
        if not normalized or any(value in normalized for value in ("/", "\\", "..")):
            raise ValueError("Invalid locale identifier")
        data: Any = json.loads(self._resources.ReadText(f"locales/{normalized}.json"))
        if not isinstance(data, dict) or not all(
            isinstance(key, str) and isinstance(value, str) for key, value in data.items()
        ):
            raise ValueError(f"Locale catalog {normalized!r} must contain string values")
        self._locale = normalized
        self._messages = data
        self.LocaleChanged.emit(normalized)

    def Translate(self, key: str, **arguments: object) -> str:
        text = self._messages.get(key, key)
        try:
            return text.format(**arguments)
        except (KeyError, ValueError) as error:
            raise ValueError(f"Invalid localization arguments for {key!r}") from error


__all__ = ["LocalizationManager"]
