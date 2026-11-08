"""BlockWorkspace: the single public entry point. Owns and wires every component."""
from __future__ import annotations
from typing import Callable, Optional

from PySide6.QtCore import Qt, Signal, QTimer, QPointF, QMimeData
from PySide6.QtGui import QAction, QKeySequence, QGuiApplication
from PySide6.QtWidgets import QWidget, QHBoxLayout, QMenu, QInputDialog, QApplication

from .canvas import BlockCanvas
from .codegen import BlockCodeGenerator, CodeTarget
from .commands import BlockCommandStack
from .connections import BlockConnectionManager
from .definition import NEXT, BlockAction
from .model import BlockDocument
from .registry import BlockRegistry
from .selection import BlockSelectionManager
from .serializer import BlockSerializer, ImportReport
from .stdlib import STANDARD_BLOCKS, STANDARD_CATEGORY_ORDER
from .theme import BlockTheme
from .toolbox import BlockToolbox
from .validation import ValidateDocument

CLIP_MIME = "application/x-blockworkspace+xml"


class BlockWorkspace(QWidget):
    BlockAdded = Signal(object)
    BlockRemoved = Signal(object)
    BlockChanged = Signal(object, str)
    SelectionChanged = Signal()
    DocumentChanged = Signal()
    ZoomChanged = Signal(float)
    MenuAboutToShow = Signal(object, object)    # (QMenu, BlockModel | None) -- customize context menus
    ValidationChanged = Signal(object)

    _clipboard = ""

    def __init__(self, parent=None, Theme: Optional[BlockTheme] = None, RegisterStandardBlocks: bool = True,
                 ShowToolbox: bool = True):
        super().__init__(parent)
        self.Theme = Theme or BlockTheme.Light()
        self.CategoryLabel = lambda name:name
        self.Registry = BlockRegistry()
        self.Document = BlockDocument(self.Registry)
        self.Commands: BlockCommandStack = self.Document.Commands
        self.Connections = BlockConnectionManager(self.Document, self.Registry)
        self.Selection = BlockSelectionManager(self.Document)
        self.Serializer = BlockSerializer(self.Document, self.Registry)
        self.CodeGenerator = BlockCodeGenerator(self.Document)
        self._assets: dict = {}
        self._asset_provider: Optional[Callable] = None
        self._entity_provider: Optional[Callable] = None
        self._entities: dict = {}
        self._validators: list = []
        self._ws_actions: list = []
        self._paste_n = 0
        self.Issues: list = []
        self.AutoValidate = True

        self.Canvas = BlockCanvas(self)
        self.Toolbox = BlockToolbox(self)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        lay.addWidget(self.Toolbox)
        lay.addWidget(self.Canvas, 1)
        self.Toolbox.setVisible(ShowToolbox)

        d = self.Document
        d.BlockAdded.connect(self.BlockAdded)
        d.BlockRemoved.connect(self.BlockRemoved)
        d.BlockChanged.connect(self.BlockChanged)
        d.Changed.connect(self.DocumentChanged)
        d.Changed.connect(self._ScheduleValidate)
        self.Selection.Changed.connect(self.SelectionChanged)
        self.Selection.Changed.connect(self._UpdateActions)
        self.Commands.Changed.connect(self._UpdateActions)
        self.Canvas.ZoomChanged.connect(self.ZoomChanged)
        self._vtimer = QTimer(self)
        self._vtimer.setSingleShot(True)
        self._vtimer.setInterval(0)
        self._vtimer.timeout.connect(self._RunValidation)

        self.Actions: dict = {}
        self._BuildActions()
        if RegisterStandardBlocks:
            self.Registry.RegisterMany(STANDARD_BLOCKS)
            for i, n in enumerate(STANDARD_CATEGORY_ORDER):
                self.Registry.AddCategory(n, Order=i)
        self.Toolbox.Refresh()
        self._UpdateActions()

    # ================================================================= registration
    def RegisterBlock(self, block_type):
        self.RegisterBlocks([block_type])

    def RegisterBlocks(self, block_types):
        self.Registry.RegisterMany(block_types)
        self._ResolveUnknown()

    def UnregisterBlock(self, type_id: str):
        self.Registry.Unregister(type_id)

    def RegisterValueType(self, Name, Parent="Any", Color=None):
        return self.Registry.ValueTypes.Register(Name, Parent, Color)

    def AddCategory(self, Name, Color=None, Icon=None, Order=None):
        if Color:
            self.Theme.SetCategoryColor(Name, Color)
        c = self.Registry.AddCategory(Name, Color, Icon, Order)
        self._RefreshColors()
        return c

    def SetCategoryColor(self, Name: str, Color: str):
        self.Theme.SetCategoryColor(Name, Color)
        self._RefreshColors()

    def _RefreshColors(self):
        self.Canvas.SetTheme(self.Theme)
        self.Toolbox.ApplyTheme(self.Theme)

    def SetTheme(self, theme: BlockTheme):
        self.Theme = theme
        self.Canvas.SetTheme(theme)
        self.Toolbox.ApplyTheme(theme)

    def _ResolveUnknown(self):
        d = self.Document
        if any(b.IsUnknown and self.Registry.Contains(b.Type) for b in d.Blocks.values()):
            xml = self.Serializer.ExportWorkspaceXml()
            self.Serializer.ImportWorkspaceXml(xml, Clear=True, Undoable=False)

    # ---- asset / entity providers (used by AssetInput / EntityInput)
    def SetAssetProvider(self, fn: Callable):
        self._asset_provider = fn

    def SetAssets(self, AssetType: str, names: list):
        self._assets[AssetType] = list(names)

    def GetAssetNames(self, AssetType: str) -> list:
        if self._asset_provider is not None:
            return list(self._asset_provider(AssetType) or [])
        return list(self._assets.get(AssetType, []))

    def SetEntityProvider(self, fn: Callable):
        self._entity_provider = fn

    def SetEntities(self, EntityType: str, names: list):
        self._entities[EntityType] = list(names)

    def GetEntityNames(self, EntityType: str) -> list:
        if self._entity_provider is not None:
            return list(self._entity_provider(EntityType) or [])
        return list(self._entities.get(EntityType, []))

    # ================================================================= document API
    def CreateBlock(self, TypeId: str, X: float = 0.0, Y: float = 0.0, Values: Optional[dict] = None):
        return self.Document.CreateBlock(TypeId, X, Y, Values)

    def FindBlock(self, Id: str):
        return self.Document.Find(Id)

    def Blocks(self) -> list:
        return list(self.Document.Blocks.values())

    def Connect(self, Parent, Slot: str, Child) -> bool:
        return self.Connections.Connect(Parent, Slot, Child)

    def CanConnect(self, Parent, Slot: str, Child):
        return self.Connections.CanConnect(Parent, Slot, Child)

    def Disconnect(self, Block):
        self.Document.Disconnect(Block)

    def SetFieldValue(self, Block, Key: str, Value):
        return self.Document.SetFieldValue(Block, Key, Value)

    def AddSection(self, Block, Name: str):
        return self.Document.AddSection(Block, Name)

    def RemoveSection(self, Block, Uid: int):
        return self.Document.RemoveSection(Block, Uid)

    def Clear(self):
        self.Document.Clear()

    # ---- undo / redo
    def Undo(self):
        self.Commands.Undo()

    def Redo(self):
        self.Commands.Redo()

    # ---- serialization / codegen
    def ExportWorkspaceXml(self, IncludeView: bool = False) -> str:
        view = None
        if IncludeView:
            c = self.Canvas.ViewCenterScene()
            view = {"zoom": round(self.Canvas.Zoom, 4), "x": round(c.x(), 2), "y": round(c.y(), 2)}
        return self.Serializer.ExportWorkspaceXml(view)

    def ImportWorkspaceXml(self, Xml: str, Undoable: bool = False) -> ImportReport:
        self.Selection.Clear()
        rep = self.Serializer.ImportWorkspaceXml(Xml, Clear=True, Undoable=Undoable)
        if rep.View:
            try:
                self.Canvas.SetZoom(float(rep.View["zoom"]))
                self.Canvas.centerOn(float(rep.View["x"]), float(rep.View["y"]))
            except (KeyError, ValueError):
                pass
        return rep

    def GenerateCode(self, Target: str = "xml") -> str:
        return self.CodeGenerator.Generate(Target)

    def RegisterCodeTarget(self, target: CodeTarget):
        self.CodeGenerator.RegisterTarget(target)

    def RegisterGenerator(self, Target: str, TypeId: str, fn: Callable):
        self.CodeGenerator.RegisterGenerator(Target, TypeId, fn)

    # ---- validation
    def AddValidator(self, fn: Callable):
        """fn(document) -> list[ValidationIssue]"""
        self._validators.append(fn)

    def AddConnectionValidator(self, fn: Callable):
        self.Connections.AddValidator(fn)

    def Validate(self) -> list:
        return ValidateDocument(self.Document, self._validators)

    def _ScheduleValidate(self):
        if self.AutoValidate:
            self._vtimer.start()

    def _RunValidation(self):
        self.Issues = self.Validate()
        self.Canvas.SetIssues(self.Issues)
        self.ValidationChanged.emit(self.Issues)

    # ---- view
    def ZoomIn(self):
        self.Canvas.ZoomBy(1.2)

    def ZoomOut(self):
        self.Canvas.ZoomBy(1 / 1.2)

    def ResetZoom(self):
        self.Canvas.ResetZoom()

    def ZoomToFit(self):
        self.Canvas.ZoomToFit()

    def SetZoom(self, z: float):
        self.Canvas.SetZoom(z)

    def SetGridVisible(self, v: bool):
        self.Canvas.GridVisible = v
        self.Canvas.viewport().update()

    def SetSnapToGrid(self, v: bool):
        self.Canvas.SnapToGrid = v

    def SetReadOnly(self, v: bool):
        self.Canvas.ReadOnly = v
        self.Toolbox.setEnabled(not v)
        self._UpdateActions()

    def SetToolboxVisible(self, v: bool):
        self.Toolbox.setVisible(v)

    def CenterOnBlock(self, block):
        self.Canvas.CenterOnBlock(block)

    # ================================================================= clipboard operations
    def _CopyXml(self, roots, sel_ids=None, tail=False) -> str:
        pos = {r.Id: self.Document.ScenePos(r) for r in roots}
        if tail:
            pred = None
        elif sel_ids is not None:
            pred = lambda n: n.Id in sel_ids
        else:
            pred = lambda n: False
        return self.Serializer.ExportBlocksXml(roots, pred, pos)

    def Copy(self):
        if not self.Selection.Count():
            return
        roots = self.Selection.CopyRoots()
        xml = self._CopyXml(roots, self.Selection.SelectedIds())
        BlockWorkspace._clipboard = xml
        self._SetSystemClipboard(xml)
        self._paste_n = 0
        self._UpdateActions()

    _mime_keepalive: list = []
    _atexit_registered = False

    def _SetSystemClipboard(self, xml: str):
        md = QMimeData()
        md.setData(CLIP_MIME, xml.encode("utf-8"))
        md.setText(xml)
        cls = BlockWorkspace
        cls._mime_keepalive = [md]          # keep the wrapper alive: Qt's clipboard owns the C++ object
        QGuiApplication.clipboard().setMimeData(md)
        if QGuiApplication.platformName() == "offscreen" and not cls._atexit_registered:
            # headless platform plugin double-frees the mime data at shutdown unless the clipboard is cleared
            import atexit
            atexit.register(lambda: QGuiApplication.instance() and QGuiApplication.clipboard().clear())
            cls._atexit_registered = True

    def Cut(self):
        if self.Canvas.ReadOnly:
            return
        self.Copy()
        self.DeleteSelection()

    def _ClipboardXml(self) -> str:
        md = QGuiApplication.clipboard().mimeData()
        if md is not None and md.hasFormat(CLIP_MIME):
            return bytes(md.data(CLIP_MIME)).decode("utf-8")
        return BlockWorkspace._clipboard

    def Paste(self):
        xml = self._ClipboardXml()
        if not xml or self.Canvas.ReadOnly:
            return
        self._paste_n += 1
        off = 24.0 * self._paste_n
        self._ImportFragment(xml, (off, off), "Paste")

    def _ImportFragment(self, xml, offset, name):
        doc = self.Document
        try:
            with doc.Transaction(name):
                rep = self.Serializer.ImportWorkspaceXml(xml, Clear=False, Undoable=True, NewIds=True, Offset=offset)
        except Exception:
            return None
        self.Selection.Set(rep.Roots)
        return rep

    def Duplicate(self):
        if not self.Selection.Count() or self.Canvas.ReadOnly:
            return
        roots = self.Selection.CopyRoots()
        self._ImportFragment(self._CopyXml(roots, self.Selection.SelectedIds()), (24.0, 24.0), "Duplicate")

    def DuplicateStack(self):
        if not self.Selection.Count():
            return
        roots = self.Selection.CopyRoots()
        self._ImportFragment(self._CopyXml(roots, None, tail=True), (24.0, 24.0), "Duplicate Stack")

    def DeleteSelection(self, Heal: bool = True):
        if self.Canvas.ReadOnly:
            return
        sel = self.Selection.Selected()
        if sel:
            self.Document.RemoveBlocks(sel, Heal=Heal)

    def SelectAll(self):
        self.Selection.SelectAll()

    def AddBlockFromXml(self, xml: str):
        if self.Canvas.ReadOnly:return
        c = self.Canvas.ViewCenterScene()
        self._ImportFragment(xml, (c.x() - 60, c.y() - 20), "Add Block")

    def EditComment(self, block):
        text, ok = QInputDialog.getMultiLineText(self, "Block comment", "Comment:", block.Comment)
        if ok:
            self.Document.SetComment(block, text.strip(), True if text.strip() else False)

    def CleanUp(self):
        if self.Canvas.ReadOnly:return
        doc, cv = self.Document, self.Canvas
        y = 40.0
        with doc.Transaction("Clean Up Blocks"):
            for r in doc.SortedRoots():
                doc._SetPos(r, 40.0, y)
                it = cv.Items.get(r.Id)
                hat = cv.Metrics.Hat if (it is not None and it._hat) else 0
                doc._Touch(r)
                r.Y = y + hat
                y += hat + cv.StackHeight(r) + 40

    def SetAllCollapsed(self, v: bool):
        if self.Canvas.ReadOnly:return
        with self.Document.Transaction("Collapse All" if v else "Expand All"):
            for b in self.Document.Blocks.values():
                if b.Collapsed != v and any(i.Kind == "statement" for i in b.Inputs()) or (b.Collapsed != v and b.Definition.HasSections):
                    self.Document.SetCollapsed(b, v)

    # ================================================================= actions / shortcuts
    def _Act(self, name, text, keys, slot):
        a = QAction(text, self.Canvas)
        a.setShortcuts([QKeySequence(k) for k in keys])
        a.setShortcutContext(Qt.WidgetWithChildrenShortcut)
        a.triggered.connect(lambda _=False: slot())
        self.Canvas.addAction(a)
        self.Actions[name] = a
        return a

    def _BuildActions(self):
        A = self._Act
        A("Undo", "Undo", [], self.Undo)
        A("Redo", "Redo", [], self.Redo)
        A("Copy", "Copy", ["Ctrl+C"], self.Copy)
        A("Cut", "Cut", ["Ctrl+X"], self.Cut)
        A("Paste", "Paste", ["Ctrl+V"], self.Paste)
        A("Duplicate", "Duplicate", ["Ctrl+D"], self.Duplicate)
        A("Delete", "Delete", ["Delete", "Backspace"], self.DeleteSelection)
        A("SelectAll", "Select All Blocks", ["Ctrl+A"], self.SelectAll)
        A("ZoomIn", "Zoom In", ["Ctrl+=", "Ctrl++"], self.ZoomIn)
        A("ZoomOut", "Zoom Out", ["Ctrl+-"], self.ZoomOut)
        A("ZoomReset", "Reset Zoom", ["Ctrl+0"], self.ResetZoom)
        A("ZoomFit", "Zoom to Fit", ["Ctrl+Shift+0"], self.ZoomToFit)
        A("ToggleDisable", "Disable/Enable Block", ["Ctrl+/"], self._ToggleDisableSel)
        A("ToggleCollapse", "Collapse/Expand Block", ["Ctrl+Shift+E"], self._ToggleCollapseSel)
        A("FocusSearch", "Search Blocks", ["Ctrl+F"], self.Toolbox.FocusSearch)

    def _UpdateActions(self, *_):
        ro = self.Canvas.ReadOnly
        has = self.Selection.Count() > 0
        self.Actions["Undo"].setEnabled(self.Commands.CanUndo and not ro)
        self.Actions["Redo"].setEnabled(self.Commands.CanRedo and not ro)
        self.Actions["Copy"].setEnabled(has)
        self.Actions["Cut"].setEnabled(has and not ro)
        self.Actions["Delete"].setEnabled(has and not ro)
        self.Actions["Duplicate"].setEnabled(has and not ro)
        self.Actions["Paste"].setEnabled(bool(self._ClipboardXml()) and not ro)

    def _ToggleDisableSel(self):
        sel = self.Selection.Selected()
        if not sel or self.Canvas.ReadOnly:
            return
        v = not all(b.Disabled for b in sel)
        with self.Document.Transaction("Enable/Disable Blocks"):
            for b in sel:
                self.Document.SetDisabled(b, v)

    def _ToggleCollapseSel(self):
        sel = self.Selection.Selected()
        if not sel or self.Canvas.ReadOnly:
            return
        v = not all(b.Collapsed for b in sel)
        with self.Document.Transaction("Collapse/Expand Blocks"):
            for b in sel:
                self.Document.SetCollapsed(b, v)

    # ================================================================= context menus
    def AddWorkspaceAction(self, Text: str, Callback: Callable):
        """Adds an entry to the workspace (background) context menu. Callback(Workspace)."""
        self._ws_actions.append((Text, Callback))

    def AddSectionActions(self, menu: QMenu, block) -> bool:
        doc = self.Document
        d = block.Definition
        added = False
        for name, sd in d.SectionDefs.items():
            if doc.CanAddSection(block, name):
                a = menu.addAction(f"Add {sd.MenuLabel}")
                a.triggered.connect(lambda _=False, n=name: doc.AddSection(block, n))
                added = True
        for name, sd in d.SectionDefs.items():
            uids = block.SectionUids(name)
            if not uids or not doc.CanRemoveSection(block, name):
                continue
            if len(uids) == 1:
                a = menu.addAction(f"Remove {sd.MenuLabel}")
                a.triggered.connect(lambda _=False, u=uids[0]: doc.RemoveSection(block, u))
            else:
                sub = menu.addMenu(f"Remove {sd.MenuLabel}")
                for i, u in enumerate(uids, 1):
                    a = sub.addAction(f"{sd.MenuLabel} #{i}")
                    a.triggered.connect(lambda _=False, uu=u: doc.RemoveSection(block, uu))
            added = True
        return added

    def _AddBlockAction(self, menu: QMenu, act: BlockAction, block):
        if act.Separator:
            menu.addSeparator()
            return
        if act.Submenu:
            sub = menu.addMenu(act.Text)
            for s in act.Submenu:
                self._AddBlockAction(sub, s, block)
            return
        a = menu.addAction(act.Text)
        a.setEnabled(act.Enabled)
        if act.Checked is not None:
            a.setCheckable(True)
            a.setChecked(act.Checked)
        if act.Callback:
            a.triggered.connect(lambda _=False, cb=act.Callback: cb(block, self))

    def _CountDeletes(self) -> int:
        ids = set()
        for b in self.Selection.Selected():
            for x in b.Walk(IncludeNext=False):
                ids.add(x.Id)
        return len(ids)

    def BuildBlockMenu(self, block, parent=None) -> QMenu:
        m = QMenu(parent or self.Canvas)
        ro = self.Canvas.ReadOnly
        if not ro:
            had = self.AddSectionActions(m, block)
            custom = block.Definition.Instance.GetContextActions(block)
            for a in custom:
                self._AddBlockAction(m, a, block)
            if had or custom:
                m.addSeparator()
        sel = self.Selection.Selected()
        m.addAction(self.Actions["Duplicate"])
        a = m.addAction("Duplicate Stack")
        a.triggered.connect(lambda _=False: self.DuplicateStack())
        a.setEnabled(not ro)
        if block.Comment:
            m.addAction("Edit Comment…").triggered.connect(lambda _=False: self.EditComment(block))
            m.addAction("Hide Comment" if block.CommentOpen else "Show Comment").triggered.connect(
                lambda _=False: self.Document.SetCommentOpen(block, not block.CommentOpen))
            m.addAction("Remove Comment").triggered.connect(lambda _=False: self.Document.SetComment(block, ""))
        else:
            m.addAction("Add Comment…").triggered.connect(lambda _=False: self.EditComment(block))
        all_collapsed = all(b.Collapsed for b in sel)
        m.addAction("Expand Block" if all_collapsed else "Collapse Block").triggered.connect(lambda _=False: self._ToggleCollapseSel())
        all_dis = all(b.Disabled for b in sel)
        m.addAction("Enable Block" if all_dis else "Disable Block").triggered.connect(lambda _=False: self._ToggleDisableSel())
        m.addSeparator()
        m.addAction(self.Actions["Copy"])
        m.addAction(self.Actions["Cut"])
        n = self._CountDeletes()
        d = m.addAction(f"Delete {n} Block{'s' if n != 1 else ''}")
        d.setEnabled(not ro)
        d.triggered.connect(lambda _=False: self.DeleteSelection())
        if ro:
            for act in m.actions():
                if act.text().startswith(("Add Comment", "Edit Comment", "Remove Comment")):
                    act.setEnabled(False)
        return m

    def BuildWorkspaceMenu(self, scene_pos: QPointF, parent=None) -> QMenu:
        m = QMenu(parent or self.Canvas)
        m.addAction(self.Actions["Undo"])
        m.addAction(self.Actions["Redo"])
        m.addSeparator()
        m.addAction(self.Actions["Paste"])
        m.addAction(self.Actions["SelectAll"])
        m.addSeparator()
        m.addAction("Clean Up Blocks").triggered.connect(lambda _=False: self.CleanUp())
        m.addAction("Collapse All Blocks").triggered.connect(lambda _=False: self.SetAllCollapsed(True))
        m.addAction("Expand All Blocks").triggered.connect(lambda _=False: self.SetAllCollapsed(False))
        m.addSeparator()
        m.addAction(self.Actions["ZoomFit"])
        m.addAction(self.Actions["ZoomReset"])
        g = m.addAction("Show Grid")
        g.setCheckable(True)
        g.setChecked(self.Canvas.GridVisible)
        g.triggered.connect(lambda v: self.SetGridVisible(bool(v)))
        sg = m.addAction("Snap to Grid")
        sg.setCheckable(True)
        sg.setChecked(self.Canvas.SnapToGrid)
        sg.triggered.connect(lambda v: self.SetSnapToGrid(bool(v)))
        if self._ws_actions:
            m.addSeparator()
            for text, cb in self._ws_actions:
                m.addAction(text).triggered.connect(lambda _=False, c=cb: c(self))
        n = len(self.Document.Blocks)
        if n:
            m.addSeparator()
            a = m.addAction(f"Delete All {n} Block{'s' if n != 1 else ''}")
            a.setEnabled(not self.Canvas.ReadOnly)
            a.triggered.connect(lambda _=False: self.Document.Clear())
        return m
