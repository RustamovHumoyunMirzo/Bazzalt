"""Cinematic sequence authoring with transactional preview and document history."""

import copy
import json
import math
import uuid
from pathlib import Path
from time import monotonic
from PySide6.QtCore import Qt, Signal, QTimer, QPointF, QRectF, QSignalBlocker, QSize
from PySide6.QtGui import (
    QColor,
    QPen,
    QPolygonF,
    QUndoStack,
    QUndoCommand,
    QIcon,
    QPainter,
)
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QGraphicsView,
    QGraphicsScene,
    QGraphicsPolygonItem,
    QGraphicsRectItem,
    QGraphicsTextItem,
    QLabel,
    QToolButton,
    QFormLayout,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QApplication,
    QScrollArea,
    QStyle,
    QAbstractItemView,
    QToolBar,
    QSizePolicy,
)
from ..widgets.fields import (
    FloatInput,
    IntInput,
    BoolInput,
    Vec3Input,
    Vec4Input,
    AssetPickerInput,
    StringInput,
)
from ..widgets.menu_bar import EditorMenu
from ... import timeline as Data


class Change(QUndoCommand):
    def __init__(self, panel, before, after):
        super().__init__(panel.Tr("edit"))
        self.Panel = panel
        self.Before = before
        self.After = after

    def undo(self):
        self.Panel.Apply(copy.deepcopy(self.Before))

    def redo(self):
        self.Panel.Apply(copy.deepcopy(self.After))


class TimelineView(QGraphicsView):
    Seek = Signal(float)

    def __init__(self, panel):
        super().__init__()
        self.Panel = panel
        self.Zoom = 80.0
        self.Time = 0.0
        self.Scrubbing = False
        self.Before = None
        self.setScene(QGraphicsScene(self))
        self.setDragMode(self.DragMode.RubberBandDrag)
        self.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft)
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(panel.Menu)

    def drawBackground(self, painter, rect):
        theme = self.Panel.Themes.GetTheme()
        painter.fillRect(rect, QColor(theme.input_background))
        painter.setPen(QPen(QColor(theme.border)))
        for row in range(max(0, int(rect.top() / 28)), int(rect.bottom() / 28) + 2):
            painter.drawLine(
                QPointF(rect.left(), row * 28), QPointF(rect.right(), row * 28)
            )
        step = 10 ** math.floor(math.log10(max(0.01, 80 / self.Zoom)))
        if step * self.Zoom < 40:
            step *= 5
        start = max(0, math.floor(rect.left() / self.Zoom / step))
        for index in range(start, int(rect.right() / self.Zoom / step) + 2):
            time = index * step
            x = time * self.Zoom
            painter.setPen(QPen(QColor(theme.border)))
            painter.drawLine(QPointF(x, 0), QPointF(x, rect.bottom()))
            painter.setPen(QColor(theme.text_muted))
            painter.drawText(QPointF(x + 4, 18), f"{time:g}s")

    def drawForeground(self, painter, rect):
        painter.setPen(QPen(QColor(self.Panel.Themes.GetTheme().accent), 2))
        painter.drawLine(
            QPointF(self.Time * self.Zoom, rect.top()),
            QPointF(self.Time * self.Zoom, rect.bottom()),
        )

    def mousePressEvent(self, event):
        point = self.mapToScene(event.position().toPoint())
        self.Scrubbing = event.button() == Qt.MouseButton.LeftButton and point.y() < 28
        if self.Scrubbing:
            self.Seek.emit(point.x() / self.Zoom)
            event.accept()
            return
        self.Before = copy.deepcopy(self.Panel.Document)
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self.Scrubbing:
            self.Seek.emit(self.mapToScene(event.position().toPoint()).x() / self.Zoom)
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self.Scrubbing:
            self.Scrubbing = False
            event.accept()
            return
        super().mouseReleaseEvent(event)
        moves = [
            (item.data(0), item.pos().x() / self.Zoom)
            for item in self.scene().selectedItems()
            if item.data(0)
        ]
        if moves and self.Before:
            before = self.Before
            QTimer.singleShot(
                0, self.Panel, lambda: self.Panel.MoveItems(before, moves)
            )
        self.Before = None

    def wheelEvent(self, event):
        if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            self.Zoom = max(
                8,
                min(2000, self.Zoom * (1.2 if event.angleDelta().y() > 0 else 1 / 1.2)),
            )
            self.Panel.Rebuild()
            event.accept()
            return
        super().wheelEvent(event)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Delete:
            self.Panel.Delete()
            event.accept()
            return
        super().keyPressEvent(event)


class BindingTree(QTreeWidget):
    def __init__(self, panel):
        super().__init__()
        self.Panel = panel
        self.setAcceptDrops(True)
        self.setHeaderHidden(False)

    def dragEnterEvent(self, event):
        if event.mimeData().hasFormat("application/x-bazzalt-entity"):
            event.acceptProposedAction()
        else:
            super().dragEnterEvent(event)

    def dragMoveEvent(self, event):
        self.dragEnterEvent(event)

    def dropEvent(self, event):
        if not event.mimeData().hasFormat("application/x-bazzalt-entity"):
            return
        identity = bytes(event.mimeData().data("application/x-bazzalt-entity")).decode(
            "utf-8"
        )
        row = self.itemAt(event.position().toPoint())
        if row and row.data(0, Qt.ItemDataRole.UserRole)[0] == "track":
            self.Panel.Rebind(row.data(0, Qt.ItemDataRole.UserRole)[1], identity)
        else:
            self.Panel.AddTransform(identity)
        event.acceptProposedAction()


class TimelinePanel(QWidget):
    AssetChanged = Signal(object)

    def __init__(self, runtime, locale, resources, themes):
        super().__init__()
        self.Runtime = runtime
        self.Locale = locale
        self.Resources = resources
        self.Themes = themes
        self.Path = None
        self.Dirty = False
        self.Document = None
        self.Entity = ""
        self.GetAssets = lambda: []
        self.Updating = False
        self.Preview = False
        self.History = QUndoStack(self)
        self.History.setUndoLimit(100)
        self.Timer = QTimer(self)
        self.Timer.setInterval(16)
        self.Timer.timeout.connect(self.Advance)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        toolbar = QToolBar(self)
        toolbar.setMovable(False)
        toolbar.setIconSize(QSize(16, 16))
        layout.addWidget(toolbar)
        self.Buttons = {}
        for key, callback in [
            ("play", self.Toggle),
            ("stop", self.Stop),
            ("previous", lambda: self.Step(-1)),
            ("next", lambda: self.Step(1)),
            ("add_transform", lambda: self.AddTransform()),
            ("add_camera", self.AddCamera),
            ("add_signal", self.AddSignal),
            ("key", self.CaptureKey),
            ("delete", self.Delete),
            ("undo", self.History.undo),
            ("redo", self.History.redo),
        ]:
            button = QToolButton()
            button.clicked.connect(callback)
            toolbar.addWidget(button)
            self.Buttons[key] = button
        self.Loop = BoolInput(False)
        toolbar.addWidget(self.Loop)
        self.Snap = BoolInput(True)
        toolbar.addWidget(self.Snap)
        self.Time = FloatInput(value=0, minimum=0, maximum=86400)
        self.Time.setMaximumWidth(95)
        toolbar.addWidget(self.Time)
        self.Speed = FloatInput(value=1, minimum=0.01, maximum=100)
        self.Speed.setMaximumWidth(70)
        toolbar.addWidget(self.Speed)
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        toolbar.addWidget(spacer)
        settings = QHBoxLayout()
        layout.addLayout(settings)
        self.Title = QLabel()
        settings.addWidget(self.Title, 1)
        self.Duration = FloatInput(value=10, minimum=0.001, maximum=86400)
        self.FPS = IntInput(value=30, minimum=1, maximum=240)
        self.RangeIn = FloatInput(value=0, minimum=0, maximum=86400)
        self.RangeOut = FloatInput(value=10, minimum=0, maximum=86400)
        self.SettingsLabels = []
        for key, field in [
            ("duration", self.Duration),
            ("fps", self.FPS),
            ("in", self.RangeIn),
            ("out", self.RangeOut),
        ]:
            label = QLabel()
            self.SettingsLabels.append((label, key))
            settings.addWidget(label)
            field.setMinimumWidth(0)
            field.setMaximumWidth(65)
            settings.addWidget(field)
        split = QSplitter()
        layout.addWidget(split, 1)
        self.Tracks = BindingTree(self)
        self.Tracks.setColumnCount(2)
        self.Tracks.setHeaderLabels(["", ""])
        self.Tracks.header().setFixedHeight(28)
        self.Tracks.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
        split.addWidget(self.Tracks)
        self.View = TimelineView(self)
        split.addWidget(self.View)
        split.setSizes([250, 750])
        self.Scene = self.View.scene()
        self.Tracks.verticalScrollBar().valueChanged.connect(
            self.View.verticalScrollBar().setValue
        )
        self.View.verticalScrollBar().valueChanged.connect(
            self.Tracks.verticalScrollBar().setValue
        )
        self.Properties = QWidget()
        self.Form = QFormLayout(self.Properties)
        self.Form.setContentsMargins(4, 4, 4, 4)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.Properties)
        scroll.setMaximumHeight(180)
        layout.addWidget(scroll)
        self.Error = QLabel()
        self.Error.setWordWrap(True)
        layout.addWidget(self.Error)
        self.View.Seek.connect(self.Seek)
        self.Time.valueChanged.connect(self.Seek)
        self.Duration.valueChanged.connect(
            lambda value: self.Edit(lambda doc: doc.update(duration=value))
        )
        self.FPS.valueChanged.connect(
            lambda value: self.Edit(lambda doc: doc.update(frameRate=value))
        )
        self.Scene.selectionChanged.connect(self.Inspect)
        self.Tracks.itemDoubleClicked.connect(self.EditBinding)
        self.Tracks.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.Tracks.customContextMenuRequested.connect(self.TrackMenu)
        self.Themes.ThemeChanged.connect(
            lambda *_: (self.Rebuild(), self.Retranslate())
        )
        self.Locale.LocaleChanged.connect(self.Retranslate)
        if hasattr(runtime, "ProjectChanging"):
            runtime.ProjectChanging.connect(self.ResetDocument)
        if hasattr(runtime, "SceneChanged"):
            runtime.SceneChanged.connect(self.EndPreview)
        self.Retranslate()

    def Tr(self, key):
        return self.Locale.Translate("timeline." + key)

    def Retranslate(self, *_):
        icons = {
            "play": QStyle.StandardPixmap.SP_MediaPlay,
            "stop": QStyle.StandardPixmap.SP_MediaStop,
            "previous": QStyle.StandardPixmap.SP_MediaSeekBackward,
            "next": QStyle.StandardPixmap.SP_MediaSeekForward,
            "delete": QStyle.StandardPixmap.SP_TrashIcon,
            "undo": QStyle.StandardPixmap.SP_ArrowBack,
            "redo": QStyle.StandardPixmap.SP_ArrowForward,
            "add_transform": QStyle.StandardPixmap.SP_FileDialogNewFolder,
            "add_camera": QStyle.StandardPixmap.SP_ComputerIcon,
            "add_signal": QStyle.StandardPixmap.SP_MessageBoxInformation,
            "key": QStyle.StandardPixmap.SP_DialogApplyButton,
        }
        for key, button in self.Buttons.items():
            button.setText(self.Tr(key))
            button.setToolTip(self.Tr(key))
            button.setAccessibleName(self.Tr(key))
            pixmap = self.style().standardIcon(icons[key]).pixmap(16, 16)
            painter = QPainter(pixmap)
            painter.setCompositionMode(
                QPainter.CompositionMode.CompositionMode_SourceIn
            )
            painter.fillRect(pixmap.rect(), QColor(self.Themes.GetTheme().text))
            painter.end()
            button.setIcon(QIcon(pixmap))
        self.Loop.setText(self.Tr("loop"))
        self.Snap.setText(self.Tr("snap"))
        self.Time.setToolTip(self.Tr("time"))
        self.Speed.setToolTip(self.Tr("speed"))
        for label, key in self.SettingsLabels:
            label.setText(self.Tr(key))
        self.Tracks.setHeaderLabels([self.Tr("track"), self.Tr("binding")])
        self.Rebuild()

    def SetEntity(self, identity):
        self.Entity = str(identity or "")

    def Open(self, path):
        if not self.Flush():
            return False
        self.EndPreview()
        try:
            document = Data.Load(path)
            self.Path = Path(path)
            self.Document = document
            self.Dirty = False
            self.Scene.clear()
            self.History.clear()
            for widget, value in (
                (self.Duration, document["duration"]),
                (self.FPS, document["frameRate"]),
                (self.RangeIn, 0),
                (self.RangeOut, document["duration"]),
                (self.Time, 0),
            ):
                blocker = QSignalBlocker(widget)
                widget.setValue(value)
                del blocker
            self.Rebuild()
            self.Error.clear()
            return True
        except (ValueError, OSError, TypeError, KeyError) as error:
            self.Error.setText(str(error))
            return False

    def Flush(self):
        if not self.Path or self.Document is None or not self.Dirty:
            return True
        try:
            if Data.Save(self.Path, self.Document):
                self.AssetChanged.emit(self.Path)
            self.Title.setText(self.Path.stem)
            self.Dirty = False
            return True
        except (ValueError, OSError) as error:
            self.Title.setText(self.Path.stem + "*")
            self.Error.setText(str(error))
            return False

    def EnsureIds(self):
        if self.Document is None:
            return
        seen = set()
        entries = (
            list(self.Document["tracks"])
            + list(self.Document["cameras"])
            + list(self.Document["signals"])
        )
        for track in self.Document["tracks"]:
            entries.extend(track["keys"])
        for entry in entries:
            value = entry.get("editorId")
            try:
                Data.Identity(value)
            except ValueError:
                value = None
            if value is None or value in seen:
                value = str(uuid.uuid4())
            entry["editorId"] = value
            seen.add(value)

    def Apply(self, document):
        self.EndPreview()
        self.Document = document
        self.Dirty = True
        blocker = QSignalBlocker(self.Duration)
        self.Duration.setValue(document["duration"])
        del blocker
        blocker = QSignalBlocker(self.FPS)
        self.FPS.setValue(document["frameRate"])
        del blocker
        self.RangeOut.setMaximum(document["duration"])
        self.RangeIn.setMaximum(document["duration"])
        self.Rebuild()
        self.Flush()

    def Edit(self, operation):
        if self.Updating or self.Document is None:
            return
        before = copy.deepcopy(self.Document)
        after = copy.deepcopy(before)
        try:
            operation(after)
            Data.Validate(after)
        except (ValueError, TypeError, KeyError, IndexError) as error:
            self.Error.setText(str(error))
            self.Rebuild()
            return
        if after != before:
            self.History.push(Change(self, before, after))

    def ResetDocument(self, *_):
        self.Flush()
        self.EndPreview()
        self.Document = None
        self.Path = None
        self.History.clear()
        self.Rebuild()

    def EndPreview(self, *_):
        self.Timer.stop()
        self.Preview = False
        try:
            self.Runtime.EndTimelinePreview()
        except (RuntimeError, ValueError, AttributeError):
            pass

    def Stop(self):
        self.EndPreview()
        blocker = QSignalBlocker(self.Time)
        self.Time.setValue(self.RangeIn.value())
        del blocker
        self.View.Time = self.Time.value()
        self.View.viewport().update()

    def hideEvent(self, event):
        self.EndPreview()
        super().hideEvent(event)

    def Seek(self, time):
        if self.Document is None:
            return
        time = max(0, min(self.Document["duration"], float(time)))
        if self.Snap.isChecked() and not self.Timer.isActive():
            time = Data.Snap(time, self.Document)
        blocker = QSignalBlocker(self.Time)
        self.Time.setValue(time)
        del blocker
        self.View.Time = time
        self.View.viewport().update()
        if getattr(self.Runtime, "IsPlaying", lambda: False)():
            self.EndPreview()
            return
        try:
            self.Runtime.PreviewTimeline(
                Data.Source(self.Document), time, not self.Preview
            )
            self.Preview = True
            self.Error.clear()
        except (RuntimeError, ValueError, AttributeError) as error:
            self.Timer.stop()
            self.Error.setText(str(error))

    def Toggle(self):
        if self.Timer.isActive():
            self.Timer.stop()
            return
        if self.Document is None:
            return
        start, end = self.RangeIn.value(), self.RangeOut.value()
        if start >= end:
            self.Error.setText(self.Tr("invalid_range"))
            return
        if not start <= self.Time.value() < end:
            self.Seek(start)
        self.LastTick = monotonic()
        self.Timer.start()

    def Advance(self):
        now = monotonic()
        delta = min(now - self.LastTick, 0.25) * self.Speed.value()
        self.LastTick = now
        end = min(self.RangeOut.value(), self.Document["duration"])
        start = min(self.RangeIn.value(), end)
        time = self.View.Time + delta
        if time >= end:
            if self.Loop.isChecked() and end > start:
                time = start + (time - start) % (end - start)
            else:
                time = end
                self.Timer.stop()
        self.Seek(time)

    def Step(self, direction):
        self.Timer.stop()
        if self.Document:
            self.Seek(self.View.Time + direction / self.Document["frameRate"])

    def EntityName(self, identity):
        try:
            return str(
                self.Runtime.EntityDetails(identity).get("name")
                or self.Tr("missing_binding")
            )
        except (RuntimeError, ValueError):
            return self.Tr("missing_binding")

    def PickEntity(self, camera=False):
        dialog = QDialog(self)
        dialog.setWindowTitle(self.Tr("binding"))
        layout = QVBoxLayout(dialog)
        search = QLineEdit()
        search.setPlaceholderText(self.Tr("search"))
        layout.addWidget(search)
        items = QListWidget()
        layout.addWidget(items)
        for row in self.Runtime.Entities():
            identity = str(row.get("uuid", ""))
            if not identity or identity == Data.ZERO:
                continue
            details = self.Runtime.EntityDetails(identity)
            if camera and "Camera" not in details.get("components", []):
                continue
            item = QListWidgetItem(str(details.get("name", row.get("name", "Entity"))))
            item.setData(Qt.ItemDataRole.UserRole, identity)
            items.addItem(item)
        search.textChanged.connect(
            lambda text: [
                items.item(i).setHidden(
                    text.casefold() not in items.item(i).text().casefold()
                )
                for i in range(items.count())
            ]
        )
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)
        items.itemDoubleClicked.connect(lambda _: dialog.accept())
        return (
            items.currentItem().data(Qt.ItemDataRole.UserRole)
            if dialog.exec() == QDialog.DialogCode.Accepted and items.currentItem()
            else None
        )

    def AddTransform(self, identity=None):
        if self.Document is None:
            return
        identity = identity or self.Entity or self.PickEntity()
        if not identity:
            return
        try:
            pose = self.Runtime.CaptureTimelinePose(identity, False)
        except (RuntimeError, ValueError) as error:
            self.Error.setText(str(error))
            return
        key = dict(
            pose, time=Data.Snap(self.Time.value(), self.Document), interpolation=1
        )
        self.Edit(
            lambda doc: doc["tracks"].append(
                dict(
                    name=self.EntityName(identity),
                    target=identity,
                    worldSpace=False,
                    muted=False,
                    keys=[key],
                )
            )
        )

    def Rebind(self, index, identity):
        self.Edit(
            lambda doc: doc["tracks"][index].update(
                target=identity, name=self.EntityName(identity)
            )
        )

    def EditBinding(self, item, column):
        kind, index = item.data(0, Qt.ItemDataRole.UserRole)
        if kind == "track":
            identity = self.PickEntity()
            if identity:
                self.Rebind(index, identity)

    def AddCamera(self):
        if self.Document is None:
            return
        identity = self.PickEntity(True)
        if not identity:
            return
        start = Data.Snap(self.Time.value(), self.Document)
        end = min(start + 2, self.Document["duration"])
        self.Edit(
            lambda doc: (
                doc["cameras"].append(
                    dict(
                        name=self.EntityName(identity),
                        camera=identity,
                        start=start,
                        end=end,
                        blendIn=0,
                    )
                ),
                doc["cameras"].sort(key=lambda row: row["start"]),
            )
        )

    def AddSignal(self):
        if self.Document is None:
            return
        self.Edit(
            lambda doc: (
                doc["signals"].append(
                    dict(
                        time=Data.Snap(self.Time.value(), doc),
                        name=self.Tr("signal"),
                        event=Data.ZERO,
                        arguments=[],
                    )
                ),
                doc["signals"].sort(key=lambda row: row["time"]),
            )
        )

    def CaptureKey(self):
        item = self.Tracks.currentItem()
        if not item or self.Document is None:
            return
        kind, index = item.data(0, Qt.ItemDataRole.UserRole)
        if kind != "track":
            return
        track = self.Document["tracks"][index]
        try:
            pose = self.Runtime.CaptureTimelinePose(
                track["target"], track.get("worldSpace", False)
            )
        except (RuntimeError, ValueError) as error:
            self.Error.setText(str(error))
            return
        time = Data.Snap(self.Time.value(), self.Document)

        def insert(doc):
            keys = doc["tracks"][index]["keys"]
            keys[:] = [key for key in keys if abs(key["time"] - time) > 1e-8]
            keys.append(dict(pose, time=time, interpolation=1))
            keys.sort(key=lambda key: key["time"])

        self.Edit(insert)

    def Rebuild(self):
        if not hasattr(self, "View"):
            return
        selected = {item.data(1) for item in self.Scene.selectedItems() if item.data(0)}
        current = self.Tracks.currentItem()
        current_token = (
            current.data(0, Qt.ItemDataRole.UserRole + 1) if current else None
        )
        self.EnsureIds()
        self.Updating = True
        self.Scene.blockSignals(True)
        self.Scene.clear()
        self.Tracks.clear()
        theme = self.Themes.GetTheme()
        document = self.Document
        if document:
            self.Title.setText(self.Path.stem if self.Path else document["name"])
            rows = [
                (
                    track.get("name") or self.EntityName(track["target"]),
                    self.EntityName(track["target"]),
                    ("track", i),
                )
                for i, track in enumerate(document["tracks"])
            ]
            rows.extend(
                [
                    (self.Tr("cameras"), "", ("camera", 0)),
                    (self.Tr("signals"), "", ("signal", 0)),
                ]
            )
            for row, (title, binding, data) in enumerate(rows):
                item = QTreeWidgetItem([title, binding])
                item.setData(0, Qt.ItemDataRole.UserRole, data)
                item.setSizeHint(0, QSize(170, 28))
                token = (
                    document["tracks"][data[1]]["editorId"]
                    if data[0] == "track"
                    else data[0]
                )
                item.setData(0, Qt.ItemDataRole.UserRole + 1, token)
                self.Tracks.addTopLevelItem(item)
                if token == current_token:
                    self.Tracks.setCurrentItem(item)

            def add_item(item, identity, time, row, color):
                item.setData(0, identity)
                value = (
                    document["tracks"][identity[1]]["keys"][identity[2]]
                    if identity[0] == "key"
                    else document["cameras" if identity[0] == "camera" else "signals"][
                        identity[1]
                    ]
                )
                item.setData(1, value["editorId"])
                item.setPos(time * self.View.Zoom, 28 + row * 28 + 14)
                item.setFlags(
                    item.GraphicsItemFlag.ItemIsSelectable
                    | item.GraphicsItemFlag.ItemIsMovable
                )
                item.setBrush(QColor(color))
                item.setPen(QPen(QColor(theme.border)))
                self.Scene.addItem(item)
                if value["editorId"] in selected:
                    item.setSelected(True)

            for row, track in enumerate(document["tracks"]):
                for index, key in enumerate(track["keys"]):
                    item = QGraphicsPolygonItem(
                        QPolygonF(
                            [
                                QPointF(-5, 0),
                                QPointF(0, -6),
                                QPointF(5, 0),
                                QPointF(0, 6),
                            ]
                        )
                    )
                    add_item(
                        item,
                        ("key", row, index),
                        key["time"],
                        row,
                        theme.text_muted if track.get("muted") else theme.accent,
                    )
                    item.setToolTip(f"{track['name']} @ {key['time']:g}s")
            for index, shot in enumerate(document["cameras"]):
                item = QGraphicsRectItem(
                    0, -10, (shot["end"] - shot["start"]) * self.View.Zoom, 20
                )
                add_item(
                    item,
                    ("camera", index),
                    shot["start"],
                    len(document["tracks"]),
                    theme.selection,
                )
                text = QGraphicsTextItem(
                    shot.get("name") or self.EntityName(shot["camera"]), item
                )
                text.setDefaultTextColor(QColor(theme.text))
                text.setPos(3, -13)
                text.setAcceptedMouseButtons(Qt.MouseButton.NoButton)
            for index, signal in enumerate(document["signals"]):
                item = QGraphicsPolygonItem(
                    QPolygonF([QPointF(-6, -7), QPointF(6, -7), QPointF(0, 7)])
                )
                add_item(
                    item,
                    ("signal", index),
                    signal["time"],
                    len(document["tracks"]) + 1,
                    theme.modified,
                )
                item.setToolTip(signal.get("name", ""))
            self.Scene.setSceneRect(
                0, 0, document["duration"] * self.View.Zoom + 60, (len(rows) + 1) * 28
            )
        else:
            self.Title.setText(self.Tr("select_asset"))
            self.Scene.setSceneRect(0, 0, 600, 150)
        self.Scene.blockSignals(False)
        self.Updating = False
        self.View.viewport().update()
        self.Inspect()

    def MoveItems(self, before, moves):
        if self.Document != before:
            self.Rebuild()
            return

        def move(doc):
            for identity, time in moves:
                time = (
                    Data.Snap(time, doc)
                    if self.Snap.isChecked()
                    else max(0, min(doc["duration"], time))
                )
                if identity[0] == "key":
                    doc["tracks"][identity[1]]["keys"][identity[2]]["time"] = time
                elif identity[0] == "signal":
                    doc["signals"][identity[1]]["time"] = time
                else:
                    shot = doc["cameras"][identity[1]]
                    duration = shot["end"] - shot["start"]
                    shot["start"] = min(time, doc["duration"] - duration)
                    shot["end"] = shot["start"] + duration
            for track in doc["tracks"]:
                track["keys"].sort(key=lambda key: key["time"])
            doc["signals"].sort(key=lambda row: row["time"])
            doc["cameras"].sort(key=lambda row: row["start"])

        self.Edit(move)
        self.Rebuild()

    def Selected(self):
        return [item.data(0) for item in self.Scene.selectedItems() if item.data(0)]

    def Delete(self):
        selected = self.Selected()

        def remove(doc):
            for identity in sorted(selected, reverse=True):
                if identity[0] == "key":
                    del doc["tracks"][identity[1]]["keys"][identity[2]]
                elif identity[0] == "camera":
                    del doc["cameras"][identity[1]]
                else:
                    del doc["signals"][identity[1]]
            doc["tracks"][:] = [track for track in doc["tracks"] if track["keys"]]

        self.Edit(remove)

    def Copy(self):
        if not self.Document:
            return
        records = []
        for identity in self.Selected():
            kind, index = identity[:2]
            if kind == "key":
                records.append(
                    dict(
                        kind=kind,
                        target=self.Document["tracks"][index]["target"],
                        value=self.Document["tracks"][index]["keys"][identity[2]],
                    )
                )
            else:
                records.append(
                    dict(
                        kind=kind,
                        value=self.Document[
                            "cameras" if kind == "camera" else "signals"
                        ][index],
                    )
                )
        QApplication.clipboard().setText(
            json.dumps(dict(bazzaltTimeline=1, records=records))
        )

    def Paste(self):
        try:
            text = QApplication.clipboard().text()
            if len(text) > 1024 * 1024:
                return
            payload = json.loads(text)
            if payload.get("bazzaltTimeline") != 1 or not payload.get("records"):
                return
            records = payload["records"]
            if not isinstance(records, list) or len(records) > 65536:
                return
            for record in records:
                Data.Record(record)
                Data.Record(record.get("value"))
                if record.get("kind") not in ("key", "camera", "signal"):
                    raise ValueError("Invalid timeline clipboard record")
            first = min(
                row["value"].get("time", row["value"].get("start", 0))
                for row in records
            )

            def paste(doc):
                for row in records:
                    value = copy.deepcopy(row["value"])
                    value["editorId"] = str(uuid.uuid4())
                    offset = self.Time.value() - first
                    if row["kind"] == "key":
                        track = next(
                            track
                            for track in doc["tracks"]
                            if track["target"] == row["target"]
                        )
                        value["time"] += offset
                        track["keys"].append(value)
                    elif row["kind"] == "camera":
                        value["start"] += offset
                        value["end"] += offset
                        doc["cameras"].append(value)
                    else:
                        value["time"] += offset
                        doc["signals"].append(value)
                for track in doc["tracks"]:
                    track["keys"].sort(key=lambda key: key["time"])
                doc["signals"].sort(key=lambda row: row["time"])
                doc["cameras"].sort(key=lambda row: row["start"])

            self.Edit(paste)
        except (
            ValueError,
            KeyError,
            TypeError,
            StopIteration,
            RecursionError,
        ) as error:
            self.Error.setText(str(error))

    def Duplicate(self):
        self.Copy()
        self.Time.setValue(self.Time.value() + 1 / self.Document["frameRate"])
        self.Paste()

    def Menu(self, point):
        menu = EditorMenu("", self.Themes, self)
        for key, callback in [
            ("key", self.CaptureKey),
            ("add_camera", self.AddCamera),
            ("add_signal", self.AddSignal),
            ("copy", self.Copy),
            ("paste", self.Paste),
            ("delete", self.Delete),
        ]:
            menu.addAction(self.Tr(key)).triggered.connect(callback)
        menu.exec(self.View.viewport().mapToGlobal(point))

    def TrackMenu(self, point):
        menu = EditorMenu("", self.Themes, self)
        item = self.Tracks.itemAt(point)
        menu.addAction(self.Tr("add_transform")).triggered.connect(
            lambda: self.AddTransform()
        )
        if item and self.Document:
            kind, index = item.data(0, Qt.ItemDataRole.UserRole)
            if kind == "track":
                mute = menu.addAction(self.Tr("mute"))
                mute.setCheckable(True)
                mute.setChecked(self.Document["tracks"][index].get("muted", False))
                mute.toggled.connect(
                    lambda value: self.Edit(
                        lambda doc: doc["tracks"][index].update(muted=value)
                    )
                )
                world = menu.addAction(self.Tr("world_space"))
                world.setCheckable(True)
                world.setChecked(
                    self.Document["tracks"][index].get("worldSpace", False)
                )
                world.toggled.connect(
                    lambda value: self.Edit(
                        lambda doc: doc["tracks"][index].update(worldSpace=value)
                    )
                )
                menu.addAction(self.Tr("delete")).triggered.connect(
                    lambda: self.Edit(lambda doc: doc["tracks"].pop(index))
                )
        menu.exec(self.Tracks.viewport().mapToGlobal(point))

    def Inspect(self):
        if self.Updating:
            return
        while self.Form.rowCount():
            self.Form.removeRow(0)
        selected = self.Selected()
        if len(selected) != 1 or not self.Document:
            return
        identity = selected[0]
        kind, index = identity[:2]
        row = (
            self.Document["tracks"][index]["keys"][identity[2]]
            if kind == "key"
            else self.Document["cameras" if kind == "camera" else "signals"][index]
        )

        def commit(key, value):
            def set_value(doc):
                value_row = (
                    doc["tracks"][index]["keys"][identity[2]]
                    if kind == "key"
                    else doc["cameras" if kind == "camera" else "signals"][index]
                )
                value_row[key] = value
                if kind == "key":
                    doc["tracks"][index]["keys"].sort(key=lambda key: key["time"])
                else:
                    doc["cameras" if kind == "camera" else "signals"].sort(
                        key=lambda row: row["start" if kind == "camera" else "time"]
                    )

            self.Edit(set_value)

        for key in ("time", "start", "end", "blendIn"):
            if key in row:
                field = FloatInput(
                    value=row[key], minimum=0, maximum=self.Document["duration"]
                )
                field.editingFinished.connect(
                    lambda key=key, field=field: commit(key, field.value())
                )
                self.Form.addRow(self.Tr(key), field)
        if kind == "key":
            for key, field in [
                ("position", Vec3Input(row["position"])),
                ("rotation", Vec4Input(row["rotation"])),
                ("scale", Vec3Input(row["scale"])),
            ]:
                for axis in field.Inputs:
                    axis.editingFinished.connect(
                        lambda key=key, field=field: commit(key, list(field.GetValue()))
                    )
                self.Form.addRow(self.Tr(key), field)
            mode = QComboBox()
            mode.addItems([self.Tr("step"), self.Tr("linear"), self.Tr("smooth")])
            mode.setCurrentIndex(row.get("interpolation", 1))
            mode.currentIndexChanged.connect(
                lambda value: commit("interpolation", value)
            )
            self.Form.addRow(self.Tr("interpolation"), mode)
        else:
            name = StringInput(row.get("name", ""))
            name.editingFinished.connect(lambda: commit("name", name.GetValue()))
            self.Form.addRow(self.Tr("name"), name)
        if kind == "camera":
            button = QToolButton()
            button.setText(self.EntityName(row["camera"]))

            def choose_camera():
                value = self.PickEntity(True)
                if value:
                    commit("camera", value)

            button.clicked.connect(choose_camera)
            self.Form.addRow(self.Tr("binding"), button)
        if kind == "signal":
            picker = AssetPickerInput(self.Tr("event"), accepted_extensions={".event"})
            picker.AssetResolver = getattr(self.Runtime, "ResolveAssetRecord", None)
            picker.ConfigureAssets(self.GetAssets())
            picker.SetValue(row["event"] if row["event"] != Data.ZERO else "")
            picker.PickRequested.connect(picker.OpenProjectPicker)
            picker.ValueChanged.connect(
                lambda value: commit("event", value or Data.ZERO)
            )
            self.Form.addRow(self.Tr("event"), picker)
            args = StringInput(json.dumps(row.get("arguments", [])))
            args.setToolTip(self.Tr("arguments_hint"))

            def arguments():
                try:
                    commit("arguments", json.loads(args.GetValue()))
                except (ValueError, TypeError) as error:
                    self.Error.setText(str(error))

            args.editingFinished.connect(arguments)
            self.Form.addRow(self.Tr("arguments"), args)
