from __future__ import annotations

import os
from pathlib import Path
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication, QAbstractItemView, QLabel, QLineEdit, QSizePolicy, QWidget
from PySide6.QtWidgets import QListWidgetItem
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
from Editor.gui.preferences import PreferencesDialog
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
        # QWidget.close() only hides the window. Do not retain every editor's
        # widgets and theme listeners throughout the offscreen suite.
        self.Window.deleteLater()
        self.Application.sendPostedEvents(None, QEvent.Type.DeferredDelete)

    def test_shared_theme_updates_docking(self) -> None:
        self.Window.Controller.RefreshHierarchy()
        dark_icon=self.Window.Hierarchy.Tree.topLevelItem(0).icon(0).cacheKey()
        self.Themes.SetTheme(Theme.light())
        self.assertEqual(self.Window.Docking.theme.background, Theme.light().background)
        light_icon=self.Window.Hierarchy.Tree.topLevelItem(0).icon(0).cacheKey()
        self.assertNotEqual(dark_icon,light_icon)
        self.assertFalse(self.Window.windowIcon().isNull())

    def test_preferences_are_modal_persistent_and_applied(self) -> None:
        dialog=PreferencesDialog(self.Window)
        self.assertIs(dialog.parent(),self.Window);self.assertTrue(dialog.isModal())
        self.assertEqual(dialog.Sections.count(),9) # Includes file associations.
        self.assertEqual(dialog.Sections.count(),dialog.Pages.count())
        dialog.Controls["expand_new_hierarchy_items"].setChecked(True)
        dialog.Controls["theme"].setCurrentIndex(dialog.Controls["theme"].findData("light"))
        dialog.Controls["navigation_speed"].setValue(10.0);dialog.Controls["clear_on_play"].setChecked(True)
        dialog._Apply()
        self.assertEqual(self.Window.GetPreferences()["appearance"]["theme"],"light")
        self.assertEqual(self.Window.Scene.Surface._move_speed,10.0)
        self.assertTrue(self.Window.PreferenceValue("console","clear_on_play"))
        self.assertTrue(self.Window.Hierarchy.ExpandNewItems)
        self.assertTrue(self.Window.PreferenceValue("general","expand_new_hierarchy_items"))
        self.assertEqual(self.Window.MenuBar.PreferencesAction.text(),"Preferences…")

    def test_scene_toolbar_autosaves_and_preferences_restore_history_limits(self) -> None:
        from copy import deepcopy
        saved=[];self.Window._settings_saver=lambda value:saved.append(deepcopy(value))
        self.Window.Scene.PivotMode.setCurrentIndex(1);self.Window.Scene.LocalToggle.setChecked(True);self.Window.Scene.StatsToggle.setChecked(True)
        self.assertTrue(saved[-1]["preferences"]["scene"]["pivot_center"])
        self.assertTrue(saved[-1]["preferences"]["scene"]["local_space"])
        preferences=deepcopy(saved[-1]["preferences"]);preferences["history"]={"command_limit":12,"memory_mb":8}
        self.Window.Scene.PivotMode.setCurrentIndex(0);self.Window.ApplyPreferences(preferences)
        self.assertEqual(self.Window.Scene.PivotMode.currentIndex(),1)
        self.assertTrue(self.Window.Scene.StatsToggle.isChecked())
        self.assertEqual(self.Window.Controller.History.Limit,12);self.assertEqual(self.Window.Controller.History.ByteLimit,8*1024*1024)

    def test_hierarchy_scene_shows_per_scene_dirty_marker(self) -> None:
        self.Window.Controller.RefreshHierarchy()
        scene=self.Window.Hierarchy.Tree.topLevelItem(0);scene_id=str(scene.data(0,Qt.ItemDataRole.UserRole))
        self.Window.Controller.SetDirty(True,[scene_id])
        self.assertTrue(scene.text(0).endswith(" *"))
        self.assertTrue(self.Window.Controller.IsDirty)
        self.Window.Controller.SetDirty(False)
        self.assertFalse(scene.text(0).endswith(" *"))
        self.assertFalse(self.Window.Controller.IsDirty)

    def test_orientation_preference_is_saved_and_disables_hit_testing(self) -> None:
        from copy import deepcopy
        saved=[];self.Window._settings_saver=lambda value:saved.append(deepcopy(value))
        dialog=PreferencesDialog(self.Window)
        dialog.Controls["orientation_visible"].setChecked(False);dialog._Apply()
        self.assertFalse(saved[-1]["preferences"]["scene"]["orientation_visible"])
        surface=self.Window.Scene.Surface;surface.resize(640,480)
        self.assertFalse(surface._orientation_visible)
        self.assertFalse(surface._PickOrientation(QPoint(610,44)))
        self.Window.ApplyPreferences(saved[-1]["preferences"],save=False)
        self.assertFalse(surface._orientation_visible)

    def test_edit_menu_duplicates_hierarchy_and_preserves_clipboard(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        runtime=self.Window.Runtime;controller=self.Window.Controller
        parent=runtime.CreateEntity("Original");child=runtime.CreateEntity("Child",parent)
        runtime.AddComponent(child,"Light");runtime.SetComponentEnabled(child,"Light",False)
        controller.SelectEntity(parent)
        old_clipboard=controller._hierarchy_clipboard
        self.Window.ExecuteEditCommand("duplicate")
        duplicate=controller.SelectedEntity
        self.assertNotEqual(duplicate,parent)
        self.assertIs(controller._hierarchy_clipboard,old_clipboard)
        self.assertEqual(runtime.EntityDetails(duplicate)["name"],"Original Copy")
        duplicate_children=[entity for entity in runtime.Entities() if entity["parent"]==duplicate]
        self.assertEqual(len(duplicate_children),1)
        self.assertFalse(runtime.EntityDetails(duplicate_children[0]["uuid"])["component_enabled"]["Light"])
        self.assertEqual(self.Window.MenuBar.EditActions["duplicate"].shortcut().toString(),"Ctrl+D")
        controller.Undo()
        self.assertNotIn(duplicate,[entity["uuid"] for entity in runtime.Entities()])

    def test_edit_menu_copy_routes_to_text_instead_of_entities(self) -> None:
        from unittest.mock import patch
        field=QLineEdit("Example");field.selectAll()
        with patch.object(self.Window,"_EditContext",return_value=("text",field)):
            self.Window.ExecuteEditCommand("copy")
            self.assertEqual(QApplication.clipboard().text(),"Example")
            self.Window.ExecuteEditCommand("delete")
            self.assertEqual(field.text(),"")

    def test_loaded_scene_cannot_be_activated_again_from_assets(self) -> None:
        browser=self.Window.AssetBrowser;activated=[];browser.AssetActivated.connect(activated.append)
        item=QListWidgetItem("Loaded");item.setData(Qt.ItemDataRole.UserRole,Path("Loaded.bscene"))
        browser.SceneLoadedChecker=lambda _path:True
        browser._Activate(item)
        self.assertEqual(activated,[])

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
        camera = self.Window.MenuBar.CameraMenu
        self.assertIsNotNone(camera)
        for menu in (view, camera):
            menu.resize(menu.sizeHint())
            self.assertTrue(all(menu.actionGeometry(action).right() <= menu.width()
                            for action in menu.actions()))

    def test_view_menu_owns_viewport_controls_not_the_theme(self) -> None:
        menu=self.Window.MenuBar
        self.assertFalse(hasattr(menu,"ThemeMenu"));self.assertEqual(menu.FocusSelectedAction.shortcut().toString(),"F")
        self.assertEqual(tuple(menu.CameraActions),("perspective","top","bottom","left","right","front","back"))
        menu.CameraPresetRequested.emit("top");self.assertEqual(self.Window.Scene.Surface._pitch,89.0)
        menu.GridAction.setChecked(False);self.assertFalse(self.Window.Scene.GridToggle.isChecked())

    def test_viewport_maximize_restores_workspace(self) -> None:
        before=self.Window.Docking.save_layout();self.Window.SetViewportMaximized(True)
        self.assertIsNotNone(self.Window._pre_maximize_layout);self.Window.SetViewportMaximized(False)
        self.assertIsNone(self.Window._pre_maximize_layout);self.assertEqual(self.Window.Docking.save_layout()["root"],before["root"])

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

    def test_primitive_entity_creation_and_shape_specific_inspector(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        before={item["uuid"] for item in self.Window.Runtime.Entities()}
        self.Window.Controller.CreateTypedEntity("Primitive Object:6",None)
        entity_id=next(item["uuid"] for item in self.Window.Runtime.Entities() if item["uuid"] not in before)
        details=self.Window.Runtime.EntityDetails(entity_id)
        self.assertIn("Primitive Object",details["components"])
        fields=details["component_data"]["Primitive Object"]
        self.assertEqual(fields["Shape"],6);self.assertIn("Major Radius",fields);self.assertIn("Minor Radius",fields);self.assertNotIn("Height",fields)
        self.Window.Controller.SelectEntity(entity_id,force=True)
        section=self.Window.Properties._sections["runtime.Primitive Object"]
        self.assertIn("Shape",section._fields);self.assertIn("Major Radius",section._fields)

    def test_scene_click_hit_tests_primitive_objects(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        entity_id=self.Window.Runtime.CreateEntity("Pickable Cube");self.assertTrue(self.Window.Runtime.AddComponent(entity_id,"Primitive Object"))
        surface=self.Window.Scene.Surface;surface.resize(640,480);projected=surface._Project((0,0,0));self.assertIsNotNone(projected)
        self.assertEqual(surface._PickSceneObject(QPoint(round(projected[0]),round(projected[1]))),entity_id)

    def test_primitive_picking_rejects_space_above_plane_and_torus_hole(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        plane=self.Window.Runtime.CreateEntity("Precise Plane");self.Window.Runtime.AddComponent(plane,"Primitive Object");self.Window.Runtime.SetComponentProperty(plane,"Primitive Object","Shape",4)
        self.assertEqual(self.Window.Runtime.PickPrimitive((0,1,2),(0,0,-1)),"")
        self.assertEqual(self.Window.Runtime.PickPrimitive((0,2,0),(0,-1,0)),plane)
        cube=self.Window.Runtime.CreateEntity("Cube above plane");self.Window.Runtime.AddComponent(cube,"Primitive Object");self.Window.Runtime.SetTransform(cube,(0,1,0),(0,0,0,1),(1,1,1))
        self.assertEqual(self.Window.Runtime.PickPrimitive((0,3,0),(0,-1,0)),cube)
        self.Window.Runtime.DestroyEntity(cube);self.Window.Runtime.DestroyEntity(plane)
        torus=self.Window.Runtime.CreateEntity("Torus hole");self.Window.Runtime.AddComponent(torus,"Primitive Object");self.Window.Runtime.SetComponentProperty(torus,"Primitive Object","Shape",6)
        self.assertEqual(self.Window.Runtime.PickPrimitive((0,2,0),(0,-1,0)),"")

    def test_plane_is_selected_by_scene_mouse_click(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        plane=self.Window.Runtime.CreateEntity("Clickable plane");self.Window.Runtime.AddComponent(plane,"Primitive Object");self.Window.Runtime.SetComponentProperty(plane,"Primitive Object","Shape",4)
        surface=self.Window.Scene.Surface;surface.resize(640,480);surface._UpdateCamera();surface.SetGizmoMode(GizmoMode.Select)
        projected=surface._Project((0,0,0));point=QPointF(round(projected[0]),round(projected[1]))
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,point,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,point,Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.assertEqual(self.Window.Controller.SelectedEntity,plane)

    def test_switching_selected_primitives_and_hierarchy_refresh_is_stable(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        first=self.Window.Runtime.CreateEntity("First clickable");second=self.Window.Runtime.CreateEntity("Second clickable")
        for entity in (first,second):self.Window.Runtime.AddComponent(entity,"Primitive Object")
        self.Window.Runtime.SetTransform(second,(2,0,0),(0,0,0,1),(1,1,1))
        surface=self.Window.Scene.Surface;surface.resize(640,480);surface._UpdateCamera();surface.SetGizmoMode(GizmoMode.Translate)
        self.Window.Controller.SelectSceneEntity(first)
        projected=surface._Project((2,0,0));center=QPoint(round(projected[0]),round(projected[1]))
        # The first object's X handle overlaps the second object's center.
        # Click its visible geometry outside the overlay to switch selection.
        target=next((QPoint(x,y) for y in range(center.y()-40,center.y()+41,2) for x in range(center.x()-40,center.x()+41,2) if surface._PickSceneObject(QPoint(x,y))==second and surface._PickGizmo(QPoint(x,y)) is None),None)
        self.assertIsNotNone(target);point=QPointF(target)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,point,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,point,Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.assertEqual(self.Window.Controller.SelectedEntity,second)
        self.Window.Controller.SelectEntities([first,second]);self.Window.Controller.RefreshHierarchy()
        self.assertEqual(self.Window.Controller.SelectedEntities,[first,second]);self.assertEqual(set(self.Window.Hierarchy.GetSelectedData()),{first,second})

    def test_primitive_drag_starts_marquee_and_gizmo_remains_pickable(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        entity_id=self.Window.Runtime.CreateEntity("Interactive Cube");self.Window.Runtime.AddComponent(entity_id,"Primitive Object")
        surface=self.Window.Scene.Surface;surface.resize(640,480);surface.SetGizmoMode(GizmoMode.Translate);self.Window.Controller.SelectEntity(entity_id)
        projected=surface._Project((0,0,0));point=QPoint(round(projected[0]),round(projected[1]))
        object_point=next((QPoint(x,y) for y in range(point.y()-45,point.y()+46,3) for x in range(point.x()-45,point.x()+46,3) if surface._PickSceneObject(QPoint(x,y))==entity_id and surface._PickGizmo(QPoint(x,y)) is None),None);self.assertIsNotNone(object_point)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(object_point),Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        destination=object_point+QPoint(120,80);surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(destination),Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        self.assertTrue(surface._selection_box_dragging);self.assertIsNotNone(surface._selection_band)
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,QPointF(destination),Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.Window.Controller.SelectEntity(entity_id);surface.SetGizmoMode(GizmoMode.Translate)
        handle=next((QPoint(x,y) for y in range(max(0,point.y()-120),min(surface.height(),point.y()+121),4) for x in range(max(0,point.x()-120),min(surface.width(),point.x()+121),4) if surface._PickGizmo(QPoint(x,y)) is not None),None)
        self.assertIsNotNone(handle)
        before=self.Window.Runtime.EntityDetails(entity_id)["position"]
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(handle),Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(handle+QPoint(48,24)),Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,QPointF(handle+QPoint(48,24)),Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.assertNotEqual(self.Window.Runtime.EntityDetails(entity_id)["position"],before)

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

    def test_hierarchy_search_survives_controller_refresh(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        camera=self.Window.Runtime.CreateEntity("Search Camera");other=self.Window.Runtime.CreateEntity("Unrelated")
        self.Window.Controller.RefreshHierarchy();panel=self.Window.Hierarchy
        panel.Search.setText("camera");self.Window.Controller.RefreshHierarchy()
        from PySide6.QtWidgets import QTreeWidgetItemIterator
        items={};iterator=QTreeWidgetItemIterator(panel.Tree)
        while iterator.value() is not None:
            item=iterator.value();items[str(item.data(0,Qt.ItemDataRole.UserRole))]=item;iterator+=1
        self.assertFalse(items[camera].isHidden());self.assertTrue(items[other].isHidden())
        self.assertEqual(panel.Search.text(),"camera")

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

    def test_multi_selection_pivot_and_center_transform_semantics(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        first=self.Window.Runtime.CreateEntity("Pivot First");second=self.Window.Runtime.CreateEntity("Pivot Second")
        self.Window.Runtime.SetTransform(first,(0,0,0),(0,0,0,1),(1,1,1));self.Window.Runtime.SetTransform(second,(4,0,0),(0,0,0,1),(1,1,1))
        self.Window.Controller.SelectEntities([first,second])
        self.Window.Scene.PivotMode.setCurrentText("Pivot")
        self.assertTrue(self.Window.Controller.ApplyGizmoRotation(Vec3(0,1,0),0.25))
        self.assertEqual(self.Window.Runtime.EntityDetails(first)["position"],(0.0,0.0,0.0))
        self.assertEqual(self.Window.Runtime.EntityDetails(second)["position"],(4.0,0.0,0.0))
        self.assertTrue(self.Window.Controller.ApplyGizmoScale(Vec3(2,2,2)))
        self.assertEqual(self.Window.Runtime.EntityDetails(first)["position"],(0.0,0.0,0.0))
        self.assertEqual(self.Window.Runtime.EntityDetails(second)["position"],(4.0,0.0,0.0))
        self.Window.Scene.PivotMode.setCurrentText("Center")
        self.assertTrue(self.Window.Controller.ApplyGizmoScale(Vec3(2,1,1)))
        self.assertEqual(self.Window.Runtime.EntityDetails(first)["position"],(-2.0,0.0,0.0))
        self.assertEqual(self.Window.Runtime.EntityDetails(second)["position"],(6.0,0.0,0.0))

    def test_scene_box_selection_works_outside_select_tool(self) -> None:
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
        entity=self.Window.Runtime.CreateEntity("Box selected");surface=self.Window.Scene.Surface;surface.resize(400,400);surface.SetGizmoMode(GizmoMode.Translate);self.Window.Controller.SelectEntity(entity)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(1,1),Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
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

    def test_scene_returns_to_orbit_after_fly_navigation_ends(self) -> None:
        surface=self.Window.Scene.Surface;surface._UpdateCamera()
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        surface.keyPressEvent(QKeyEvent(QKeyEvent.Type.KeyPress,Qt.Key.Key_W,Qt.KeyboardModifier.NoModifier));surface._FlyTick()
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.assertFalse(surface._fly_navigation);self.assertEqual(surface._keys,set())
        focus=tuple(surface._target);eye=surface._eye
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove,QPointF(140,120),Qt.MouseButton.NoButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        self.assertEqual(tuple(surface._target),focus);self.assertNotEqual(surface._eye,eye)

    def test_frame_all_shortcut_is_suspended_during_right_mouse_navigation(self) -> None:
        surface=self.Window.Scene.Surface;action=self.Window.MenuBar.FrameAllAction
        self.assertTrue(action.isEnabled())
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.RightButton,Qt.KeyboardModifier.NoModifier))
        self.assertFalse(action.isEnabled())
        surface.keyPressEvent(QKeyEvent(QKeyEvent.Type.KeyPress,Qt.Key.Key_A,Qt.KeyboardModifier.NoModifier))
        self.assertIn(Qt.Key.Key_A,surface._keys)
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease,QPointF(100,100),Qt.MouseButton.RightButton,Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier))
        self.assertTrue(action.isEnabled())

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
        self.assertFalse(parent_item.isExpanded()) # New object groups start collapsed.
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
