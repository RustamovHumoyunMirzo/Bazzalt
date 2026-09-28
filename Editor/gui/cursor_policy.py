"""Consistent cursor ownership for the editor's mixed native/Qt UI."""

from __future__ import annotations

from PySide6.QtCore import QEvent, QObject, Qt
from PySide6.QtWidgets import QLineEdit, QPlainTextEdit, QTextEdit, QWidget


class EditorCursorPolicy(QObject):
    """Restore the cursor when crossing native render and Qt widget boundaries.

    On Windows, a cursor selected by a child HWND can remain active after Qt has
    entered a sibling whose inherited cursor is already ``ArrowCursor``.  Merely
    querying that sibling therefore looks correct while the visible OS cursor is
    still an I-beam.  Reapplying the intended cursor on entry gives each editor
    surface explicit ownership of it.
    """

    _ENTRY_EVENTS = (QEvent.Type.Enter, QEvent.Type.HoverEnter)

    def __init__(self, editor: QWidget) -> None:
        super().__init__(editor)
        self._editor = editor

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() not in self._ENTRY_EVENTS or not isinstance(watched, QWidget):
            return False
        if not self._BelongsToEditor(watched):
            return False

        if isinstance(watched, (QLineEdit, QPlainTextEdit, QTextEdit)):
            watched.setCursor(Qt.CursorShape.IBeamCursor)
        else:
            # Keep intentional resize, drag, pointing-hand, and busy cursors.
            # Only an inherited/stale I-beam is invalid on ordinary widgets.
            shape = watched.cursor().shape()
            intended = (Qt.CursorShape.ArrowCursor
                        if shape in (Qt.CursorShape.ArrowCursor,
                                     Qt.CursorShape.IBeamCursor)
                        else shape)
            watched.setCursor(intended)
        return False

    def _BelongsToEditor(self, widget: QWidget) -> bool:
        current: QObject | None = widget
        while current is not None:
            if current is self._editor:
                return True
            current = current.parent()
        return False


__all__ = ["EditorCursorPolicy"]
