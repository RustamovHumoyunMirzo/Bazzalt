from __future__ import annotations

import os
from pathlib import Path
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication, QAbstractItemView, QLabel, QLineEdit, QSizePolicy, QWidget
from PySide6.QtGui import QColor
from PySide6.QtGui import QKeyEvent, QMouseEvent
from PySide6.QtCore import QEvent, Qt
from PySide6.QtCore import QPoint, QPointF

from Editor.gui.application import Editor
from Editor.gui.panels import ConsoleLevel
from Editor.gui.widgets import (
    AssetPickerInput,
    ColorInput,
    EnumInput,
    FieldState,
    FloatInput,
    MultiSelectInput,
    RangeInput,
    StringInput,
    UIntInput,
    Vec3Input,
    PlayState,
)
from Editor.gui.gizmos import GizmoHandle, GizmoMode, Vec3
from Editor.gui.widgets import EditorMenu
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.theme import BuildStyleSheet, Theme, ThemeManager


class EditorShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.Application = QApplication.instance() or QApplication([])

    def setUp(self) -> None:
        self.Themes = ThemeManager(self.Application)
        self.Window = Editor(self.Themes)

    def tearDown(self) -> None:
        self.Window.close()

    def test_shared_theme_updates_docking(self) -> None:
        self.Window.Controller.RefreshHierarchy()
        dark_icon=self.Window.Hierarchy.Tree.topLevelItem(0).icon(0).cacheKey()
        self.Themes.SetTheme(Theme.light())
        self.assertEqual(self.Window.Docking.theme.background, Theme.light().background)
        light_icon=self.Window.Hierarchy.Tree.topLevelItem(0).icon(0).cacheKey()
        self.assertNotEqual(dark_icon,light_icon)
        self.assertFalse(self.Window.windowIcon().isNull())

    def test_shortcuts_use_custom_menu_rendering(self) -> None:
        self.assertIsInstance(self.Window.MenuBar.FileMenu, EditorMenu)
        save = next(action for action in self.Window.MenuBar.FileMenu.actions()
                    if action.text() == "Save Scene")
        # Native shortcut layout stays enabled for correct sizing; EditorMenu's
        # paint pass strips that text and draws exactly one muted shortcut.
        self.assertTrue(save.isShortcutVisibleInContextMenu())
        self.assertFalse(save.shortcut().isEmpty())
        menu = self.Window.MenuBar.FileMenu
        menu.resize(menu.sizeHint())
        self.assertLess(menu.sizeHint().width(), 500)
        self.assertTrue(all(menu.actionGeometry(action).right() <= menu.width()
                            for action in menu.actions()))

    def test_nested_menus_fit_their_native_action_rows(self) -> None:
        view = self.Window.MenuBar.ViewMenu
        theme = view.actions()[0].menu()
        self.assertIsNotNone(theme)
        for menu in (view, theme):
            menu.resize(menu.sizeHint())
            self.assertTrue(all(menu.actionGeometry(action).right() <= menu.width()
                                for action in menu.actions()))

    def test_resources_and_localization_are_shared_services(self) -> None:
        self.assertIsInstance(self.Window.Resources, ResourceManager)
        self.assertIsInstance(self.Window.Localization, LocalizationManager)
        self.assertEqual(self.Window.Localization.Translate("action.save_project"), "Save Project")
        self.assertTrue(self.Window.Resources.Resolve("locales/en.json").is_file())
        with self.assertRaises(ValueError):
            self.Window.Resources.Resolve(Path("..") / "outside.svg")

    def test_layout_restore_rejects_oversized_input(self) -> None:
        self.assertFalse(self.Window.Docking.restore_layout(b" " * (2 * 1024 * 1024 + 1)))

    def test_window_menu_lists_shared_dock_panels(self) -> None:
        self.Window.Docking.add_panel(
            QLabel("Inspector"), "Inspector", panel_id="inspector"
        )
        self.Window.MenuBar.WindowMenu.aboutToShow.emit()
        labels = [action.text() for action in self.Window.MenuBar.WindowMenu.actions()]
        self.assertIn("Inspector", labels)

    def test_window_menu_checks_every_open_tab_not_only_active_tab(self) -> None:
        self.Window.MenuBar.WindowMenu.aboutToShow.emit()
        actions = {action.text(): action for action in self.Window.MenuBar.WindowMenu.actions()}
        self.assertTrue(actions["Scene"].isChecked())
        self.assertTrue(actions["Output"].isChecked())
        self.assertTrue(actions["Scene"].icon().isNull())
        self.Window.Docking.close_panel("output")
        self.Window.MenuBar.WindowMenu.aboutToShow.emit()
        actions = {action.text(): action for action in self.Window.MenuBar.WindowMenu.actions()}
        self.assertFalse(actions["Output"].isChecked())

    def test_typed_component_field_widgets(self) -> None:
        number = FloatInput(value=2.5)
        number.SetFieldState(FieldState.Modified)
        self.assertEqual(number.GetValue(), 2.5)
        self.assertEqual(number.GetFieldState(), FieldState.Modified)

        vector = Vec3Input((1.0, 2.0, 3.0))
        self.assertEqual(vector.GetValue(), (1.0, 2.0, 3.0))
        vector.SetValue((4.0, 5.0, 6.0))
        self.assertEqual(vector.GetValue(), (4.0, 5.0, 6.0))

        ranged = RangeInput(-1.0, 1.0, 0.25)
        ranged.SetValue(9.0)
        self.assertEqual(ranged.GetValue(), 1.0)

        color = ColorInput(QColor("white"))
        color.SetValue((0.1,0.2,0.3,0.4))
        self.assertAlmostEqual(color.GetValue().alphaF(),0.4,places=2)

        unsigned=UIntInput(4_294_967_295)
        self.assertEqual(unsigned.GetValue(),4_294_967_295)

    def test_enum_multiselect_and_picker_widgets(self) -> None:
        enum = EnumInput()
        enum.SetOptions((("Perspective", "perspective"), ("Orthographic", "ortho")))
        self.assertTrue(enum.SetValue("ortho"))
        self.assertEqual(enum.GetValue(), "ortho")

        choices = MultiSelectInput()
        choices.SetOptions((("Static", 1), ("Visible", 2), ("Locked", 3)))
        choices.SetValues((1, 3))
        self.assertEqual(choices.GetValues(), (1, 3))

        picker = AssetPickerInput("Select asset")
        picker.SetValue("asset-uuid", "Player Mesh")
        self.assertEqual(picker.GetValue(), "asset-uuid")
        self.assertTrue(picker.ClearButton.isEnabled())
        picker.Clear()
        self.assertIsNone(picker.GetValue())
        typed=AssetPickerInput("Mesh",accepted_extensions={".glb"})
        typed.ConfigureAssets(({"uuid":"mesh-id","name":"Robot.glb","path":"C:/Assets/Robot.glb","extension":".glb"},
                               {"uuid":"image-id","name":"Albedo.png","path":"C:/Assets/Albedo.png","extension":".png"}))
        self.assertTrue(typed._Accepts(typed._Record("C:/Assets/Robot.glb")))
        self.assertFalse(typed._Accepts(typed._Record("C:/Assets/Albedo.png")))
        typed.SetValue("mesh-id")
        self.assertEqual(typed.Display.text(),"Robot.glb")
        self.assertNotIn("mesh-id",typed.Display.text())

    def test_editor_uses_custom_menu_bar(self) -> None:
        self.assertIs(self.Window.menuBar(), self.Window.MenuBar)
        self.assertFalse(self.Window.MenuBar.isNativeMenuBar())

    def test_native_runtime_populates_hierarchy_and_inspector(self) -> None:
        if not self.Window.Runtime.IsAvailable():
            self.skipTest("native editor bridge is not built")
        entity_id = self.Window.Runtime.CreateEntity("Bridge Entity")
        self.assertEqual(self.Window.Hierarchy.Tree.topLevelItemCount(), 1)
        self.Window.Controller.SelectEntity(entity_id)
        self.assertIn("transform", self.Window.Properties._sections)
        details = self.Window.Runtime.EntityDetails(entity_id)
        self.assertEqual(details["name"], "Bridge Entity")
        self.assertTrue(self.Window.Runtime.SetTransform(
            entity_id, (3.0, 2.0, 1.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0)))
        self.assertEqual(self.Window.Runtime.EntityDetails(entity_id)["position"], (3.0, 2.0, 1.0))

    def test_inspector_supports_many_components_and_structural_refresh(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        entity_id=self.Window.Runtime.CreateEntity("Inspector Entity")
        for component in ("Camera","Light","Mesh","Scene Query Bounds"):
            self.assertTrue(self.Window.Runtime.AddComponent(entity_id,component))
        self.Window.Controller.SelectEntity(entity_id,force=True)
        self.assertGreaterEqual(len(self.Window.Properties._sections),6)
        layer=self.Window.Properties._sections["runtime.Scene Query Bounds"]._fields["Layer Mask"]
        self.assertEqual(layer.GetValue(),4_294_967_295)
        light_section=self.Window.Properties._sections["runtime.Light"]
        self.Window.Hierarchy.SetSelectedData([entity_id])
        light_section.Enabled.setChecked(False);self.Application.processEvents()
        self.assertEqual(self.Window.Controller.SelectedEntity,entity_id)
        self.assertEqual(self.Window.Hierarchy.GetSelectedData(),[entity_id])
        self.assertFalse(self.Window.Runtime.EntityDetails(entity_id)["component_enabled"]["Light"])
        self.Window.Controller.RemoveComponent("runtime.Light")
        self.assertNotIn("runtime.Light",self.Window.Properties._sections)
        self.Window.Controller._AddComponent("Light")
        self.assertIn("runtime.Light",self.Window.Properties._sections)

    def test_game_output_warns_until_an_active_scene_camera_exists(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        output=self.Window.Output
        self.assertIs(output._output_stack.currentWidget(),output._no_camera)
        self.assertEqual(output._no_camera.text(),"No active camera in the scene")
        entity_id=self.Window.Runtime.CreateEntity("Game Camera")
        self.assertFalse(self.Window.Runtime.HasActiveCamera())
        self.assertTrue(self.Window.Runtime.AddComponent(entity_id,"Camera"))
        output.SetGameCameraAvailable(self.Window.Runtime.HasActiveCamera())
        self.assertIs(output._output_stack.currentWidget(),output.Surface)
        self.assertTrue(self.Window.Runtime.SetComponentEnabled(entity_id,"Camera",False))
        output.SetGameCameraAvailable(self.Window.Runtime.HasActiveCamera())
        self.assertIs(output._output_stack.currentWidget(),output._no_camera)
        self.assertTrue(self.Window.Runtime.SetComponentEnabled(entity_id,"Camera",True))
        output.SetGameCameraAvailable(self.Window.Runtime.HasActiveCamera())
        self.assertIs(output._output_stack.currentWidget(),output.Surface)
        self.assertTrue(self.Window.Runtime.RemoveComponent(entity_id,"Camera"))
        output.SetGameCameraAvailable(self.Window.Runtime.HasActiveCamera())
        self.assertIs(output._output_stack.currentWidget(),output._no_camera)

    def test_editor_hierarchy_preserves_and_displays_world_transforms(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        parent=self.Window.Runtime.CreateEntity("Parent")
        child=self.Window.Runtime.CreateEntity("Child")
        self.assertTrue(self.Window.Runtime.SetTransform(parent,(10,0,0),(0,0,0,1),(1,1,1)))
        self.assertTrue(self.Window.Runtime.SetTransform(child,(3,2,1),(0,0,0,1),(1,1,1)))
        self.assertTrue(self.Window.Runtime.SetParent(child,parent))
        self.assertEqual(self.Window.Runtime.EntityDetails(child)["position"],(3.0,2.0,1.0))
        self.assertTrue(self.Window.Runtime.SetTransform(parent,(20,0,0),(0,0,0,1),(1,1,1)))
        self.assertEqual(self.Window.Runtime.EntityDetails(child)["position"],(13.0,2.0,1.0))
        self.assertTrue(self.Window.Runtime.SetParent(child,""))
        self.assertEqual(self.Window.Runtime.EntityDetails(child)["position"],(13.0,2.0,1.0))

    def test_asset_browser_creates_types_renames_and_populates_inspector(self) -> None:
        root=Path("Editor/tests/fixtures/assets").resolve();browser=self.Window.AssetBrowser;browser.SetProjectRoot(root)
        self.assertGreater(browser.Browser.count(),0)
        item=browser.Browser.item(0);path=Path(item.data(Qt.ItemDataRole.UserRole))
        self.assertFalse(item.icon().isNull());browser.AssetSelected.emit(path)
        self.assertIn("asset",self.Window.Properties._sections)
        self.assertFalse(self.Window.Properties.AddComponentButton.isVisible())
        self.assertEqual(browser._Unique("NewComponent.cpp").parent,root)
        textures=next(browser.Tree.topLevelItem(0).child(i) for i in range(browser.Tree.topLevelItem(0).childCount()) if browser.Tree.topLevelItem(0).child(i).text(0)=="Textures")
        browser.Tree.setCurrentItem(textures);browser.Refresh(root/"Textures"/".keep")
        self.assertEqual(browser.CurrentFolder(),root/"Textures")
        self.assertEqual(Path(browser.Browser.currentItem().data(Qt.ItemDataRole.UserRole)),root/"Textures"/".keep")

    def test_hierarchy_multi_selection_uses_center_gizmo_and_batch_translation(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        first=self.Window.Runtime.CreateEntity("First");second=self.Window.Runtime.CreateEntity("Second")
        self.Window.Runtime.SetTransform(first,(0,0,0),(0,0,0,1),(1,1,1));self.Window.Runtime.SetTransform(second,(4,0,0),(0,0,0,1),(1,1,1))
        self.Window.Controller.SelectEntities([first,second])
        self.Window.Controller.RefreshHierarchy();self.Window.Hierarchy.SetSelectedData([first,second])
        self.assertEqual(self.Window.Controller.SelectedEntities,[first,second])
        self.assertEqual(set(self.Window.Hierarchy.GetSelectedData()),{first,second})
        self.assertIn("selection",self.Window.Properties._sections)
        self.assertTrue(self.Window.Controller.ApplyGizmoTranslation(Vec3(1,2,3)))
        self.assertEqual(self.Window.Runtime.EntityDetails(first)["position"],(1.0,2.0,3.0))
        self.assertEqual(self.Window.Runtime.EntityDetails(second)["position"],(5.0,2.0,3.0))
        self.assertTrue(self.Window.Controller.ApplyGizmoRotation(Vec3(0,1,0),0.25))

    def test_scene_box_selection_works_outside_select_tool(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        entity=self.Window.Runtime.CreateEntity("Box selected");surface=self.Window.Scene.Surface;surface.resize(400,400);surface.SetGizmoMode(GizmoMode.Translate);self.Window.Controller.SelectEntity(entity)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(1,1),Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        self.assertEqual(self.Window.Controller.SelectedEntities,[])
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(399,399),Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        self.assertTrue(surface._selection_band.testAttribute(Qt.WidgetAttribute.WA_TranslucentBackground))
        self.assertIn(entity,self.Window.Controller.SelectedEntities)
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(2,2),Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        self.assertNotIn(entity,self.Window.Controller.SelectedEntities)
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(399,399),Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        self.assertIn(entity,self.Window.Controller.SelectedEntities)
        self.Application.processEvents();pixel=surface._selection_band.grab().toImage().pixelColor(10,10)
        self.assertGreater(pixel.alpha(),0);self.assertLess(pixel.alpha(),255)
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,QPointF(399,399),Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.assertIn(entity,self.Window.Controller.SelectedEntities)

    def test_scene_history_restores_create_transform_and_uuid(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        before={item["uuid"] for item in self.Window.Runtime.Entities()};self.Window.Controller.CreateEntity(None);created=next(item["uuid"] for item in self.Window.Runtime.Entities() if item["uuid"] not in before)
        self.assertTrue(self.Window.Controller.History.CanUndo());self.Window.Controller.Undo();self.assertFalse(self.Window.Runtime.EntityDetails(created));self.Window.Controller.Redo();self.assertTrue(self.Window.Runtime.EntityDetails(created))
        self.Window.Controller._Mutate("Move entity",lambda:self.Window.Runtime.SetTransform(created,(8,7,6),(0,0,0,1),(1,1,1)));self.assertEqual(self.Window.Runtime.EntityDetails(created)["position"],(8.0,7.0,6.0));self.Window.Controller.Undo();self.assertEqual(self.Window.Runtime.EntityDetails(created)["position"],(0.0,0.0,0.0))

    def test_native_scene_surface_has_no_qt_paint_overlay_and_same_selection_is_stable(self) -> None:
        self.assertEqual(self.Window.Scene.Surface.findChildren(QWidget),[])
        if not self.Window.Runtime.IsAvailable():
            self.skipTest("native editor bridge is not built")
        entity_id=self.Window.Runtime.CreateEntity("Stable Selection")
        self.Window.Controller.SelectEntity(entity_id)
        transform=self.Window.Properties._sections["transform"]
        identity=self.Window.Properties._sections["identity"]
        self.assertFalse(identity.IconLabel.isWindow())
        self.assertIs(identity.IconLabel.parentWidget(),identity.findChild(QWidget,"ComponentHeaderRow"))
        self.assertFalse(any(widget.isWindow() for widget in self.Window.Properties.findChildren(QWidget)))
        self.Window.Controller.SelectEntity(entity_id)
        self.assertIs(self.Window.Properties._sections["transform"],transform)

    def test_editor_toolbar_transport_and_modes(self) -> None:
        toolbar = self.Window.Toolbar
        self.assertFalse(toolbar.isMovable())
        self.assertEqual(toolbar.GetPlayState(), PlayState.Stopped)
        self.assertFalse(toolbar.StopAction.isEnabled())
        toolbar.PlayAction.trigger()
        self.assertEqual(toolbar.GetPlayState(), PlayState.Playing)
        self.assertTrue(toolbar.StopAction.isEnabled())
        toolbar.PauseAction.trigger()
        self.assertEqual(toolbar.GetPlayState(), PlayState.Paused)
        toolbar.SetGizmoMode(GizmoMode.Rotate)
        self.assertEqual(toolbar.GetGizmoMode(), GizmoMode.Rotate)
        self.assertTrue(all(not action.icon().isNull()
                            for action in toolbar.ModeActions.values()))
        self.assertEqual(toolbar.toggleViewAction().text(), "Editor Toolbar")
        self.assertIsInstance(toolbar.ModeMenu, EditorMenu)
        toolbar.ModeSelector.resize(toolbar.ModeSelector.sizeHint())
        icon_rect = toolbar.ModeSelector._IconRect()
        text_rect = toolbar.ModeSelector._TextRect()
        arrow_rect = toolbar.ModeSelector._ArrowRect()
        self.assertGreaterEqual(text_rect.left() - icon_rect.right() - 1, 7)
        self.assertGreaterEqual(arrow_rect.left() - text_rect.right(), 8)
        self.assertGreaterEqual(toolbar.ModeSelector.width() - arrow_rect.right() - 1, 9)
        self.assertEqual(arrow_rect.center().y(), toolbar.ModeSelector.rect().center().y())

    def test_toolbar_icons_refresh_for_theme(self) -> None:
        before = self.Window.Toolbar.PlayAction.icon().cacheKey()
        self.Themes.SetTheme(Theme.light())
        after = self.Window.Toolbar.PlayAction.icon().cacheKey()
        self.assertNotEqual(before, after)

    def test_menu_icons_and_dock_tabs_have_clean_edge_spacing(self) -> None:
        style = BuildStyleSheet(Theme.dark())
        self.assertIn("QMenu::icon { left: 12px; }", style)
        self.assertIn("border-top: 0;", style)

    def test_default_workspace_registers_all_builtin_panels(self) -> None:
        self.assertEqual(
            {panel.panel_id for panel in self.Window.Docking.panels()},
            {"console", "output", "scene", "hierarchy", "properties", "asset_browser"},
        )
        self.assertTrue(all(not panel.icon.isNull() for panel in self.Window.Docking.panels()))

    def test_console_api_filters_and_clears_messages(self) -> None:
        self.Window.Console.AddMessage("built", ConsoleLevel.Info)
        self.Window.Console.AddMessage("failed", ConsoleLevel.Error)
        self.Window.Console.AddMessage("plain",ConsoleLevel.Info,show_icon=False)
        self.assertEqual(self.Window.Console.View.count(),3)
        self.assertFalse(self.Window.Console.View.item(0).icon().isNull())
        self.assertTrue(self.Window.Console.View.item(2).icon().isNull())
        self.Window.Console.View.item(1).setSelected(True)
        self.assertIn("failed",self.Window.Console.View.selectedItems()[0].text())
        self.Window.Console.SetLevelVisible(ConsoleLevel.Info, False)
        self.assertNotIn("built", self.Window.Console.View.toPlainText())
        self.assertIn("failed", self.Window.Console.View.toPlainText())
        self.Window.Console.Clear()
        self.assertEqual(self.Window.Console.GetMessages(), ())

    def test_console_filters_sources_and_removes_rows(self) -> None:
        console=self.Window.Console
        console.AddMessage("editor ready",ConsoleLevel.Info,source="Editor")
        console.AddMessage("body sleeping",ConsoleLevel.Info,source="Physics")
        console.AddMessage("body failed",ConsoleLevel.Error,source="Physics")
        console.SetSourceFilter("Physics")
        self.assertEqual(console.View.count(),2)
        self.assertTrue(console.View.item(0).text().endswith("Physics"))
        self.assertEqual(set(console.LevelFilter.GetValues()),{"info","warning","error"})
        self.assertEqual(console.LevelFilter.text(),"Filters")
        console.Search.setText("failed")
        self.assertEqual(console.View.count(),1)
        console.ClearFiltered()
        self.assertEqual(len(console.GetMessages()),2)
        self.assertTrue(all(message.Text!="body failed" for message in console.GetMessages()))
        console.Search.clear();console.SetSourceFilter("")
        console.View.item(0).setSelected(True);console.View.item(1).setSelected(True)
        console.RemoveSelected()
        self.assertEqual(console.GetMessages(),())

    def test_properties_sections_and_sticky_add_button(self) -> None:
        section = self.Window.Properties.AddComponentSection(
            "transform", "Transform",
            icon=self.Window.Resources.Icon("icons/comp_transform.svg"))
        section.SetExpanded(False)
        self.assertFalse(section.Body.isVisible())
        self.assertFalse(section.IconLabel.pixmap().isNull())
        iconless = self.Window.Properties.AddComponentSection("custom", "Custom")
        self.assertFalse(iconless.IconLabel.isVisible())
        field=StringInput("compact");second=StringInput("equal");iconless.AddField("Value",field);iconless.AddField("Long Property Name",second)
        self.assertEqual(field.maximumWidth(),260);self.assertEqual(field.sizePolicy().horizontalPolicy(),QSizePolicy.Policy.Expanding)
        self.Window.show();self.Application.processEvents();self.assertGreater(field.width(),0);self.assertEqual(field.width(),second.width())
        caption=next(label for label in iconless.findChildren(QLabel,"InspectorFieldLabel") if label.text()=="Long Property Name")
        self.assertGreater(caption.width(),72);self.assertTrue(caption.alignment()&Qt.AlignmentFlag.AlignLeading)
        self.assertEqual(self.Window.Properties.layout().itemAt(
            self.Window.Properties.layout().count() - 1
        ).widget(), self.Window.Properties.AddComponentButton)

    def test_asset_browser_hides_sidecar_metadata(self) -> None:
        root = Path(__file__).parent / "fixtures" / "assets"
        self.Window.AssetBrowser.SetProjectRoot(root)
        names = [self.Window.AssetBrowser.Browser.item(i).text()
                 for i in range(self.Window.AssetBrowser.Browser.count())]
        self.assertIn("Textures", names)
        self.assertIn("player", names)
        self.assertNotIn("player.png.meta", names)
        self.assertEqual(self.Window.AssetBrowser.Browser.dragDropMode(),QAbstractItemView.DragDropMode.DragOnly)
        self.assertEqual(self.Window.AssetBrowser.Browser.supportedDragActions(),Qt.DropAction.CopyAction)
        player=next(self.Window.AssetBrowser.Browser.item(index) for index in range(self.Window.AssetBrowser.Browser.count()) if self.Window.AssetBrowser.Browser.item(index).text()=="player")
        self.assertEqual(Path(player.data(Qt.ItemDataRole.UserRole)).name,"player.png")
        self.assertTrue(self.Window.AssetBrowser.Browser.mimeData([player]).hasFormat("application/x-bazzalt-asset"))

    def test_reset_workspace_restores_closed_panel(self) -> None:
        self.Window.Docking.close_panel("console")
        self.Window._ResetWorkspace()
        placed = str(self.Window.Docking.save_layout())
        self.assertIn("console", placed)

    def test_workspace_layout_autosaves_and_restores_from_settings(self) -> None:
        settings={"schema_version":2};saved=[]
        window=Editor(self.Themes,settings=settings,settings_saver=lambda value:saved.append(dict(value)))
        try:
            window.Docking.close_panel("hierarchy");window._SaveWorkspace()
            self.assertTrue(saved);self.assertIn("panel_layout",settings)
            restored=Editor(self.Themes,settings=settings)
            try:self.assertFalse(restored.Docking.is_panel_open("hierarchy"))
            finally:restored.close()
        finally:window.close()

    def test_default_workspace_is_readable_pseudo_json(self) -> None:
        import json
        value=json.loads(self.Window.Resources.ReadText("layouts/default.json"))
        self.assertEqual(value["rootNode"]["type"],"split")
        self.assertEqual(value["rootNode"]["ratios"],[0.20,0.55,0.25])

    def test_gizmo_hover_and_drag_do_not_replace_scene_cursor(self) -> None:
        surface=self.Window.Scene.Surface;self.assertEqual(surface.cursor().shape(),Qt.CursorShape.ArrowCursor);surface._SetHover(GizmoHandle.X)
        self.assertEqual(surface.cursor().shape(),Qt.CursorShape.ArrowCursor)

    def test_cursor_policy_limits_ibeam_to_text_editors(self) -> None:
        ordinary=QLabel("Panel content",self.Window)
        ordinary.setCursor(Qt.CursorShape.IBeamCursor)
        QApplication.sendEvent(ordinary,QEvent(QEvent.Type.Enter))
        self.assertEqual(ordinary.cursor().shape(),Qt.CursorShape.ArrowCursor)

        text=QLineEdit(self.Window)
        text.setCursor(Qt.CursorShape.ArrowCursor)
        QApplication.sendEvent(text,QEvent(QEvent.Type.Enter))
        self.assertEqual(text.cursor().shape(),Qt.CursorShape.IBeamCursor)

    def test_asset_browser_uses_uniform_tiles(self) -> None:
        browser=self.Window.AssetBrowser.Browser
        self.assertTrue(browser.uniformItemSizes())
        self.assertEqual(browser.gridSize(),browser.item(0).sizeHint() if browser.count() else browser.gridSize())

    def test_hierarchy_drag_uses_non_destructive_transport(self) -> None:
        self.assertEqual(self.Window.Hierarchy.Tree.supportedDragActions(),Qt.DropAction.CopyAction)

    def test_scene_right_drag_orbits_without_fly_keys(self) -> None:
        surface=self.Window.Scene.Surface;eye=surface._eye;target=tuple(surface._target)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(130,115),Qt.MouseButton.NoButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        self.assertNotEqual(surface._eye,eye);self.assertEqual(tuple(surface._target),target)

    def test_scene_right_drag_looks_from_eye_while_flying(self) -> None:
        surface=self.Window.Scene.Surface;surface._UpdateCamera();eye=surface._eye;target=tuple(surface._target)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        surface.keyPressEvent(QKeyEvent(QKeyEvent.Type.KeyPress,Qt.Key.Key_W,Qt.KeyboardModifier.NoModifier))
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(130,115),Qt.MouseButton.NoButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        self.assertEqual(surface._eye,eye);self.assertNotEqual(tuple(surface._target),target)

    def test_shift_right_drag_pans_scene_camera(self) -> None:
        surface=self.Window.Scene.Surface;surface._UpdateCamera();eye=Vec3(*surface._eye);target=Vec3(*surface._target)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.ShiftModifier))
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(130,115),Qt.MouseButton.NoButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.ShiftModifier))
        moved_eye=Vec3(*surface._eye);moved_target=Vec3(*surface._target)
        self.assertGreater((moved_eye-eye).Length(),0.0)
        self.assertLess(((moved_eye-eye)-(moved_target-target)).Length(),.0001)

    def test_native_orientation_axis_is_clickable(self) -> None:
        surface=self.Window.Scene.Surface;surface.resize(400,400)
        # Projected +X endpoint for the default 36° yaw / 20° pitch.
        self.assertTrue(surface._PickOrientation(QPoint(373,48)))
        self.assertTrue(surface._orientation_animation.isActive())

    def test_hierarchy_rebuild_preserves_real_parent_and_expansion(self) -> None:
        parent=self.Window.Runtime.CreateEntity("Tree Parent");child=self.Window.Runtime.CreateEntity("Tree Child")
        self.assertTrue(self.Window.Runtime.SetParent(child,parent));self.Window.Controller.RefreshHierarchy()
        def find(value):
            from PySide6.QtWidgets import QTreeWidgetItemIterator
            iterator=QTreeWidgetItemIterator(self.Window.Hierarchy.Tree)
            while iterator.value() is not None:
                item=iterator.value()
                if str(item.data(0,Qt.ItemDataRole.UserRole))==value:return item
                iterator+=1
            return None
        parent_item=find(parent);child_item=find(child)
        self.assertIsNotNone(parent_item);self.assertIsNotNone(child_item);self.assertIs(child_item.parent(),parent_item)
        self.assertTrue(parent_item.isExpanded())
        parent_item.setExpanded(False);self.Window.Controller.RefreshHierarchy()
        self.assertFalse(find(parent).isExpanded())
        find(parent).setExpanded(True);self.Window.Controller.RefreshHierarchy()
        self.assertTrue(find(parent).isExpanded())
        self.Window.Controller.ReparentEntity(child,"")
        child_item=find(child)
        self.assertIsNotNone(child_item);self.assertEqual(child_item.parent().data(0,Qt.ItemDataRole.UserRole+1),"scene")
        self.assertFalse(child_item.isHidden())


if __name__ == "__main__":
    unittest.main()
