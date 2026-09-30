"""Top-level BAZZALT editor window."""

from __future__ import annotations

import json
from typing import Callable

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import QApplication, QMainWindow

from ..localization import LocalizationManager
from ..resources import ResourceManager
from ..runtime import RuntimeService
from ..theme import ThemeManager
from .docking import DockingSystem
from .panels import (
    AssetBrowserPanel,
    ConsolePanel,
    HierarchyPanel,
    PropertiesPanel,
    ViewportPanel,
)
from .widgets import EditorMenuBar, EditorToolbar
from .controller import EditorController
from .cursor_policy import EditorCursorPolicy


class Editor(QMainWindow):
    def __init__(
        self,
        theme_manager: ThemeManager | None = None,
        resources: ResourceManager | None = None,
        localization: LocalizationManager | None = None,
        runtime: RuntimeService | None = None,
        settings: dict | None = None,
        settings_saver: Callable[[dict],None] | None = None,
    ) -> None:
        super().__init__()
        self.setObjectName("BazzaltEditor")
        self.resize(1024, 720)

        application = QApplication.instance()
        if application is None:
            raise RuntimeError("Create QApplication before constructing Editor")
        # Native Filament surfaces and Qt text controls use separate Windows
        # cursor paths.  Keep one editor-scoped policy so an I-beam cannot leak
        # from a field into panels, menus, docking tabs, or toolbars.
        self.CursorPolicy = EditorCursorPolicy(self)
        application.installEventFilter(self.CursorPolicy)
        self.ThemeManager = theme_manager or ThemeManager(application)
        self.Resources = resources or ResourceManager()
        self.Localization = localization or LocalizationManager(self.Resources)
        self.Runtime = runtime or RuntimeService(self)
        self._settings=settings if settings is not None else {"schema_version":2}
        self._settings_saver=settings_saver
        self._layout_save_timer=QTimer(self);self._layout_save_timer.setSingleShot(True);self._layout_save_timer.setInterval(400);self._layout_save_timer.timeout.connect(self._SaveWorkspace)
        self.setWindowTitle(self.Localization.Translate("app.title"))
        self.Localization.LocaleChanged.connect(
            lambda _: self.setWindowTitle(self.Localization.Translate("app.title"))
        )
        self.Docking = DockingSystem(
            theme=self.ThemeManager.GetTheme(),
            parent=self,
            localization=self.Localization,
            floating_always_on_top=True,
            double_click_float=False,
        )
        self.MenuBar = EditorMenuBar(self.ThemeManager, self.Localization, self)
        self.Toolbar = EditorToolbar(self.Localization, self.ThemeManager, self)
        self.MenuBar.SetDockingSystem(self.Docking)
        self.ThemeManager.ThemeChanged.connect(self.Docking.set_theme)

        self.Console = ConsolePanel(self.Localization,self.Resources,self.ThemeManager)
        self.Output = ViewportPanel(self.Runtime, False, self.Localization, self.Resources)
        self.Scene = ViewportPanel(self.Runtime, True, self.Localization, self.Resources)
        self.Hierarchy = HierarchyPanel(self.Localization)
        self.Properties = PropertiesPanel(self.Localization)
        self.AssetBrowser = AssetBrowserPanel(self.Localization, self.Resources)
        self._PanelWidgets = {
            "console": self.Console,
            "output": self.Output,
            "scene": self.Scene,
            "hierarchy": self.Hierarchy,
            "properties": self.Properties,
            "asset_browser": self.AssetBrowser,
        }
        self._RegisterPanels()
        self._default_layout=self._LoadDefaultWorkspace()
        saved_layout=self._settings.get("panel_layout")
        if not self.Docking.restore_layout(saved_layout if isinstance(saved_layout,dict) else self._default_layout):self.Docking.restore_layout(self._default_layout)
        self.Docking.layout_changed.connect(lambda:self._layout_save_timer.start())
        self.MenuBar.ResetWorkspaceRequested.connect(self._ResetWorkspace)
        self.ThemeManager.ThemeChanged.connect(
            lambda _: self._UpdatePanelPresentation()
        )
        self.Localization.LocaleChanged.connect(
            lambda _: self._UpdatePanelPresentation()
        )

        self.setMenuBar(self.MenuBar)
        self.addToolBar(Qt.ToolBarArea.TopToolBarArea, self.Toolbar)
        self.setCentralWidget(self.Docking)
        self.Controller = EditorController(self, self.Runtime)
        # Hierarchy row icons are theme-specific SVGs rather than palette icons.
        # Rebuild the rows when the theme changes so already-visible objects do
        # not retain the previous theme's low-contrast artwork.
        self.ThemeManager.ThemeChanged.connect(
            lambda _: self.Controller.RefreshHierarchy()
        )

        # Lower-case aliases preserve the original prototype's attributes.
        self.docking = self.Docking

    def _PanelIcon(self, panel_id: str):
        theme_name = (
            "light" if self.ThemeManager.GetTheme().background == "#d4d4d4" else "dark"
        )
        filename = "assetbrowser" if panel_id == "asset_browser" else panel_id
        return self.Resources.Icon(f"icons/{theme_name}/tab_{filename}.svg")

    def _RegisterPanels(self) -> None:
        tr = self.Localization.Translate
        scene = self.Docking.add_panel(
            self.Scene, tr("panel.scene"), self._PanelIcon("scene"), panel_id="scene"
        )
        self.Docking.add_panel(
            self.Output,
            tr("panel.output"),
            self._PanelIcon("output"),
            relative_to=scene,
            panel_id="output",
        )
        self.Docking.add_panel(
            self.Hierarchy,
            tr("panel.hierarchy"),
            self._PanelIcon("hierarchy"),
            area="left",
            relative_to=scene,
            panel_id="hierarchy",
        )
        self.Docking.add_panel(
            self.Properties,
            tr("panel.properties"),
            self._PanelIcon("properties"),
            area="right",
            relative_to=scene,
            panel_id="properties",
        )
        assets = self.Docking.add_panel(
            self.AssetBrowser,
            tr("panel.asset_browser"),
            self._PanelIcon("asset_browser"),
            area="bottom",
            relative_to=scene,
            panel_id="asset_browser",
        )
        self.Docking.add_panel(
            self.Console,
            tr("panel.console"),
            self._PanelIcon("console"),
            relative_to=assets,
            panel_id="console",
        )

    def _UpdatePanelPresentation(self) -> None:
        for panel_id in self._PanelWidgets:
            self.Docking.set_panel_presentation(
                panel_id,
                title=self.Localization.Translate(f"panel.{panel_id}"),
                icon=self._PanelIcon(panel_id),
            )

    def _ResetWorkspace(self) -> None:
        self.Docking.restore_layout(self._default_layout)

    def _LoadDefaultWorkspace(self)->dict:
        aliases={"panel_hierarchy":"hierarchy","panel_assets":"asset_browser","panel_scene":"scene","panel_game":"output","panel_console":"console","panel_inspector":"properties"}
        try:source=json.loads(self.Resources.ReadText("layouts/default.json"))
        except (OSError,ValueError,TypeError,json.JSONDecodeError):return self.Docking.save_layout()
        def convert(node):
            if not isinstance(node,dict):return None
            if node.get("type")=="panelContainer":
                panels=[];names=[]
                for tab in node.get("tabs",[]):
                    if not isinstance(tab,dict):continue
                    panel=aliases.get(str(tab.get("id","")))
                    if panel and panel not in panels:panels.append(panel);names.append(str(tab.get("name","")))
                if not panels:return None
                active=str(node.get("activeTab",""));current=names.index(active) if active in names else 0
                return {"type":"tabs","panels":panels,"current":current}
            if node.get("type")=="split" and node.get("orientation") in ("horizontal","vertical"):
                children=[value for child in node.get("children",[]) if (value:=convert(child)) is not None]
                if not children:return None
                if len(children)==1:return children[0]
                ratios=node.get("ratios",[]);sizes=[max(1,round(float(value)*1000)) for value in ratios] if isinstance(ratios,list) and len(ratios)==len(children) else [1]*len(children)
                return {"type":"split","orientation":node["orientation"],"children":children,"sizes":sizes}
            return None
        root=convert(source.get("rootNode"));return {"version":1,"root":root,"pinned":[],"floating":[]}

    def _SaveWorkspace(self)->None:
        self._settings["panel_layout"]=self.Docking.save_layout()
        if self._settings_saver is not None:
            try:self._settings_saver(self._settings)
            except (OSError,ValueError):pass

    def closeEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        self._layout_save_timer.stop();self._SaveWorkspace()
        self.Scene.Detach(); self.Output.Detach()
        self.Controller.Stop()
        application=QApplication.instance()
        if application is not None:application.removeEventFilter(self.CursorPolicy)
        super().closeEvent(event)


__all__ = ["Editor"]
