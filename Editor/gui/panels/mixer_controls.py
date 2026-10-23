"""Mixer controls adapted from the project\'s testamixer.py. Asset ownership is in audio_mixer.py."""

from __future__ import annotations

import bisect
import copy
import math
import time
from dataclasses import dataclass, field
from typing import Iterable

from PySide6.QtCore import QPoint, QPointF, QRectF, QSize, Qt, QTimer, Signal, QObject
from PySide6.QtGui import (
    QBrush,
    QColor,
    QFont,
    QFontMetrics,
    QIcon,
    QLinearGradient,
    QPainter,
    QPainterPath,
    QPalette,
    QPen,
    QPixmap,
)
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QCheckBox,
    QColorDialog,
    QDoubleSpinBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QScrollArea,
    QSizePolicy,
    QSlider,
    QSplitter,
    QStyle,
    QStyledItemDelegate,
    QStyleOptionViewItem,
    QToolButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

# --------------------------------------------------------------------------------------
# Shared editor theme and menu integration.
# --------------------------------------------------------------------------------------
from ...theme import Theme
from ..widgets.menu_bar import EditorMenu

class _MenuTheme(QObject):
    ThemeChanged=Signal(object)
    def GetTheme(self):
        node=self.parent()
        while node is not None:
            if hasattr(node,'GetTheme'):return node.GetTheme()
            if hasattr(node,'ThemeManager'):return node.ThemeManager.GetTheme()
            node=node.parent()
        return Theme.dark()

class QMenu(EditorMenu):
    """Use the editor's shared menu painting and application stylesheet."""
    def __init__(self,parent=None):
        source=_MenuTheme(parent);super().__init__('',source,parent);self._source=source
        self.aboutToShow.connect(self._Translate)
    def _Translate(self):
        owner=self.parent()
        while owner is not None and not hasattr(owner,'Translate'):owner=owner.parent()
        if owner is not None:
            for action in self.actions():
                source=action.property('mixer_source') or action.text();action.setProperty('mixer_source',source);action.setText(owner.Translate(source))

# --------------------------------------------------------------------------------------
# Constants and small helpers
# --------------------------------------------------------------------------------------
MASTER = "Master"
MIN_DB = -80.0
MAX_DB = 20.0
TRACK_MARGIN = 9  # vertical padding shared by fader and meter so their scales line up

ROLE_NAME = int(Qt.ItemDataRole.UserRole) + 1
ROLE_VISIBLE = ROLE_NAME + 1
ROLE_LOCKED = ROLE_NAME + 2
ROLE_FAVORITE = ROLE_NAME + 3
ROLE_ACTIVE = ROLE_NAME + 4

ICON_SIZE = 12.0
ICON_GAP = 6.0
ICON_PAD = 6.0

_GROUP_COLORS = (
    "#4f8a8b",
    "#c9a227",
    "#6a9f58",
    "#b5651d",
    "#7b68a6",
    "#3f7fbf",
    "#b0546e",
)
_MASTER_COLOR = "#5f7f9f"

# Fader law: (dB, normalised position). Piece-wise linear => finer resolution around 0 dB,
# coarse at the bottom, like a real console / Unity's mixer.
_FADER_CURVE = (
    (-80.0, 0.00),
    (-60.0, 0.15),
    (-40.0, 0.32),
    (-30.0, 0.42),
    (-20.0, 0.54),
    (-10.0, 0.68),
    (0.0, 0.82),
    (10.0, 0.92),
    (20.0, 1.00),
)
_CURVE_DB = [p[0] for p in _FADER_CURVE]
_CURVE_POS = [p[1] for p in _FADER_CURVE]
_MAJOR_TICKS = (20, 10, 0, -10, -20, -40, -60, -80)
_MINOR_TICKS = (15, 5, -5, -15, -30, -50, -70)


def _Clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def _Interp(xs: list[float], ys: list[float], x: float) -> float:
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    i = bisect.bisect_right(xs, x)
    x0, x1, y0, y1 = xs[i - 1], xs[i], ys[i - 1], ys[i]
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def DbToPosition(db: float) -> float:
    """Map decibels to a 0..1 fader position using the non-linear fader law."""
    return _Interp(_CURVE_DB, _CURVE_POS, db)


def PositionToDb(position: float) -> float:
    """Inverse of :func:`DbToPosition`."""
    return _Interp(_CURVE_POS, _CURVE_DB, _Clamp(position, 0.0, 1.0))


def DbToLinear(db: float) -> float:
    """Amplitude multiplier for a gain in dB (-80 dB and below is silence)."""
    return 0.0 if db <= MIN_DB else 10.0 ** (db / 20.0)


def LinearToDb(linear: float) -> float:
    return MIN_DB if linear <= 0.0 else max(MIN_DB, 20.0 * math.log10(linear))


def _Alpha(color: str | QColor, alpha: int) -> QColor:
    c = QColor(color)
    c.setAlpha(alpha)
    return c


def _Contrast(color: QColor) -> QColor:
    return QColor("#1b1b1b") if color.lightness() > 125 else QColor("#f2f2f2")


def _UniqueName(base: str, existing: Iterable[str]) -> str:
    taken = set(existing)
    if base not in taken:
        return base
    i = 1
    while f"{base} {i}" in taken:
        i += 1
    return f"{base} {i}"


def _ColorIcon(color: str, size: int = 10) -> QIcon:
    pm = QPixmap(size, size)
    pm.fill(Qt.GlobalColor.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor(color))
    p.drawRoundedRect(0, 0, size, size, 2, 2)
    p.end()
    return QIcon(pm)


def _ClearLayout(layout) -> None:
    while layout.count():
        item = layout.takeAt(0)
        widget = item.widget()
        if widget is not None:
            widget.hide()
            widget.deleteLater()


# --------------------------------------------------------------------------------------
# Effect library
# --------------------------------------------------------------------------------------
@dataclass(frozen=True)
class EffectParam:
    """Description of one effect parameter (drives the inspector UI)."""

    name: str
    default: float
    minimum: float
    maximum: float
    suffix: str = ""
    decimals: int = 2
    log: bool = False  # logarithmic slider (frequencies)


EFFECT_LIBRARY: dict[str, tuple[EffectParam, ...]] = {
    "Lowpass": (
        EffectParam("Cutoff Freq", 5000.0, 10.0, 22000.0, " Hz", 0, True),
        EffectParam("Resonance", 1.0, 1.0, 10.0),
    ),
    "Highpass": (
        EffectParam("Cutoff Freq", 10.0, 10.0, 22000.0, " Hz", 0, True),
        EffectParam("Resonance", 1.0, 1.0, 10.0),
    ),
    "Echo": (
        EffectParam("Delay", 500.0, 10.0, 5000.0, " ms", 0),
        EffectParam("Decay Ratio", 0.5, 0.0, 1.0),
        EffectParam("Dry Mix", 1.0, 0.0, 1.0),
        EffectParam("Wet Mix", 1.0, 0.0, 1.0),
    ),
    "Distortion": (EffectParam("Level", 0.5, 0.0, 1.0),),
    "Reverb": (
        EffectParam("Dry Level", 0.0, -80.0, 0.0, " dB", 1),
        EffectParam("Room", -10.0, -80.0, 0.0, " dB", 1),
        EffectParam("Decay Time", 1.0, 0.1, 20.0, " s", 2),
        EffectParam("Density", 100.0, 0.0, 100.0, " %", 0),
    ),
    "Equalizer": (
        EffectParam("Center Freq", 8000.0, 20.0, 22000.0, " Hz", 0, True),
        EffectParam("Octave Range", 1.0, 0.2, 5.0),
        EffectParam("Frequency Gain", 1.0, 0.05, 3.0),
    ),
}
_EFFECT_ALIASES = {
    "eq": "Equalizer",
    "parameq": "Equalizer",
    "paramEQ".lower(): "Equalizer",
}


# --------------------------------------------------------------------------------------
# Data model (plain dataclasses; the widget classes only render this state)
# --------------------------------------------------------------------------------------
@dataclass
class _Effect:
    id: str
    type: str
    enabled: bool = True
    params: dict[str, float] = field(default_factory=dict)


@dataclass
class _Group:
    name: str
    parent: str | None
    color: str
    volume: float = 0.0
    muted: bool = False
    soloed: bool = False
    bypass: bool = False
    visible: bool = True
    locked: bool = False
    expanded: bool = True
    effects: list[_Effect] = field(default_factory=list)
    children: list[str] = field(default_factory=list)


@dataclass
class _Snapshot:
    name: str
    favorite: bool = False
    data: dict = field(
        default_factory=dict
    )  # group name -> mix state (see _CaptureGroup)


@dataclass
class _View:
    name: str
    hidden: set[str] = field(default_factory=set)


@dataclass
class _Mixer:
    name: str
    groups: dict[str, _Group] = field(default_factory=dict)
    snapshots: dict[str, _Snapshot] = field(default_factory=dict)
    views: dict[str, _View] = field(default_factory=dict)
    active_snapshot: str = "Snapshot"
    active_view: str = "View"
    selected: str = MASTER
    fx_counter: int = 0
    color_counter: int = 0


def _CaptureGroup(g: _Group) -> dict:
    """Snapshot payload for one group. Solo is a session state and is not stored."""
    return {
        "volume": g.volume,
        "muted": g.muted,
        "bypass": g.bypass,
        "effects": {
            e.id: {"enabled": e.enabled, "params": dict(e.params)} for e in g.effects
        },
    }


def _CaptureAll(m: _Mixer) -> dict:
    return {name: _CaptureGroup(g) for name, g in m.groups.items()}


def _NewMixerState(name: str) -> _Mixer:
    m = _Mixer(name)
    master = _Group(MASTER, None, _MASTER_COLOR)
    m.groups[MASTER] = master
    m.snapshots["Snapshot"] = _Snapshot("Snapshot", data=_CaptureAll(m))
    m.views["View"] = _View("View")
    return m


def _MixerToDict(m: _Mixer) -> dict:
    return {
        "Name": m.name,
        "ActiveSnapshot": m.active_snapshot,
        "ActiveView": m.active_view,
        "EffectCounter": m.fx_counter,
        "ColorCounter": m.color_counter,
        "Groups": [
            {
                "Name": g.name,
                "Parent": g.parent,
                "Color": g.color,
                "Volume": g.volume,
                "Muted": g.muted,
                "Soloed": g.soloed,
                "Bypass": g.bypass,
                "Visible": g.visible,
                "Locked": g.locked,
                "Expanded": g.expanded,
                "Children": list(g.children),
                "Effects": [
                    {
                        "Id": e.id,
                        "Type": e.type,
                        "Enabled": e.enabled,
                        "Parameters": dict(e.params),
                    }
                    for e in g.effects
                ],
            }
            for g in m.groups.values()
        ],
        "Snapshots": [
            {"Name": s.name, "Favorite": s.favorite, "Data": copy.deepcopy(s.data)}
            for s in m.snapshots.values()
        ],
        "Views": [
            {"Name": v.name, "Hidden": sorted(v.hidden)} for v in m.views.values()
        ],
    }


def _MixerFromDict(d: dict) -> _Mixer:
    m = _Mixer(str(d["Name"]))
    for gd in d["Groups"]:
        g = _Group(
            name=gd["Name"],
            parent=gd.get("Parent"),
            color=gd.get("Color", "#808080"),
            volume=float(gd.get("Volume", 0.0)),
            muted=bool(gd.get("Muted", False)),
            soloed=bool(gd.get("Soloed", False)),
            bypass=bool(gd.get("Bypass", False)),
            visible=bool(gd.get("Visible", True)),
            locked=bool(gd.get("Locked", False)),
            expanded=bool(gd.get("Expanded", True)),
            children=list(gd.get("Children", [])),
        )
        g.effects = [
            _Effect(
                e["Id"],
                e["Type"],
                bool(e.get("Enabled", True)),
                {k: float(v) for k, v in e.get("Parameters", {}).items()},
            )
            for e in gd.get("Effects", [])
        ]
        m.groups[g.name] = g
    if MASTER not in m.groups:
        raise ValueError("Mixer data does not contain a Master group")
    for sd in d.get("Snapshots", []):
        m.snapshots[sd["Name"]] = _Snapshot(
            sd["Name"],
            bool(sd.get("Favorite", False)),
            copy.deepcopy(sd.get("Data", {})),
        )
    if not m.snapshots:
        m.snapshots["Snapshot"] = _Snapshot("Snapshot", data=_CaptureAll(m))
    for vd in d.get("Views", []):
        m.views[vd["Name"]] = _View(vd["Name"], set(vd.get("Hidden", [])))
    if not m.views:
        m.views["View"] = _View("View")
    m.active_snapshot = d.get("ActiveSnapshot", next(iter(m.snapshots)))
    if m.active_snapshot not in m.snapshots:
        m.active_snapshot = next(iter(m.snapshots))
    m.active_view = d.get("ActiveView", next(iter(m.views)))
    if m.active_view not in m.views:
        m.active_view = next(iter(m.views))
    m.fx_counter = int(d.get("EffectCounter", 0))
    m.color_counter = int(d.get("ColorCounter", 0))
    return m


# --------------------------------------------------------------------------------------
# Style sheet (tokens come from theme.py; object names match the ones in BuildStyleSheet)
# --------------------------------------------------------------------------------------
# --------------------------------------------------------------------------------------
# Tiny vector icons (no resource files needed)
# --------------------------------------------------------------------------------------
class _Icons:
    @staticmethod
    def DrawStar(p: QPainter, rect: QRectF, filled: bool, color: QColor) -> None:
        c = rect.center()
        r_out = min(rect.width(), rect.height()) / 2
        r_in = r_out * 0.45
        path = QPainterPath()
        for i in range(10):
            angle = math.pi / 2 + i * math.pi / 5
            r = r_out if i % 2 == 0 else r_in
            pt = QPointF(c.x() + r * math.cos(angle), c.y() - r * math.sin(angle))
            path.moveTo(pt) if i == 0 else path.lineTo(pt)
        path.closeSubpath()
        p.setPen(QPen(color, 1.1))
        p.setBrush(color if filled else Qt.BrushStyle.NoBrush)
        p.drawPath(path)

    @staticmethod
    def DrawEye(p: QPainter, rect: QRectF, on: bool, color: QColor) -> None:
        c = rect.center()
        h = rect.height() * 0.55
        path = QPainterPath()
        left, right = QPointF(rect.left(), c.y()), QPointF(rect.right(), c.y())
        path.moveTo(left)
        path.quadTo(QPointF(c.x(), c.y() - h), right)
        path.quadTo(QPointF(c.x(), c.y() + h), left)
        p.setPen(QPen(color, 1.2))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawPath(path)
        if on:
            p.setBrush(color)
            p.drawEllipse(c, rect.width() * 0.16, rect.width() * 0.16)
        else:
            p.drawLine(
                QPointF(rect.left() + 1, rect.bottom() - 1),
                QPointF(rect.right() - 1, rect.top() + 1),
            )

    @staticmethod
    def DrawLock(p: QPainter, rect: QRectF, locked: bool, color: QColor) -> None:
        body = QRectF(
            rect.left() + 1, rect.center().y(), rect.width() - 2, rect.height() / 2
        )
        shackle = QRectF(
            rect.left() + rect.width() * 0.28,
            rect.top() + 0.5,
            rect.width() * 0.44,
            rect.height() - 1,
        )
        p.setPen(QPen(color, 1.2))
        p.setBrush(Qt.BrushStyle.NoBrush)
        (
            p.drawArc(shackle, 0, 180 * 16)
            if locked
            else p.drawArc(shackle, 60 * 16, 120 * 16)
        )
        p.setBrush(color if locked else Qt.BrushStyle.NoBrush)
        p.drawRoundedRect(body, 1.5, 1.5)


def _IconRects(rect) -> tuple[QRectF, QRectF]:
    """(eye, lock) rectangles right-aligned inside a hierarchy row."""
    cy = rect.center().y()
    lock = QRectF(
        rect.right() - ICON_PAD - ICON_SIZE, cy - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE
    )
    eye = QRectF(
        lock.left() - ICON_GAP - ICON_SIZE, cy - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE
    )
    return eye, lock


def _StarRect(rect) -> QRectF:
    cy = rect.center().y()
    return QRectF(
        rect.right() - ICON_PAD - ICON_SIZE, cy - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE
    )


# --------------------------------------------------------------------------------------
# Fader and meter
# --------------------------------------------------------------------------------------
class FaderWidget(QWidget):
    """Vertical dB fader with a scale on the left and the exact value printed on the handle."""

    ValueChanged = Signal(float)  # only emitted for user interaction

    def __init__(self, theme: Theme, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._theme = theme
        self._value = 0.0
        self._interactive = True
        self._dragging = False
        self._hover = False
        self._grab_offset = 0.0
        self.setFixedWidth(68)
        self.setMinimumHeight(110)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)
        self.setFocusPolicy(Qt.FocusPolicy.ClickFocus)
        self.setMouseTracking(True)
        self.setToolTip(
            "Drag to change volume\nDouble-click: reset to 0 dB\n"
            "Wheel: 1 dB (Shift 0.1, Ctrl 5)"
        )

    # ---- public API
    def SetValue(self, db: float) -> None:
        db = _Clamp(float(db), MIN_DB, MAX_DB)
        if db != self._value:
            self._value = db
            self.update()

    def GetValue(self) -> float:
        return self._value

    def SetInteractive(self, interactive: bool) -> None:
        self._interactive = interactive
        self.update()

    def SetTheme(self, theme: Theme) -> None:
        self._theme = theme
        self.update()

    # ---- geometry
    def _TrackRect(self) -> QRectF:
        return QRectF(
            0, TRACK_MARGIN, self.width(), max(1, self.height() - 2 * TRACK_MARGIN)
        )

    def _CenterX(self) -> float:
        return self.width() - 19.0

    def _ValueToY(self, db: float) -> float:
        tr = self._TrackRect()
        return tr.bottom() - DbToPosition(db) * tr.height()

    def _YToValue(self, y: float) -> float:
        tr = self._TrackRect()
        return PositionToDb((tr.bottom() - y) / tr.height())

    def _HandleRect(self) -> QRectF:
        return QRectF(self._CenterX() - 16, self._ValueToY(self._value) - 8, 32, 16)

    @staticmethod
    def _Format(db: float) -> str:
        if abs(db) < 0.05:
            return "0.0"
        return f"{db:+.1f}" if db > 0 else f"{db:.1f}"

    # ---- painting
    def paintEvent(self, event) -> None:  # noqa: N802
        t = self._theme
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        track, cx = self._TrackRect(), self._CenterX()
        font = QFont(self.font())
        font.setPixelSize(9)
        p.setFont(font)

        last_label_y = -1e9
        for db in sorted(_MAJOR_TICKS + _MINOR_TICKS, reverse=True):
            y = self._ValueToY(float(db))
            major = db in _MAJOR_TICKS
            p.setPen(
                QPen(
                    QColor(
                        t.text
                        if db == 0
                        else t.text_muted if major else t.text_disabled
                    ),
                    1,
                )
            )
            x1 = cx - 17
            x0 = x1 - (6 if major else 3)
            p.drawLine(QPointF(x0, y), QPointF(x1, y))
            if major and y - last_label_y >= 11:
                label = "0" if db == 0 else f"{db:+d}"
                p.drawText(
                    QRectF(0, y - 6, x0 - 3, 12),
                    Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                    label,
                )
                last_label_y = y

        groove = QRectF(cx - 2, track.top(), 4, track.height())
        p.setPen(QPen(_Alpha(t.text_disabled, 110), 1))
        p.setBrush(QColor(t.input_background))
        p.drawRoundedRect(groove, 2, 2)

        handle = self._HandleRect()
        accent = QColor(t.accent if self._interactive else t.text_disabled)
        fill = QRectF(
            groove.left(),
            handle.center().y(),
            groove.width(),
            groove.bottom() - handle.center().y(),
        )
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(_Alpha(accent, 170))
        p.drawRoundedRect(fill, 2, 2)

        body = QColor(accent)
        if self._interactive and (self._hover or self._dragging):
            body = body.lighter(115)
        p.setPen(QPen(QColor(t.background), 1))
        p.setBrush(body)
        p.drawRoundedRect(handle, 3, 3)
        if self.hasFocus():
            p.setPen(QPen(QColor(t.border_focus), 1.2))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(handle.adjusted(-1.5, -1.5, 1.5, 1.5), 4, 4)
        font.setBold(True)
        p.setFont(font)
        p.setPen(_Contrast(body))
        p.drawText(handle, Qt.AlignmentFlag.AlignCenter, self._Format(self._value))

    # ---- interaction
    def _Commit(self, db: float) -> None:
        db = _Clamp(db, MIN_DB, MAX_DB)
        if abs(db - self._value) > 1e-6:
            self._value = db
            self.update()
            self.ValueChanged.emit(db)

    def _SetFromY(self, y: float, modifiers) -> None:
        db = round(self._YToValue(y), 1)
        if abs(db) < 0.4 and not (modifiers & Qt.KeyboardModifier.ControlModifier):
            db = 0.0  # soft detent at unity gain
        self._Commit(db)

    def mousePressEvent(self, e) -> None:  # noqa: N802
        if e.button() != Qt.MouseButton.LeftButton or not self._interactive:
            return super().mousePressEvent(e)
        handle = self._HandleRect()
        if handle.contains(e.position()):
            self._grab_offset = e.position().y() - handle.center().y()
        else:
            self._grab_offset = 0.0
            self._SetFromY(e.position().y(), e.modifiers())
        self._dragging = True
        self.setFocus()
        e.accept()

    def mouseMoveEvent(self, e) -> None:  # noqa: N802
        hover = self._HandleRect().contains(e.position())
        if hover != self._hover:
            self._hover = hover
            self.update()
        if self._dragging:
            self._SetFromY(e.position().y() - self._grab_offset, e.modifiers())

    def mouseReleaseEvent(self, e) -> None:  # noqa: N802
        self._dragging = False
        self.update()

    def mouseDoubleClickEvent(self, e) -> None:  # noqa: N802
        if self._interactive and e.button() == Qt.MouseButton.LeftButton:
            self._Commit(0.0)

    def leaveEvent(self, e) -> None:  # noqa: N802
        self._hover = False
        self.update()

    def wheelEvent(self, e) -> None:  # noqa: N802
        if not self._interactive or not self.hasFocus():
            return e.ignore()
        mods = e.modifiers()
        step = (
            0.1
            if mods & Qt.KeyboardModifier.ShiftModifier
            else 5.0 if mods & Qt.KeyboardModifier.ControlModifier else 1.0
        )
        self._Commit(round(self._value + (e.angleDelta().y() / 120.0) * step, 1))
        e.accept()

    def keyPressEvent(self, e) -> None:  # noqa: N802
        if not self._interactive:
            return super().keyPressEvent(e)
        steps = {
            Qt.Key.Key_Up: 1.0,
            Qt.Key.Key_Down: -1.0,
            Qt.Key.Key_PageUp: 5.0,
            Qt.Key.Key_PageDown: -5.0,
        }
        if e.key() in steps:
            self._Commit(round(self._value + steps[e.key()], 1))
        elif e.key() == Qt.Key.Key_Home:
            self._Commit(MAX_DB)
        elif e.key() == Qt.Key.Key_End:
            self._Commit(MIN_DB)
        else:
            super().keyPressEvent(e)


class MeterWidget(QWidget):
    """Stereo peak meter with instant attack, smooth release and peak-hold markers."""

    _FALL_DB_PER_S = 45.0
    _PEAK_HOLD_S = 1.2
    _PEAK_FALL_DB_PER_S = 25.0

    def __init__(self, theme: Theme, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._theme = theme
        self._target = [MIN_DB, MIN_DB]
        self._level = [MIN_DB, MIN_DB]
        self._peak = [MIN_DB, MIN_DB]
        self._age = [0.0, 0.0]
        self.setFixedWidth(22)
        self.setMinimumHeight(150)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)

    def SetLevels(self, left_db: float, right_db: float) -> None:
        self._target = [
            _Clamp(left_db, MIN_DB, MAX_DB),
            _Clamp(right_db, MIN_DB, MAX_DB),
        ]

    def Tick(self, dt: float) -> bool:
        changed = False
        for i in range(2):
            old_level, old_peak = self._level[i], self._peak[i]
            target, level = self._target[i], self._level[i]
            level = (
                target
                if target >= level
                else max(target, level - self._FALL_DB_PER_S * dt)
            )
            if level >= self._peak[i]:
                self._peak[i], self._age[i] = level, 0.0
            else:
                self._age[i] += dt
                if self._age[i] > self._PEAK_HOLD_S:
                    self._peak[i] = max(
                        level, self._peak[i] - self._PEAK_FALL_DB_PER_S * dt
                    )
            self._level[i] = level
            changed |= (
                abs(level - old_level) > 0.01 or abs(self._peak[i] - old_peak) > 0.01
            )
        if changed:
            self.update()
        return changed

    def SetTheme(self, theme: Theme) -> None:
        self._theme = theme
        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        t = self._theme
        p = QPainter(self)
        tr = QRectF(
            0, TRACK_MARGIN, self.width(), max(1, self.height() - 2 * TRACK_MARGIN)
        )
        bar_w, gap = 8.0, 3.0
        x0 = (self.width() - (2 * bar_w + gap)) / 2
        grad = QLinearGradient(0, tr.bottom(), 0, tr.top())
        grad.setColorAt(0.0, QColor(t.success))
        grad.setColorAt(DbToPosition(-20.0), QColor(t.success))
        grad.setColorAt(DbToPosition(-6.0), QColor(t.warning))
        grad.setColorAt(DbToPosition(0.0), QColor(t.error))
        grad.setColorAt(1.0, QColor(t.error))
        brush = QBrush(grad)
        for ch in range(2):
            x = x0 + ch * (bar_w + gap)
            p.fillRect(
                QRectF(x, tr.top(), bar_w, tr.height()), QColor(t.input_background)
            )
            y_level = tr.bottom() - DbToPosition(self._level[ch]) * tr.height()
            if self._level[ch] > MIN_DB + 0.5:
                p.fillRect(QRectF(x, y_level, bar_w, tr.bottom() - y_level), brush)
            if self._peak[ch] > MIN_DB + 1.0:
                y_peak = tr.bottom() - DbToPosition(self._peak[ch]) * tr.height()
                p.setPen(QPen(QColor(t.error if self._peak[ch] > 0 else t.text), 1.5))
                p.drawLine(QPointF(x, y_peak), QPointF(x + bar_w, y_peak))


# --------------------------------------------------------------------------------------
# Small building blocks
# --------------------------------------------------------------------------------------
class _Dot(QWidget):
    """Round bypass indicator used in the effects rack."""

    Clicked = Signal()

    def __init__(self, theme: Theme, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._theme = theme
        self._on = True
        self._toggleable = True
        self.setFixedSize(14, 14)

    def SetState(self, on: bool, toggleable: bool) -> None:
        self._on, self._toggleable = on, toggleable
        self.setCursor(
            Qt.CursorShape.PointingHandCursor
            if toggleable
            else Qt.CursorShape.ArrowCursor
        )
        self.setToolTip("Click to bypass / enable" if toggleable else "")
        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        t = self._theme
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        c = QPointF(self.width() / 2, self.height() / 2)
        if self._on:
            p.setPen(QPen(QColor(t.success).darker(140), 1))
            p.setBrush(
                QColor(t.success) if self._toggleable else _Alpha(t.success, 120)
            )
        else:
            p.setPen(QPen(QColor(t.text_disabled), 1.2))
            p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawEllipse(c, 4.0, 4.0)

    def mousePressEvent(self, e) -> None:  # noqa: N802
        if self._toggleable and e.button() == Qt.MouseButton.LeftButton:
            self.Clicked.emit()
            e.accept()
        else:
            e.ignore()


class _HeaderFrame(QFrame):
    DoubleClicked = Signal()

    def mouseDoubleClickEvent(self, e) -> None:  # noqa: N802
        self.DoubleClicked.emit()


class _Section(QFrame):
    """Collapsible sidebar section with a title and a ``+`` button."""

    AddClicked = Signal()

    def __init__(
        self, title: str, grow: bool = False, parent: QWidget | None = None
    ) -> None:
        super().__init__(parent)
        self.setObjectName("MixerSection")
        self._grow, self._expanded, self._body = grow, True, None
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)
        header = QFrame(self)
        header.setObjectName("MixerSectionHeader")
        header.setFixedHeight(24)
        row = QHBoxLayout(header)
        row.setContentsMargins(4, 0, 4, 0)
        row.setSpacing(4)
        self._arrow = QToolButton(header)
        self._arrow.setObjectName("MixerSectionArrow")
        self._arrow.setText("▾")
        self._arrow.clicked.connect(lambda: self.SetExpanded(not self._expanded))
        label = QLabel(title, header)
        label.setObjectName("MixerSectionTitle")
        self.AddButton = QToolButton(header)
        self.AddButton.setObjectName("MixerSectionAdd")
        self.AddButton.setText("+")
        self.AddButton.setToolTip(f"Add to {title}")
        self.AddButton.clicked.connect(self.AddClicked)
        row.addWidget(self._arrow)
        row.addWidget(label, 1)
        row.addWidget(self.AddButton)
        root.addWidget(header)
        self._UpdatePolicy()

    def SetBody(self, widget: QWidget) -> None:
        self._body = widget
        self.layout().addWidget(widget)

    def SetExpanded(self, expanded: bool) -> None:
        self._expanded = expanded
        self._arrow.setText("▾" if expanded else "▸")
        if self._body is not None:
            self._body.setVisible(expanded)
        self._UpdatePolicy()

    def _UpdatePolicy(self) -> None:
        vertical = (
            QSizePolicy.Policy.Expanding
            if (self._grow and self._expanded)
            else QSizePolicy.Policy.Maximum
        )
        self.setSizePolicy(QSizePolicy.Policy.Preferred, vertical)


# --------------------------------------------------------------------------------------
# Sidebar views and delegates
# --------------------------------------------------------------------------------------
class _BaseDelegate(QStyledItemDelegate):
    def __init__(self, theme: Theme, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._theme = theme

    def SetTheme(self, theme: Theme) -> None:
        self._theme = theme

    def _PaintBase(self, painter: QPainter, option, index):
        """Draw background/icon via the style, return (opt, text, text_left)."""
        opt = QStyleOptionViewItem(option)
        self.initStyleOption(opt, index)
        text, opt.text = opt.text, ""
        widget = opt.widget
        style = widget.style() if widget is not None else QApplication.style()
        style.drawControl(QStyle.ControlElement.CE_ItemViewItem, opt, painter, widget)
        deco = style.subElementRect(
            QStyle.SubElement.SE_ItemViewItemDecoration, opt, widget
        )
        text_left = (
            deco.right() + 6
            if (deco.isValid() and deco.width() > 0)
            else opt.rect.left() + 6
        )
        return opt, text, text_left


class _GroupDelegate(_BaseDelegate):
    """Hierarchy row: colour swatch (icon), name, then visibility + lock toggles on the right."""

    def paint(self, painter, option, index) -> None:  # noqa: N802
        t = self._theme
        opt, text, text_left = self._PaintBase(painter, option, index)
        visible, locked = bool(index.data(ROLE_VISIBLE)), bool(index.data(ROLE_LOCKED))
        eye, lock = _IconRects(opt.rect)
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setPen(QColor(t.text if visible else t.text_muted))
        right = eye.left() - 6
        elided = QFontMetrics(opt.font).elidedText(
            text, Qt.TextElideMode.ElideRight, int(right - text_left)
        )
        painter.setFont(opt.font)
        painter.drawText(
            QRectF(text_left, opt.rect.top(), right - text_left, opt.rect.height()),
            Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
            elided,
        )
        _Icons.DrawEye(
            painter, eye, visible, QColor(t.text if visible else t.text_disabled)
        )
        _Icons.DrawLock(
            painter, lock, locked, QColor(t.warning if locked else t.text_muted)
        )
        painter.restore()


class _SnapshotDelegate(_BaseDelegate):
    """Snapshot row: active radio dot, name, favourite star."""

    def paint(self, painter, option, index) -> None:  # noqa: N802
        t = self._theme
        opt, text, _ = self._PaintBase(painter, option, index)
        active, favorite = bool(index.data(ROLE_ACTIVE)), bool(
            index.data(ROLE_FAVORITE)
        )
        star = _StarRect(opt.rect)
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        centre = QPointF(opt.rect.left() + 11, opt.rect.center().y())
        painter.setPen(QPen(QColor(t.border_focus if active else t.text_disabled), 1.2))
        painter.setBrush(QColor(t.border_focus) if active else Qt.BrushStyle.NoBrush)
        painter.drawEllipse(centre, 4.0, 4.0)
        font = QFont(opt.font)
        font.setBold(active)
        painter.setFont(font)
        painter.setPen(QColor(t.text))
        left = opt.rect.left() + 24
        elided = QFontMetrics(font).elidedText(
            text, Qt.TextElideMode.ElideRight, int(star.left() - 6 - left)
        )
        painter.drawText(
            QRectF(left, opt.rect.top(), star.left() - 6 - left, opt.rect.height()),
            Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
            elided,
        )
        _Icons.DrawStar(
            painter, star, favorite, QColor(t.warning if favorite else t.text_disabled)
        )
        painter.restore()


class _GroupTree(QTreeWidget):
    IconClicked = Signal(str, str)  # (group, "visible" | "locked")
    ReparentRequested = Signal(str, str)  # (group, new parent)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("MixerGroupTree")
        self.setHeaderHidden(True)
        self.setIndentation(14)
        self.setIconSize(QSize(10, 10))
        self.setUniformRowHeights(True)
        self.setExpandsOnDoubleClick(False)
        self.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDropIndicatorShown(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)

    def mousePressEvent(self, e) -> None:  # noqa: N802
        if e.button() == Qt.MouseButton.LeftButton:
            index = self.indexAt(e.position().toPoint())
            if index.isValid():
                eye, lock = _IconRects(self.visualRect(index))
                name = index.data(ROLE_NAME)
                if eye.contains(e.position()):
                    self.IconClicked.emit(name, "visible")
                    return
                if lock.contains(e.position()):
                    self.IconClicked.emit(name, "locked")
                    return
        super().mousePressEvent(e)

    def startDrag(self, actions) -> None:  # noqa: N802
        item = self.currentItem()
        if item is not None and item.data(0, ROLE_NAME) == MASTER:
            return  # the master bus cannot be re-parented
        super().startDrag(actions)

    def dropEvent(self, e) -> None:  # noqa: N802
        # The model owns the hierarchy: report the drop and let the mixer rebuild the tree.
        src = self.currentItem()
        dst = self.itemAt(e.position().toPoint())
        e.ignore()
        if src is not None and dst is not None and src is not dst:
            self.ReparentRequested.emit(src.data(0, ROLE_NAME), dst.data(0, ROLE_NAME))


class _SnapshotList(QListWidget):
    StarClicked = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("MixerSnapshotList")
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)

    def mousePressEvent(self, e) -> None:  # noqa: N802
        if e.button() == Qt.MouseButton.LeftButton:
            index = self.indexAt(e.position().toPoint())
            if index.isValid() and _StarRect(self.visualRect(index)).adjusted(
                -3, -3, 3, 3
            ).contains(e.position()):
                self.StarClicked.emit(index.data(ROLE_NAME))
                return
        super().mousePressEvent(e)


# --------------------------------------------------------------------------------------
# Effects rack
# --------------------------------------------------------------------------------------
class _EffectRow(QFrame):
    """One row in a strip's rack. ``effect is None`` renders the fixed Attenuation stage."""

    def __init__(
        self,
        mixer: "AudioMixer",
        group: str,
        effect: _Effect | None,
        group_bypassed: bool,
        theme: Theme,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("MixerEffectRow")
        self.setMinimumHeight(22)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self._mixer, self._group, self._effect = mixer, group, effect
        row = QHBoxLayout(self)
        row.setContentsMargins(5, 0, 5, 0)
        row.setSpacing(5)
        self._dot = _Dot(theme, self)
        self._dot.SetState(
            True if effect is None else effect.enabled, effect is not None
        )
        self._dot.Clicked.connect(self._ToggleEnabled)
        label = QLabel("Attenuation" if effect is None else effect.type, self)
        label.setToolTip("Click to edit parameters")
        label.setEnabled(effect is None or (effect.enabled and not group_bypassed))
        row.addWidget(self._dot)
        row.addWidget(label, 1)

    def _EffectId(self) -> str:
        return "" if self._effect is None else self._effect.id

    def _ToggleEnabled(self) -> None:
        if self._effect is not None:
            self._mixer.SetEffectEnabled(
                self._group, self._effect.id, not self._effect.enabled
            )

    def mousePressEvent(self, e) -> None:  # noqa: N802
        if e.button() == Qt.MouseButton.LeftButton:
            e.accept()
        else:
            super().mousePressEvent(e)

    def mouseReleaseEvent(self, e) -> None:  # noqa: N802
        if e.button() == Qt.MouseButton.LeftButton and self.rect().contains(
            e.position().toPoint()
        ):
            self._mixer._ShowEffectInspector(self, self._group, self._EffectId())

    def contextMenuEvent(self, e) -> None:  # noqa: N802
        menu = QMenu(self)
        menu.addAction(
            "Edit Parameters",
            lambda: self._mixer._ShowEffectInspector(
                self, self._group, self._EffectId()
            ),
        )
        if self._effect is not None:
            eff, group, mixer = self._effect, self._group, self._mixer
            menu.addAction(
                "Enable" if not eff.enabled else "Bypass", self._ToggleEnabled
            )
            menu.addSeparator()
            index = [x.id for x in mixer._Group(group).effects].index(eff.id)
            menu.addAction(
                "Move Up", lambda: mixer.MoveEffect(group, eff.id, index - 1)
            ).setEnabled(index > 0)
            last = len(mixer._Group(group).effects) - 1
            menu.addAction(
                "Move Down", lambda: mixer.MoveEffect(group, eff.id, index + 1)
            ).setEnabled(index < last)
            menu.addSeparator()
            menu.addAction("Remove", lambda: mixer.RemoveEffect(group, eff.id))
        menu.exec(e.globalPos())
        e.accept()


class _EffectInspector(QFrame):
    """Popup with one slider + spin box per effect parameter."""

    def __init__(
        self, mixer: "AudioMixer", group: str, effect_id: str, theme: Theme
    ) -> None:
        super().__init__(mixer, Qt.WindowType.Popup)
        self.setObjectName("MixerEffectInspector")
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self._mixer, self._group, self._effect_id = mixer, group, effect_id
        g = mixer._Group(group)
        root = QVBoxLayout(self)
        root.setContentsMargins(10, 8, 10, 10)
        root.setSpacing(6)
        if effect_id:
            eff = mixer._Effect(group, effect_id)
            title, params, values = eff.type, mixer._effect_types[eff.type], eff.params
        else:
            title = "Attenuation"
            params = (EffectParam("Volume", 0.0, MIN_DB, MAX_DB, " dB", 1),)
            values = {"Volume": g.volume}
        head = QLabel(f"{title}  —  {group}")
        font = head.font()
        font.setBold(True)
        head.setFont(font)
        root.addWidget(head)
        if effect_id:
            enabled = QCheckBox("Enabled")
            enabled.setChecked(mixer._Effect(group, effect_id).enabled)
            enabled.clicked.connect(
                lambda c: mixer.SetEffectEnabled(group, effect_id, bool(c))
            )
            root.addWidget(enabled)
        grid = QGridLayout()
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(5)
        for row, param in enumerate(params):
            self._AddRow(grid, row, param, values.get(param.name, param.default))
        grid.setColumnStretch(1, 1)
        root.addLayout(grid)
        self.setMinimumWidth(300)

    @staticmethod
    def _ToSlider(p: EffectParam, v: float) -> int:
        if p.log:
            r = math.log(max(v, p.minimum) / p.minimum) / math.log(
                p.maximum / p.minimum
            )
        else:
            r = (v - p.minimum) / (p.maximum - p.minimum)
        return int(round(_Clamp(r, 0.0, 1.0) * 1000))

    @staticmethod
    def _FromSlider(p: EffectParam, s: int) -> float:
        r = s / 1000.0
        return (
            p.minimum * (p.maximum / p.minimum) ** r
            if p.log
            else p.minimum + r * (p.maximum - p.minimum)
        )

    def _Apply(self, p: EffectParam, value: float) -> None:
        if self._effect_id:
            self._mixer.SetEffectParameter(self._group, self._effect_id, p.name, value)
        else:
            self._mixer.SetVolume(self._group, value)

    def _AddRow(
        self, grid: QGridLayout, row: int, p: EffectParam, value: float
    ) -> None:
        slider = QSlider(Qt.Orientation.Horizontal)
        slider.setRange(0, 1000)
        slider.setValue(self._ToSlider(p, value))
        spin = QDoubleSpinBox()
        spin.setDecimals(p.decimals)
        spin.setRange(p.minimum, p.maximum)
        spin.setSuffix(p.suffix)
        spin.setValue(value)
        spin.setMinimumWidth(92)

        def from_slider(s: int) -> None:
            spin.blockSignals(True)
            spin.setValue(self._FromSlider(p, s))
            spin.blockSignals(False)
            self._Apply(p, spin.value())

        def from_spin(v: float) -> None:
            slider.blockSignals(True)
            slider.setValue(self._ToSlider(p, v))
            slider.blockSignals(False)
            self._Apply(p, v)

        slider.valueChanged.connect(from_slider)
        spin.valueChanged.connect(from_spin)
        grid.addWidget(QLabel(p.name), row, 0)
        grid.addWidget(slider, row, 1)
        grid.addWidget(spin, row, 2)


# --------------------------------------------------------------------------------------
# Channel strip
# --------------------------------------------------------------------------------------
class _ChannelStrip(QFrame):
    """One vertical mixer channel. Reads state from the mixer and calls its public API."""

    STRIP_WIDTH = 112

    def __init__(
        self,
        mixer: "AudioMixer",
        name: str,
        theme: Theme,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("AudioMixerChannel")
        self._mixer, self._name, self._theme = mixer, name, theme
        self.setFixedWidth(self.STRIP_WIDTH)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)

        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # Header: coloured bar with the name and the numeric dB readout
        self._header = _HeaderFrame(self)
        self._header.setObjectName("MixerStripHeader")
        hl = QVBoxLayout(self._header)
        hl.setContentsMargins(4, 4, 4, 4)
        hl.setSpacing(3)
        self._title = QLabel(self._header)
        self._title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        font = self._title.font()
        font.setBold(True)
        self._title.setFont(font)
        self._spin = QDoubleSpinBox(self._header)
        self._spin.setRange(MIN_DB, MAX_DB)
        self._spin.setDecimals(1)
        self._spin.setSuffix(" dB")
        self._spin.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._spin.setButtonSymbols(QDoubleSpinBox.ButtonSymbols.NoButtons)
        self._spin.setKeyboardTracking(False)
        self._spin.setToolTip("Type a value in dB")
        hl.addWidget(self._title)
        hl.addWidget(self._spin)
        root.addWidget(self._header)
        self._header.DoubleClicked.connect(lambda: mixer._PromptRename(name))
        self._header.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._header.customContextMenuRequested.connect(
            lambda pos: mixer._ShowGroupMenu(name, self._header.mapToGlobal(pos))
        )

        # Fader + VU meter
        row = QHBoxLayout()
        row.setSpacing(2)
        row.setContentsMargins(0, 0, 0, 0)
        self._fader = FaderWidget(theme, self)
        self.Meter = MeterWidget(theme, self)
        row.addWidget(self._fader)
        row.addWidget(self.Meter)
        root.addLayout(row, 1)

        # S / M / B switches
        switches = QHBoxLayout()
        switches.setSpacing(3)
        self._btn_solo = self._MakeSwitch("S", "solo", "Solo")
        self._btn_mute = self._MakeSwitch("M", "mute", "Mute")
        self._btn_bypass = self._MakeSwitch("B", "bypass", "Bypass effects")
        for b in (self._btn_solo, self._btn_mute, self._btn_bypass):
            switches.addWidget(b)
        root.addLayout(switches)

        # Effects rack
        self._fx_scroll = QScrollArea(self)
        self._fx_scroll.setObjectName("MixerEffectScroll")
        self._fx_scroll.setWidgetResizable(True)
        self._fx_scroll.setFrameShape(QFrame.Shape.NoFrame)
        self._fx_scroll.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )
        self._fx_scroll.setMinimumHeight(88)
        self._fx_scroll.setMaximumHeight(190)
        self._fx_scroll.viewport().setObjectName("MixerEffectViewport")
        host = QWidget()
        host.setObjectName("MixerEffectHost")
        self._fx_layout = QVBoxLayout(host)
        self._fx_layout.setContentsMargins(0, 0, 0, 0)
        self._fx_layout.setSpacing(2)
        self._fx_scroll.setWidget(host)
        root.addWidget(self._fx_scroll)

        # Add.. menu
        self._add = QToolButton(self)
        self._add.setObjectName("MixerAddEffect")
        self._add.setText("Add..")
        self._add.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
        self._add.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        self._add.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        menu = QMenu(self._add)
        menu.aboutToShow.connect(lambda m=menu: self._FillAddMenu(m))
        self._add.setMenu(menu)
        root.addWidget(self._add)

        self._fader.ValueChanged.connect(lambda v: mixer.SetVolume(name, v))
        self._spin.valueChanged.connect(lambda v: mixer.SetVolume(name, v))
        self._btn_solo.clicked.connect(lambda c: mixer.SetSolo(name, bool(c)))
        self._btn_mute.clicked.connect(lambda c: mixer.SetMute(name, bool(c)))
        self._btn_bypass.clicked.connect(lambda c: mixer.SetBypass(name, bool(c)))

    # ---- construction helpers
    def _MakeSwitch(self, text: str, kind: str, tooltip: str) -> QToolButton:
        b = QToolButton(self)
        b.setObjectName("MixerChannelSwitch")
        b.setText(text)
        b.setCheckable(True)
        b.setToolTip(tooltip)
        b.setProperty("switchKind", kind)
        b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        return b

    def _FillAddMenu(self, menu: QMenu) -> None:
        menu.clear()
        for effect_type in self._mixer.GetEffectTypes():
            action=menu.addAction(
                effect_type,
                lambda *_, t=effect_type: self._mixer.AddEffect(self._name, t),
            )
            action.setEnabled(len(self._G().effects)<16)

    def _G(self) -> _Group:
        return self._mixer._Group(self._name)

    # ---- refresh
    def Refresh(self) -> None:
        self.RefreshHeader()
        self.RefreshMix()
        self.RefreshEffects()

    def RefreshHeader(self) -> None:
        g = self._G()
        bg = QColor(g.color)
        fg = _Contrast(bg).name()
        self._header.setStyleSheet(
            f"QFrame#MixerStripHeader {{ background: {bg.name()}; border: 0; border-radius: 3px; }}"
        )
        self._title.setStyleSheet(f"color: {fg}; background: transparent;")
        self._title.setText(
            QFontMetrics(self._title.font()).elidedText(
                g.name, Qt.TextElideMode.ElideRight, self.STRIP_WIDTH - 24
            )
        )
        self._title.setToolTip(g.name)

    def RefreshMix(self) -> None:
        g = self._G()
        self._spin.blockSignals(True)
        self._spin.setValue(g.volume)
        self._spin.blockSignals(False)
        self._fader.SetValue(g.volume)
        self._btn_solo.setChecked(g.soloed)
        self._btn_mute.setChecked(g.muted)
        self._btn_bypass.setChecked(g.bypass)
        free = not g.locked
        self._fader.SetInteractive(free)
        for w in (
            self._spin,
            self._btn_solo,
            self._btn_mute,
            self._btn_bypass,
            self._add,
        ):
            w.setEnabled(free)

    def RefreshEffects(self) -> None:
        g = self._G()
        _ClearLayout(self._fx_layout)
        self._fx_layout.addWidget(
            _EffectRow(self._mixer, g.name, None, g.bypass, self._theme)
        )
        for effect in g.effects:
            self._fx_layout.addWidget(
                _EffectRow(self._mixer, g.name, effect, g.bypass, self._theme)
            )
        self._fx_layout.addStretch(1)

    def SetSelected(self, selected: bool) -> None:
        self.setProperty("stripSelected", selected)
        self.style().unpolish(self)
        self.style().polish(self)

    # ---- events
    def mousePressEvent(self, e) -> None:  # noqa: N802
        if e.button() == Qt.MouseButton.LeftButton:
            self._mixer.SelectGroup(self._name)
            e.accept()
        else:
            super().mousePressEvent(e)

    def contextMenuEvent(self, e) -> None:  # noqa: N802
        self._mixer._ShowGroupMenu(self._name, e.globalPos())
        e.accept()


# --------------------------------------------------------------------------------------
# The public widget
# --------------------------------------------------------------------------------------
class AudioMixer(QWidget):
    """Unity-style audio mixer panel.

    All public API is PascalCase. Programmatic changes and UI interaction share one code path,
    so every change emits the matching signal exactly once.
    """

    # Mixers
    AssetPickRequested = Signal()
    MixerCreated = Signal(str)
    MixerRemoved = Signal(str)
    MixerSwitched = Signal(str)
    # Groups
    GroupAdded = Signal(str)
    GroupRemoved = Signal(str)
    GroupRenamed = Signal(str, str)  # old, new
    GroupReparented = Signal(str, str)  # group, new parent
    GroupSelected = Signal(str)
    GroupColorChanged = Signal(str, str)
    GroupVisibilityChanged = Signal(str, bool)
    GroupLockChanged = Signal(str, bool)
    # Mix state
    VolumeChanged = Signal(str, float)
    MuteChanged = Signal(str, bool)
    SoloChanged = Signal(str, bool)
    BypassChanged = Signal(str, bool)
    # Effects
    EffectAdded = Signal(str, str, str)  # group, effect id, effect type
    EffectRemoved = Signal(str, str)
    EffectMoved = Signal(str, str, int)
    EffectEnabledChanged = Signal(str, str, bool)
    EffectParameterChanged = Signal(
        str, str, str, float
    )  # group, effect id, parameter, value
    # Snapshots and views
    SnapshotCreated = Signal(str)
    SnapshotRemoved = Signal(str)
    SnapshotRenamed = Signal(str, str)
    SnapshotSwitched = Signal(str)
    SnapshotFavoriteChanged = Signal(str, bool)
    ViewCreated = Signal(str)
    ViewRemoved = Signal(str)
    ViewRenamed = Signal(str, str)
    ViewSwitched = Signal(str)

    def __init__(
        self,
        parent: QWidget | None = None,
        theme: Theme | None = None,
        mixer_name: str = "Mixer",
    ) -> None:
        super().__init__(parent)
        self.setObjectName("AudioMixer")
        self._theme: Theme = theme or Theme.dark()
        self._effect_types: dict[str, tuple[EffectParam, ...]] = dict(EFFECT_LIBRARY)
        self._mixers: dict[str, _Mixer] = {}
        self._mixer: _Mixer = None  # type: ignore[assignment]
        self._strips: dict[str, _ChannelStrip] = {}
        self._tree_items: dict[str, QTreeWidgetItem] = {}
        self._inspector: _EffectInspector | None = None
        self._building = False
        self._last_tick = time.monotonic()

        self._BuildUi()
        
        self.setMinimumSize(180, 150)
        self.resize(1100, 620)

        self._meter_timer = QTimer(self)
        self._meter_timer.setInterval(33)
        self._meter_timer.timeout.connect(self._OnMeterTick)

        self.CreateMixer(mixer_name, activate=True)

    # ==================================================================================
    # Properties
    # ==================================================================================
    @property
    def ActiveMixer(self) -> str:
        return self._mixer.name

    @property
    def ActiveSnapshot(self) -> str:
        return self._mixer.active_snapshot

    @property
    def ActiveView(self) -> str:
        return self._mixer.active_view

    @property
    def SelectedGroup(self) -> str:
        return self._mixer.selected

    # ==================================================================================
    # Theme
    # ==================================================================================
    def GetTheme(self) -> Theme:
        return self._theme

    def SetTheme(self, theme: Theme) -> None:
        """Apply a Theme instance (tokens from theme.py) to the whole panel."""
        self._theme = theme
        
        self._group_delegate.SetTheme(theme)
        self._snapshot_delegate.SetTheme(theme)
        self._RebuildStrips()
        for view in (
            self._tree,
            self._snapshot_list,
            self._mixer_list,
            self._view_list,
        ):
            view.viewport().update()

    def BindThemeManager(self, manager) -> None:
        """Follow a ThemeManager: apply its theme now and on every ThemeChanged."""
        manager.ThemeChanged.connect(self.SetTheme)
        self.SetTheme(manager.GetTheme())

    # ==================================================================================
    # Mixers
    # ==================================================================================
    def CreateMixer(self, mixer_name: str, activate: bool = True) -> str:
        name = _UniqueName(mixer_name.strip() or "Mixer", self._mixers)
        self._mixers[name] = _NewMixerState(name)
        self.MixerCreated.emit(name)
        if activate:
            self.SwitchMixer(name)
        else:
            self._RefreshMixerList()
        return name

    def SwitchMixer(self, mixer_name: str) -> None:
        if mixer_name not in self._mixers:
            raise KeyError(f"Unknown mixer '{mixer_name}'")
        self._mixer = self._mixers[mixer_name]
        self._RebuildAll()
        self.MixerSwitched.emit(mixer_name)

    def RemoveMixer(self, mixer_name: str) -> None:
        if mixer_name not in self._mixers:
            raise KeyError(f"Unknown mixer '{mixer_name}'")
        if len(self._mixers) == 1:
            raise ValueError("The last mixer cannot be removed")
        del self._mixers[mixer_name]
        self.MixerRemoved.emit(mixer_name)
        if self._mixer.name == mixer_name:
            self.SwitchMixer(next(iter(self._mixers)))
        else:
            self._RefreshMixerList()

    def RenameMixer(self, old_name: str, new_name: str) -> str:
        if old_name not in self._mixers:
            raise KeyError(f"Unknown mixer '{old_name}'")
        new_name = new_name.strip()
        if not new_name or new_name == old_name:
            return old_name
        new_name = _UniqueName(new_name, self._mixers)
        renamed: dict[str, _Mixer] = {}
        for key, mixer in self._mixers.items():
            if key == old_name:
                mixer.name = new_name
            renamed[mixer.name] = mixer
        self._mixers = renamed
        self._RefreshMixerList()
        return new_name

    def GetMixers(self) -> list[str]:
        return list(self._mixers)

    def Serialize(self, mixer_name: str | None = None) -> dict:
        """Return a JSON-serialisable description of a mixer (active one by default)."""
        mixer = self._mixer if mixer_name is None else self._mixers[mixer_name]
        return _MixerToDict(mixer)

    def Deserialize(self, data: dict, activate: bool = True) -> str:
        mixer = _MixerFromDict(data)
        mixer.name = _UniqueName(mixer.name, self._mixers)
        self._mixers[mixer.name] = mixer
        self.MixerCreated.emit(mixer.name)
        if activate:
            self.SwitchMixer(mixer.name)
        else:
            self._RefreshMixerList()
        return mixer.name

    # ==================================================================================
    # Groups
    # ==================================================================================
    def AddGroup(self, group_name: str, parent_group: str = MASTER) -> str:
        """Create a group under ``parent_group`` and return its (unique) name."""
        parent = self._Group(parent_group)
        m = self._mixer
        if len(m.groups)>=256:raise ValueError('Audio Mixer supports at most 256 groups')
        name = _UniqueName(group_name.strip() or "Group", m.groups)
        color = _GROUP_COLORS[m.color_counter % len(_GROUP_COLORS)]
        m.color_counter += 1
        group = _Group(name, parent.name, color)
        m.groups[name] = group
        parent.children.append(name)
        parent.expanded = True
        for snapshot in m.snapshots.values():
            snapshot.data[name] = _CaptureGroup(group)
        self._RefreshGroupTree()
        self._RebuildStrips()
        self.GroupAdded.emit(name)
        return name

    def RemoveGroup(self, group_name: str) -> None:
        """Remove a group together with all of its descendants."""
        if group_name == MASTER:
            raise ValueError("The Master group cannot be removed")
        group = self._Group(group_name)
        m = self._mixer
        doomed: list[str] = []

        def collect(n: str) -> None:
            for child in m.groups[n].children:
                collect(child)
            doomed.append(n)

        collect(group_name)
        self._CloseInspector()
        m.groups[group.parent].children.remove(group_name)  # type: ignore[index]
        for n in doomed:
            del m.groups[n]
            for snapshot in m.snapshots.values():
                snapshot.data.pop(n, None)
            for view in m.views.values():
                view.hidden.discard(n)
        if m.selected not in m.groups:
            m.selected = MASTER
        self._RefreshGroupTree()
        self._RebuildStrips()
        for n in doomed:
            self.GroupRemoved.emit(n)

    def RenameGroup(self, old_name: str, new_name: str) -> str:
        if old_name == MASTER:
            raise ValueError("The Master group cannot be renamed")
        self._Group(old_name)
        new_name = new_name.strip()
        if not new_name or new_name == old_name:
            return old_name
        m = self._mixer
        new_name = _UniqueName(new_name, m.groups)
        renamed: dict[str, _Group] = {}
        for key, g in m.groups.items():
            if key == old_name:
                g.name = new_name
            if g.parent == old_name:
                g.parent = new_name
            g.children = [new_name if c == old_name else c for c in g.children]
            renamed[g.name] = g
        m.groups = renamed
        for snapshot in m.snapshots.values():
            if old_name in snapshot.data:
                snapshot.data[new_name] = snapshot.data.pop(old_name)
        for view in m.views.values():
            if old_name in view.hidden:
                view.hidden.discard(old_name)
                view.hidden.add(new_name)
        if m.selected == old_name:
            m.selected = new_name
        self._CloseInspector()
        self._RefreshGroupTree()
        self._RebuildStrips()
        self.GroupRenamed.emit(old_name, new_name)
        return new_name

    def ReparentGroup(self, group_name: str, new_parent: str) -> None:
        if group_name == MASTER:
            raise ValueError("The Master group cannot be moved")
        group, target = self._Group(group_name), self._Group(new_parent)
        if new_parent == group.parent:
            return
        if new_parent == group_name or self._IsDescendant(new_parent, group_name):
            raise ValueError(
                "A group cannot be moved into itself or one of its descendants"
            )
        self._Group(group.parent).children.remove(group_name)  # type: ignore[arg-type]
        target.children.append(group_name)
        target.expanded = True
        group.parent = new_parent
        self._RefreshGroupTree()
        self._RebuildStrips()
        self.GroupReparented.emit(group_name, new_parent)

    def HasGroup(self, group_name: str) -> bool:
        return group_name in self._mixer.groups

    def GetGroups(self) -> list[str]:
        """All group names in hierarchy (depth-first) order."""
        return self._OrderedGroups()

    def GetChildren(self, group_name: str) -> list[str]:
        return list(self._Group(group_name).children)

    def GetParent(self, group_name: str) -> str | None:
        return self._Group(group_name).parent

    def SetGroupColor(self, group_name: str, color: str | QColor) -> None:
        c = QColor(color)
        if not c.isValid():
            raise ValueError(f"Invalid colour: {color!r}")
        g = self._Group(group_name)
        g.color = c.name()
        if group_name in self._strips:
            self._strips[group_name].RefreshHeader()
        if group_name in self._tree_items:
            self._tree_items[group_name].setIcon(0, _ColorIcon(g.color))
        self.GroupColorChanged.emit(group_name, g.color)

    def GetGroupColor(self, group_name: str) -> str:
        return self._Group(group_name).color

    def SetGroupVisible(self, group_name: str, visible: bool) -> None:
        """Show or hide a group's channel strip (stored in the active view)."""
        g = self._Group(group_name)
        visible = bool(visible)
        if g.visible == visible:
            return
        g.visible = visible
        if group_name in self._strips:
            self._strips[group_name].setVisible(visible)
        if group_name in self._tree_items:
            self._tree_items[group_name].setData(0, ROLE_VISIBLE, visible)
            self._tree.viewport().update()
        view = self._mixer.views.get(self._mixer.active_view)
        if view is not None:
            view.hidden.discard(group_name) if visible else view.hidden.add(group_name)
        self.GroupVisibilityChanged.emit(group_name, visible)

    def IsGroupVisible(self, group_name: str) -> bool:
        return self._Group(group_name).visible

    def SetGroupLocked(self, group_name: str, locked: bool) -> None:
        """Lock a strip so its fader/buttons cannot be changed from the UI."""
        g = self._Group(group_name)
        locked = bool(locked)
        if g.locked == locked:
            return
        g.locked = locked
        if group_name in self._strips:
            self._strips[group_name].RefreshMix()
        if group_name in self._tree_items:
            self._tree_items[group_name].setData(0, ROLE_LOCKED, locked)
            self._tree.viewport().update()
        self.GroupLockChanged.emit(group_name, locked)

    def IsGroupLocked(self, group_name: str) -> bool:
        return self._Group(group_name).locked

    def SelectGroup(self, group_name: str) -> None:
        self._Group(group_name)
        self._mixer.selected = group_name
        for n, strip in self._strips.items():
            strip.SetSelected(n == group_name)
        item = self._tree_items.get(group_name)
        if item is not None and self._tree.currentItem() is not item:
            self._building = True
            try:
                self._tree.setCurrentItem(item)
            finally:
                self._building = False
        if group_name in self._strips:
            self._scroll.ensureWidgetVisible(self._strips[group_name], 12, 0)
        self.GroupSelected.emit(group_name)

    # ==================================================================================
    # Volume / mute / solo / bypass
    # ==================================================================================
    def SetVolume(self, group_name: str, volume_db: float) -> None:
        g = self._Group(group_name)
        value = _Clamp(float(volume_db), MIN_DB, MAX_DB)
        if value == g.volume:
            return
        g.volume = value
        self._SyncSnapshot(group_name)
        self._RefreshStripMix(group_name)
        self.VolumeChanged.emit(group_name, value)

    def GetVolume(self, group_name: str) -> float:
        return self._Group(group_name).volume

    def SetMute(self, group_name: str, muted: bool) -> None:
        g = self._Group(group_name)
        if g.muted == bool(muted):
            return
        g.muted = bool(muted)
        self._SyncSnapshot(group_name)
        self._RefreshStripMix(group_name)
        self.MuteChanged.emit(group_name, g.muted)

    def IsMuted(self, group_name: str) -> bool:
        return self._Group(group_name).muted

    def SetSolo(self, group_name: str, soloed: bool) -> None:
        g = self._Group(group_name)
        if g.soloed == bool(soloed):
            return
        g.soloed = bool(soloed)
        self._RefreshStripMix(group_name)
        self.SoloChanged.emit(group_name, g.soloed)

    def IsSoloed(self, group_name: str) -> bool:
        return self._Group(group_name).soloed

    def SetBypass(self, group_name: str, bypassed: bool) -> None:
        """Bypass every effect of a group (the Attenuation stage stays active)."""
        g = self._Group(group_name)
        if g.bypass == bool(bypassed):
            return
        g.bypass = bool(bypassed)
        self._SyncSnapshot(group_name)
        self._RefreshStripMix(group_name)
        if group_name in self._strips:
            self._strips[group_name].RefreshEffects()
        self.BypassChanged.emit(group_name, g.bypass)

    def IsBypassed(self, group_name: str) -> bool:
        return self._Group(group_name).bypass

    # ==================================================================================
    # Effects
    # ==================================================================================
    def RegisterEffectType(
        self, effect_type: str, parameters: Iterable[EffectParam | tuple]
    ) -> None:
        """Add a custom effect to the Add.. menu. Tuples map to ``EffectParam(*tuple)``."""
        self._effect_types[effect_type] = tuple(
            p if isinstance(p, EffectParam) else EffectParam(*p) for p in parameters
        )

    def GetEffectTypes(self) -> list[str]:
        return list(self._effect_types)

    def AddEffect(
        self, group_name: str, effect_type: str, index: int | None = None
    ) -> str:
        """Append (or insert at ``index``) an effect and return its id."""
        g = self._Group(group_name)
        if len(g.effects)>=16:raise ValueError('Audio group supports at most 16 effects')
        resolved = self._ResolveEffectType(effect_type)
        m = self._mixer
        m.fx_counter += 1
        effect = _Effect(
            f"fx{m.fx_counter:04d}",
            resolved,
            True,
            {p.name: p.default for p in self._effect_types[resolved]},
        )
        g.effects.insert(
            len(g.effects) if index is None else int(_Clamp(index, 0, len(g.effects))),
            effect,
        )
        self._SyncSnapshot(group_name)
        if group_name in self._strips:
            self._strips[group_name].RefreshEffects()
        self.EffectAdded.emit(group_name, effect.id, resolved)
        return effect.id

    def RemoveEffect(self, group_name: str, effect_id: str) -> None:
        g = self._Group(group_name)
        effect = self._Effect(group_name, effect_id)
        self._CloseInspector()
        g.effects.remove(effect)
        self._SyncSnapshot(group_name)
        if group_name in self._strips:
            self._strips[group_name].RefreshEffects()
        self.EffectRemoved.emit(group_name, effect_id)

    def MoveEffect(self, group_name: str, effect_id: str, new_index: int) -> None:
        g = self._Group(group_name)
        effect = self._Effect(group_name, effect_id)
        new_index = int(_Clamp(new_index, 0, len(g.effects) - 1))
        if g.effects.index(effect) == new_index:
            return
        g.effects.remove(effect)
        g.effects.insert(new_index, effect)
        if group_name in self._strips:
            self._strips[group_name].RefreshEffects()
        self.EffectMoved.emit(group_name, effect_id, new_index)

    def SetEffectEnabled(self, group_name: str, effect_id: str, enabled: bool) -> None:
        effect = self._Effect(group_name, effect_id)
        if effect.enabled == bool(enabled):
            return
        effect.enabled = bool(enabled)
        self._SyncSnapshot(group_name)
        if group_name in self._strips:
            self._strips[group_name].RefreshEffects()
        self.EffectEnabledChanged.emit(group_name, effect_id, effect.enabled)

    def IsEffectEnabled(self, group_name: str, effect_id: str) -> bool:
        return self._Effect(group_name, effect_id).enabled

    def SetEffectParameter(
        self, group_name: str, effect_id: str, parameter: str, value: float
    ) -> None:
        effect = self._Effect(group_name, effect_id)
        spec = next(
            (p for p in self._effect_types[effect.type] if p.name == parameter), None
        )
        if spec is None:
            raise KeyError(f"Effect '{effect.type}' has no parameter '{parameter}'")
        value = _Clamp(float(value), spec.minimum, spec.maximum)
        if effect.params.get(parameter) == value:
            return
        effect.params[parameter] = value
        self._SyncSnapshot(group_name)
        self.EffectParameterChanged.emit(group_name, effect_id, parameter, value)

    def GetEffectParameter(
        self, group_name: str, effect_id: str, parameter: str
    ) -> float:
        return self._Effect(group_name, effect_id).params[parameter]

    def GetEffects(self, group_name: str) -> list[dict]:
        return [
            {
                "Id": e.id,
                "Type": e.type,
                "Enabled": e.enabled,
                "Parameters": dict(e.params),
            }
            for e in self._Group(group_name).effects
        ]

    # ==================================================================================
    # Snapshots (edits always modify the active snapshot, like Unity)
    # ==================================================================================
    def CreateSnapshot(self, snapshot_name: str, activate: bool = True) -> str:
        """Store the current mix as a new snapshot and (by default) make it active."""
        m = self._mixer
        name = _UniqueName(snapshot_name.strip() or "Snapshot", m.snapshots)
        m.snapshots[name] = _Snapshot(name, data=_CaptureAll(m))
        if activate:
            m.active_snapshot = name
        self._RefreshSnapshotList()
        self.SnapshotCreated.emit(name)
        return name

    def DuplicateSnapshot(self, snapshot_name: str) -> str:
        source = self._Snapshot(snapshot_name)
        name = _UniqueName(f"{snapshot_name}", self._mixer.snapshots)
        self._mixer.snapshots[name] = _Snapshot(name, False, copy.deepcopy(source.data))
        self._RefreshSnapshotList()
        self.SnapshotCreated.emit(name)
        return name

    def SwitchSnapshot(self, snapshot_name: str) -> None:
        snapshot = self._Snapshot(snapshot_name)
        m = self._mixer
        if m.active_snapshot == snapshot_name:
            return
        self._CloseInspector()
        m.active_snapshot = snapshot_name
        self._ApplySnapshotData(snapshot)
        for strip in self._strips.values():
            strip.RefreshMix()
            strip.RefreshEffects()
        self._RefreshSnapshotList()
        self.SnapshotSwitched.emit(snapshot_name)

    def RemoveSnapshot(self, snapshot_name: str) -> None:
        self._Snapshot(snapshot_name)
        m = self._mixer
        if len(m.snapshots) == 1:
            raise ValueError("The last snapshot cannot be removed")
        was_active = m.active_snapshot == snapshot_name
        del m.snapshots[snapshot_name]
        if was_active:
            # Make the first remaining snapshot active and load its state.
            fallback = next(iter(m.snapshots))
            m.active_snapshot = ""
            self.SwitchSnapshot(fallback)
        self._RefreshSnapshotList()
        self.SnapshotRemoved.emit(snapshot_name)

    def RenameSnapshot(self, old_name: str, new_name: str) -> str:
        self._Snapshot(old_name)
        new_name = new_name.strip()
        if not new_name or new_name == old_name:
            return old_name
        m = self._mixer
        new_name = _UniqueName(new_name, m.snapshots)
        renamed: dict[str, _Snapshot] = {}
        for key, s in m.snapshots.items():
            if key == old_name:
                s.name = new_name
            renamed[s.name] = s
        m.snapshots = renamed
        if m.active_snapshot == old_name:
            m.active_snapshot = new_name
        self._RefreshSnapshotList()
        self.SnapshotRenamed.emit(old_name, new_name)
        return new_name

    def SetSnapshotFavorite(self, snapshot_name: str, favorite: bool) -> None:
        s = self._Snapshot(snapshot_name)
        if s.favorite == bool(favorite):
            return
        s.favorite = bool(favorite)
        self._RefreshSnapshotList()
        self.SnapshotFavoriteChanged.emit(snapshot_name, s.favorite)

    def IsSnapshotFavorite(self, snapshot_name: str) -> bool:
        return self._Snapshot(snapshot_name).favorite

    def GetSnapshots(self) -> list[str]:
        return list(self._mixer.snapshots)

    # ==================================================================================
    # Views (named strip-visibility layouts)
    # ==================================================================================
    def CreateView(self, view_name: str) -> str:
        """Capture which strips are currently visible as a new view and activate it."""
        m = self._mixer
        name = _UniqueName(view_name.strip() or "View", m.views)
        m.views[name] = _View(name, {n for n, g in m.groups.items() if not g.visible})
        m.active_view = name
        self._RefreshViewList()
        self.ViewCreated.emit(name)
        return name

    def SwitchView(self, view_name: str) -> None:
        view = self._View(view_name)
        m = self._mixer
        m.active_view = view_name
        for name, g in list(m.groups.items()):
            visible = name not in view.hidden
            if g.visible != visible:
                g.visible = visible
                if name in self._strips:
                    self._strips[name].setVisible(visible)
                if name in self._tree_items:
                    self._tree_items[name].setData(0, ROLE_VISIBLE, visible)
                self.GroupVisibilityChanged.emit(name, visible)
        self._tree.viewport().update()
        self._RefreshViewList()
        self.ViewSwitched.emit(view_name)

    def RemoveView(self, view_name: str) -> None:
        self._View(view_name)
        m = self._mixer
        if len(m.views) == 1:
            raise ValueError("The last view cannot be removed")
        del m.views[view_name]
        if m.active_view == view_name:
            self.SwitchView(next(iter(m.views)))
        self._RefreshViewList()
        self.ViewRemoved.emit(view_name)

    def RenameView(self, old_name: str, new_name: str) -> str:
        self._View(old_name)
        new_name = new_name.strip()
        if not new_name or new_name == old_name:
            return old_name
        m = self._mixer
        new_name = _UniqueName(new_name, m.views)
        renamed: dict[str, _View] = {}
        for key, v in m.views.items():
            if key == old_name:
                v.name = new_name
            renamed[v.name] = v
        m.views = renamed
        if m.active_view == old_name:
            m.active_view = new_name
        self._RefreshViewList()
        self.ViewRenamed.emit(old_name, new_name)
        return new_name

    def GetViews(self) -> list[str]:
        return list(self._mixer.views)

    # ==================================================================================
    # Metering
    # ==================================================================================
    def SetMeterLevels(
        self, group_name: str, left_db: float, right_db: float | None = None
    ) -> None:
        """Feed live levels (dBFS) from the audio engine; call at your UI/audio-poll rate."""
        strip = self._strips.get(group_name)
        if strip is not None:
            strip.Meter.SetLevels(left_db, left_db if right_db is None else right_db)


    def SetSidebarVisible(self, visible: bool) -> None:
        self._sidebar.setVisible(visible)

    # ==================================================================================
    # Private: lookups
    # ==================================================================================
    def _Group(self, name: str) -> _Group:
        try:
            return self._mixer.groups[name]
        except KeyError:
            raise KeyError(f"Unknown audio group '{name}'") from None

    def _Effect(self, group: str, effect_id: str) -> _Effect:
        for e in self._Group(group).effects:
            if e.id == effect_id:
                return e
        raise KeyError(f"Group '{group}' has no effect '{effect_id}'")

    def _Snapshot(self, name: str) -> _Snapshot:
        try:
            return self._mixer.snapshots[name]
        except KeyError:
            raise KeyError(f"Unknown snapshot '{name}'") from None

    def _View(self, name: str) -> _View:
        try:
            return self._mixer.views[name]
        except KeyError:
            raise KeyError(f"Unknown view '{name}'") from None

    def _ResolveEffectType(self, effect_type: str) -> str:
        wanted = effect_type.strip().lower()
        for known in self._effect_types:
            if known.lower() == wanted:
                return known
        alias = _EFFECT_ALIASES.get(wanted)
        if alias in self._effect_types:
            return alias  # type: ignore[return-value]
        raise ValueError(
            f"Unknown effect type '{effect_type}'. Available: {', '.join(self._effect_types)}"
        )

    def _OrderedGroups(self) -> list[str]:
        order: list[str] = []

        def walk(name: str) -> None:
            order.append(name)
            for child in self._mixer.groups[name].children:
                walk(child)

        walk(MASTER)
        return order

    def _IsDescendant(self, candidate: str, ancestor: str) -> bool:
        current = self._mixer.groups[candidate].parent
        while current is not None:
            if current == ancestor:
                return True
            current = self._mixer.groups[current].parent
        return False

    # ==================================================================================
    # Private: snapshots
    # ==================================================================================
    def _SyncSnapshot(self, group_name: str) -> None:
        m = self._mixer
        snapshot = m.snapshots.get(m.active_snapshot)
        if snapshot is not None and group_name in m.groups:
            snapshot.data[group_name] = _CaptureGroup(m.groups[group_name])

    def _ApplySnapshotData(self, snapshot: _Snapshot) -> None:
        for name, d in snapshot.data.items():
            g = self._mixer.groups.get(name)
            if g is None:
                continue
            if g.volume != d["volume"]:
                g.volume = float(d["volume"])
                self.VolumeChanged.emit(name, g.volume)
            if g.muted != d["muted"]:
                g.muted = bool(d["muted"])
                self.MuteChanged.emit(name, g.muted)
            if g.bypass != d["bypass"]:
                g.bypass = bool(d["bypass"])
                self.BypassChanged.emit(name, g.bypass)
            for e in g.effects:
                stored = d["effects"].get(e.id)
                if not stored:
                    continue
                if e.enabled != stored["enabled"]:
                    e.enabled = bool(stored["enabled"])
                    self.EffectEnabledChanged.emit(name, e.id, e.enabled)
                for key, value in stored["params"].items():
                    if key in e.params and e.params[key] != value:
                        e.params[key] = float(value)
                        self.EffectParameterChanged.emit(name, e.id, key, float(value))

    # ==================================================================================
    # Private: UI construction
    # ==================================================================================
    def _BuildUi(self) -> None:
        t = self._theme
        root = QHBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)
        self._splitter = QSplitter(Qt.Orientation.Horizontal, self)
        self._splitter.setChildrenCollapsible(False)
        root.addWidget(self._splitter)

        # ---- sidebar
        self._sidebar = QWidget()
        self._sidebar.setObjectName("MixerSidebar")
        self._sidebar.setMinimumWidth(120)
        self._sidebar.setMaximumWidth(380)
        side = QVBoxLayout(self._sidebar)
        side.setContentsMargins(0, 0, 0, 0)
        side.setSpacing(0)

        self._group_delegate = _GroupDelegate(t, self)
        self._snapshot_delegate = _SnapshotDelegate(t, self)

        # Mixers
        sec = _Section("Mixers")
        self._mixer_list = QListWidget()
        self._mixer_list.setObjectName("MixerList")
        self._mixer_list.itemClicked.connect(
            lambda it: self._SafeSwitchMixer(it.data(ROLE_NAME))
        )
        sec.SetBody(self._mixer_list)
        sec.AddButton.clicked.connect(self.AssetPickRequested)
        self._mixer_section = sec
        side.addWidget(sec)

        # Snapshots
        sec = _Section("Snapshots")
        self._snapshot_list = _SnapshotList()
        self._snapshot_list.setItemDelegate(self._snapshot_delegate)
        self._snapshot_list.itemClicked.connect(
            lambda it: self.SwitchSnapshot(it.data(ROLE_NAME))
        )
        self._snapshot_list.itemDoubleClicked.connect(
            lambda it: self._PromptRenameSnapshot(it.data(ROLE_NAME))
        )
        self._snapshot_list.StarClicked.connect(
            lambda n: self.SetSnapshotFavorite(n, not self.IsSnapshotFavorite(n))
        )
        self._snapshot_list.customContextMenuRequested.connect(self._ShowSnapshotMenu)
        sec.SetBody(self._snapshot_list)
        sec.AddClicked.connect(lambda: self.CreateSnapshot("New Snapshot"))
        side.addWidget(sec)

        # Groups
        sec = _Section("Groups", grow=True)
        self._tree = _GroupTree()
        self._tree.setItemDelegate(self._group_delegate)
        self._tree.currentItemChanged.connect(self._OnTreeCurrentChanged)
        self._tree.itemExpanded.connect(lambda it: self._OnTreeExpanded(it, True))
        self._tree.itemCollapsed.connect(lambda it: self._OnTreeExpanded(it, False))
        self._tree.itemDoubleClicked.connect(
            lambda it, _c: self._PromptRename(it.data(0, ROLE_NAME))
        )
        self._tree.IconClicked.connect(self._OnTreeIconClicked)
        self._tree.ReparentRequested.connect(
            lambda g, p: QTimer.singleShot(0, lambda: self._TryReparent(g, p))
        )
        self._tree.customContextMenuRequested.connect(
            lambda pos: self._OnTreeContext(pos)
        )
        sec.SetBody(self._tree)
        sec.AddClicked.connect(lambda: self.AddGroup("New Group", self.SelectedGroup))
        side.addWidget(sec, 100)

        # Views
        sec = _Section("Views")
        self._view_list = QListWidget()
        self._view_list.setObjectName("MixerViewList")
        self._view_list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._view_list.itemClicked.connect(
            lambda it: self.SwitchView(it.data(ROLE_NAME))
        )
        self._view_list.itemDoubleClicked.connect(
            lambda it: self._PromptRenameView(it.data(ROLE_NAME))
        )
        self._view_list.customContextMenuRequested.connect(self._ShowViewMenu)
        sec.SetBody(self._view_list)
        sec.AddClicked.connect(lambda: self.CreateView("New View"))
        side.addWidget(sec)
        side.addStretch(1)

        # ---- channel strips
        self._scroll = QScrollArea()
        self._scroll.setObjectName("MixerStripScroll")
        self._scroll.setWidgetResizable(True)
        self._scroll.setFrameShape(QFrame.Shape.NoFrame)
        self._scroll.viewport().setObjectName("MixerStripViewport")
        self._strip_host = QWidget()
        self._strip_host.setObjectName("MixerStripHost")
        self._strip_layout = QHBoxLayout(self._strip_host)
        self._strip_layout.setContentsMargins(6, 6, 6, 6)
        self._strip_layout.setSpacing(5)
        self._scroll.setWidget(self._strip_host)

        self._splitter.addWidget(self._sidebar)
        self._splitter.addWidget(self._scroll)
        self._splitter.setStretchFactor(0, 0)
        self._splitter.setStretchFactor(1, 1)
        self._splitter.setSizes([230, 870])

    # ==================================================================================
    # Private: UI refresh
    # ==================================================================================
    def _RebuildAll(self) -> None:
        self._CloseInspector()
        self._RefreshMixerList()
        self._RefreshSnapshotList()
        self._RefreshGroupTree()
        self._RefreshViewList()
        self._RebuildStrips()

    def _FitList(self, widget: QListWidget, max_rows: int) -> None:
        row_h = max(
            self._theme.row_height, widget.sizeHintForRow(0) if widget.count() else 0
        )
        widget.setFixedHeight(max(1, min(widget.count(), max_rows)) * row_h + 4)

    def _RefreshMixerList(self) -> None:
        if self._mixer is None:
            return
        self._mixer_list.clear()
        for name in self._mixers:
            item = QListWidgetItem(name)
            item.setData(ROLE_NAME, name)
            self._mixer_list.addItem(item)
            if name == self._mixer.name:
                self._mixer_list.setCurrentItem(item)
        self._FitList(self._mixer_list, 4)

    def _RefreshSnapshotList(self) -> None:
        m = self._mixer
        self._snapshot_list.clear()
        for s in m.snapshots.values():
            item = QListWidgetItem(s.name)
            item.setData(ROLE_NAME, s.name)
            item.setData(ROLE_FAVORITE, s.favorite)
            item.setData(ROLE_ACTIVE, s.name == m.active_snapshot)
            self._snapshot_list.addItem(item)
            if s.name == m.active_snapshot:
                self._snapshot_list.setCurrentItem(item)
        self._FitList(self._snapshot_list, 6)

    def _RefreshViewList(self) -> None:
        m = self._mixer
        self._view_list.clear()
        for v in m.views.values():
            item = QListWidgetItem(v.name)
            item.setData(ROLE_NAME, v.name)
            self._view_list.addItem(item)
            if v.name == m.active_view:
                self._view_list.setCurrentItem(item)
        self._FitList(self._view_list, 5)

    def _RefreshGroupTree(self) -> None:
        m = self._mixer
        self._building = True
        try:
            self._tree.clear()
            self._tree_items = {}

            def add(name: str, parent_item: QTreeWidgetItem | None) -> None:
                g = m.groups[name]
                item = QTreeWidgetItem([name])
                item.setData(0, ROLE_NAME, name)
                item.setData(0, ROLE_VISIBLE, g.visible)
                item.setData(0, ROLE_LOCKED, g.locked)
                item.setIcon(0, _ColorIcon(g.color))
                if parent_item is None:
                    self._tree.addTopLevelItem(item)
                else:
                    parent_item.addChild(item)
                self._tree_items[name] = item
                for child in g.children:
                    add(child, item)
                item.setExpanded(g.expanded)

            add(MASTER, None)
            self._tree.setCurrentItem(
                self._tree_items.get(m.selected, self._tree_items[MASTER])
            )
        finally:
            self._building = False

    def _RebuildStrips(self) -> None:
        self._CloseInspector()
        _ClearLayout(self._strip_layout)
        self._strips.clear()
        m = self._mixer
        for name in self._OrderedGroups():
            g = m.groups[name]
            strip = _ChannelStrip(self, name, self._theme, self._strip_host)
            self._strip_layout.addWidget(strip)
            self._strips[name] = strip
            strip.Refresh()
            strip.SetSelected(name == m.selected)
            strip.setVisible(g.visible)
        self._strip_layout.addStretch(1)
        if hasattr(self,'Locale'):self.Retranslate(self.Locale)

    def _RefreshStripMix(self, group_name: str) -> None:
        strip = self._strips.get(group_name)
        if strip is not None:
            strip.RefreshMix()

    # ==================================================================================
    # Private: sidebar handlers and dialogs
    # ==================================================================================
    def _OnTreeCurrentChanged(self, current: QTreeWidgetItem | None, _previous) -> None:
        if self._building or current is None:
            return
        self.SelectGroup(current.data(0, ROLE_NAME))

    def _OnTreeExpanded(self, item: QTreeWidgetItem, expanded: bool) -> None:
        if self._building:
            return
        g = self._mixer.groups.get(item.data(0, ROLE_NAME))
        if g is not None:
            g.expanded = expanded

    def _OnTreeIconClicked(self, name: str, kind: str) -> None:
        if kind == "visible":
            self.SetGroupVisible(name, not self.IsGroupVisible(name))
        else:
            self.SetGroupLocked(name, not self.IsGroupLocked(name))

    def _OnTreeContext(self, pos: QPoint) -> None:
        item = self._tree.itemAt(pos)
        if item is not None:
            self._ShowGroupMenu(
                item.data(0, ROLE_NAME), self._tree.viewport().mapToGlobal(pos)
            )

    def _TryReparent(self, group: str, new_parent: str) -> None:
        try:
            self.ReparentGroup(group, new_parent)
        except (KeyError, ValueError):
            pass  # invalid drop target (e.g. into own subtree): leave the hierarchy untouched

    def _SafeSwitchMixer(self, name: str) -> None:
        if name != self._mixer.name:
            self.SwitchMixer(name)

    def _PromptText(self, title: str, label: str, text: str = "") -> str | None:
        value, ok = QInputDialog.getText(self, self.Translate(title), self.Translate(label), text=text)
        value = value.strip()
        return value if ok and value else None

    def _PromptRename(self, group: str) -> None:
        if group == MASTER:
            return
        name = self._PromptText("Rename Group", "Group name:", group)
        if name:
            self.RenameGroup(group, name)

    def _PromptRenameSnapshot(self, snapshot: str) -> None:
        name = self._PromptText("Rename Snapshot", "Snapshot name:", snapshot)
        if name:
            self.RenameSnapshot(snapshot, name)

    def _PromptRenameView(self, view: str) -> None:
        name = self._PromptText("Rename View", "View name:", view)
        if name:
            self.RenameView(view, name)

    def _PromptAddGroup(self, parent: str) -> None:
        name = self._PromptText("Add Group", "Group name:", "New Group")
        if name:
            self.AddGroup(name, parent)

    def _PromptColor(self, group: str) -> None:
        color = QColorDialog.getColor(
            QColor(self.GetGroupColor(group)), self, "Group Color", options=QColorDialog.ColorDialogOption.DontUseNativeDialog
        )
        if color.isValid():
            self.SetGroupColor(group, color)

    def _ShowGroupMenu(self, group: str, global_pos: QPoint) -> None:
        if group not in self._mixer.groups:
            return
        menu = QMenu(self)
        menu.addAction("Add Child Group…", lambda: self._PromptAddGroup(group))
        if group != MASTER:
            menu.addAction("Rename…", lambda: self._PromptRename(group))
        menu.addAction("Set Color…", lambda: self._PromptColor(group))
        menu.addAction("Hide Strip", lambda: self.SetGroupVisible(group, False))
        menu.addAction(
            "Unlock Strip" if self.IsGroupLocked(group) else "Lock Strip",
            lambda: self.SetGroupLocked(group, not self.IsGroupLocked(group)),
        )
        if group != MASTER:
            menu.addSeparator()
            menu.addAction("Delete Group", lambda: self.RemoveGroup(group))
        menu.exec(global_pos)

    def _ShowSnapshotMenu(self, pos: QPoint) -> None:
        item = self._snapshot_list.itemAt(pos)
        if item is None:
            return
        name = item.data(ROLE_NAME)
        menu = QMenu(self)
        menu.addAction("Rename…", lambda: self._PromptRenameSnapshot(name))
        menu.addAction("Duplicate", lambda: self.DuplicateSnapshot(name))
        menu.addAction(
            "Remove Favorite" if self.IsSnapshotFavorite(name) else "Mark as Favorite",
            lambda: self.SetSnapshotFavorite(name, not self.IsSnapshotFavorite(name)),
        )
        menu.addSeparator()
        menu.addAction("Delete", lambda: self.RemoveSnapshot(name)).setEnabled(
            len(self._mixer.snapshots) > 1
        )
        menu.exec(self._snapshot_list.viewport().mapToGlobal(pos))

    def _ShowViewMenu(self, pos: QPoint) -> None:
        item = self._view_list.itemAt(pos)
        if item is None:
            return
        name = item.data(ROLE_NAME)
        menu = QMenu(self)
        menu.addAction("Rename…", lambda: self._PromptRenameView(name))
        menu.addAction("Delete", lambda: self.RemoveView(name)).setEnabled(
            len(self._mixer.views) > 1
        )
        menu.exec(self._view_list.viewport().mapToGlobal(pos))

    # ==================================================================================
    # Private: effect inspector
    # ==================================================================================
    def _ShowEffectInspector(self, anchor: QWidget, group: str, effect_id: str) -> None:
        self._CloseInspector()
        inspector = _EffectInspector(self, group, effect_id, self._theme)
        self._inspector = inspector
        inspector.destroyed.connect(lambda *_: setattr(self, "_inspector", None) if self._inspector is inspector else None)
        inspector.adjustSize()
        pos = anchor.mapToGlobal(QPoint(anchor.width() + 6, 0))
        screen = anchor.screen().availableGeometry()
        pos.setX(min(pos.x(), screen.right() - inspector.width()))
        pos.setY(min(pos.y(), screen.bottom() - inspector.height()))
        inspector.move(pos)
        inspector.show()
        if hasattr(self,'Locale'):self.Retranslate(self.Locale)

    def _CloseInspector(self) -> None:
        inspector, self._inspector = self._inspector, None
        if inspector is not None:
            try:
                inspector.close()
            except RuntimeError:
                pass  # already deleted by Qt

    # ==================================================================================
    # Private: metering
    # ==================================================================================
    def showEvent(self, event) -> None:  # noqa: N802
        self._last_tick = time.monotonic()
        self._meter_timer.start()
        super().showEvent(event)

    def hideEvent(self, event) -> None:  # noqa: N802
        self._meter_timer.stop()
        super().hideEvent(event)

    def _OnMeterTick(self) -> None:
        now = time.monotonic()
        dt = min(0.1, now - self._last_tick)
        self._last_tick = now
        for strip in self._strips.values():
            strip.Meter.Tick(dt)

    def Translate(self,text):
        from .mixer_locale import UI_TEXT
        locale=getattr(self,'Locale',None);key=UI_TEXT.get(text)
        return locale.Translate(key) if locale and key else text

    def Retranslate(self,locale):
        self.Locale=locale
        for widget in self.findChildren(QWidget):
            if not isinstance(widget,(QLabel,QToolButton,QCheckBox)):continue
            source=widget.property('mixer_source') or widget.text()
            from .mixer_locale import UI_TEXT
            if source in UI_TEXT:widget.setProperty('mixer_source',source);widget.setText(self.Translate(source))
            tip=widget.property('mixer_tip') or widget.toolTip()
            if tip in UI_TEXT:widget.setProperty('mixer_tip',tip);widget.setToolTip(self.Translate(tip))


