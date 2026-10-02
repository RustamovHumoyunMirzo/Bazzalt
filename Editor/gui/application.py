"""Top-level BAZZALT editor window."""

from __future__ import annotations

import json
from typing import Callable

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import QApplication, QMainWindow, QMessageBox, QLineEdit, QTextEdit, QPlainTextEdit

from ..localization import LocalizationManager
from ..resources import ResourceManager
from ..runtime import RuntimeService
from ..theme import Theme, ThemeManager
from .preferences import MergePreferences,PreferencesDialog
from bazzalt.branding import LogoIcon
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
        self._UpdateBrandIcon()
        self.ThemeManager.ThemeChanged.connect(lambda _theme:self._UpdateBrandIcon())
        self._settings=settings if settings is not None else {"schema_version":2}
        self._settings_saver=settings_saver
        self.ThemeManager.ThemeChanged.connect(self._RememberTheme)
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

        self.Console = ConsolePanel(self.Localization,self.Resources,self.ThemeManager,install_shortcuts=False)
        self.Output = ViewportPanel(self.Runtime, False, self.Localization, self.Resources,self.ThemeManager)
        self.Scene = ViewportPanel(self.Runtime, True, self.Localization, self.Resources,self.ThemeManager)
        self.Hierarchy = HierarchyPanel(self.Localization, install_shortcuts=False)
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
        self.MenuBar.PreferencesRequested.connect(self.OpenPreferences)
        self.MenuBar.ProjectSettingsRequested.connect(self.OpenProjectSettings)
        self.MenuBar.MaximizeViewportRequested.connect(self.SetViewportMaximized)
        self.Scene.GridToggle.toggled.connect(self.MenuBar.GridAction.setChecked)
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
        self.MenuBar.EditCommandRequested.connect(self.ExecuteEditCommand)
        self.MenuBar.EditMenu.aboutToShow.connect(self.RefreshEditActions)
        application.focusChanged.connect(self.RefreshEditActions)
        self.Console.View.itemSelectionChanged.connect(self.RefreshEditActions)
        self.AssetBrowser.Browser.itemSelectionChanged.connect(self.RefreshEditActions)
        # Hierarchy row icons are theme-specific SVGs rather than palette icons.
        # Rebuild the rows when the theme changes so already-visible objects do
        # not retain the previous theme's low-contrast artwork.
        self.ThemeManager.ThemeChanged.connect(
            lambda _: self.Controller.RefreshHierarchy()
        )

        # Lower-case aliases preserve the original prototype's attributes.
        self.docking = self.Docking
        self._pre_maximize_layout=None
        initial=self._settings.get("preferences")
        if not isinstance(initial,dict):initial=MergePreferences(None);initial["appearance"]["theme"]=str(self._settings.get("theme","dark"))
        self.ApplyPreferences(initial,save=False)
        for control,signal,key in ((self.Scene.PivotMode,"currentIndexChanged","pivot_center"),(self.Scene.LocalToggle,"toggled","local_space"),(self.Scene.GridToggle,"toggled","grid_visible"),(self.Scene.GridPlane,"currentIndexChanged","grid_plane"),(self.Scene.GizmoToggle,"toggled","gizmos_visible"),(self.Scene.StatsToggle,"toggled","stats_visible"),(self.Scene.ShadingMode,"currentIndexChanged","shading_mode"),(self.MenuBar.IconsAction,"toggled","icons_visible")):
            getattr(control,signal).connect(lambda value,k=key:self._SaveScenePreference(k,bool(value) if k=="pivot_center" else value))

    def _SaveScenePreference(self,key,value)->None:
        if getattr(self,"_applying_preferences",False):return
        preferences=self.GetPreferences();preferences["scene"][key]=value;self._settings["preferences"]=preferences
        if self._settings_saver is not None:self._settings_saver(self._settings)

    def GetPreferences(self)->dict:
        value=MergePreferences(self._settings.get("preferences"));value["appearance"]["theme"]="light" if self.ThemeManager.GetTheme().background==Theme.light().background else "dark";return value

    def _RememberTheme(self,theme:Theme)->None:
        value=MergePreferences(self._settings.get("preferences"));name="light" if theme.background==Theme.light().background else "dark";value["appearance"]["theme"]=name;self._settings["preferences"]=value;self._settings["theme"]=name

    def PreferenceValue(self,section:str,key:str,default=None):
        return self.GetPreferences().get(section,{}).get(key,default)

    def OpenPreferences(self)->None:
        previous=self.PreferenceValue("rendering","backend","automatic")
        if PreferencesDialog(self).exec() and previous!=self.PreferenceValue("rendering","backend","automatic"):
            self.RequestRestart()

    def OpenProjectSettings(self)->None:
        from .project_settings import ProjectSettingsDialog
        previous=self.Runtime.ProjectInfo().get("name")
        if ProjectSettingsDialog(self).exec() and previous!=self.Runtime.ProjectInfo().get("name"):
            self.RequestRestart()

    def RequestRestart(self)->None:
        tr=self.Localization.Translate;dialog=QMessageBox(self)
        dialog.setWindowTitle(tr("restart.title"));dialog.setIcon(QMessageBox.Icon.Warning)
        dialog.setText(tr("restart.message"))
        now=dialog.addButton(tr("restart.now"),QMessageBox.ButtonRole.AcceptRole)
        later=dialog.addButton(tr("restart.later"),QMessageBox.ButtonRole.RejectRole)
        dialog.setDefaultButton(later);dialog.setEscapeButton(later);dialog.exec()
        if dialog.clickedButton() is now and self.close():
            QApplication.instance().exit(75)

    def _EditContext(self):
        focus=QApplication.focusWidget()
        if isinstance(focus,(QLineEdit,QTextEdit,QPlainTextEdit)):return "text",focus
        if focus is not None and (focus is self.AssetBrowser or self.AssetBrowser.isAncestorOf(focus)):return "assets",self.AssetBrowser.Browser
        if focus is not None and (focus is self.Console or self.Console.isAncestorOf(focus)):return "console",self.Console.View
        return "entities",focus

    def RefreshEditActions(self,*_args)->None:
        if not hasattr(self,"Controller"):return
        context,widget=self._EditContext();selected=bool(self.Controller.SelectedEntities)
        if context=="text":
            selected=widget.hasSelectedText() if isinstance(widget,QLineEdit) else widget.textCursor().hasSelection()
            writable=not widget.isReadOnly()
            states=dict(copy=selected,paste=writable,delete=selected and writable,rename=False,duplicate=False,select_all=True,deselect_all=selected)
        elif context in {"assets","console"}:
            count=len(widget.selectedItems());states=dict(copy=count>0,paste=context=="assets" and bool(self.AssetBrowser._clipboard),delete=count>0,rename=context=="assets" and count==1,duplicate=False,select_all=widget.count()>0,deselect_all=count>0)
        else:
            item=self.Hierarchy.Tree.currentItem()
            rename=bool(item and item.data(0,Qt.ItemDataRole.UserRole+1) in {"entity","scene"} and len(self.Hierarchy.Tree.selectedItems())==1)
            states=dict(copy=selected,paste=bool(self.Controller._hierarchy_clipboard),delete=selected,rename=rename,duplicate=selected,select_all=bool(self.Runtime.Entities()),deselect_all=selected)
        for key,enabled in states.items():self.MenuBar.EditActions[key].setEnabled(enabled)

    def ExecuteEditCommand(self,command:str)->None:
        context,widget=self._EditContext()
        if context=="text":
            if command in {"copy","paste","select_all"}:getattr(widget,{"select_all":"selectAll"}.get(command,command))()
            elif command=="delete" and not widget.isReadOnly():
                if isinstance(widget,QLineEdit):widget.del_()
                else:cursor=widget.textCursor();cursor.removeSelectedText();widget.setTextCursor(cursor)
            elif command=="deselect_all":
                if isinstance(widget,QLineEdit):widget.deselect()
                else:cursor=widget.textCursor();cursor.clearSelection();widget.setTextCursor(cursor)
        elif context in {"assets","console"}:
            if command=="select_all":widget.selectAll()
            elif command=="deselect_all":widget.clearSelection()
            elif context=="console":
                if command=="copy":self.Console._CopySelected()
                elif command=="delete":self.Console.RemoveSelected()
            elif command=="copy":self.AssetBrowser._Copy()
            elif command=="paste":self.AssetBrowser._Paste()
            elif command=="delete":self.AssetBrowser._Delete()
            elif command=="rename" and len(widget.selectedItems())==1:widget.editItem(widget.selectedItems()[0])
        else:
            selected=list(self.Controller.SelectedEntities)
            if command=="copy":self.Controller.CopyHierarchyEntities(selected)
            elif command=="paste":self.Controller.PasteHierarchyEntities(self.Hierarchy._ContextParent())
            elif command=="duplicate":self.Controller.DuplicateHierarchyEntities(selected)
            elif command=="delete" and selected:self.Controller.DeleteEntity(selected)
            elif command=="rename":self.Hierarchy._BeginRename()
            elif command=="select_all":
                if widget is not None and self.Hierarchy.isAncestorOf(widget):self.Hierarchy.Tree.selectAll()
                else:self.Controller.SelectEntities([str(entity["uuid"]) for entity in self.Runtime.Entities()])
            elif command=="deselect_all":self.Controller.SelectEntity(None);self.Hierarchy.SetSelectedData([])
        self.RefreshEditActions()

    def ApplyPreferences(self,preferences:dict,save:bool=True)->None:
        self._applying_preferences=True
        value=MergePreferences(preferences);self._settings["preferences"]=value
        appearance=value["appearance"]
        self.ThemeManager.SetTheme(Theme.light() if appearance["theme"]=="light" else Theme.dark())
        if appearance["locale"]!=self.Localization.GetLocale():self.Localization.SetLocale(appearance["locale"])
        scene=value["scene"];self.Runtime.SetGrid(scene["grid_visible"],scene["grid_plane"])
        self.Scene.GridToggle.setChecked(bool(scene["grid_visible"]));self.Scene.GridPlane.setCurrentIndex(int(scene["grid_plane"]))
        self.Scene.Surface.SetMoveSpeed(scene["navigation_speed"])
        self.Scene.PivotMode.setCurrentIndex(1 if scene["pivot_center"] else 0);self.Scene.LocalToggle.setChecked(bool(scene["local_space"]))
        self.Scene.GizmoToggle.setChecked(bool(scene["gizmos_visible"]));self.Scene.StatsToggle.setChecked(bool(scene["stats_visible"]))
        self.MenuBar.IconsAction.setChecked(bool(scene["icons_visible"]));self.Runtime.SetEditorIconsVisible(bool(scene["icons_visible"]))
        self.Scene.ShadingMode.setCurrentIndex(int(scene["shading_mode"]))
        self.Scene.Surface._look_sensitivity=float(scene["look_sensitivity"]);self.Scene.Surface._fly_boost=float(scene["fly_boost"])
        self.Scene.Surface.SetOrientationVisible(bool(scene["orientation_visible"]))
        self.Controller.History.Configure(value["history"]["command_limit"],int(value["history"]["memory_mb"])*1024*1024)
        self._applying_preferences=False
        if save and self._settings_saver is not None:self._settings_saver(self._settings)

    def SetViewportMaximized(self,maximized:bool)->None:
        if maximized:
            if self._pre_maximize_layout is None:self._pre_maximize_layout=self.Docking.save_layout()
            self.Docking.restore_layout({"version":1,"root":{"type":"tabs","panels":["scene"],"current":0},"pinned":[],"floating":[]})
        elif self._pre_maximize_layout is not None:
            layout=self._pre_maximize_layout;self._pre_maximize_layout=None;self.Docking.restore_layout(layout)

    def _UpdateBrandIcon(self)->None:
        color="#202020" if self.ThemeManager.GetTheme().background=="#d4d4d4" else "#eeeeee"
        icon=LogoIcon(color);self.setWindowIcon(icon)
        application=QApplication.instance()
        if application is not None:application.setWindowIcon(icon)

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
        if not self.PreferenceValue("general","save_workspace",True) or self._pre_maximize_layout is not None:return
        self._settings["panel_layout"]=self.Docking.save_layout()
        if self._settings_saver is not None:
            try:self._settings_saver(self._settings)
            except (OSError,ValueError):pass

    def closeEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        if self.Controller.IsDirty and self.isVisible() and self.PreferenceValue("general","confirm_unsaved",True):
            tr=self.Localization.Translate;dialog=QMessageBox(self)
            dialog.setWindowTitle(tr("dialog.unsaved_title"));dialog.setText(tr("dialog.unsaved_message"));dialog.setIcon(QMessageBox.Icon.Warning)
            save=dialog.addButton(tr("dialog.save_and_close"),QMessageBox.ButtonRole.AcceptRole)
            discard=dialog.addButton(tr("dialog.close_without_saving"),QMessageBox.ButtonRole.DestructiveRole)
            cancel=dialog.addButton(tr("dialog.cancel_close"),QMessageBox.ButtonRole.RejectRole)
            dialog.setDefaultButton(save);dialog.setEscapeButton(cancel);dialog.exec();clicked=dialog.clickedButton()
            if clicked is cancel or clicked is None:event.ignore();return
            if clicked is save and not self.Controller.SaveScene():event.ignore();return
            if clicked is not discard and clicked is not save:event.ignore();return
        self._layout_save_timer.stop();self._SaveWorkspace()
        self.Scene.Detach(); self.Output.Detach()
        self.Controller.Stop()
        application=QApplication.instance()
        if application is not None:application.removeEventFilter(self.CursorPolicy)
        if application is not None:
            try:application.focusChanged.disconnect(self.RefreshEditActions)
            except RuntimeError:pass
        super().closeEvent(event)


__all__ = ["Editor"]
