"""Coordinates editor widgets with the private runtime bridge."""

from __future__ import annotations

from pathlib import Path
from math import cos, sin

from PySide6.QtCore import QObject, QSignalBlocker, QTimer
from PySide6.QtWidgets import QFileDialog, QLabel, QMenu

from ..runtime import RuntimeService
from ..history import SceneHistory
from .panels import ConsoleLevel
from .gizmos import GizmoMode, Vec3
from .widgets import AssetPickerInput, BoolInput, ColorInput, EnumInput, FloatInput, IntInput, PlayState, StringInput, UIntInput, Vec3Input, Vec4Input


class EditorController(QObject):
    def __init__(self, window, runtime: RuntimeService) -> None:  # type: ignore[no-untyped-def]
        super().__init__(window)
        self.Window = window
        self.Runtime = runtime
        self.SelectedEntity = ""
        self.SelectedEntities: list[str] = []
        self.ScenePath = ""
        self._updating_inspector = False
        self.IsDirty = False
        self.History=SceneHistory(runtime,self)
        self.Timer = QTimer(self)
        self.Timer.setInterval(16)
        self.Timer.timeout.connect(self._Tick)

        window.MenuBar.OpenSceneRequested.connect(self.OpenSceneDialog)
        window.MenuBar.SaveSceneRequested.connect(self.SaveScene)
        window.MenuBar.SaveSceneAsRequested.connect(self.SaveSceneAsDialog)
        window.MenuBar.UndoRequested.connect(self.Undo)
        window.MenuBar.RedoRequested.connect(self.Redo)
        window.Toolbar.PlayRequested.connect(self.Play)
        window.Toolbar.StopRequested.connect(self.Stop)
        window.Toolbar.PauseRequested.connect(runtime.Pause)
        window.Toolbar.StepRequested.connect(runtime.Step)
        window.Toolbar.GizmoModeChanged.connect(self._GizmoModeChanged)
        window.Hierarchy.SelectionChanged.connect(self.SelectEntity)
        window.AssetBrowser.SelectionCleared.connect(lambda:self.SelectEntity(None))
        window.Hierarchy.CreateRequested.connect(self.CreateEntity)
        window.Hierarchy.CreateTypedRequested.connect(self.CreateTypedEntity)
        window.Hierarchy.ReparentRequested.connect(self.ReparentEntity)
        window.Hierarchy.AssetDropped.connect(self.InstantiateAsset)
        window.Hierarchy.DeleteRequested.connect(self.DeleteEntity)
        window.Properties.AddComponentRequested.connect(self.ShowAddComponentMenu)
        window.Properties.RemoveComponentRequested.connect(self.RemoveComponent)
        window.AssetBrowser.AssetSelected.connect(self.SelectAsset)
        runtime.SceneChanged.connect(self.RefreshHierarchy)
        runtime.ProjectChanged.connect(self._ProjectLoaded)
        runtime.ErrorOccurred.connect(lambda text: window.Console.AddMessage(text, ConsoleLevel.Error))
        window.Scene.Surface.TranslationDragged.connect(self.ApplyGizmoTranslation)
        window.Scene.Surface.RotationDragged.connect(self.ApplyGizmoRotation)
        window.Scene.Surface.ScaleDragged.connect(self.ApplyGizmoScale)
        window.Scene.Surface.GizmoDragFinished.connect(self._FinishGizmoDrag)
        window.Scene.Surface.GizmoDragStarted.connect(lambda:self.History.Begin("Transform selection"))
        window.Scene.Surface.EntityPicked.connect(self.SelectSceneEntity)
        window.Scene.Surface.EntitiesBoxSelected.connect(self.SelectSceneBox)
        window.Scene.Surface.SelectionBoxStarted.connect(self.BeginSceneBoxSelection)
        window.Scene.Surface.Attached.connect(self._UpdateGizmo)
        # The toolbar selects Translate before this controller is constructed,
        # so its initial GizmoModeChanged signal has already been emitted.
        window.Scene.Surface.SetGizmoMode(window.Toolbar.GetGizmoMode())
        self.History.Changed.connect(self._HistoryChanged);self._HistoryChanged()
        if not runtime.IsAvailable():
            window.Console.AddMessage(runtime.LastError(), ConsoleLevel.Warning)
        else:
            QTimer.singleShot(100, self.Timer.start)

    def OpenSceneDialog(self) -> None:
        dialog=QFileDialog(self.Window,self.Window.Localization.Translate("dialog.open_scene"),self.Runtime.AssetDirectory(),self.Window.Localization.Translate("dialog.scene_filter"))
        dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setFileMode(QFileDialog.FileMode.ExistingFile);dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptOpen)
        path=dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""
        if path and self.Runtime.LoadScene(path): self.ScenePath = path

    def _Tick(self) -> None:
        self.Runtime.Tick()
        self.Window.Output.SetGameCameraAvailable(self.Runtime.HasActiveCamera())
        # Editor gizmos follow the authoritative world transform every frame.
        # This also covers transforms changed by systems or native user code.
        if self.SelectedEntity:self._UpdateGizmo()

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
        self.History.Clear()
        self.ScenePath = str(self.Runtime.SceneInfo().get("path", ""))
        assets = self.Runtime.AssetDirectory()
        self.Window.AssetBrowser.SetProjectRoot(assets or Path(path).parent)
        self.Window.Console.AddMessage(
            self.Window.Localization.Translate("console.project_loaded", path=path)
        )

    def RefreshHierarchy(self) -> None:
        self.Window.Output.SetGameCameraAvailable(self.Runtime.HasActiveCamera())
        selected = list(self.SelectedEntities)
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
        if selected:
            blocker=QSignalBlocker(self.Window.Hierarchy.Tree)
            for value in selected:
                if value in items:items[value].setSelected(True)
            if selected[0] in items:self.Window.Hierarchy.Tree.setCurrentItem(items[selected[0]])
            del blocker
        elif self.SelectedEntity:
            self.SelectEntity(None)

    def SelectEntity(self, entity_id, force: bool = False) -> None:  # type: ignore[no-untyped-def]
        if isinstance(entity_id,(list,tuple)):
            self.SelectEntities([str(value) for value in entity_id]);return
        next_entity = str(entity_id or "")
        if not force and next_entity and next_entity==self.SelectedEntity and self.Window.Properties._sections:
            self._RefreshInspectorValues();self._UpdateGizmo();return
        self._updating_inspector = True
        try:self.Window.Properties.Clear()
        finally:self._updating_inspector = False
        self.SelectedEntity = next_entity
        self.SelectedEntities=[next_entity] if next_entity else []
        self.Window.Properties.AddComponentButton.setVisible(bool(next_entity))
        if not self.SelectedEntity:
            self.Window.Scene.Surface.SetSelection(None);self._UpdateGizmo();return
        details = self.Runtime.EntityDetails(self.SelectedEntity)
        if not details:self._UpdateGizmo();return
        self.Window.Scene.Surface.SetSelection(details.get("world_position", details["position"]))
        identity = self.Window.Properties.AddComponentSection("identity", "Entity", removable=False)
        inspected_entity=self.SelectedEntity
        name = StringInput(str(details["name"]));name.editingFinished.connect(lambda:self._Rename(inspected_entity,name.text()))
        identity.AddField("Name", name)
        transform = self.Window.Properties.AddComponentSection("transform", "Transform", removable=False,
            icon=self.Window.Resources.Icon("icons/comp_transform.svg"))
        position = Vec3Input(details["position"])
        rotation = Vec4Input(details["rotation"])
        scale = Vec3Input(details["scale"])
        transform.AddField("Position", position)
        transform.AddField("Rotation", rotation)
        transform.AddField("Scale", scale)
        def Commit(_value=None) -> None:
            if not self._updating_inspector:
                if inspected_entity==self.SelectedEntity and self._Mutate("Edit Transform",lambda:self.Runtime.SetTransform(inspected_entity,position.GetValue(),rotation.GetValue(),scale.GetValue())):self._UpdateGizmo()
        position.ValueChanged.connect(Commit); rotation.ValueChanged.connect(Commit)
        scale.ValueChanged.connect(Commit)
        for field in (position, rotation, scale): field.ValueChanged.connect(lambda _=None:self.SetDirty(True))
        component_data = details.get("component_data", {})
        for component in details.get("components", ())[1:]:
            if component != "Transform":
                section = self.Window.Properties.AddComponentSection(
                    f"runtime.{component}", str(component), expanded=False,
                    icon=self._ComponentIcon(str(component))
                )
                for property_name, value in dict(component_data.get(component, {})).items():
                    editor = self._ComponentEditor(inspected_entity, component, property_name, value)
                    if editor is not None: section.AddField(property_name, editor)

        self._UpdateGizmo()

    def SelectEntities(self,entity_ids:list[str])->None:
        unique=list(dict.fromkeys(value for value in entity_ids if value))
        if len(unique)<=1:self.SelectEntity(unique[0] if unique else None);return
        details=[self.Runtime.EntityDetails(value) for value in unique];details=[value for value in details if value]
        if len(details)<=1:self.SelectEntity(unique[0] if details else None);return
        self.SelectedEntities=unique;self.SelectedEntity=unique[0];self.Window.Properties.Clear();self.Window.Properties.AddComponentButton.setVisible(True)
        summary=self.Window.Properties.AddComponentSection("selection",self.Window.Localization.Translate("properties.multiple_entities",count=len(details)),removable=False)
        summary.AddField("Selection",QLabel(", ".join(str(value.get("name","")) for value in details)))
        center=tuple(sum(float(value["position"][axis]) for value in details)/len(details) for axis in range(3));last_center=[center];position=Vec3Input(center);summary.AddField("Center",position)
        def move_center(_value=None):
            target=position.GetValue();delta=tuple(target[i]-last_center[0][i] for i in range(3));self.History.Begin("Move selection")
            results=[self.Runtime.Translate(entity,delta) for entity in unique]
            if any(results):self.History.Commit();last_center[0]=target;self.SetDirty(True);self._UpdateGizmo()
            else:self.History.Cancel()
        position.ValueChanged.connect(move_center)
        shared=set(details[0].get("components",()))
        for value in details[1:]:shared.intersection_update(value.get("components",()))
        for component in sorted(shared-set(("Transform",))):
            section=self.Window.Properties.AddComponentSection(f"multi.{component}",component,expanded=False,icon=self._ComponentIcon(component))
            fields=[dict(value.get("component_data",{}).get(component,{})) for value in details]
            for name in set.intersection(*(set(value) for value in fields)) if fields else ():
                values=[value[name] for value in fields];section.AddField(name,QLabel(str(values[0]) if all(v==values[0] for v in values) else "— Mixed —"))
        self.Window.Scene.Surface.SetSelection(center);self._UpdateGizmo()

    def SelectSceneBox(self,entity_ids:list[str],additive:bool=False)->None:
        self.SelectEntities((self.SelectedEntities+entity_ids) if additive else entity_ids);self.RefreshHierarchy()

    def BeginSceneBoxSelection(self,additive:bool)->None:
        if not additive:
            blocker=QSignalBlocker(self.Window.Hierarchy.Tree);self.Window.Hierarchy.Tree.clearSelection();self.Window.Hierarchy.Tree.setCurrentItem(None);del blocker
            self.SelectEntity(None)

    def _HistoryChanged(self)->None:
        self.Window.MenuBar.UndoAction.setEnabled(self.History.CanUndo());self.Window.MenuBar.RedoAction.setEnabled(self.History.CanRedo())
        self.Window.MenuBar.UndoAction.setToolTip(self.History.UndoLabel());self.Window.MenuBar.RedoAction.setToolTip(self.History.RedoLabel())

    def _Mutate(self,label:str,operation):
        self.History.Begin(label)
        try:result=operation()
        except Exception:self.History.Cancel();raise
        if result:self.History.Commit()
        else:self.History.Cancel()
        return result

    def Undo(self)->None:
        if self.History.Undo():self.SetDirty(True);self.RefreshHierarchy();self.SelectEntities(self.SelectedEntities)

    def Redo(self)->None:
        if self.History.Redo():self.SetDirty(True);self.RefreshHierarchy();self.SelectEntities(self.SelectedEntities)

    def SelectAsset(self,path)->None:
        path=Path(path);self.SelectedEntity="";self.SelectedEntities=[];self.Window.Scene.Surface.SetSelection(None);self.Runtime.SetGizmo("",0);self.Window.Properties.Clear();self.Window.Properties.AddComponentButton.setVisible(False)
        section=self.Window.Properties.AddComponentSection("asset",self.Window.Localization.Translate("properties.asset"),removable=False)
        for label,value in (("Name",path.name),("Type",path.suffix.lower() or "Folder"),("Path",str(path)),("Size",str(path.stat().st_size) if path.is_file() else "—")):section.AddField(self.Window.Localization.Translate(f"properties.asset_{label.lower()}") if label!="Name" else self.Window.Localization.Translate("properties.asset_name"),QLabel(value))

    def _RefreshInspectorValues(self) -> None:
        if not self.SelectedEntity:return
        details=self.Runtime.EntityDetails(self.SelectedEntity)
        if not details:return
        values={"identity":{"Name":details.get("name","")},
                "transform":{"Position":details.get("position",(0,0,0)),
                             "Rotation":details.get("rotation",(0,0,0,1)),
                             "Scale":details.get("scale",(1,1,1))}}
        values.update({f"runtime.{name}":dict(fields) for name,fields in details.get("component_data",{}).items()})
        self._updating_inspector=True
        try:
            for section_id,fields in values.items():
                section=self.Window.Properties._sections.get(section_id)
                if section is None:continue
                for name,value in fields.items():
                    editor=section._fields.get(name);setter=getattr(editor,"SetValue",None) if editor else None
                    if callable(setter):
                        blocker=QSignalBlocker(editor);setter(value);del blocker
        finally:self._updating_inspector=False

    def SelectSceneEntity(self,entity_id:str)->None:
        self.SelectEntity(entity_id);self.RefreshHierarchy()

    def _ComponentIcon(self, component: str):
        names={"Camera":"comp_cam.svg","Light":"comp_light.svg","Mesh":"comp_mesh.svg",
               "Scene Query Bounds":"comp_sqb.svg","Gaussian Blur":"comp_gblur.svg","Vignette":"comp_vign.svg"}
        filename=names.get(component)
        return self.Window.Resources.Icon(f"icons/{filename}") if filename else None

    def _ComponentEditor(self, entity_id: str, component: str, name: str, value):  # type: ignore[no-untyped-def]
        if isinstance(value, bool):
            editor=BoolInput(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, int):
            if component=="Light" and name=="Type":
                editor=EnumInput();editor.SetOptions((("Directional",0),("Sun",1),("Point",2),("Spot",3)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Projection":
                editor=EnumInput();editor.SetOptions((("Perspective",0),("Orthographic",1)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Aspect Mode":
                editor=EnumInput();editor.SetOptions((("Automatic",0),("Fixed",1)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Anti Aliasing":
                editor=EnumInput();editor.SetOptions((("None",0),("FXAA",1),("TAA",2)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if component=="Camera" and name=="Tone Mapping":
                editor=EnumInput();editor.SetOptions((("Linear",0),("Filmic",1),("ACES",2)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if component=="Scene Query Bounds" and name=="Shape":
                editor=EnumInput();editor.SetOptions((("Box",0),("Sphere",1)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if value>2_147_483_647:
                editor=UIntInput(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
            editor=IntInput(value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, float):
            editor=FloatInput(value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, (tuple,list)) and len(value) in (3,4) and all(isinstance(v,(int,float)) for v in value):
            if "Color" in name:
                from PySide6.QtGui import QColor
                values=tuple(float(v) for v in value);alpha=values[3] if len(values)==4 else 1.0
                editor=ColorInput(QColor.fromRgbF(values[0],values[1],values[2],alpha));editor.ValueChanged.connect(lambda color:self._CommitComponent(entity_id,component,name,(color.redF(),color.greenF(),color.blueF(),color.alphaF()) if len(values)==4 else (color.redF(),color.greenF(),color.blueF())));return editor
            editor=Vec3Input(value) if len(value)==3 else Vec4Input(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, str):
            if "Asset" in name:
                editor=AssetPickerInput("Select asset");editor.SetValue(value,value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v or "0"));editor.PickRequested.connect(lambda:self._PickAsset(editor));return editor
            editor=StringInput(value);editor.editingFinished.connect(lambda:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
        return None

    def _PickAsset(self,editor:AssetPickerInput)->None:
        dialog=QFileDialog(self.Window,"Select Asset",self.Runtime.AssetDirectory());dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setFileMode(QFileDialog.FileMode.ExistingFile)
        if dialog.exec() and dialog.selectedFiles():editor.SetValue(dialog.selectedFiles()[0],Path(dialog.selectedFiles()[0]).name)

    def _CommitComponent(self, entity_id: str, component: str, name: str, value) -> None:  # type: ignore[no-untyped-def]
        if not self._updating_inspector and entity_id==self.SelectedEntity and self._Mutate(f"Edit {component}",lambda:self.Runtime.SetComponentProperty(entity_id,component,name,value)):self.SetDirty(True)

    def _Rename(self, entity_id: str, name: str) -> None:
        if not self._updating_inspector and entity_id==self.SelectedEntity and name.strip() and self._Mutate("Rename entity",lambda:self.Runtime.Rename(entity_id,name.strip())):self.SetDirty(True);self.RefreshHierarchy()

    def SetDirty(self, dirty: bool = True) -> None:
        self.IsDirty=dirty;title=self.Window.windowTitle().rstrip(" *")
        self.Window.setWindowTitle(title + (" *" if dirty else ""))

    def _GizmoModeChanged(self, mode) -> None: self.Window.Scene.Surface.SetGizmoMode(mode);self._UpdateGizmo()
    def _UpdateGizmo(self) -> None:
        modes={GizmoMode.Select:0,GizmoMode.Translate:1,GizmoMode.Rotate:2,GizmoMode.Scale:3}
        if len(self.SelectedEntities)>1:
            positions=[self.Runtime.EntityDetails(value).get("position") for value in self.SelectedEntities]
            positions=[value for value in positions if value]
            if positions:
                center=tuple(sum(value[axis] for value in positions)/len(positions) for axis in range(3));self.Window.Scene.Surface.SetSelection(center);self.Runtime.SetGizmoPosition(center,modes[self.Window.Toolbar.GetGizmoMode()]);return
        if self.SelectedEntity:
            details=self.Runtime.EntityDetails(self.SelectedEntity)
            if details:self.Window.Scene.Surface.SetSelection(details.get("world_position",details["position"]))
        self.Runtime.SetGizmo(self.SelectedEntity, modes[self.Window.Toolbar.GetGizmoMode()])

    def _FinishGizmoDrag(self) -> None:
        self.History.Commit()
        if self.SelectedEntity:self._RefreshInspectorValues();self._UpdateGizmo()

    def CreateEntity(self, parent) -> None:  # type: ignore[no-untyped-def]
        if self._Mutate("Create entity",lambda:bool(self.Runtime.CreateEntity(self.Window.Localization.Translate("entity.new"),str(parent or "")))):self.SetDirty(True)

    def CreateTypedEntity(self, component_type: str, parent) -> None:  # type: ignore[no-untyped-def]
        self.History.Begin(f"Create {component_type}")
        entity_id = self.Runtime.CreateEntity(component_type if component_type != "Entity" else self.Window.Localization.Translate("entity.new"), str(parent or ""))
        if entity_id and component_type != "Entity": self.Runtime.AddComponent(entity_id, component_type)
        if entity_id:self.History.Commit();self.SetDirty(True)
        else:self.History.Cancel()

    def ShowAddComponentMenu(self) -> None:
        if not self.SelectedEntity: return
        menu = QMenu(self.Window.Properties)
        targets=self.SelectedEntities or [self.SelectedEntity];existing=set.intersection(*(set(self.Runtime.EntityDetails(target).get("components",())) for target in targets))
        for component_type in self.Runtime.ComponentTypes():
            action = menu.addAction(component_type); action.setEnabled(component_type not in existing)
            action.triggered.connect(lambda _=False, name=component_type: self._AddComponent(name))
        button = self.Window.Properties.AddComponentButton
        menu.exec(button.mapToGlobal(button.rect().topLeft()))

    def _AddComponent(self, component_type: str) -> None:
        targets=self.SelectedEntities or ([self.SelectedEntity] if self.SelectedEntity else [])
        self.History.Begin(f"Add {component_type}")
        if targets and all(self.Runtime.AddComponent(target,component_type) or component_type in self.Runtime.EntityDetails(target).get("components",()) for target in targets):
            self.History.Commit()
            self.SetDirty(True)
            self.SelectEntities(targets) if len(targets)>1 else self.SelectEntity(targets[0],force=True)
        else:self.History.Cancel()

    def RemoveComponent(self, component_id: str) -> None:
        if not self.SelectedEntity:return
        prefix="multi." if component_id.startswith("multi.") else "runtime." if component_id.startswith("runtime.") else ""
        if not prefix:return
        component=component_id.removeprefix(prefix);targets=self.SelectedEntities or [self.SelectedEntity];self.History.Begin(f"Remove {component}");results=[self.Runtime.RemoveComponent(target,component) for target in targets]
        if any(results):self.History.Commit();self.SetDirty(True);self.SelectEntities(targets) if len(targets)>1 else self.SelectEntity(targets[0],force=True)
        else:self.History.Cancel()

    def InstantiateAsset(self,path:str,parent_id:str)->None:
        if Path(path).suffix.lower() in {".gltf",".glb",".obj",".fbx",".dae",".filamesh"}:
            self.History.Begin("Instantiate model");entity=self.Runtime.InstantiateModelPath(path,parent_id)
            if entity:self.History.Commit();self.SetDirty(True);self.SelectEntity(entity)
            else:self.History.Cancel()

    def DeleteEntity(self, entity_id) -> None:  # type: ignore[no-untyped-def]
        targets=entity_id if isinstance(entity_id,(list,tuple)) else [entity_id]
        self.History.Begin("Delete entities");results=[self.Runtime.DestroyEntity(str(value)) for value in targets if value]
        if any(results):self.History.Commit();self.SetDirty(True)
        else:self.History.Cancel()

    def ReparentEntity(self, entity_id: str, parent_id: str) -> None:
        if self._Mutate("Reparent entity",lambda:self.Runtime.SetParent(entity_id,parent_id)):
            self.SetDirty(True)
            if entity_id==self.SelectedEntity:self.SelectEntity(entity_id,force=True)

    def Play(self) -> None:
        if self.Runtime.Play(): pass
        else: self.Window.Toolbar.SetPlayState(PlayState.Stopped)

    def Stop(self) -> None:
        self.Runtime.Stop()

    def ApplyGizmoTranslation(self, delta) -> bool:  # type: ignore[no-untyped-def]
        targets=self.SelectedEntities or ([self.SelectedEntity] if self.SelectedEntity else [])
        results=[self.Runtime.Translate(entity,(delta.X,delta.Y,delta.Z)) for entity in targets];changed=any(results);self.SetDirty(changed or self.IsDirty);self._UpdateGizmo();return changed

    def ApplyGizmoRotation(self, axis, angle: float) -> bool:  # type: ignore[no-untyped-def]
        if len(self.SelectedEntities)>1:
            details=[self.Runtime.EntityDetails(value) for value in self.SelectedEntities]
            center=Vec3(*(sum(item["position"][index] for item in details)/len(details) for index in range(3)));unit=axis.Normalized();sine,cosine=sin(angle),cos(angle);half_sine=sin(angle*.5);changed=False
            for entity,item in zip(self.SelectedEntities,details):
                relative=Vec3(*item["position"])-center;rotated=relative*cosine+unit.Cross(relative)*sine+unit*(unit.Dot(relative)*(1-cosine));x,y,z,w=item["rotation"];dx,dy,dz,dw=unit.X*half_sine,unit.Y*half_sine,unit.Z*half_sine,cos(angle*.5);rotation=(dw*x+dx*w+dy*z-dz*y,dw*y-dx*z+dy*w+dz*x,dw*z+dx*y-dy*x+dz*w,dw*w-dx*x-dy*y-dz*z);position=(center.X+rotated.X,center.Y+rotated.Y,center.Z+rotated.Z);changed=self.Runtime.SetTransform(entity,position,rotation,item["scale"]) or changed
            if changed:self.SetDirty(True);self._UpdateGizmo()
            return changed
        details=self.Runtime.EntityDetails(self.SelectedEntity) if self.SelectedEntity else {}
        if not details:return False
        x,y,z,w=details["rotation"];s=sin(angle*.5);dx,dy,dz,dw=axis.X*s,axis.Y*s,axis.Z*s,cos(angle*.5)
        rotation=(dw*x+dx*w+dy*z-dz*y,dw*y-dx*z+dy*w+dz*x,dw*z+dx*y-dy*x+dz*w,dw*w-dx*x-dy*y-dz*z)
        changed=self.Runtime.SetTransform(self.SelectedEntity,details["position"],rotation,details["scale"])
        if changed:self.SetDirty(True);self._UpdateGizmo()
        return changed

    def ApplyGizmoScale(self, factor) -> bool:  # type: ignore[no-untyped-def]
        if len(self.SelectedEntities)>1:
            details=[self.Runtime.EntityDetails(value) for value in self.SelectedEntities];center=tuple(sum(item["position"][i] for item in details)/len(details) for i in range(3));factors=(factor.X,factor.Y,factor.Z);changed=False
            for entity,item in zip(self.SelectedEntities,details):
                position=tuple(center[i]+(item["position"][i]-center[i])*factors[i] for i in range(3));scale=tuple(item["scale"][i]*factors[i] for i in range(3));changed=self.Runtime.SetTransform(entity,position,item["rotation"],scale) or changed
            if changed:self.SetDirty(True);self._UpdateGizmo()
            return changed
        details=self.Runtime.EntityDetails(self.SelectedEntity) if self.SelectedEntity else {}
        if not details:return False
        scale=tuple(a*b for a,b in zip(details["scale"],(factor.X,factor.Y,factor.Z)))
        changed=self.Runtime.SetTransform(self.SelectedEntity,details["position"],details["rotation"],scale)
        if changed:self.SetDirty(True);self._UpdateGizmo()
        return changed


__all__ = ["EditorController"]
