"""Reusable typed controls for component inspectors and editor tooling."""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from enum import Enum

from PySide6.QtCore import QSize, Qt, Signal
from PySide6.QtGui import QAction, QColor, QPainter, QPaintEvent
from PySide6.QtWidgets import (
    QAbstractSpinBox, QCheckBox,
    QColorDialog,
    QComboBox,
    QDoubleSpinBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMenu,
    QPushButton,
    QSlider,
    QSizePolicy,
    QSpinBox,
    QToolButton,
    QWidget,
)


class FieldState(Enum):
    Default = "default"
    Modified = "modified"
    Warning = "warning"
    Error = "error"


class FieldStateSupport:
    """Adds semantic validation/modification state to a Qt input."""

    def SetFieldState(self, state: FieldState) -> None:
        if not isinstance(state, FieldState):
            raise TypeError("state must be a FieldState")
        self.setProperty("fieldState", state.value)
        self.style().unpolish(self)
        self.style().polish(self)
        self.update()

    def GetFieldState(self) -> FieldState:
        return FieldState(self.property("fieldState") or FieldState.Default.value)

    def SetReadOnly(self, read_only: bool) -> None:
        self.setEnabled(not read_only)


class FieldWidget(FieldStateSupport, QWidget):
    """Base widget for composite inspector inputs."""


class StringInput(FieldStateSupport, QLineEdit):
    def GetValue(self) -> str:
        return self.text()

    def SetValue(self, value: str) -> None:
        self.setText(str(value))


class BoolInput(FieldStateSupport, QCheckBox):
    ValueChanged = Signal(bool)

    def __init__(self, value: bool = False, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setChecked(value)
        self.toggled.connect(self.ValueChanged)

    def GetValue(self) -> bool:
        return self.isChecked()

    def SetValue(self, value: bool) -> None:
        self.setChecked(bool(value))


class IntInput(FieldStateSupport, QSpinBox):
    def __init__(self, minimum: int = -2_147_483_648,
                 maximum: int = 2_147_483_647,
                 value: int = 0, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setRange(minimum, maximum)
        self.setValue(value)
        self.setKeyboardTracking(False)
        self.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)

    def GetValue(self) -> int:
        return self.value()

    def SetValue(self, value: int) -> None:
        self.setValue(value)


class FloatInput(FieldStateSupport, QDoubleSpinBox):
    def __init__(self, minimum: float = -1.0e12, maximum: float = 1.0e12,
                 value: float = 0.0, decimals: int = 4,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setRange(minimum, maximum)
        self.setDecimals(decimals)
        self.setValue(value)
        self.setKeyboardTracking(False)
        self.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)
        self.setMinimumWidth(0)

    def sizeHint(self) -> QSize:
        return QSize(52, 22)

    def minimumSizeHint(self) -> QSize:
        return QSize(24, 22)

    def GetValue(self) -> float:
        return self.value()

    def SetValue(self, value: float) -> None:
        self.setValue(value)


class VectorInput(FieldWidget):
    ValueChanged = Signal(tuple)
    AxisNames = ("X", "Y", "Z", "W")

    def __init__(self, dimensions: int, value: Sequence[float] | None = None,
                 decimals: int = 4, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        if dimensions < 2 or dimensions > 4:
            raise ValueError("dimensions must be between 2 and 4")
        values = tuple(value or (0.0,) * dimensions)
        if len(values) != dimensions:
            raise ValueError("value size must match dimensions")
        self._layout = QHBoxLayout(self)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self._layout.setSpacing(3)
        self.Inputs: list[FloatInput] = []
        self.Labels: list[QLabel] = []
        for axis, component in zip(self.AxisNames, values):
            label = QLabel(axis)
            label.setObjectName("VectorAxisLabel");label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            label.setFixedWidth(10)
            field = FloatInput(value=float(component), decimals=decimals)
            field.setSizePolicy(QSizePolicy.Policy.Ignored,QSizePolicy.Policy.Fixed)
            field.valueChanged.connect(lambda _value: self.ValueChanged.emit(self.GetValue()))
            self.Labels.append(label);self.Inputs.append(field)
            self._layout.addWidget(label);self._layout.addWidget(field,1)

    def GetValue(self) -> tuple[float, ...]:
        return tuple(field.value() for field in self.Inputs)

    def SetValue(self, value: Sequence[float]) -> None:
        if len(value) != len(self.Inputs):
            raise ValueError("value size must match dimensions")
        for field, component in zip(self.Inputs, value):
            field.setValue(float(component))

    def SetRange(self, minimum: float, maximum: float) -> None:
        for field in self.Inputs:
            field.setRange(minimum, maximum)


class Vec2Input(VectorInput):
    def __init__(self, value: Sequence[float] | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(2, value, parent=parent)


class Vec3Input(VectorInput):
    def __init__(self, value: Sequence[float] | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(3, value, parent=parent)


class Vec4Input(VectorInput):
    def __init__(self, value: Sequence[float] | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(4, value, parent=parent)


class RangeInput(FieldWidget):
    """A synchronized floating-point slider and numeric entry."""

    ValueChanged = Signal(float)
    _Resolution = 10_000

    def __init__(self, minimum: float = 0.0, maximum: float = 1.0,
                 value: float = 0.0, decimals: int = 3,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        if maximum <= minimum:
            raise ValueError("maximum must be greater than minimum")
        self._minimum, self._maximum = float(minimum), float(maximum)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.Slider = QSlider(Qt.Orientation.Horizontal)
        self.Slider.setRange(0, self._Resolution)
        self.Input = FloatInput(minimum, maximum, value, decimals)
        layout.addWidget(self.Slider, 1)
        layout.addWidget(self.Input)
        self.Slider.valueChanged.connect(self._SliderChanged)
        self.Input.valueChanged.connect(self._InputChanged)
        self.SetValue(value)

    def GetValue(self) -> float:
        return self.Input.value()

    def SetValue(self, value: float) -> None:
        clamped = max(self._minimum, min(self._maximum, float(value)))
        self.Input.setValue(clamped)
        position = round((clamped - self._minimum) /
                         (self._maximum - self._minimum) * self._Resolution)
        self.Slider.setValue(position)

    def _SliderChanged(self, position: int) -> None:
        value = self._minimum + (self._maximum - self._minimum) * position / self._Resolution
        self.Input.blockSignals(True)
        self.Input.setValue(value)
        self.Input.blockSignals(False)
        self.ValueChanged.emit(self.Input.value())

    def _InputChanged(self, value: float) -> None:
        position = round((value - self._minimum) /
                         (self._maximum - self._minimum) * self._Resolution)
        self.Slider.blockSignals(True)
        self.Slider.setValue(position)
        self.Slider.blockSignals(False)
        self.ValueChanged.emit(value)


class EnumInput(FieldStateSupport, QComboBox):
    def SetOptions(self, options: Iterable[tuple[str, object]]) -> None:
        current = self.currentData()
        self.clear()
        for label, value in options:
            self.addItem(label, value)
        index = self.findData(current)
        if index >= 0:
            self.setCurrentIndex(index)

    def GetValue(self):
        return self.currentData()

    def SetValue(self, value: object) -> bool:
        index = self.findData(value)
        if index < 0:
            return False
        self.setCurrentIndex(index)
        return True


class MultiSelectInput(FieldStateSupport, QToolButton):
    SelectionChanged = Signal(tuple)

    def __init__(self, placeholder: str = "—", parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._placeholder = placeholder
        self._menu = QMenu(self)
        self.setMenu(self._menu)
        self.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        self._UpdateText()

    def SetOptions(self, options: Iterable[tuple[str, object]]) -> None:
        selected = set(self.GetValues())
        self._menu.clear()
        for label, value in options:
            action = QAction(label, self._menu)
            action.setData(value)
            action.setCheckable(True)
            action.setChecked(value in selected)
            action.toggled.connect(lambda _checked: self._SelectionUpdated())
            self._menu.addAction(action)
        self._UpdateText()

    def GetValues(self) -> tuple[object, ...]:
        return tuple(action.data() for action in self._menu.actions() if action.isChecked())

    def SetValues(self, values: Iterable[object]) -> None:
        selected = set(values)
        for action in self._menu.actions():
            action.blockSignals(True)
            action.setChecked(action.data() in selected)
            action.blockSignals(False)
        self._SelectionUpdated()

    def _SelectionUpdated(self) -> None:
        self._UpdateText()
        self.SelectionChanged.emit(self.GetValues())

    def _UpdateText(self) -> None:
        labels = [action.text() for action in self._menu.actions() if action.isChecked()]
        self.setText(", ".join(labels) if labels else self._placeholder)


class PickerInput(FieldWidget):
    PickRequested = Signal()
    Cleared = Signal()
    ValueChanged = Signal(object)

    def __init__(self, placeholder: str = "", parent: QWidget | None = None,
                 accepted_mime: str | None = None, validator=None) -> None:
        super().__init__(parent)
        self._value = None; self._accepted_mime = accepted_mime; self._validator = validator
        self.setAcceptDrops(accepted_mime is not None)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(3)
        self.Display = QLineEdit()
        self.Display.setReadOnly(True)
        self.Display.setPlaceholderText(placeholder)
        self.PickButton = QPushButton("…")
        self.PickButton.setObjectName("PickerButton")
        self.ClearButton = QPushButton("×")
        self.ClearButton.setObjectName("PickerClearButton")
        self.ClearButton.setEnabled(False)
        self.PickButton.clicked.connect(self.PickRequested)
        self.ClearButton.clicked.connect(self.Clear)
        layout.addWidget(self.Display, 1)
        layout.addWidget(self.PickButton)
        layout.addWidget(self.ClearButton)

    def GetValue(self):
        return self._value

    def SetValue(self, value: object, display_name: str | None = None) -> None:
        self._value = value
        self.Display.setText(display_name if display_name is not None else str(value or ""))
        self.ClearButton.setEnabled(value is not None)
        self.ValueChanged.emit(value)

    def Clear(self) -> None:
        if self._value is None:
            return
        self.SetValue(None, "")
        self.Cleared.emit()

    def dragEnterEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        if self._accepted_mime and event.mimeData().hasFormat(self._accepted_mime): event.acceptProposedAction()
        else: event.ignore()

    def dropEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        value = bytes(event.mimeData().data(self._accepted_mime)).decode() if self._accepted_mime else ""
        if value and (self._validator is None or self._validator(value)):
            self.SetValue(value); event.acceptProposedAction()
        else: event.ignore()


class ObjectPickerInput(PickerInput):
    def __init__(self, placeholder: str = "", parent=None, validator=None) -> None:
        super().__init__(placeholder, parent, "application/x-bazzalt-entity", validator)


class AssetPickerInput(PickerInput):
    def __init__(self, placeholder: str = "", parent=None, validator=None) -> None:
        super().__init__(placeholder, parent, "application/x-bazzalt-asset", validator)


class ColorInput(FieldStateSupport, QPushButton):
    ValueChanged = Signal(QColor)

    def __init__(self, color: QColor | str = QColor("white"),
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._color = QColor(color)
        self.clicked.connect(self._Pick)
        self._UpdateSwatch()

    def GetValue(self) -> QColor:
        return QColor(self._color)

    def SetValue(self, color: QColor | str) -> None:
        candidate = QColor(color)
        if not candidate.isValid():
            raise ValueError("invalid color")
        self._color = candidate
        self._UpdateSwatch()
        self.ValueChanged.emit(self.GetValue())

    def _Pick(self) -> None:
        dialog=QColorDialog(self._color,self);dialog.setOption(QColorDialog.ColorDialogOption.DontUseNativeDialog)
        if dialog.exec():self.SetValue(dialog.selectedColor())

    def _UpdateSwatch(self) -> None:
        self.setText(self._color.name(QColor.NameFormat.HexArgb))
        self.update()

    def paintEvent(self,event:QPaintEvent)->None:
        super().paintEvent(event);painter=QPainter(self);swatch=self.rect().adjusted(5,5,-5,-5)
        swatch.setWidth(min(18,swatch.width()));painter.setPen(Qt.PenStyle.NoPen);painter.setBrush(self._color)
        painter.drawRoundedRect(swatch,2,2);painter.end()


__all__ = [
    "AssetPickerInput", "BoolInput", "ColorInput", "EnumInput", "FieldState",
    "FieldStateSupport", "FieldWidget", "FloatInput", "IntInput", "MultiSelectInput", "ObjectPickerInput",
    "PickerInput", "RangeInput", "StringInput", "Vec2Input", "Vec3Input", "Vec4Input",
    "VectorInput",
]
