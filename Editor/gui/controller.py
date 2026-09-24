"""Coordinates editor widgets with the private runtime bridge."""

from __future__ import annotations

from pathlib import Path
from math import cos, sin

from PySide6.QtCore import QObject, QTimer
from PySide6.QtWidgets import QFileDialog, QMenu

from ..runtime import RuntimeService
from .panels import ConsoleLevel
from .gizmos import GizmoMode
from .widgets import BoolInput, EnumInput, FloatInput, IntInput, PlayState, StringInput, Vec3Input, Vec4Input


class EditorController(QObject):
    def __init__(self, window, runtime: RuntimeService) -> None:  # type: ignore[no-untyped-def]
        super().__init__(window)
        self.Window = window
        self.Runtime = runtime
        self.SelectedEntity = ""
        self.ScenePath = ""
        self._updating_inspector = False
        self.IsDirty = False
        self.Timer = QTimer(self)
        self.Timer.setInterval(16)
        self.Timer.timeout.connect(runtime.Tick)

        window.MenuBar.OpenSceneRequested.connect(self.OpenSceneDialog)
        window.MenuBar.SaveSceneRequested.connect(self.SaveScene)
        window.MenuBar.SaveSceneAsRequested.connect(self.SaveSceneAsDialog)
        window.Toolbar.PlayRequested.connect(self.Play)
        window.Toolbar.StopRequested.connect(self.Stop)
        window.Toolbar.PauseRequested.connect(runtime.Pause)
        window.Toolbar.StepRequested.connect(runtime.Step)
        window.Toolbar.GizmoModeChanged.connect(self._GizmoModeChanged)
        window.Hierarchy.SelectionChanged.connect(self.SelectEntity)
        window.Hierarchy.CreateRequested.connect(self.CreateEntity)
        window.Hierarchy.CreateTypedRequested.connect(self.CreateTypedEntity)
        window.Hierarchy.ReparentRequested.connect(self.ReparentEntity)
        window.Hierarchy.DeleteRequested.connect(self.DeleteEntity)
        window.Properties.AddComponentRequested.connect(self.ShowAddComponentMenu)
        window.Properties.RemoveComponentRequested.connect(self.RemoveComponent)
        runtime.SceneChanged.connect(self.RefreshHierarchy)
        runtime.ProjectChanged.connect(self._ProjectLoaded)
        runtime.ErrorOccurred.connect(lambda text: window.Console.AddMessage(text, ConsoleLevel.Error))
        window.Scene.Surface.TranslationDragged.connect(self.ApplyGizmoTranslation)
        window.Scene.Surface.RotationDragged.connect(self.ApplyGizmoRotation)
        window.Scene.Surface.ScaleDragged.connect(self.ApplyGizmoScale)
        window.Scene.Surface.EntityPicked.connect(self.SelectSceneEntity)
        window.Scene.Surface.Attached.connect(self._UpdateGizmo)
        if not runtime.IsAvailable():
            window.Console.AddMessage(runtime.LastError(), ConsoleLevel.Warning)
        else:
            QTimer.singleShot(100, self.Timer.start)

    def OpenSceneDialog(self) -> None:
        dialog=QFileDialog(self.Window,self.Window.Localization.Translate("dialog.open_scene"),self.Runtime.AssetDirectory(),self.Window.Localization.Translate("dialog.scene_filter"))
        dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setFileMode(QFileDialog.FileMode.ExistingFile);dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptOpen)
        path=dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""
        if path and self.Runtime.LoadScene(path): self.ScenePath = path

    def SaveScene(self) -> None:
        if self.ScenePath:
            if self.Runtime.SaveScene(self.ScenePath): self.SetDirty(False)
        else: self.SaveSceneAsDialog()

    def SaveSceneAsDialog(self) -> None:
        dialog=QFileDialog(self.Window,self.Window.Localization.Translate("dialog.save_scene"),self.Runtime.AssetDirectory(),self.Window.Localization.Translate("dialog.scene_filter"))
        dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)
        path=dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""
        if path and self.Runtime.SaveScene(path): self.ScenePath = path; self.SetDirty(False)

    def _ProjectLoaded(self, path: str) -> None:
        self.ScenePath = str(self.Runtime.SceneInfo().get("path", ""))
        assets = self.Runtime.AssetDirectory()
        self.Window.AssetBrowser.SetProjectRoot(assets or Path(path).parent)
        self.Window.Console.AddMessage(
            self.Window.Localization.Translate("console.project_loaded", path=path)
        )

    def RefreshHierarchy(self) -> None:
        selected = self.SelectedEntity
        self.Window.Hierarchy.Clear()
        items = {}
        entities = self.Runtime.Entities()
        scene_info = self.Runtime.SceneInfo()
        theme = "light" if self.Window.ThemeManager.GetTheme().background == "#d4d4d4" else "dark"
        scene_root = self.Window.Hierarchy.AddItem(
            str(scene_info.get("name", "Untitled")), str(scene_info.get("uuid", "")),
            icon=self.Window.Resources.Icon(f"icons/{theme}/scene.svg"), kind="scene")
        scene_root.setExpanded(True)
        for entity in entities:
            parent = items.get(entity["parent"], scene_root)
            items[entity["uuid"]] = self.Window.Hierarchy.AddItem(
                entity["name"], entity["uuid"], parent,
                self.Window.Resources.Icon(f"icons/{theme}/obj.svg"), "entity"
            )
        if selected in items:
            self.Window.Hierarchy.Tree.setCurrentItem(items[selected])
        elif selected:
            self.SelectEntity(None)

    def SelectEntity(self, entity_id) -> None:  # type: ignore[no-untyped-def]
        self.SelectedEntity = str(entity_id or "")
        self.Window.Properties.Clear()
        if not self.SelectedEntity:
            self.Window.Scene.Surface.SetSelection(None);self._UpdateGizmo();return
        details = self.Runtime.EntityDetails(self.SelectedEntity)
        if not details:self._UpdateGizmo();return
        self.Window.Scene.Surface.SetSelection(details.get("world_position", details["position"]))
        identity = self.Window.Properties.AddComponentSection("identity", "Entity", removable=False)
        name = StringInput(str(details["name"]));name.editingFinished.connect(lambda:self._Rename(name.text()))
        identity.AddField("Name", name)
        transform = self.Window.Properties.AddComponentSection("transform", "Transform", removable=False)
        position = Vec3Input(details["position"])
        rotation = Vec4Input(details["rotation"])
        scale = Vec3Input(details["scale"])
        transform.AddField("Position", position)
        transform.AddField("Rotation", rotation)
        transform.AddField("Scale", scale)
        def Commit(_value=None) -> None:
            if not self._updating_inspector:
                self.Runtime.SetTransform(self.SelectedEntity, position.GetValue(),
                                          rotation.GetValue(), scale.GetValue())
        position.ValueChanged.connect(Commit); rotation.ValueChanged.connect(Commit)
        scale.ValueChanged.connect(Commit)
        for field in (position, rotation, scale): field.ValueChanged.connect(lambda _=None:self.SetDirty(True))
        component_data = details.get("component_data", {})
        for component in details.get("components", ())[1:]:
            if component != "Transform":
                section = self.Window.Properties.AddComponentSection(
                    f"runtime.{component}", str(component), expanded=False
                )
                for property_name, value in dict(component_data.get(component, {})).items():
                    editor = self._ComponentEditor(component, property_name, value)
                    if editor is not None: section.AddField(property_name, editor)

        self._UpdateGizmo()

    def SelectSceneEntity(self,entity_id:str)->None:
        self.SelectEntity(entity_id);self.RefreshHierarchy()

    def _ComponentEditor(self, component: str, name: str, value):  # type: ignore[no-untyped-def]
        if isinstance(value, bool):
            editor=BoolInput(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(component,name,v));return editor
        if isinstance(value, int):
            if component=="Light" and name=="Type":
                editor=EnumInput();editor.SetOptions((("Directional",0),("Sun",1),("Point",2),("Spot",3)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Projection":
                editor=EnumInput();editor.SetOptions((("Perspective",0),("Orthographic",1)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Aspect Mode":
                editor=EnumInput();editor.SetOptions((("Automatic",0),("Fixed",1)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Anti Aliasing":
                editor=EnumInput();editor.SetOptions((("None",0),("FXAA",1),("TAA",2)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Tone Mapping":
                editor=EnumInput();editor.SetOptions((("Linear",0),("Filmic",1),("ACES",2)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(component,name,editor.GetValue()));return editor
            if component=="Scene Query Bounds" and name=="Shape":
                editor=EnumInput();editor.SetOptions((("Box",0),("Sphere",1)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(component,name,editor.GetValue()));return editor
            editor=IntInput(value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(component,name,v));return editor
        if isinstance(value, float):
            editor=FloatInput(value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(component,name,v));return editor
        if isinstance(value, (tuple,list)) and len(value) in (3,4) and all(isinstance(v,(int,float)) for v in value):
            editor=Vec3Input(value) if len(value)==3 else Vec4Input(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(component,name,v));return editor
        if isinstance(value, str):
            editor=StringInput(value);editor.editingFinished.connect(lambda:self._CommitComponent(component,name,editor.GetValue()));return editor
        return None

    def _CommitComponent(self, component: str, name: str, value) -> None:  # type: ignore[no-untyped-def]
        if self.SelectedEntity and self.Runtime.SetComponentProperty(self.SelectedEntity,component,name,value): self.SetDirty(True)

    def _Rename(self, name: str) -> None:
        if self.SelectedEntity and name.strip() and self.Runtime.Rename(self.SelectedEntity,name.strip()):self.SetDirty(True);self.RefreshHierarchy()

    def SetDirty(self, dirty: bool = True) -> None:
        self.IsDirty=dirty;title=self.Window.windowTitle().rstrip(" *")
        self.Window.setWindowTitle(title + (" *" if dirty else ""))

    def _GizmoModeChanged(self, mode) -> None: self.Window.Scene.Surface.SetGizmoMode(mode);self._UpdateGizmo()
    def _UpdateGizmo(self) -> None:
        modes={GizmoMode.Select:0,GizmoMode.Translate:1,GizmoMode.Rotate:2,GizmoMode.Scale:3}
        self.Runtime.SetGizmo(self.SelectedEntity, modes[self.Window.Toolbar.GetGizmoMode()])

    def CreateEntity(self, parent) -> None:  # type: ignore[no-untyped-def]
        self.Runtime.CreateEntity(self.Window.Localization.Translate("entity.new"), str(parent or ""))
        self.SetDirty(True)

    def CreateTypedEntity(self, component_type: str, parent) -> None:  # type: ignore[no-untyped-def]
        entity_id = self.Runtime.CreateEntity(component_type if component_type != "Entity" else self.Window.Localization.Translate("entity.new"), str(parent or ""))
        if entity_id and component_type != "Entity": self.Runtime.AddComponent(entity_id, component_type)
        if entity_id:self.SetDirty(True)

    def ShowAddComponentMenu(self) -> None:
        if not self.SelectedEntity: return
        menu = QMenu(self.Window.Properties)
        existing = set(self.Runtime.EntityDetails(self.SelectedEntity).get("components", ()))
        for component_type in self.Runtime.ComponentTypes():
            action = menu.addAction(component_type); action.setEnabled(component_type not in existing)
            action.triggered.connect(lambda _=False, name=component_type: self._AddComponent(name))
        button = self.Window.Properties.AddComponentButton
        menu.exec(button.mapToGlobal(button.rect().topLeft()))

    def _AddComponent(self, component_type: str) -> None:
        if self.SelectedEntity and self.Runtime.AddComponent(self.SelectedEntity, component_type):
            self.SetDirty(True)
            self.SelectEntity(self.SelectedEntity)

    def RemoveComponent(self, component_id: str) -> None:
        if not component_id.startswith("runtime.") or not self.SelectedEntity:return
        if self.Runtime.RemoveComponent(self.SelectedEntity,component_id.removeprefix("runtime.")):
            self.SetDirty(True);self.SelectEntity(self.SelectedEntity)

    def DeleteEntity(self, entity_id) -> None:  # type: ignore[no-untyped-def]
        if entity_id and self.Runtime.DestroyEntity(str(entity_id)): self.SetDirty(True)

    def ReparentEntity(self, entity_id: str, parent_id: str) -> None:
        if self.Runtime.SetParent(entity_id,parent_id):self.SetDirty(True)

    def Play(self) -> None:
        if self.Runtime.Play(): pass
        else: self.Window.Toolbar.SetPlayState(PlayState.Stopped)

    def Stop(self) -> None:
        self.Runtime.Stop()

    def ApplyGizmoTranslation(self, delta) -> bool:  # type: ignore[no-untyped-def]
        changed=bool(self.SelectedEntity and self.Runtime.Translate(
            self.SelectedEntity, (delta.X, delta.Y, delta.Z)
        ));self.SetDirty(changed or self.IsDirty);self._UpdateGizmo();return changed

    def ApplyGizmoRotation(self, axis, angle: float) -> bool:  # type: ignore[no-untyped-def]
        details=self.Runtime.EntityDetails(self.SelectedEntity) if self.SelectedEntity else {}
        if not details:return False
        x,y,z,w=details["rotation"];s=sin(angle*.5);dx,dy,dz,dw=axis.X*s,axis.Y*s,axis.Z*s,cos(angle*.5)
        rotation=(dw*x+dx*w+dy*z-dz*y,dw*y-dx*z+dy*w+dz*x,dw*z+dx*y-dy*x+dz*w,dw*w-dx*x-dy*y-dz*z)
        changed=self.Runtime.SetTransform(self.SelectedEntity,details["position"],rotation,details["scale"])
        if changed:self.SetDirty(True);self._UpdateGizmo()
        return changed

    def ApplyGizmoScale(self, factor) -> bool:  # type: ignore[no-untyped-def]
        details=self.Runtime.EntityDetails(self.SelectedEntity) if self.SelectedEntity else {}
        if not details:return False
        scale=tuple(a*b for a,b in zip(details["scale"],(factor.X,factor.Y,factor.Z)))
        changed=self.Runtime.SetTransform(self.SelectedEntity,details["position"],details["rotation"],scale)
        if changed:self.SetDirty(True);self._UpdateGizmo()
        return changed


__all__ = ["EditorController"]
