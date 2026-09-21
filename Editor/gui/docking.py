"""Reusable editor-style docking widgets for PySide6.

The public surface is intentionally small: :class:`DockingSystem`,
:class:`DockPanel`, and :class:`Theme`.  Layout state contains panel identifiers,
never QWidget objects, so it is safe to store as JSON.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterable, Literal, TypeAlias
from uuid import uuid4

from PySide6.QtCore import (
    QByteArray,
    QEvent,
    QMimeData,
    QPoint,
    QRect,
    QSignalBlocker,
    QSize,
    Qt,
    QTimer,
    Signal,
)
from PySide6.QtGui import (
    QColor,
    QCursor,
    QDrag,
    QIcon,
    QKeyEvent,
    QMouseEvent,
    QPainter,
    QPainterPath,
    QPen,
    QPixmap,
)
from PySide6.QtWidgets import (
    QApplication,
    QFrame,
    QLabel,
    QMenu,
    QSplitter,
    QTabBar,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

# Register the compiled SVG resources. Package imports are canonical; the
# fallback keeps this reusable module runnable directly during widget work.
try:
    from . import docking_resources_rc as _docking_resources_rc
except ImportError:
    import docking_resources_rc as _docking_resources_rc  # type: ignore[no-redef]

from ..theme import ApplyWidgetTheme, BuildPalette, BuildStyleSheet, Theme
from ..localization import LocalizationManager
from ..resources import ResourceManager

DockArea = Literal["left", "right", "top", "bottom", "center"]
VALID_AREAS = {"left", "right", "top", "bottom", "center"}
MIME_TYPE = "application/x-veniera-dock-panel"
LOCK_ICON_PATH = ":/docking/lock.svg"
CHECK_ICON_PATH = ":/docking/check.svg"


def _small_lock_icon() -> QIcon:
    """Render the lock smaller while retaining a standard-sized tab icon slot."""
    result = QIcon()
    source = QIcon(LOCK_ICON_PATH)
    canvas_size, glyph_size = 16, 10
    # Common DPR variants keep the inset SVG crisp without changing the size of
    # ordinary panel icons in the same tab group.
    for scale in (1, 2, 3):
        canvas = QPixmap(canvas_size * scale, canvas_size * scale)
        canvas.fill(Qt.GlobalColor.transparent)
        glyph = source.pixmap(glyph_size * scale, glyph_size * scale)
        offset = (canvas_size - glyph_size) * scale // 2
        painter = QPainter(canvas)
        painter.drawPixmap(offset, offset, glyph)
        painter.end()
        canvas.setDevicePixelRatio(scale)
        result.addPixmap(canvas)
    return result


@dataclass
class TabNode:
    """A leaf in the layout tree containing one or more panel identifiers."""

    panels: list[str] = field(default_factory=list)
    current: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {"type": "tabs", "panels": list(self.panels), "current": self.current}


@dataclass
class SplitNode:
    """A branch in the layout tree, represented by a Qt splitter."""

    orientation: Literal["horizontal", "vertical"]
    children: list["LayoutNode"] = field(default_factory=list)
    sizes: list[int] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "split",
            "orientation": self.orientation,
            "children": [child.to_dict() for child in self.children],
            "sizes": list(self.sizes),
        }


LayoutNode: TypeAlias = TabNode | SplitNode


def node_from_dict(
    data: dict[str, Any], valid_ids: set[str] | None = None, _depth: int = 0
) -> LayoutNode | None:
    """Create and sanitize a tree node from JSON-compatible data."""
    if not isinstance(data, dict) or _depth > 64:
        return None
    if data.get("type") == "tabs":
        raw = data.get("panels", [])
        if not isinstance(raw, list):
            return None
        panels = [
            value
            for value in raw[:4096]
            if isinstance(value, str) and (valid_ids is None or value in valid_ids)
        ]
        panels = list(dict.fromkeys(panels))
        if not panels:
            return None
        current = max(0, min(int(data.get("current", 0)), len(panels) - 1))
        return TabNode(panels, current)
    if data.get("type") == "split" and data.get("orientation") in {
        "horizontal",
        "vertical",
    }:
        raw_children = data.get("children", [])
        if not isinstance(raw_children, list):
            return None
        children = [
            node_from_dict(item, valid_ids, _depth + 1)
            for item in raw_children[:256]
        ]
        clean = [child for child in children if child is not None]
        if not clean:
            return None
        if len(clean) == 1:
            return clean[0]
        sizes = [
            max(1, int(value))
            for value in data.get("sizes", [])
            if isinstance(value, (int, float))
        ]
        if len(sizes) != len(clean):
            sizes = [1] * len(clean)
        return SplitNode(data["orientation"], clean, sizes)
    return None


def iter_panel_ids(node: LayoutNode | None) -> Iterable[str]:
    """Yield panel identifiers in visual tree order."""
    if node is None:
        return
    if isinstance(node, TabNode):
        yield from node.panels
    else:
        for child in node.children:
            yield from iter_panel_ids(child)


def find_tab(node: LayoutNode | None, panel_id: str) -> TabNode | None:
    """Find the tab leaf containing *panel_id*."""
    if node is None:
        return None
    if isinstance(node, TabNode):
        return node if panel_id in node.panels else None
    for child in node.children:
        found = find_tab(child, panel_id)
        if found is not None:
            return found
    return None


def remove_panel_from_tree(node: LayoutNode | None, panel_id: str) -> LayoutNode | None:
    """Remove a panel and recursively collapse empty/redundant splitters.

    This is the central layout-tree invariant: every split has at least two
    children and every tab node has at least one panel.  Repeated docking and
    closing therefore cannot accumulate dead containers.
    """
    if node is None:
        return None
    if isinstance(node, TabNode):
        if panel_id not in node.panels:
            return node
        old_current_id = node.panels[node.current] if node.panels else None
        node.panels.remove(panel_id)
        if not node.panels:
            return None
        if old_current_id in node.panels:
            node.current = node.panels.index(old_current_id)
        else:
            node.current = min(node.current, len(node.panels) - 1)
        return node
    node.children = [
        clean
        for child in node.children
        if (clean := remove_panel_from_tree(child, panel_id)) is not None
    ]
    if not node.children:
        return None
    if len(node.children) == 1:
        return node.children[0]
    if len(node.sizes) != len(node.children):
        node.sizes = [1] * len(node.children)
    return node


def replace_node(
    root: LayoutNode, target: LayoutNode, replacement: LayoutNode
) -> LayoutNode:
    """Replace *target* by identity and return the possibly-new root."""
    if root is target:
        return replacement
    if isinstance(root, SplitNode):
        for index, child in enumerate(root.children):
            if child is target:
                root.children[index] = replacement
                return root
            updated = replace_node(child, target, replacement)
            if updated is not child:
                root.children[index] = updated
                return root
    return root


def insert_relative(
    root: LayoutNode | None, panel_id: str, area: DockArea, target_id: str | None = None
) -> LayoutNode:
    """Insert a panel as a tab or directional split relative to a leaf/root."""
    new_leaf = TabNode([panel_id])
    if root is None:
        return new_leaf
    target = find_tab(root, target_id) if target_id else None
    if area == "center":
        if target is None:
            target = next((leaf for leaf in walk_tabs(root)), None)
        if target is None:
            return new_leaf
        if panel_id not in target.panels:
            target.panels.append(panel_id)
        target.current = target.panels.index(panel_id)
        return root
    anchor: LayoutNode = target or root
    orientation: Literal["horizontal", "vertical"] = (
        "horizontal" if area in {"left", "right"} else "vertical"
    )
    children: list[LayoutNode] = (
        [new_leaf, anchor] if area in {"left", "top"} else [anchor, new_leaf]
    )
    return replace_node(root, anchor, SplitNode(orientation, children, [1, 1]))


def walk_tabs(node: LayoutNode | None) -> Iterable[TabNode]:
    """Yield all tab leaves in a layout tree."""
    if node is None:
        return
    if isinstance(node, TabNode):
        yield node
    else:
        for child in node.children:
            yield from walk_tabs(child)


def deduplicate_tree(
    node: LayoutNode | None, seen: set[str] | None = None
) -> LayoutNode | None:
    """Keep the first occurrence of each panel and restore tree invariants."""
    if node is None:
        return None
    seen = seen if seen is not None else set()
    if isinstance(node, TabNode):
        unique: list[str] = []
        for value in node.panels:
            if value not in seen:
                seen.add(value)
                unique.append(value)
        node.panels = unique
        if not node.panels:
            return None
        node.current = min(node.current, len(node.panels) - 1)
        return node
    node.children = [
        clean
        for child in node.children
        if (clean := deduplicate_tree(child, seen)) is not None
    ]
    if not node.children:
        return None
    if len(node.children) == 1:
        return node.children[0]
    if len(node.sizes) != len(node.children):
        node.sizes = [1] * len(node.children)
    return node


class DockPanel(QFrame):
    """Stable handle and chrome container for host-provided QWidget content."""

    def __init__(
        self,
        system: "DockingSystem",
        widget: QWidget,
        title: str,
        icon: QIcon,
        panel_id: str,
        closable: bool,
    ) -> None:
        super().__init__()
        self.system = system
        self.widget = widget
        self.title = title
        self.icon = icon
        self.panel_id = panel_id
        self.closable = closable
        self.pinned = False
        self.setObjectName("DockPanel")
        self.setMinimumSize(
            system.theme.minimum_panel_size, system.theme.minimum_panel_size
        )
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        widget.setParent(self)
        layout.addWidget(widget)

    def __repr__(self) -> str:
        return f"DockPanel(id={self.panel_id!r}, title={self.title!r})"


class _DockTabBar(QTabBar):
    def __init__(self, group: "_DockGroup") -> None:
        super().__init__(group)
        self.group = group
        self._press_pos = QPoint()
        self.setMovable(True)
        self.setExpanding(False)
        self.setUsesScrollButtons(True)
        self.setElideMode(Qt.TextElideMode.ElideRight)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._menu)
        self.tabBarDoubleClicked.connect(self._double_click)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._press_pos = event.position().toPoint()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if event.buttons() & Qt.MouseButton.LeftButton:
            distance = (event.position().toPoint() - self._press_pos).manhattanLength()
            if distance >= QApplication.startDragDistance():
                # Let QTabBar perform its native reorder while the pointer stays
                # on the strip; crossing its bounds turns the gesture into dock DnD.
                if self.rect().contains(event.position().toPoint()):
                    super().mouseMoveEvent(event)
                    return
                index = self.tabAt(self._press_pos)
                if index >= 0:
                    panel_id = self.group.node.panels[index]
                    if self.group.system.panel(panel_id).pinned:
                        return
                    self.group.system._start_drag(panel_id, self)
                    return
        super().mouseMoveEvent(event)

    def _double_click(self, index: int) -> None:
        if index < 0 or not self.group.system.double_click_float:
            return
        panel_id = self.group.node.panels[index]
        if self.group.system.panel(panel_id).pinned:
            return
        if self.group.system._floating_for(panel_id) is None:
            self.group.system.float_panel(panel_id)
        else:
            self.group.system.dock(panel_id, "center")

    def _menu(self, pos: QPoint) -> None:
        index = self.tabAt(pos)
        if index < 0:
            return
        panel_id = self.group.node.panels[index]
        system = self.group.system
        menu = QMenu(self)
        panel = system.panel(panel_id)
        tr = system.localization.Translate
        close = menu.addAction(tr("dock.close"))
        close.setEnabled(panel.closable and not panel.pinned)
        close.triggered.connect(lambda: system.close_panel(panel_id))
        close_others = menu.addAction(tr("dock.close_others"))
        close_others.setEnabled(
            any(
                system.panel(value).closable and not system.panel(value).pinned
                for value in self.group.node.panels
                if value != panel_id
            )
        )
        close_others.triggered.connect(
            lambda: system._close_group(panel_id, others=True)
        )
        close_all = menu.addAction(tr("dock.close_all"))
        close_all.setEnabled(
            any(
                system.panel(value).closable and not system.panel(value).pinned
                for value in self.group.node.panels
            )
        )
        close_all.triggered.connect(lambda: system._close_group(panel_id, others=False))
        menu.addSeparator()
        pin = menu.addAction(tr("dock.pin"))
        pin.setCheckable(True)
        pin.setChecked(panel.pinned)
        pin.triggered.connect(lambda checked: system.pin_panel(panel_id, checked))
        float_action = menu.addAction(tr("dock.float"))
        float_action.setEnabled(not panel.pinned)
        float_action.triggered.connect(lambda: system.float_panel(panel_id))
        split_menu = menu.addMenu(tr("dock.split"))
        split_menu.setEnabled(not panel.pinned)
        split_menu.menuAction().setEnabled(not panel.pinned)
        for area in ("left", "right", "top", "bottom"):
            action = split_menu.addAction(tr(f"dock.{area}"))
            action.triggered.connect(
                lambda checked=False, a=area: system.dock(
                    panel_id, a, system._neighbor_panel(panel_id)
                )
            )
        menu.exec(self.mapToGlobal(pos))


class _DockGroup(QTabWidget):
    def __init__(self, system: "DockingSystem", node: TabNode, host: QWidget) -> None:
        super().__init__()
        self.system, self.node, self.host = system, node, host
        self.setObjectName("DockGroup")
        self.setDocumentMode(True)
        self.setTabsClosable(True)
        self.setTabBar(_DockTabBar(self))
        self.tabCloseRequested.connect(self._close)
        self.currentChanged.connect(self._activated)
        self.tabBar().tabMoved.connect(self._moved)
        for panel_id in node.panels:
            panel = system.panel(panel_id)
            tab_icon = _small_lock_icon() if panel.pinned else panel.icon
            index = self.addTab(panel, tab_icon, panel.title)
            self.setTabToolTip(index,
                system.localization.Translate("dock.pinned_tooltip", title=panel.title)
                if panel.pinned else panel.title)
            can_close = panel.closable and not panel.pinned
            self.tabBar().setTabButton(
                index,
                QTabBar.ButtonPosition.RightSide,
                (
                    self.tabBar().tabButton(index, QTabBar.ButtonPosition.RightSide)
                    if can_close
                    else None
                ),
            )
        self.setCurrentIndex(max(0, min(node.current, self.count() - 1)))
        system._groups.append(self)

    def _close(self, index: int) -> None:
        if 0 <= index < len(self.node.panels):
            self.system.close_panel(self.node.panels[index])

    def _activated(self, index: int) -> None:
        if 0 <= index < len(self.node.panels):
            self.node.current = index
            self.system.panel_activated.emit(self.system.panel(self.node.panels[index]))

    def _moved(self, old: int, new: int) -> None:
        if old == new or not (
            0 <= old < len(self.node.panels) and 0 <= new < len(self.node.panels)
        ):
            return
        locked = [
            index
            for index, value in enumerate(self.node.panels)
            if self.system.panel(value).pinned
        ]
        crosses_locked = any(
            min(old, new) <= index <= max(old, new) for index in locked
        )
        if self.system.panel(self.node.panels[old]).pinned or crosses_locked:
            blocker = QSignalBlocker(self.tabBar())
            self.tabBar().moveTab(new, old)
            del blocker
            return
        panel_id = self.node.panels.pop(old)
        self.node.panels.insert(new, panel_id)
        self.node.current = self.currentIndex()
        self.system.layout_changed.emit()


class _EmptyState(QFrame):
    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__()
        self.setObjectName("DockEmpty")
        layout = QVBoxLayout(self)
        label = QLabel(localization.Translate("dock.empty"))
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setObjectName("DockEmptyLabel")
        layout.addWidget(label)


class _DropOverlay(QWidget):
    """Vector overlay; hit regions and preview use the same calculated area."""

    def __init__(self, parent: QWidget, theme: Theme) -> None:
        super().__init__(parent)
        self.theme = theme
        self.area: DockArea = "center"
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.hide()

    def set_area(self, area: DockArea) -> None:
        if self.area != area:
            self.area = area
            self.update()

    def preview_rect(self) -> QRect:
        rect = self.rect().adjusted(2, 2, -2, -2)
        if self.area == "center":
            return rect
        if self.area == "left":
            rect.setWidth(rect.width() // 2)
        elif self.area == "right":
            rect.setLeft(rect.center().x())
        elif self.area == "top":
            rect.setHeight(rect.height() // 2)
        else:
            rect.setTop(rect.center().y())
        return rect

    def paintEvent(self, event: Any) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        accent = QColor(self.theme.accent)
        fill = QColor(accent)
        fill.setAlpha(max(30, self.theme.overlay_opacity // 3))
        painter.setPen(QPen(accent, 2))
        painter.setBrush(fill)
        painter.drawRoundedRect(
            self.preview_rect(), self.theme.radius, self.theme.radius
        )

        # Five SVG-equivalent vector tiles form the familiar docking cross. The
        # geometry below also drives hit testing, keeping feedback and drop exact.
        tile = max(22, min(38, min(self.width(), self.height()) // 6))
        center = self.rect().center()
        offsets = {
            "center": (0, 0),
            "left": (-tile, 0),
            "right": (tile, 0),
            "top": (0, -tile),
            "bottom": (0, tile),
        }
        for name, (dx, dy) in offsets.items():
            box = QRect(
                center.x() + dx - tile // 2, center.y() + dy - tile // 2, tile, tile
            )
            color = QColor(
                self.theme.accent if name == self.area else self.theme.surface_alt
            )
            color.setAlpha(self.theme.overlay_opacity if name == self.area else 220)
            painter.setBrush(color)
            painter.setPen(QPen(QColor(self.theme.border), 1))
            painter.drawRoundedRect(box, 3, 3)
            painter.setPen(QPen(QColor(self.theme.text), 2))
            c, inset = box.center(), max(6, tile // 4)
            path = QPainterPath()
            if name == "left":
                path.moveTo(c.x() + 3, c.y() - inset)
                path.lineTo(c.x() - 4, c.y())
                path.lineTo(c.x() + 3, c.y() + inset)
            elif name == "right":
                path.moveTo(c.x() - 3, c.y() - inset)
                path.lineTo(c.x() + 4, c.y())
                path.lineTo(c.x() - 3, c.y() + inset)
            elif name == "top":
                path.moveTo(c.x() - inset, c.y() + 3)
                path.lineTo(c.x(), c.y() - 4)
                path.lineTo(c.x() + inset, c.y() + 3)
            elif name == "bottom":
                path.moveTo(c.x() - inset, c.y() - 3)
                path.lineTo(c.x(), c.y() + 4)
                path.lineTo(c.x() + inset, c.y() - 3)
            else:
                painter.drawRect(box.adjusted(inset, inset, -inset, -inset))
                continue
            painter.drawPath(path)


@dataclass
class _FloatingState:
    root: LayoutNode
    window: "_FloatingWindow | None" = None
    geometry: list[int] = field(default_factory=lambda: [100, 100, 480, 320])


class _FloatingWindow(QWidget):
    def __init__(self, system: "DockingSystem", state: _FloatingState) -> None:
        flags = Qt.WindowType.Window
        if system.floating_always_on_top:
            flags |= Qt.WindowType.WindowStaysOnTopHint
        # An owned native window normally stays out of the taskbar while retaining
        # standard minimize/maximize chrome. An unowned one gets its own taskbar item.
        owner = (
            None
            if system.floating_taskbar
            else (system.window() if system.window() is not system else system)
        )
        super().__init__(owner, flags)
        self.system, self.state = system, state
        if owner is None:
            system.destroyed.connect(self.close)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)
        self.setAcceptDrops(True)
        self.setWindowTitle(system.localization.Translate("dock.floating"))
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self._overlay = _DropOverlay(self, system.theme)

    def closeEvent(self, event: Any) -> None:
        if self.state in self.system._floating and self.state.root is not None:
            self.system._floating_window_closed(self.state)
        event.accept()

    def moveEvent(self, event: Any) -> None:
        super().moveEvent(event)
        self.system._capture_floating_geometry(self.state)

    def resizeEvent(self, event: Any) -> None:
        super().resizeEvent(event)
        self.system._capture_floating_geometry(self.state)

    def dragEnterEvent(self, event: Any) -> None:
        self.system._drag_enter(self, event)

    def dragMoveEvent(self, event: Any) -> None:
        self.system._drag_move(self, event)

    def dragLeaveEvent(self, event: Any) -> None:
        self._overlay.hide()

    def dropEvent(self, event: Any) -> None:
        self.system._drop(self, event)


class DockingSystem(QWidget):
    """A self-contained, serializable editor docking workspace.

    Add arbitrary QWidget instances with :meth:`add_panel`, then move them with
    :meth:`dock`, :meth:`tabify`, and :meth:`float_panel`, or by dragging tabs.
    Multiple instances are fully independent.
    """

    panel_closed = Signal(object)
    panel_docked = Signal(object, str)
    panel_floated = Signal(object)
    layout_changed = Signal()
    panel_activated = Signal(object)

    def __init__(
        self,
        theme: Theme | None = None,
        parent: QWidget | None = None,
        *,
        double_click_float: bool = True,
        floating_taskbar: bool = False,
        floating_always_on_top: bool = False,
        localization: LocalizationManager | None = None,
    ) -> None:
        super().__init__(parent)
        self.theme = theme or Theme.dark()
        self.double_click_float = double_click_float
        self.floating_taskbar = floating_taskbar
        self.floating_always_on_top = floating_always_on_top
        self.localization = localization or LocalizationManager(ResourceManager())
        self.localization.LocaleChanged.connect(lambda _: self._rebuild_views())
        self._panels: dict[str, DockPanel] = {}
        self._root: LayoutNode | None = None
        self._floating: list[_FloatingState] = []
        self._groups: list[_DockGroup] = []
        self._drag_panel_id: str | None = None
        self._drag_cancelled = False
        self._drop_target: tuple[QWidget, TabNode | None, DockArea] | None = None
        self.setObjectName("DockingSystem")
        self.setAcceptDrops(True)
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self._overlay = _DropOverlay(self, self.theme)
        self.set_theme(self.theme)
        self._rebuild_views()

    def add_panel(
        self,
        widget: QWidget,
        title: str,
        icon: QIcon | None = None,
        area: DockArea = "center",
        relative_to: DockPanel | str | None = None,
        closable: bool = True,
        floating: bool = False,
        panel_id: str | None = None,
    ) -> DockPanel:
        """Register *widget* and place it in the layout, returning its panel handle."""
        self._check_area(area)
        identifier = panel_id or widget.objectName() or f"panel-{uuid4().hex}"
        if identifier in self._panels:
            raise ValueError(f"A panel with id {identifier!r} already exists")
        panel = DockPanel(self, widget, title, icon or QIcon(), identifier, closable)
        self._panels[identifier] = panel
        if floating:
            self.float_panel(panel)
        else:
            self.dock(panel, area, relative_to)
        return panel

    def panel(self, panel: DockPanel | str) -> DockPanel:
        """Resolve a panel handle or identifier, raising a useful error if unknown."""
        if isinstance(panel, DockPanel):
            if panel.system is not self:
                raise ValueError("Panel belongs to another DockingSystem")
            return panel
        try:
            return self._panels[panel]
        except KeyError as exc:
            raise KeyError(f"Unknown panel id: {panel!r}") from exc

    def panels(self) -> tuple[DockPanel, ...]:
        """Return all registered panels in registration order."""
        return tuple(self._panels.values())

    def is_panel_open(self, panel: DockPanel | str) -> bool:
        """Return whether a panel exists in a docked or floating layout."""
        return self._is_placed(self.panel(panel).panel_id)

    def set_panel_presentation(
        self, panel: DockPanel | str, *, title: str | None = None,
        icon: QIcon | None = None
    ) -> None:
        """Update a panel's translated title or themed icon in every dock view."""
        item = self.panel(panel)
        if title is not None:
            item.title = title
        if icon is not None:
            item.icon = icon
        self._rebuild_views()

    def dock(
        self,
        panel: DockPanel | str,
        area: DockArea = "center",
        relative_to: DockPanel | str | None = None,
    ) -> None:
        """Dock a panel in the main area or relative to a specific panel."""
        self._check_area(area)
        item = self.panel(panel)
        if item.pinned and self._is_placed(item.panel_id):
            return
        target_id = (
            self.panel(relative_to).panel_id if relative_to is not None else None
        )
        if target_id == item.panel_id:
            target_id = self._neighbor_panel(item.panel_id)
        destination, floating = (
            self._tree_containing(target_id) if target_id else (self._root, None)
        )
        self._remove_from_all(item.panel_id)
        if floating is None:
            self._root = insert_relative(self._root, item.panel_id, area, target_id)
        else:
            floating.root = insert_relative(
                floating.root, item.panel_id, area, target_id
            )
        self._prune_floating()
        self._rebuild_views()
        self.panel_docked.emit(item, area)
        self.layout_changed.emit()

    def tabify(self, panel_a: DockPanel | str, panel_b: DockPanel | str) -> None:
        """Move *panel_a* into the tab group containing *panel_b*."""
        first, second = self.panel(panel_a), self.panel(panel_b)
        if first is second:
            return
        self.dock(first, "center", second)

    def float_panel(
        self, panel: DockPanel | str, position: QPoint | None = None
    ) -> None:
        """Detach a panel into an independently movable native top-level window."""
        item = self.panel(panel)
        if item.pinned:
            return
        self._remove_from_all(item.panel_id)
        pos = position or QCursor.pos() - QPoint(40, 16)
        state = _FloatingState(
            TabNode([item.panel_id]), geometry=[pos.x(), pos.y(), 480, 320]
        )
        self._floating.append(state)
        self._prune_floating()
        self._rebuild_views()
        self.panel_floated.emit(item)
        self.layout_changed.emit()

    def close_panel(self, panel: DockPanel | str) -> None:
        """Remove a closable panel from all layouts while keeping its handle reusable."""
        item = self.panel(panel)
        if not item.closable or item.pinned:
            return
        if not self._is_placed(item.panel_id):
            return
        self._remove_from_all(item.panel_id)
        item.hide()
        self._prune_floating()
        self._rebuild_views()
        self.panel_closed.emit(item)
        self.layout_changed.emit()

    def open_panel(self, panel: DockPanel | str, area: DockArea = "center") -> None:
        """Show a previously closed panel by docking it into the main root."""
        self.dock(panel, area)

    def pin_panel(self, panel: DockPanel | str, pinned: bool = True) -> None:
        """Pin or unpin a panel, locking its tab against close and movement."""
        item = self.panel(panel)
        pinned = bool(pinned)
        if item.pinned == pinned:
            return
        item.pinned = pinned
        self._rebuild_views()
        self.layout_changed.emit()

    def save_layout(self) -> dict[str, Any]:
        """Return a JSON-serializable snapshot of splits, tabs, focus and floats."""
        self._capture_splitter_sizes(self)
        for state in self._floating:
            if state.window:
                self._capture_splitter_sizes(state.window)
                self._capture_floating_geometry(state)
        return {
            "version": 1,
            "root": self._root.to_dict() if self._root else None,
            "pinned": sorted(
                panel.panel_id for panel in self._panels.values() if panel.pinned
            ),
            "floating": [
                {"root": state.root.to_dict(), "geometry": list(state.geometry)}
                for state in self._floating
            ],
        }

    def restore_layout(self, state: dict[str, Any] | bytes | QByteArray) -> bool:
        """Restore a layout; unknown panel ids are ignored and malformed state returns False."""
        import json

        try:
            if isinstance(state, QByteArray):
                state = bytes(state)
            if isinstance(state, bytes):
                if len(state) > 2 * 1024 * 1024:
                    return False
                state = json.loads(state.decode("utf-8"))
            if not isinstance(state, dict) or int(state.get("version", 1)) != 1:
                return False
            valid = set(self._panels)
            pinned_data = state.get("pinned")
            if pinned_data is not None and not isinstance(pinned_data, list):
                return False
            pinned_ids = {
                value
                for value in (pinned_data or [])
                if isinstance(value, str) and value in valid
            }
            root = deduplicate_tree(node_from_dict(state.get("root"), valid))
            used = set(iter_panel_ids(root))
            floats: list[_FloatingState] = []
            floating_data = state.get("floating", [])
            if not isinstance(floating_data, list):
                return False
            for item in floating_data[:128]:
                if not isinstance(item, dict):
                    continue
                tree = deduplicate_tree(node_from_dict(item.get("root"), valid - used))
                if tree is None:
                    continue
                used.update(iter_panel_ids(tree))
                geometry = item.get("geometry", [100, 100, 480, 320])
                if not (isinstance(geometry, list) and len(geometry) == 4):
                    geometry = [100, 100, 480, 320]
                values = [int(v) for v in geometry]
                values[0] = max(-1_000_000, min(1_000_000, values[0]))
                values[1] = max(-1_000_000, min(1_000_000, values[1]))
                values[2] = max(180, min(16_384, values[2]))
                values[3] = max(120, min(16_384, values[3]))
                floats.append(_FloatingState(tree, geometry=values))
            self._dispose_floating_windows()
            if pinned_data is not None:
                for panel in self._panels.values():
                    panel.pinned = panel.panel_id in pinned_ids
            self._root, self._floating = root, floats
            self._rebuild_views()
            self.layout_changed.emit()
            return True
        except (TypeError, ValueError, KeyError, UnicodeDecodeError,
                OverflowError, RecursionError):
            return False

    def set_theme(self, theme: Theme) -> None:
        """Apply a Theme immediately to this system and its floating windows."""
        if not isinstance(theme, Theme):
            raise TypeError("theme must be a Theme instance")
        self.theme = theme
        qss = self._style_sheet(theme)
        palette = self._qt_palette(theme)
        self._apply_theme(self, qss, palette)
        self._overlay.theme = theme
        for panel in self._panels.values():
            panel.setMinimumSize(theme.minimum_panel_size, theme.minimum_panel_size)
        for splitter in self.findChildren(QSplitter):
            splitter.setHandleWidth(theme.splitter_width)
        for state in self._floating:
            if state.window:
                self._apply_theme(state.window, qss, palette)
                state.window._overlay.theme = theme
                for splitter in state.window.findChildren(QSplitter):
                    splitter.setHandleWidth(theme.splitter_width)

    def reset(self) -> None:
        """Close all layouts without unregistering their panel handles."""
        self._dispose_floating_windows()
        self._root = None
        self._floating.clear()
        self._rebuild_views()
        self.layout_changed.emit()

    def dragEnterEvent(self, event: Any) -> None:
        self._drag_enter(self, event)

    def dragMoveEvent(self, event: Any) -> None:
        self._drag_move(self, event)

    def dragLeaveEvent(self, event: Any) -> None:
        self._overlay.hide()

    def dropEvent(self, event: Any) -> None:
        self._drop(self, event)

    def eventFilter(self, watched: Any, event: QEvent) -> bool:
        if (
            self._drag_panel_id
            and event.type() == QEvent.Type.KeyPress
            and isinstance(event, QKeyEvent)
            and event.key() == Qt.Key.Key_Escape
        ):
            self._drag_cancelled = True
        return False

    def _start_drag(self, panel_id: str, source: QWidget) -> None:
        panel = self.panel(panel_id)
        self._drag_panel_id, self._drag_cancelled = panel_id, False
        app = QApplication.instance()
        if app:
            app.installEventFilter(self)
        drag = QDrag(source)
        mime = QMimeData()
        mime.setData(MIME_TYPE, panel_id.encode("utf-8"))
        drag.setMimeData(mime)
        pixmap = panel.grab()
        if not pixmap.isNull():
            translucent = pixmap.scaled(
                QSize(min(360, pixmap.width()), min(240, pixmap.height())),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
            ghost = QPixmap(translucent.size())
            ghost.fill(Qt.GlobalColor.transparent)
            painter = QPainter(ghost)
            painter.setOpacity(0.62)
            painter.drawPixmap(0, 0, translucent)
            painter.end()
            drag.setPixmap(ghost)
            drag.setHotSpot(QPoint(min(40, ghost.width() // 2), 12))
        result = drag.exec(Qt.DropAction.MoveAction)
        if app:
            app.removeEventFilter(self)
        self._hide_overlays()
        should_float = result == Qt.DropAction.IgnoreAction and not self._drag_cancelled
        self._drag_panel_id = None
        if should_float:
            self.float_panel(panel, QCursor.pos())

    def _drag_enter(self, host: QWidget, event: Any) -> None:
        if event.mimeData().hasFormat(MIME_TYPE):
            event.acceptProposedAction()

    def _drag_move(self, host: QWidget, event: Any) -> None:
        if not event.mimeData().hasFormat(MIME_TYPE):
            return
        pos = event.position().toPoint()
        overlay = host._overlay if isinstance(host, _FloatingWindow) else self._overlay
        edge = max(20, min(42, min(host.width(), host.height()) // 12))
        area: DockArea | None = None
        target_node: TabNode | None = None
        target_rect = host.rect()
        if pos.x() < edge:
            area = "left"
        elif pos.x() > host.width() - edge:
            area = "right"
        elif pos.y() < edge:
            area = "top"
        elif pos.y() > host.height() - edge:
            area = "bottom"
        else:
            global_pos = host.mapToGlobal(pos)
            candidates = [
                group
                for group in self._groups
                if group.isVisible()
                and (group is host or host.isAncestorOf(group))
                and QRect(group.mapToGlobal(QPoint()), group.size()).contains(
                    global_pos
                )
            ]
            if candidates:
                group = min(candidates, key=lambda g: g.width() * g.height())
                target_node = group.node
                target_rect = QRect(
                    host.mapFromGlobal(group.mapToGlobal(QPoint())), group.size()
                )
                local = group.mapFromGlobal(global_pos)
                x, y, w, h = (
                    local.x(),
                    local.y(),
                    max(1, group.width()),
                    max(1, group.height()),
                )
                # Local hit testing divides a panel into four triangular-ish edge
                # bands plus center. It remains generous even on tiny panels.
                nx, ny = x / w, y / h
                if nx < 0.25 and abs(ny - 0.5) <= 0.35:
                    area = "left"
                elif nx > 0.75 and abs(ny - 0.5) <= 0.35:
                    area = "right"
                elif ny < 0.25:
                    area = "top"
                elif ny > 0.75:
                    area = "bottom"
                else:
                    area = "center"
        if area is None:
            overlay.hide()
            self._drop_target = None
            return
        overlay.setGeometry(target_rect)
        overlay.set_area(area)
        overlay.show()
        overlay.raise_()
        self._drop_target = (host, target_node, area)
        event.acceptProposedAction()

    def _drop(self, host: QWidget, event: Any) -> None:
        raw = bytes(event.mimeData().data(MIME_TYPE)).decode("utf-8", errors="ignore")
        overlay = host._overlay if isinstance(host, _FloatingWindow) else self._overlay
        overlay.hide()
        if raw not in self._panels or self._drop_target is None:
            event.ignore()
            return
        _, node, area = self._drop_target
        relative = node.panels[node.current] if node and node.panels else None
        if relative == raw and node is not None:
            relative = next((value for value in node.panels if value != raw), None)
            if relative is None:
                self._drop_target = None
                event.setDropAction(Qt.DropAction.MoveAction)
                event.accept()
                return
        if relative:
            self.dock(raw, area, relative)
        elif isinstance(host, _FloatingWindow) and host.state.root:
            anchor = next(iter_panel_ids(host.state.root), None)
            self.dock(raw, area, anchor)
        else:
            self.dock(raw, area)
        self._drop_target = None
        event.setDropAction(Qt.DropAction.MoveAction)
        event.accept()

    def _remove_from_all(self, panel_id: str) -> None:
        self._root = remove_panel_from_tree(self._root, panel_id)
        for state in self._floating:
            state.root = remove_panel_from_tree(state.root, panel_id)  # type: ignore[assignment]

    def _prune_floating(self) -> None:
        alive: list[_FloatingState] = []
        for state in self._floating:
            if state.root is None:
                if state.window:
                    state.window.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)
                    state.window.close()
            else:
                alive.append(state)
        self._floating = alive

    def _floating_window_closed(self, state: _FloatingState) -> None:
        """Handle a native floating-window close without recursive rebuilds."""
        panel_ids = list(iter_panel_ids(state.root))
        if state in self._floating:
            self._floating.remove(state)
        state.root = None  # type: ignore[assignment]
        for panel_id in panel_ids:
            panel = self.panel(panel_id)
            if panel.closable and not panel.pinned:
                panel.hide()
                self.panel_closed.emit(panel)
            else:
                self._root = insert_relative(self._root, panel_id, "center")
                self.panel_docked.emit(panel, "center")
        self._rebuild_views()
        self.layout_changed.emit()

    def _dispose_floating_windows(self) -> None:
        """Close obsolete native windows without treating it as a user close."""
        old = list(self._floating)
        self._floating = []
        for state in old:
            state.root = None  # type: ignore[assignment]
            if state.window:
                state.window.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)
                state.window.close()

    def _tree_containing(
        self, panel_id: str | None
    ) -> tuple[LayoutNode | None, _FloatingState | None]:
        if panel_id and find_tab(self._root, panel_id):
            return self._root, None
        for state in self._floating:
            if panel_id and find_tab(state.root, panel_id):
                return state.root, state
        return self._root, None

    def _floating_for(self, panel_id: str) -> _FloatingState | None:
        return next(
            (state for state in self._floating if find_tab(state.root, panel_id)), None
        )

    def _is_placed(self, panel_id: str) -> bool:
        return (
            find_tab(self._root, panel_id) is not None
            or self._floating_for(panel_id) is not None
        )

    def _neighbor_panel(self, panel_id: str) -> str | None:
        tree, _ = self._tree_containing(panel_id)
        return next(
            (value for value in iter_panel_ids(tree) if value != panel_id), None
        )

    def _close_group(self, panel_id: str, others: bool) -> None:
        tree, _ = self._tree_containing(panel_id)
        tab = find_tab(tree, panel_id)
        if not tab:
            return
        ids = [value for value in tab.panels if (value != panel_id if others else True)]
        for value in ids:
            self.close_panel(value)

    def _rebuild_views(self) -> None:
        # Panels are stable objects owned by the system. Detaching them before
        # deleting container widgets prevents Qt from deleting host content.
        for panel in self._panels.values():
            panel.setParent(self)
            panel.hide()
        self._groups.clear()
        self._clear_layout(self._layout)
        self._layout.addWidget(
            self._build_widget(self._root, self)
            if self._root else _EmptyState(self.localization)
        )
        existing = {id(state): state.window for state in self._floating if state.window}
        for state in self._floating:
            window = existing.get(id(state)) or _FloatingWindow(self, state)
            state.window = window
            self._clear_layout(window._layout)
            window._layout.addWidget(self._build_widget(state.root, window))
            titles = [self.panel(value).title for value in iter_panel_ids(state.root)]
            window.setWindowTitle(
                " - ".join(titles[:3]) or self.localization.Translate("dock.floating")
            )
            x, y, w, h = state.geometry
            window.setGeometry(x, y, max(180, w), max(120, h))
            self._apply_theme(
                window, self._style_sheet(self.theme), self._qt_palette(self.theme)
            )
            window.show()
        self._overlay.raise_()

    def _build_widget(self, node: LayoutNode, host: QWidget) -> QWidget:
        if isinstance(node, TabNode):
            return _DockGroup(self, node, host)
        orientation = (
            Qt.Orientation.Horizontal
            if node.orientation == "horizontal"
            else Qt.Orientation.Vertical
        )
        splitter = QSplitter(orientation)
        splitter.setObjectName("DockSplitter")
        splitter.setHandleWidth(self.theme.splitter_width)
        splitter.setChildrenCollapsible(False)
        splitter.setProperty("layoutNode", node)
        for index, child in enumerate(node.children):
            widget = self._build_widget(child, host)
            widget.setMinimumSize(
                self.theme.minimum_panel_size, self.theme.minimum_panel_size
            )
            splitter.addWidget(widget)
            splitter.setStretchFactor(index, 1)
        if node.sizes and len(node.sizes) == len(node.children):
            QTimer.singleShot(
                0, lambda s=splitter, sizes=list(node.sizes): s.setSizes(sizes)
            )
        splitter.splitterMoved.connect(
            lambda pos, index, s=splitter, n=node: setattr(n, "sizes", s.sizes())
        )
        return splitter

    @staticmethod
    def _clear_layout(layout: QVBoxLayout) -> None:
        while layout.count():
            item = layout.takeAt(0)
            widget = item.widget()
            if widget:
                widget.hide()
                widget.deleteLater()

    def _capture_splitter_sizes(self, root: QWidget) -> None:
        for splitter in root.findChildren(QSplitter):
            node = splitter.property("layoutNode")
            if isinstance(node, SplitNode):
                sizes = splitter.sizes()
                node.sizes = sizes if sum(sizes) > 0 else [1] * splitter.count()

    def _capture_floating_geometry(self, state: _FloatingState) -> None:
        if state.window and state.window.isVisible():
            rect = state.window.geometry()
            state.geometry = [rect.x(), rect.y(), rect.width(), rect.height()]

    def _hide_overlays(self) -> None:
        self._overlay.hide()
        for state in self._floating:
            if state.window:
                state.window._overlay.hide()
        self._drop_target = None

    @staticmethod
    def _check_area(area: str) -> None:
        if area not in VALID_AREAS:
            raise ValueError(f"area must be one of {sorted(VALID_AREAS)}, got {area!r}")

    @staticmethod
    def _style_sheet(t: Theme) -> str:
        return BuildStyleSheet(t)

    @staticmethod
    def _qt_palette(t: Theme):
        return BuildPalette(t)

    @staticmethod
    def _apply_theme(widget: QWidget, qss: str, palette) -> None:
        # Keep this adapter for existing docking call sites; all actual theme
        # construction and application lives in the shared theme module.
        del qss, palette
        system = widget if isinstance(widget, DockingSystem) else getattr(widget, "system", None)
        theme = system.theme if system is not None else Theme.dark()
        ApplyWidgetTheme(widget, theme)


__all__ = ["DockingSystem", "DockPanel", "Theme"]
