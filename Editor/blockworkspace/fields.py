"""Input / field definitions used in ``BlockType.Inputs``.

Hierarchy
---------
InputDef
 ├─ LabelInput                 static text
 ├─ FieldInput                 embedded editable value (String/Int/Float/Bool/Dropdown/Asset/Entity/Color/Vector/Custom)
 ├─ ValueInput / BooleanInput  value socket (optionally with an inline *Shadow* field)
 └─ StatementInput             C-shaped mouth for nested statements
"""
from __future__ import annotations
from typing import Any, Callable, Optional, Sequence

from PySide6.QtCore import Qt, QRectF, QEvent, QObject, QPointF
from PySide6.QtGui import QColor, QPainter, QPen, QBrush, QFontMetricsF, QIntValidator, QDoubleValidator
from PySide6.QtWidgets import (QLineEdit, QMenu, QColorDialog, QWidgetAction, QWidget, QHBoxLayout,
                               QDoubleSpinBox, QLabel)

from .theme import Qc


# ---------------------------------------------------------------------------------------------
class FieldStyle:
    """Paint/measure context handed to fields by the graphics layer."""
    def __init__(self, Theme, Font, TextWidth, FieldHeight=22, PadX=9):
        self.Theme = Theme
        self.Font = Font
        self.TextWidth = TextWidth
        self.FieldHeight = FieldHeight
        self.PadX = PadX
        self.Disabled = False


class FieldEditContext:
    """Everything a field editor needs. ``Rect`` is in viewport coordinates."""
    def __init__(self, Parent, Rect, Value, Commit: Callable[[Any], None], Workspace=None, Block=None, Key=None, Input=None):
        self.Parent, self.Rect, self.Value, self.Commit = Parent, Rect, Value, Commit
        self.Workspace, self.Block, self.Key, self.Input = Workspace, Block, Key, Input

    def GlobalPos(self):
        return self.Parent.mapToGlobal(self.Rect.bottomLeft())


class _EscFilter(QObject):
    def __init__(self, cb):
        super().__init__()
        self.cb = cb

    def eventFilter(self, obj, ev):
        if ev.type() == QEvent.KeyPress and ev.key() == Qt.Key_Escape:
            self.cb()
            return True
        return False


def ShowLineEditor(ctx: FieldEditContext, text: str, commit: Callable[[str], None], validator=None):
    ed = QLineEdit(ctx.Parent)
    r = ctx.Rect.adjusted(-2, -2, 2, 2)
    r.setWidth(max(r.width(), 56))
    ed.setGeometry(r)
    if validator is not None:
        ed.setValidator(validator)
    ed.setText(text)
    # Inherit application theme for inline editors.
    done = {"v": False}

    def finish(ok):
        if done["v"]:
            return
        done["v"] = True
        t = ed.text()
        ed.removeEventFilter(flt)
        ed.hide()
        ed.deleteLater()
        if ok:
            commit(t)

    flt = _EscFilter(lambda: finish(False))
    ed._flt = flt
    ed.installEventFilter(flt)
    ed.returnPressed.connect(lambda: finish(True))
    ed.editingFinished.connect(lambda: finish(True))
    ed.show()
    ed.setFocus()
    ed.selectAll()
    return ed


def ShowChoiceMenu(ctx: FieldEditContext, options: Sequence[tuple[str, Any]], commit: Callable[[Any], None], current=None):
    m = QMenu(ctx.Parent)
    for label, value in options:
        a = m.addAction(label)
        a.setCheckable(True)
        a.setChecked(value == current)
        a.triggered.connect(lambda _=False, v=value: commit(v))
    m.exec(ctx.GlobalPos())
    return m


# ---------------------------------------------------------------------------------------------
class InputDef:
    Kind = "field"

    def __init__(self, Name: str = "", Label: Optional[str] = None, Tooltip: str = "", Required: bool = False):
        self.Name = Name
        self.Label = Label
        self.Tooltip = Tooltip
        self.Required = Required


class LabelInput(InputDef):
    Kind = "label"
    _n = 0

    def __init__(self, Text: str):
        LabelInput._n += 1
        super().__init__(Name=f"@label{LabelInput._n}")
        self.Text = Text


class FieldInput(InputDef):
    """Base for all embedded fields. Subclass this to build custom fields."""
    Kind = "field"
    Pill = True  # draw as pill (True) or rounded rect

    def __init__(self, Name: str, DefaultValue: Any = None, Label: Optional[str] = None, Tooltip: str = "", Required: bool = False):
        super().__init__(Name, Label, Tooltip, Required)
        self.DefaultValue = DefaultValue

    # ---- value handling
    def GetDefault(self):
        return self.DefaultValue

    def Coerce(self, value):
        return value

    def Serialize(self, value) -> str:
        return "" if value is None else str(value)

    def Deserialize(self, text: str):
        return text

    def Validate(self, value) -> Optional[str]:
        return None

    def DisplayText(self, value) -> str:
        return "" if value is None else str(value)

    # ---- visuals
    MaxWidth = 170

    def Measure(self, value, style: FieldStyle):
        t = self.DisplayText(value)
        w = style.TextWidth(t if t else " ") + style.PadX * 2
        return (max(min(w, self.MaxWidth), 28), style.FieldHeight)

    def Paint(self, p: QPainter, r: QRectF, value, style: FieldStyle):
        th = style.Theme
        p.setPen(Qt.NoPen)
        p.setBrush(Qc(th.FieldFill if not style.Disabled else th.DisabledText))
        rad = r.height() / 2 if self.Pill else 4
        p.drawRoundedRect(r, rad, rad)
        self.PaintText(p, r, value, style)

    def PaintText(self, p, r, value, style, text=None):
        th = style.Theme
        t = self.DisplayText(value) if text is None else text
        p.setFont(style.Font)
        if t == "" and self.Placeholder():
            p.setPen(Qc(th.FieldPlaceholder))
            t = self.Placeholder()
        else:
            p.setPen(Qc(th.FieldText))
        fm = QFontMetricsF(style.Font)
        p.drawText(r.adjusted(style.PadX, 0, -style.PadX, 0), int(Qt.AlignVCenter | Qt.AlignHCenter),
                   fm.elidedText(t, Qt.ElideRight, r.width() - style.PadX * 2))

    def Placeholder(self) -> str:
        return ""

    # ---- interaction
    def OnClick(self, ctx: FieldEditContext):
        self.Edit(ctx)

    def Edit(self, ctx: FieldEditContext):
        ShowLineEditor(ctx, self.DisplayText(ctx.Value), lambda t: ctx.Commit(self.Deserialize(t)))


CustomInput = FieldInput  # alias: subclass for fully custom fields


class StringInput(FieldInput):
    def __init__(self, Name, DefaultValue="", Label=None, Placeholder="", **kw):
        super().__init__(Name, DefaultValue, Label, **kw)
        self._placeholder = Placeholder

    def Placeholder(self):
        return self._placeholder

    def Coerce(self, v):
        return "" if v is None else str(v)


class IntInput(FieldInput):
    def __init__(self, Name, DefaultValue=0, Label=None, Min=None, Max=None, **kw):
        super().__init__(Name, DefaultValue, Label, **kw)
        self.Min, self.Max = Min, Max

    def Coerce(self, v):
        try:
            v = int(round(float(v)))
        except (TypeError, ValueError):
            v = int(self.DefaultValue or 0)
        if self.Min is not None:
            v = max(self.Min, v)
        if self.Max is not None:
            v = min(self.Max, v)
        return v

    def Deserialize(self, text):
        return self.Coerce(text)

    def Edit(self, ctx):
        ShowLineEditor(ctx, self.DisplayText(ctx.Value), lambda t: ctx.Commit(self.Coerce(t)), QIntValidator())


class FloatInput(FieldInput):
    def __init__(self, Name, DefaultValue=0.0, Label=None, Min=None, Max=None, **kw):
        super().__init__(Name, float(DefaultValue), Label, **kw)
        self.Min, self.Max = Min, Max

    def Coerce(self, v):
        try:
            v = float(v)
        except (TypeError, ValueError):
            v = float(self.DefaultValue or 0.0)
        if self.Min is not None:
            v = max(self.Min, v)
        if self.Max is not None:
            v = min(self.Max, v)
        return v

    def Serialize(self, v):
        return repr(round(float(v), 6))

    def Deserialize(self, text):
        return self.Coerce(text)

    def DisplayText(self, v):
        return f"{float(v):.6g}" if v is not None else ""

    def Edit(self, ctx):
        ShowLineEditor(ctx, self.DisplayText(ctx.Value), lambda t: ctx.Commit(self.Coerce(t)), QDoubleValidator())


class BoolInput(FieldInput):
    def __init__(self, Name, DefaultValue=False, Label=None, **kw):
        super().__init__(Name, bool(DefaultValue), Label, **kw)

    def Coerce(self, v):
        if isinstance(v, str):
            return v.strip().lower() in ("1", "true", "yes", "on")
        return bool(v)

    def Serialize(self, v):
        return "true" if v else "false"

    def Deserialize(self, text):
        return self.Coerce(text)

    def Measure(self, value, style):
        return (style.FieldHeight + 4, style.FieldHeight)

    def Paint(self, p, r, value, style):
        th = style.Theme
        p.setPen(Qt.NoPen)
        p.setBrush(Qc(th.FieldFill))
        p.drawRoundedRect(r, 5, 5)
        if value:
            p.setPen(QPen(Qc("#4C97FF"), 2.4, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
            c = r.center()
            p.drawPolyline([QPointF(c.x() - 5, c.y()), QPointF(c.x() - 1.5, c.y() + 4), QPointF(c.x() + 5, c.y() - 4)])

    def OnClick(self, ctx):
        ctx.Commit(not bool(ctx.Value))


class DropdownInput(FieldInput):
    """Options: list of values or (label, value) tuples."""
    Pill = False

    def __init__(self, Name, Options: Sequence, DefaultValue=None, Label=None, **kw):
        self.Options = [(o, o) if not isinstance(o, (tuple, list)) else (str(o[0]), o[1]) for o in Options]
        if DefaultValue is None and self.Options:
            DefaultValue = self.Options[0][1]
        super().__init__(Name, DefaultValue, Label, **kw)

    def Coerce(self, v):
        for _, val in self.Options:
            if val == v or str(val) == str(v):
                return val
        return self.DefaultValue

    def Deserialize(self, text):
        return self.Coerce(text)

    def DisplayText(self, v):
        for label, val in self.Options:
            if val == v:
                return str(label)
        return str(v) if v is not None else ""

    def Measure(self, value, style):
        w, h = super().Measure(value, style)
        return (w + 12, h)

    def PaintText(self, p, r, value, style, text=None):
        super().PaintText(p, r.adjusted(0, 0, -10, 0), value, style)
        c = QPointF(r.right() - 10, r.center().y())
        p.setBrush(Qc(style.Theme.FieldText))
        p.setPen(Qt.NoPen)
        p.drawPolygon([QPointF(c.x() - 4, c.y() - 2), QPointF(c.x() + 4, c.y() - 2), QPointF(c.x(), c.y() + 3)])

    def Edit(self, ctx):
        ShowChoiceMenu(ctx, self.Options, ctx.Commit, ctx.Value)


class _ReferenceInput(FieldInput):
    Pill = False
    _provider_attr = ""

    def Placeholder(self):
        return "<none>"

    def _Names(self, ctx):
        return []

    def Edit(self, ctx):
        names = self._Names(ctx)
        if not names:
            return ShowLineEditor(ctx, self.DisplayText(ctx.Value), ctx.Commit)
        opts = [("<none>", "")] + [(n if isinstance(n, str) else n[0], n if isinstance(n, str) else n[1]) for n in names]
        if ctx.Value and ctx.Value not in [v for _, v in opts]:
            opts.append((str(ctx.Value), ctx.Value))
        ShowChoiceMenu(ctx, opts, ctx.Commit, ctx.Value)


class AssetInput(_ReferenceInput):
    def __init__(self, Name, AssetType="Asset", DefaultValue="", Label=None, **kw):
        super().__init__(Name, DefaultValue, Label, **kw)
        self.AssetType = AssetType

    def Coerce(self, v):
        return "" if v is None else str(v)

    def _Names(self, ctx):
        ws = ctx.Workspace
        return ws.GetAssetNames(self.AssetType) if ws is not None else []


class EntityInput(_ReferenceInput):
    def __init__(self, Name, EntityType="Entity", DefaultValue="", Label=None, **kw):
        super().__init__(Name, DefaultValue, Label, **kw)
        self.EntityType = EntityType

    def Coerce(self, v):
        return "" if v is None else str(v)

    def _Names(self, ctx):
        ws = ctx.Workspace
        return ws.GetEntityNames(self.EntityType) if ws is not None else []


class ColorInput(FieldInput):
    def __init__(self, Name, DefaultValue="#FFFFFF", Label=None, **kw):
        super().__init__(Name, DefaultValue, Label, **kw)

    def Coerce(self, v):
        c = QColor(str(v)) if not isinstance(v, QColor) else v
        return c.name().upper() if c.isValid() else self.DefaultValue

    def Deserialize(self, text):
        return self.Coerce(text)

    def Measure(self, value, style):
        return (style.FieldHeight + 14, style.FieldHeight)

    def Paint(self, p, r, value, style):
        p.setPen(QPen(Qc("#FFFFFF"), 2))
        p.setBrush(Qc(value or "#FFFFFF"))
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5)

    def Edit(self, ctx):
        c = QColorDialog.getColor(QColor(ctx.Value or "#FFFFFF"), ctx.Parent, "Pick color")
        if c.isValid():
            ctx.Commit(c.name().upper())


class VectorInput(FieldInput):
    Pill = False

    def __init__(self, Name, Dimensions=3, DefaultValue=None, Label=None, **kw):
        self.Dimensions = Dimensions
        super().__init__(Name, tuple(DefaultValue) if DefaultValue is not None else (0.0,) * Dimensions, Label, **kw)

    def Coerce(self, v):
        if isinstance(v, str):
            v = [x for x in v.replace(";", ",").split(",") if x.strip()]
        try:
            vals = [float(x) for x in v]
        except (TypeError, ValueError):
            vals = list(self.DefaultValue)
        vals = (vals + [0.0] * self.Dimensions)[: self.Dimensions]
        return tuple(vals)

    def Serialize(self, v):
        return ",".join(repr(round(float(x), 6)) for x in v)

    def Deserialize(self, text):
        return self.Coerce(text)

    def DisplayText(self, v):
        return "(" + ", ".join(f"{float(x):.4g}" for x in (v or ())) + ")"

    def Edit(self, ctx):
        menu = QMenu(ctx.Parent)
        box = QWidget()
        lay = QHBoxLayout(box)
        lay.setContentsMargins(6, 4, 6, 4)
        spins = []
        for i, val in enumerate(ctx.Value or self.DefaultValue):
            lay.addWidget(QLabel("XYZW"[i] if i < 4 else str(i)))
            s = QDoubleSpinBox()
            s.setRange(-1e9, 1e9)
            s.setDecimals(4)
            s.setValue(float(val))
            s.setFixedWidth(84)
            lay.addWidget(s)
            spins.append(s)
        wa = QWidgetAction(menu)
        wa.setDefaultWidget(box)
        menu.addAction(wa)
        menu.aboutToHide.connect(lambda: ctx.Commit(tuple(s.value() for s in spins)))
        menu.exec(ctx.GlobalPos())


# ---------------------------------------------------------------------------------------------
class ValueInput(InputDef):
    """Value socket. ``Accepts`` is a list of ValueType names. ``Shadow`` (a FieldInput) shows an inline editable
    default while the socket is empty (Scratch-style)."""
    Kind = "value"

    def __init__(self, Name: str, Accepts: Sequence[str] = ("Any",), Label: Optional[str] = None,
                 Shadow: Optional[FieldInput] = None, Tooltip: str = "", Required: bool = False):
        super().__init__(Name, Label, Tooltip, Required)
        self.Accepts = list(Accepts)
        self.Shadow = Shadow
        self.Boolean = False


class BooleanInput(ValueInput):
    def __init__(self, Name: str, Label: Optional[str] = None, Tooltip: str = "", Required: bool = False):
        super().__init__(Name, ("Boolean",), Label, None, Tooltip, Required)
        self.Boolean = True


def NumberSocket(Name, DefaultValue=0.0, Label=None, **kw) -> ValueInput:
    return ValueInput(Name, ("Number",), Label, FloatInput(Name, DefaultValue), **kw)


def StringSocket(Name, DefaultValue="", Label=None, **kw) -> ValueInput:
    return ValueInput(Name, ("Any",), Label, StringInput(Name, DefaultValue), **kw)


class StatementInput(InputDef):
    Kind = "statement"

    def __init__(self, Name: str, Label: Optional[str] = None, Tooltip: str = ""):
        super().__init__(Name, Label, Tooltip)


class RawFieldInput(FieldInput):
    """Read-only field used by UnknownBlock to display preserved raw data."""
    def OnClick(self, ctx):
        pass

    def Edit(self, ctx):
        pass
