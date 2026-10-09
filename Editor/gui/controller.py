"""Coordinates editor widgets with the private runtime bridge."""

from __future__ import annotations

from pathlib import Path
from math import cos, sin
from time import monotonic

from PySide6.QtCore import QObject, QSignalBlocker, QTimer, Qt
from PySide6.QtWidgets import QApplication, QFileDialog, QLabel, QMenu, QProgressDialog, QLineEdit, QWidgetAction, QDialog, QVBoxLayout, QListWidget, QListWidgetItem

from ..runtime import RuntimeService
from ..history import SceneHistory
from ..scripting import ScriptAttachments, ScriptCompiler, ScriptValidationError, BuildResult
from ..lua_assets import LuaAssetWatcher
from ..property_rules import State as PropertyState,Validate as ValidateProperty,RuleError
from .widgets.fields import FieldState
from ..materials import MaterialCompiler, MaterialError, ZERO, COUNTS
from ..environments import InspectEnvironment
from ..game_input import GameInputRouter
from .widgets.inspector_viewport import InspectorViewport
from .panels import ConsoleLevel
from .panels.assets import (ENVIRONMENT_EXTENSIONS, IMAGE_EXTENSIONS,
                            MODEL_EXTENSIONS, SHADER_EXTENSIONS)
from .gizmos import GizmoMode, Vec3
from .display_names import DisplayName
from .preferences import STATISTIC_FIELDS
from .widgets import AssetPickerInput, ObjectPickerInput, BoolInput, ColorInput, EnumInput, FloatInput, IntInput, PlayState, StringInput, UIntInput, Vec2Input, Vec3Input, Vec4Input


class EditorController(QObject):
    def __init__(self, window, runtime: RuntimeService) -> None:  # type: ignore[no-untyped-def]
        super().__init__(window)
        self.Window = window
        self.Runtime = runtime
        self.Window.Console.BindRuntime(runtime)
        self.GameInput=GameInputRouter(window,runtime)
        self.SelectedEntity = ""
        self._inspected_asset=None
        self.InspectedScene = ""
        self.SelectedEntities: list[str] = []
        self.ScenePath = ""
        self._updating_inspector = False
        self.IsDirty = False
        self._play_authoring=None
        self._dirty_scenes:set[str]=set()
        self._hierarchy_clipboard: list[dict] = []
        self._box_selection_base: list[str] = []
        self._gizmos_visible=True;self._stats_frames=0;self._stats_started=monotonic()
        self._snap_modes=set();self._pivot_center=False;self._local_space=False
        self.ScriptCompiler=None;self.ScriptAttachments=None
        self._lua_play_pending=False;self.LuaAssets=LuaAssetWatcher(self.Runtime,self)
        self.LuaAssets.Finished.connect(self._LuaImportFinished);self.LuaAssets.Failed.connect(self._LuaImportFailed)
        self.MaterialCompiler=MaterialCompiler(runtime);self._material_check=0.0;self._material_error=""
        self._asset_database_dirty=True
        self._material_scenes=None
        self.History=SceneHistory(runtime,self)
        self.Timer = QTimer(self)
        self.Timer.setInterval(16)
        self.Timer.timeout.connect(self._Tick)

        window.MenuBar.OpenSceneRequested.connect(self.OpenSceneDialog)
        window.MenuBar.SaveSceneRequested.connect(self.SaveScene)
        window.MenuBar.SaveSceneAsRequested.connect(self.SaveSceneAsDialog)
        window.MenuBar.UndoRequested.connect(self.Undo)
        window.MenuBar.RedoRequested.connect(self.Redo)
        window.MenuBar.CameraPresetRequested.connect(window.Scene.Surface.SetCameraPreset)
        window.MenuBar.FocusSelectedRequested.connect(self.FocusSelected)
        window.MenuBar.FrameAllRequested.connect(self.FrameAll)
        window.MenuBar.GizmosToggled.connect(self.SetGizmosVisible)
        window.MenuBar.GridToggled.connect(window.Scene.GridToggle.setChecked)
        window.MenuBar.IconsToggled.connect(runtime.SetEditorIconsVisible)
        window.MenuBar.StatsToggled.connect(window.Scene.StatsToggle.setChecked)
        window.MenuBar.RenderModeRequested.connect(self.SetRenderMode)
        window.Scene.ShadingMode.currentIndexChanged.connect(lambda index:self.SetRenderMode(("lit","unlit","wireframe","lighting_only","overdraw")[index]))
        window.Scene.LocalToggle.toggled.connect(lambda checked:setattr(self,"_local_space",checked))
        window.Scene.PivotMode.currentIndexChanged.connect(lambda index:(setattr(self,"_pivot_center",index==1),self._UpdateGizmo()))
        window.Scene.GizmoToggle.toggled.connect(self.SetGizmosVisible);window.Scene.StatsToggle.toggled.connect(window.Scene.StatsLabel.setVisible)
        window.Toolbar.PlayRequested.connect(self.Play)
        window.Toolbar.StopRequested.connect(self.Stop)
        window.Toolbar.PauseRequested.connect(runtime.Pause)
        window.Toolbar.StepRequested.connect(runtime.Step)
        window.Toolbar.GizmoModeChanged.connect(self._GizmoModeChanged)
        window.Hierarchy.SelectionChanged.connect(self.SelectEntity)
        window.Hierarchy.EditorStateRequested.connect(self.SetEntityEditorState)
        window.AssetBrowser.SelectionCleared.connect(lambda:self.SelectEntity(None))
        window.Hierarchy.CreateRequested.connect(self.CreateEntity)
        window.Hierarchy.CreateTypedRequested.connect(self.CreateTypedEntity)
        window.Hierarchy.ReparentRequested.connect(self.ReparentEntity)
        window.Hierarchy.AssetDropped.connect(self.InstantiateAsset)
        window.Hierarchy.DeleteRequested.connect(self.DeleteEntity)
        window.Hierarchy.RenameRequested.connect(self.RenameHierarchyEntity)
        window.Hierarchy.CopyRequested.connect(self.CopyHierarchyEntities)
        window.Hierarchy.PasteRequested.connect(self.PasteHierarchyEntities)
        window.Hierarchy.ActivateSceneRequested.connect(self.ActivateScene)
        window.Hierarchy.UnloadSceneRequested.connect(self.UnloadScene)
        window.Hierarchy.RenameSceneRequested.connect(self.RenameScene)
        window.Properties.AddComponentRequested.connect(self.ShowAddComponentMenu)
        window.Properties.RemoveComponentRequested.connect(self.RemoveComponent)
        window.AssetBrowser.AssetSelected.connect(self.SelectAsset)
        window.AssetBrowser.AssetsChanged.connect(lambda:setattr(self,"_asset_database_dirty",True))
        window.AssetBrowser.AssetActivated.connect(self.ActivateAsset)
        window.AssetBrowser.LoadSceneRequested.connect(self.LoadSceneAdditive)
        window.AssetBrowser.SceneRenameHandler=self.RenameSceneAsset
        window.AssetBrowser.DirectoryRenameHandler=runtime.RenameAssetDirectory
        window.AssetBrowser.SceneLoadedChecker=self.Runtime.IsSceneLoaded
        window.AssetBrowser.AssetOperationFailed.connect(lambda text:window.Console.AddMessage(text,ConsoleLevel.Error,True,"Editor"))
        runtime.SceneChanged.connect(self.RefreshHierarchy)
        runtime.ProjectChanged.connect(self._ProjectLoaded)
        runtime.ErrorOccurred.connect(lambda text: window.Console.AddMessage(text, ConsoleLevel.Error))
        window.Scene.Surface.TranslationDragged.connect(self.ApplyGizmoTranslation)
        window.Scene.Surface.RotationDragged.connect(self.ApplyGizmoRotation)
        window.Scene.Surface.ScaleDragged.connect(self.ApplyGizmoScale)
        window.Scene.Surface.GizmoDragFinished.connect(self._FinishGizmoDrag)
        window.Scene.Surface.GizmoDragStarted.connect(lambda:self.History.BeginTransforms("Transform selection",self.SelectedEntities))
        window.Scene.Surface.EntityPicked.connect(self.SelectSceneEntity)
        window.Scene.Surface.EntitiesBoxSelected.connect(self.SelectSceneBox)
        window.Scene.Surface.SelectionBoxStarted.connect(self.BeginSceneBoxSelection)
        window.Scene.Surface.SelectionBoxFinished.connect(lambda:self._box_selection_base.clear())
        window.Scene.Surface.NavigationChanged.connect(lambda active:window.MenuBar.FrameAllAction.setEnabled(not active))
        window.Scene.Surface.Attached.connect(self._UpdateGizmo)
        # The toolbar selects Translate before this controller is constructed,
        # so its initial GizmoModeChanged signal has already been emitted.
        window.Scene.Surface.SetGizmoMode(window.Toolbar.GetGizmoMode())
        self.History.Changed.connect(self._HistoryChanged);self._HistoryChanged()
        if not runtime.IsAvailable():
            window.Console.AddMessage(runtime.LastError(), ConsoleLevel.Warning)
        else:
            QTimer.singleShot(100, self, self._StartTicking)

    def _StartTicking(self)->None:
        # An accepted close releases the runtime before this delayed callback.
        if self.Runtime.IsAvailable():self.Timer.start()

    def OpenSceneDialog(self) -> None:
        dialog=QFileDialog(self.Window,self.Window.Localization.Translate("dialog.open_scene"),self.Runtime.AssetDirectory(),self.Window.Localization.Translate("dialog.scene_filter"))
        dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setFileMode(QFileDialog.FileMode.ExistingFile);dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptOpen)
        path=dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""
        if path and self.Runtime.LoadScene(path): self.ScenePath = path

    def _Tick(self) -> None:
        self.Window.Console.RefreshNative(False)
        self.GameInput.SyncFocus()
        surface=self.Window.Scene.Surface
        if monotonic()-self._material_check>2.0 and not surface._navigating and surface._gizmo_drag is None:
            self._material_check=monotonic();self._PrepareUsedMaterials()
        tick_start=monotonic()
        if self.Runtime.Tick() is False:
            self.Timer.stop()
            return
        tick_ms=(monotonic()-tick_start)*1000
        if self._play_authoring is not None and not self.Runtime.IsPlaying():self._RestorePlayAuthoring()
        self._stats_frames+=1;now=monotonic()
        if now-self._stats_started>=1.0:
            preferences=self.Window.GetPreferences()["statistics"]
            if self.Window.Scene.StatsToggle.isChecked():
                fps=self._stats_frames/(now-self._stats_started);values=self.Runtime.Statistics()
                values={key:int(value) for key,value in values.items()};values.update(fps=round(fps),frame_ms=round(1000/max(.01,fps),2),tick_ms=round(tick_ms,2),selected=len(self.SelectedEntities))
                text="\n".join(self.Window.Localization.Translate("statistics.value."+key,value=values.get(key,0)) for key in STATISTIC_FIELDS if preferences.get(key,False))
                self.Window.Scene.StatsLabel.setText(text)
            self._stats_frames=0;self._stats_started=now
        self.Window.Output.SetGameCameraAvailable(self.Runtime.HasGameOutput())
        # Editor gizmos follow the authoritative world transform every frame.
        # This also covers transforms changed by systems or native user code.
        if self.SelectedEntity:self._UpdateGizmo()
        if self.SelectedEntity and self.Runtime.IsPlaying():self._RefreshInspectorValues(preserve_editing=True)

    def SaveScene(self) -> bool:
        if self.ScriptAttachments and not self._SyncLuaScripts():return False
        if self.ScenePath:
            if self.Runtime.SaveScene(self.ScenePath): self.SetDirty(False);return True
            return False
        return self.SaveSceneAsDialog()

    def SaveSceneAsDialog(self) -> bool:
        if self.ScriptAttachments and not self._SyncLuaScripts():return False
        dialog=QFileDialog(self.Window,self.Window.Localization.Translate("dialog.save_scene"),self.Runtime.AssetDirectory(),self.Window.Localization.Translate("dialog.scene_filter"))
        dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)
        path=dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""
        if path and self.Runtime.SaveScene(path): self.ScenePath = path; self.SetDirty(False);return True
        return False

    def _ProjectLoaded(self, path: str) -> None:
        self.History.Clear()
        self.SetDirty(False)
        self.ScenePath = str(self.Runtime.SceneInfo().get("path", ""))
        assets = self.Runtime.AssetDirectory()
        self.Window.AssetBrowser.SetProjectRoot(assets or Path(path).parent)
        project=Path(path).parent if Path(path).is_file() else Path(path)
        project_id=str(self.Runtime.ProjectInfo().get("uuid",""));saved=self.Window._settings.get("entity_editor_state",{}).get(project_id,{})
        self.Runtime.EditorEntityState={str(entity):{key:bool(value) for key,value in flags.items() if key in {"locked","hidden"}} for entity,flags in saved.items() if isinstance(flags,dict)} if isinstance(saved,dict) else {}
        self.ScriptCompiler=ScriptCompiler(project);self.ScriptAttachments=ScriptAttachments(project)
        self.ScriptAttachments.MergeLuaSceneBindings(self.Runtime.LuaSceneScripts())
        self.LuaAssets.Track(value["source"] for value in self.ScriptAttachments.LuaSceneBindings())
        self.Window.Console.AddMessage(
            self.Window.Localization.Translate("console.project_loaded", path=path)
        )

    def RefreshHierarchy(self) -> None:
        if self.ScriptAttachments and not self.Runtime.IsPlaying():self.ScriptAttachments.MergeLuaSceneBindings(self.Runtime.LuaSceneScripts())
        self.Window.Output.SetGameCameraAvailable(self.Runtime.HasGameOutput())
        selected = list(self.SelectedEntities) or ([self.InspectedScene] if self.InspectedScene else [])
        hierarchy_blocker=QSignalBlocker(self.Window.Hierarchy.Tree)
        self.Window.Hierarchy.Tree.setUpdatesEnabled(False)
        self.Window.Hierarchy.Clear()
        items = {}
        theme = "light" if self.Window.ThemeManager.GetTheme().background == "#d4d4d4" else "dark"
        root_ids={"","0","00000000-0000-0000-0000-000000000000"}
        loaded_scenes=self.Runtime.LoadedScenes();self.Runtime.SyncEditorEntityState(loaded_scenes)
        self._material_scenes=loaded_scenes
        self._loaded_scene_ids={str(scene.get("uuid","")) for scene in loaded_scenes}
        self._active_entity_count=sum(len(scene.get("entities",())) for scene in loaded_scenes if scene.get("active"))
        for scene_info in loaded_scenes:
            scene_id=str(scene_info.get("uuid",""));scene_name=str(scene_info.get("name","Untitled"))
            scene_root=self.Window.Hierarchy.AddItem(scene_name,scene_id,icon=self.Window.Resources.Icon(f"icons/{theme}/scene.svg"),kind="scene",active=bool(scene_info.get("active")))
            scene_root.setExpanded(self.Window.Hierarchy._DefaultExpanded(scene_id,scene_root));pending=list(scene_info.get("entities",()))
            while pending:
                progress=False
                for entity in list(pending):
                    parent_id=str(entity.get("parent",""))
                    if parent_id not in root_ids and parent_id not in items:continue
                    parent=items.get(parent_id,scene_root);items[entity["uuid"]]=self.Window.Hierarchy.AddItem(entity["name"],entity["uuid"],parent,self.Window.Resources.Icon(f"icons/{theme}/obj.svg"),"entity");pending.remove(entity);progress=True
                if not progress:
                    for entity in pending:items[entity["uuid"]]=self.Window.Hierarchy.AddItem(entity["name"],entity["uuid"],scene_root,self.Window.Resources.Icon(f"icons/{theme}/obj.svg"),"entity")
                    break
        for entity_id,item in items.items():self.Window.Hierarchy.SetEditorState(item,self.Runtime.IsEditorLocked(entity_id),self.Runtime.IsEditorHidden(entity_id),self.Window.Resources.Icon(f"icons/{theme}/lock.svg"),self.Runtime.EditorEntityState.get(entity_id,{}))
        self.Window.Hierarchy.ApplyExpansionState(items)
        self.Window.Hierarchy.SetDirtyScenes(self._dirty_scenes)
        self.Window.Hierarchy.SetSelectedData(selected)
        self.Window.Hierarchy.ApplySearch()
        self.Window.Hierarchy.Tree.setUpdatesEnabled(True)
        del hierarchy_blocker
        if self.SelectedEntity and self.SelectedEntity not in items:self.SelectEntity(None)

    def SelectEntity(self, entity_id, force: bool = False) -> None:  # type: ignore[no-untyped-def]
        self._inspected_asset=None
        if isinstance(entity_id,(list,tuple)):
            self.SelectEntities([str(value) for value in entity_id]);return
        next_entity = str(entity_id or "")
        self.InspectedScene=""
        if self.Runtime.IsEditorLocked(next_entity):next_entity=""
        if not force and next_entity and next_entity==self.SelectedEntity and self.Window.Properties._sections:
            self._RefreshInspectorValues();self._UpdateGizmo();return
        self._updating_inspector = True
        self._script_rule_rows=[]
        self._script_descriptor_cache={}
        try:self.Window.Properties.Clear()
        finally:self._updating_inspector = False
        self.SelectedEntity = next_entity
        self.SelectedEntities=[next_entity] if next_entity else []
        self.Window.Properties.AddComponentButton.setVisible(bool(next_entity))
        if not self.SelectedEntity:
            self.Window.Scene.Surface.SetSelection(None);self._UpdateGizmo();return
        if self.SelectedEntity in getattr(self,"_loaded_scene_ids",set()) or self.SelectedEntity==str(self.Runtime.SceneInfo().get("uuid","")):
            scene_id=self.SelectedEntity;self.InspectedScene=scene_id;self.SelectedEntity="";self.SelectedEntities=[];self.Window.Scene.Surface.SetSelection(None);self._UpdateGizmo();self._InspectScene(scene_id);return
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
        component_enabled=details.get("component_enabled",{})
        for component in details.get("components", ())[1:]:
            if component not in {"Transform","ScriptComponents"}:
                section = self.Window.Properties.AddComponentSection(
                    f"runtime.{component}", str(component), expanded=False,
                    icon=self._ComponentIcon(str(component)),
                    enabled=bool(component_enabled.get(component,True))
                )
                section.EnabledChanged.connect(lambda enabled,c=component:self._SetComponentEnabled([inspected_entity],str(c),enabled))
                for property_name, value in dict(component_data.get(component, {})).items():
                    editor = self._ComponentEditor(inspected_entity, component, property_name, value)
                    if editor is not None: section.AddField(property_name, editor)
        if self.ScriptAttachments is not None:
            for script in self.ScriptAttachments.For(inspected_entity):
                name=str(script.get("type","Script"));section=self.Window.Properties.AddComponentSection(f"script.{name}",name,expanded=False,enabled=bool(script.get("enabled",True)))
                section.EnabledChanged.connect(lambda enabled,s=script:self._SetScriptEnabled(s,enabled))
                for property_name,value in script.get("properties",{}).items():
                    editor=self._ScriptEditor(script,property_name,value)
                    if editor is not None:
                        section.AddField(property_name,editor)
                        if getattr(editor,"_inspector_rules",{}).get("Label"):section.SetFieldTitle(property_name,editor._inspector_rules["Label"])
                        self._script_rule_rows.append((script,property_name,section,editor,getattr(editor,"_inspector_rules",{})))
        self._RefreshScriptRules()

        self._UpdateGizmo()

    def SelectEntities(self,entity_ids:list[str])->None:
        unique=list(dict.fromkeys(value for value in entity_ids if value and not self.Runtime.IsEditorLocked(value)))
        if len(unique)<=1:self.SelectEntity(unique[0] if unique else None);return
        details=[self.Runtime.EntityDetails(value) for value in unique];details=[value for value in details if value]
        if len(details)<=1:self.SelectEntity(unique[0] if details else None);return
        self._script_rule_rows=[];self._script_descriptor_cache={}
        self.SelectedEntities=unique;self.SelectedEntity=unique[0];self.Window.Properties.Clear();self.Window.Properties.AddComponentButton.setVisible(True)
        summary=self.Window.Properties.AddComponentSection("selection",self.Window.Localization.Translate("properties.multiple_entities",count=len(details)),removable=False)
        summary.AddField("Selection",QLabel(", ".join(str(value.get("name","")) for value in details)))
        center=tuple(sum(float(value.get("world_position",value["position"])[axis]) for value in details)/len(details) for axis in range(3));last_center=[center];position=Vec3Input(center);summary.AddField("Center",position)
        def move_center(_value=None):
            target=position.GetValue();delta=tuple(target[i]-last_center[0][i] for i in range(3));self.History.BeginTransforms("Move selection",unique)
            results=[self.Runtime.Translate(entity,delta) for entity in unique]
            if any(results):self.History.Commit();last_center[0]=target;self.SetDirty(True);self._UpdateGizmo()
            else:self.History.Cancel()
        position.ValueChanged.connect(move_center)
        shared=set(details[0].get("components",()))
        for value in details[1:]:shared.intersection_update(value.get("components",()))
        for component in sorted(shared-set(("Transform",))):
            states=[bool(value.get("component_enabled",{}).get(component,True)) for value in details]
            section=self.Window.Properties.AddComponentSection(f"multi.{component}",component,expanded=False,icon=self._ComponentIcon(component),enabled=all(states))
            section.EnabledChanged.connect(lambda enabled,c=component:self._SetComponentEnabled(unique,c,enabled))
            fields=[dict(value.get("component_data",{}).get(component,{})) for value in details]
            for name in set.intersection(*(set(value) for value in fields)) if fields else ():
                values=[value[name] for value in fields];section.AddField(name,QLabel(str(values[0]) if all(v==values[0] for v in values) else "— Mixed —"))
        self.Window.Scene.Surface.SetSelection(center);self._UpdateGizmo()

    def _SetComponentEnabled(self,entities:list[str],component:str,enabled:bool)->None:
        if self._updating_inspector:return
        self.History.Begin(("Enable " if enabled else "Disable ")+component)
        results=[self.Runtime.SetComponentEnabled(entity,component,enabled) for entity in entities]
        if any(results):
            self.History.Commit();self.SetDirty(True)
        else:self.History.Cancel()

    def SelectSceneBox(self,entity_ids:list[str],additive:bool=False)->None:
        desired=list(dict.fromkeys(value for value in (self._box_selection_base if additive else [])+entity_ids if self.Runtime.IsEditorSelectable(value)))
        if desired==self.SelectedEntities:return
        self.SelectEntities(desired);self.Window.Hierarchy.SetSelectedData(desired)

    def BeginSceneBoxSelection(self,additive:bool)->None:
        self._box_selection_base=list(self.SelectedEntities) if additive else []
        if not additive:
            blocker=QSignalBlocker(self.Window.Hierarchy.Tree);self.Window.Hierarchy.Tree.clearSelection();self.Window.Hierarchy.Tree.setCurrentItem(None);del blocker
            self.SelectEntity(None)

    def _HistoryChanged(self)->None:
        self.Window.MenuBar.UndoAction.setEnabled(self.History.CanUndo());self.Window.MenuBar.RedoAction.setEnabled(self.History.CanRedo())
        self.Window.MenuBar.UndoAction.setToolTip(self.History.UndoLabel());self.Window.MenuBar.RedoAction.setToolTip(self.History.RedoLabel())

    def _Mutate(self,label:str,operation):
        if label=="Edit Transform":self.History.BeginTransforms(label,self.SelectedEntities)
        else:self.History.Begin(label)
        try:result=operation()
        except Exception:self.History.Cancel();raise
        if result:self.History.Commit()
        else:self.History.Cancel()
        return result

    def Undo(self)->None:
        scene=self.InspectedScene
        if self.History.Undo():
            self.SetDirty(True)
            if self.History.LastRestoreStructural:self.SelectEntity(scene,force=True) if scene else self.SelectEntities(self.SelectedEntities)
            else:self._RefreshInspectorValues();self._UpdateGizmo()

    def Redo(self)->None:
        scene=self.InspectedScene
        if self.History.Redo():
            self.SetDirty(True)
            if self.History.LastRestoreStructural:self.SelectEntity(scene,force=True) if scene else self.SelectEntities(self.SelectedEntities)
            else:self._RefreshInspectorValues();self._UpdateGizmo()

    def SelectAsset(self,path)->None:
        self._script_rule_rows=[];self._script_descriptor_cache={}
        self._inspected_asset=Path(path)
        path=Path(path);self.SelectedEntity="";self.SelectedEntities=[];self.Window.Scene.Surface.SetSelection(None);self.Runtime.SetGizmo("",0);self.Window.Properties.Clear();self.Window.Properties.AddComponentButton.setVisible(False)
        self.InspectedScene="";self._UpdateGizmo()
        section=self.Window.Properties.AddComponentSection("asset",self.Window.Localization.Translate("properties.asset"),removable=False)
        for label,value in (("Name",path.name),("Type",path.suffix.lower() or "Folder"),("Path",str(path)),("Size",self._FormatAssetSize(self._AssetSize(path)))):section.AddField(self.Window.Localization.Translate(f"properties.asset_{label.lower()}") if label!="Name" else self.Window.Localization.Translate("properties.asset_name"),QLabel(value))
        if path.suffix.lower()==".matinst":self._InspectMaterial(path)
        elif path.suffix.lower()==".btexture":self._InspectTexture(path)
        elif path.suffix.lower() in {".hdr",".exr",".ktx"}:self._InspectEnvironment(path)
        elif path.suffix.lower() in {".mat",".shad",".bshader"}:
            try:
                reflection=self.MaterialCompiler.InspectShader(path)
                section.AddField(self.Window.Localization.Translate("materials.parameters"),QLabel(str(len(reflection["parameters"]))))
                section.AddField(self.Window.Localization.Translate("materials.requires"),QLabel(", ".join(reflection["requires"])))
            except (OSError,ValueError) as error:self._MaterialError(error)

    def _MaterialError(self,error)->None:
        message=self.Window.Localization.Translate("materials.failed",error=str(error))
        if message!=self._material_error:self.Window.Console.AddMessage(message,ConsoleLevel.Error,True,self.Window.Localization.Translate("materials.source"));self._material_error=message

    def _InspectScene(self,scene_id):
        self._script_rule_rows=[];self._script_descriptor_cache={}
        tr=self.Window.Localization.Translate;self.Window.Properties.AddComponentButton.hide()
        data=self.Runtime.SceneEnvironment(scene_id)
        if not data:return
        section=self.Window.Properties.AddComponentSection("scene.environment",tr("environment.section"),removable=False)
        commit=lambda key,value:self._SetSceneEnvironment(scene_id,key,value)
        mode=EnumInput();mode.setObjectName("SceneEnvironmentMode");mode.SetOptions(((tr("environment.map_mode"),0),(tr("environment.material_mode"),1)));mode.SetValue(data.get("mode",0));mode.currentIndexChanged.connect(lambda _:commit("mode",mode.GetValue()));section.AddField(tr("environment.mode"),mode)
        if data.get("mode",0)==0:section.AddField(tr("environment.source"),self._AssetPicker(data["source"],{".hdr",".exr",".ktx"},lambda value:commit("source",value)))
        else:section.AddField(tr("environment.material"),self._AssetPicker(data["material"],{".matinst"},lambda value:commit("material",value)))
        intensity=FloatInput(minimum=0,maximum=1000000,value=data["intensity"]);intensity.valueChanged.connect(lambda value:commit("intensity",value));section.AddField(tr("environment.intensity"),intensity)
        from math import degrees,radians
        rotation=Vec3Input([degrees(value) for value in data["rotation"]]);rotation.ValueChanged.connect(lambda value:commit("rotation",[radians(item) for item in value]));section.AddField(tr("environment.rotation"),rotation)
        from PySide6.QtGui import QColor
        color=ColorInput(QColor.fromRgbF(*data["clear_color"]));color.ValueChanged.connect(lambda value:commit("clear_color",[value.redF(),value.greenF(),value.blueF(),value.alphaF()]));section.AddField(tr("environment.clear_color"),color)
        for key in ("ibl","skybox","show_sun"):
            field=BoolInput(data[key]);field.ValueChanged.connect(lambda value,k=key:commit(k,value));section.AddField(tr("environment."+key),field)
        preview=InspectorViewport(empty_text=tr("environment.empty"));preview.SetClearColor(data["clear_color"])
        color.ValueChanged.connect(preview.SetClearColor)
        info=self.Runtime.AssetInfo(data.get("resolved_source",data["source"] if data.get("mode",0)==0 else ZERO))
        if info:
            try:preview.SetImage(str(InspectEnvironment(info["path"],info)["preview"]))
            except OSError:pass
        section.AddViewport(preview)

    def SetEntityEditorState(self,entity_id,key,value):
        if key not in {"locked","hidden"}:return
        state=self.Runtime.EditorEntityState.setdefault(str(entity_id),{});state[key]=bool(value)
        if not any(state.values()):self.Runtime.EditorEntityState.pop(str(entity_id),None)
        project=str(self.Runtime.ProjectInfo().get("uuid",""))
        if project and project!=ZERO:
            self.Window._settings.setdefault("entity_editor_state",{})[project]=dict(self.Runtime.EditorEntityState)
            if self.Window._settings_saver is not None:self.Window._settings_saver(self.Window._settings)
        selected=[entity for entity in self.SelectedEntities if not self.Runtime.IsEditorLocked(entity) and not self.Runtime.IsEditorHidden(entity)]
        self.SelectEntities(selected);self.RefreshHierarchy()

    def _SetSceneEnvironment(self,scene_id,key,value):
        active=scene_id==str(self.Runtime.SceneInfo().get("uuid"));
        if active:self.History.Begin(self.Window.Localization.Translate("environment.history"))
        if self.Runtime.SetSceneEnvironment(scene_id,{key:value}):
            if active:self.History.Commit()
            self.SetDirty(True,[scene_id]);self._PrepareUsedMaterials()
            if key in {"mode","source","material"}:QTimer.singleShot(0,self,lambda:self.SelectEntity(scene_id,force=True) if self.InspectedScene==scene_id else None)
        elif active:self.History.Cancel()

    def _InspectEnvironment(self,path):
        tr=self.Window.Localization.Translate;info=self.Runtime.AssetInfo(str(path))
        try:data=InspectEnvironment(path,info)
        except (OSError,ValueError) as error:self._MaterialError(error);return
        section=self.Window.Properties.AddComponentSection("asset.environment",tr("environment.asset"),removable=False)
        section.AddField(tr("environment.resolution"),QLabel(f'{data["width"]} × {data["height"]}'))
        section.AddField(tr("environment.ibl_status"),QLabel(tr("environment.ready" if data["ibl"].is_file() or path.suffix.lower()==".ktx" else "environment.missing")))
        settings=data["settings"]
        resolution=EnumInput();resolution.SetOptions([(str(value),str(value)) for value in (32,64,128,256,512,1024)]);resolution.SetValue(settings.get("Resolution","128"))
        samples=EnumInput();samples.SetOptions([(str(value),str(value)) for value in (64,128,256,512,1024)]);samples.SetValue(settings.get("Samples","256"))
        section.AddField(tr("environment.ibl_resolution"),resolution);section.AddField(tr("environment.samples"),samples)
        from PySide6.QtWidgets import QPushButton
        apply=QPushButton(tr("environment.reimport"));apply.setEnabled(path.suffix.lower()!=".ktx")
        apply.clicked.connect(lambda:self._ReimportEnvironment(path,info,resolution.currentText(),samples.currentText()));section.AddViewport(apply)
        preview=InspectorViewport(empty_text=tr("environment.preview_missing"));preview.SetImage(str(data["preview"]));section.AddViewport(preview)

    def _ReimportEnvironment(self,path,info,resolution,samples):
        if self.Runtime.SetEnvironmentImportSettings(info["uuid"],{"Resolution":resolution,"Samples":samples}):self.SelectAsset(path)
        else:self._MaterialError(self.Runtime.LastError())

    def _AssetPicker(self,value,extensions,commit):
        tr=self.Window.Localization.Translate
        editor=AssetPickerInput(tr("properties.select_project_asset"),accepted_extensions=extensions,picker_title=tr("properties.select_project_asset"),search_placeholder=tr("properties.search_project_assets"),missing_label=tr("properties.missing_asset"))
        editor.ConfigureAssets(self._ProjectAssets());editor.SetValue(value);editor.PickRequested.connect(editor.OpenProjectPicker);editor.ValueChanged.connect(lambda v:commit(v or ZERO));return editor

    def _InspectTexture(self,path:Path)->None:
        tr=self.Window.Localization.Translate;data=self.Runtime.TextureAssetInfo(path)
        if not data:self.Window.Console.AddMessage(self.Runtime.LastError(),ConsoleLevel.Error);return
        section=self.Window.Properties.AddComponentSection("asset.texture",tr("texture.title"),removable=False)
        options={"Samples":(("1×",1),("2×",2),("4×",4),("8×",8)),"Color Format":(("RGBA8",0),("RGBA16F",1),("RGBA32F",2)),"Depth Format":((tr("texture.depth.none"),0),("Depth16",1),("Depth24",2),("Depth32F",3)),"Filter":tuple((tr("texture.filter."+key),index) for index,key in enumerate(("point","linear","trilinear"))),"Wrap":tuple((tr("texture.wrap."+key),index) for index,key in enumerate(("clamp","repeat","mirror")))}
        def commit(name,value):
            updated=dict(data);updated[name]=value
            if not updated["Mipmaps"]:updated["Mip Levels"]=0
            try:ok=self.Runtime.SaveTextureAsset(path,updated)
            except (ValueError,TypeError,RuntimeError) as error:ok=False;self.Window.Console.AddMessage(str(error),ConsoleLevel.Error)
            if ok:data.update(updated)
            else:self.Window.Console.AddMessage(self.Runtime.LastError(),ConsoleLevel.Error)
            # Reload normalized settings; changing size or mipmaps may constrain levels.
            QTimer.singleShot(0,self,lambda:self.SelectAsset(path) if self._inspected_asset==path and not self.SelectedEntity and path.exists() else None)
        for name,value in data.items():
            callback=lambda v,key=name:commit(key,v)
            if name in options:
                editor=EnumInput();editor.SetOptions(options[name]);editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0,e=editor,c=callback:c(e.GetValue()))
            elif isinstance(value,bool):editor=BoolInput(value);editor.ValueChanged.connect(callback)
            elif isinstance(value,int):
                editor=IntInput(value=value);editor.setRange(1,4096) if name in {"Width","Height"} else editor.setRange(0,max(data["Width"],data["Height"]).bit_length());editor.valueChanged.connect(callback)
                if name=="Mip Levels":editor.setEnabled(bool(data["Mipmaps"]))
            else:
                color=tuple(float(part) for part in value);editor=Vec4Input(color);editor.ValueChanged.connect(callback)
            section.AddField(tr("texture.field."+name),editor)

    def _InspectMaterial(self,path:Path)->None:
        tr=self.Window.Localization.Translate
        try:
            data=self.MaterialCompiler.ReadMaterial(path)
            section=self.Window.Properties.AddComponentSection("material",tr("materials.title"),removable=False)
            section.AddField(tr("materials.shader"),self._AssetPicker(data["shader"],{".mat",".shad",".bshader"},lambda v:self._SetMaterialShader(path,data,v)))
            if data["shader"]==ZERO:return
            reflection=self.MaterialCompiler.InspectShader(data["shader"])
            for parameter in reflection["parameters"]:
                name=parameter["name"];value=data["properties"].get(name,parameter["default"]);kind=parameter["kind"]
                commit=lambda v,p=parameter:self._SetMaterialProperty(path,data,p,v)
                if kind==6:editor=self._AssetPicker(value,{".png",".jpg",".jpeg",".hdr",".exr",".btexture"},commit)
                elif kind==5:editor=BoolInput(value);editor.ValueChanged.connect(commit)
                elif kind==4:editor=IntInput(value=value);editor.valueChanged.connect(commit)
                elif kind==0:editor=FloatInput(value=value);editor.valueChanged.connect(commit)
                elif kind in (1,2,3):editor={1:Vec2Input,2:Vec3Input,3:Vec4Input}[kind](value);editor.ValueChanged.connect(commit)
                else:
                    from PySide6.QtWidgets import QWidget,QGridLayout
                    editor=QWidget();layout=QGridLayout(editor);layout.setContentsMargins(0,0,0,0);layout.setSpacing(3);numbers=[];side=3 if kind==7 else 4
                    for index,number in enumerate(value):
                        field=FloatInput(value=number);numbers.append(field);layout.addWidget(field,index//side,index%side)
                    for field in numbers:field.valueChanged.connect(lambda _,fields=numbers,fn=commit:fn([f.value() for f in fields]))
                section.AddField(name,editor)
        except (OSError,ValueError) as error:self._MaterialError(error)

    def _SetMaterialShader(self,path,data,value)->None:
        try:
            updated=dict(data);updated["shader"]=value;updated["properties"]={}
            if value!=ZERO:
                reflection=self.MaterialCompiler.InspectShader(value)
                updated["properties"]={p["name"]:p["default"] for p in reflection["parameters"]}
            self.MaterialCompiler.SaveMaterial(path,updated);self._PrepareUsedMaterials();QTimer.singleShot(0,self,lambda:self.SelectAsset(path))
        except (OSError,ValueError) as error:self._MaterialError(error)

    def _SetMaterialProperty(self,path,data,parameter,value)->None:
        try:
            data["properties"][parameter["name"]]=value;self.MaterialCompiler.SaveMaterial(path,data);self._PrepareUsedMaterials()
        except (OSError,ValueError) as error:self._MaterialError(error)

    def _PrepareUsedMaterials(self)->bool:
        if not self.Runtime.AssetDirectory():return True
        try:
            if self._asset_database_dirty:self.Runtime.RefreshAssets();self._asset_database_dirty=False
            used=set();shaders=set(getattr(self.Runtime,"UsedShaderAssets",lambda:[])())
            if self._material_scenes is None or self.Runtime.IsPlaying():self._material_scenes=self.Runtime.LoadedScenes()
            scenes=self._material_scenes
            for scene in scenes:
                environment=self.Runtime.SceneEnvironment(str(scene.get("uuid","")));material=environment.get("material",ZERO)
                if environment.get("mode",0)==1 and material!=ZERO:used.add(material)
            entities=[entity for scene in scenes for entity in scene.get("entities",[])] if scenes else self.Runtime.Entities()
            for entity in entities:
                details=entity
                primitive=details.get("component_data",{}).get("Primitive Object",{})
                if details.get("component_enabled",{}).get("Primitive Object",True) and primitive.get("Material Asset",ZERO)!=ZERO:used.add(primitive["Material Asset"])
                if not details.get("component_enabled",{}).get("Mesh",True):continue
                mesh=details.get("component_data",{}).get("Mesh",{})
                material=mesh.get("Material Asset",ZERO)
                if mesh.get("Mesh Asset",ZERO)!=ZERO and material!=ZERO:used.add(material)
                if mesh.get("Mesh Asset",ZERO)!=ZERO:used.update(v for v in mesh.get("Material Slots",[]) if v!=ZERO)
            if self.ScriptAttachments:
                for entries in self.ScriptAttachments.values.get("entities",{}).values():
                    for script in entries:
                        if not script.get("enabled",True):continue
                        try:descriptor=ScriptCompiler.Inspect(script["source"])
                        except ScriptValidationError:continue
                        for p in descriptor.properties if descriptor else ():
                            kind=p.type.replace("Bazzalt::","").strip();value=script.get("properties",{}).get(p.name,ZERO)
                            if kind=="Material" and value not in (ZERO,"0","",None):used.add(value)
                            elif kind=="Shader" and value not in (ZERO,"0","",None):shaders.add(value)
            for material in used:
                info=self.Runtime.AssetInfo(material)
                if not info:continue  # Runtime-only material instances have no source asset.
                extension=Path(info["path"]).suffix.lower()
                if extension==".matinst":self.MaterialCompiler.PrepareMaterial(material)
                elif extension in {".mat",".shad",".bshader"}:self.MaterialCompiler.CompileShader(material)
            for shader in shaders:self.MaterialCompiler.CompileShader(shader)
            self._material_error="";return True
        except (OSError,ValueError) as error:self._MaterialError(error);return False

    @staticmethod
    def _AssetSize(path:Path)->int:
        try:
            if path.is_symlink():return 0
            if path.is_file():return path.stat().st_size
            return sum(child.stat().st_size for child in path.rglob("*") if child.is_file() and not child.is_symlink())
        except OSError:return 0

    @staticmethod
    def _FormatAssetSize(size:int)->str:
        value=float(size)
        for unit in ("B","KB","MB","GB","TB"):
            if value<1024.0 or unit=="TB":return f"{int(value)} {unit}" if unit=="B" else f"{value:.1f} {unit}"
            value/=1024.0

    def _RefreshInspectorValues(self,preserve_editing:bool=False) -> None:
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
            for component,state in details.get("component_enabled",{}).items():
                section=self.Window.Properties._sections.get(f"runtime.{component}")
                if section is not None:
                    blocker=QSignalBlocker(section.Enabled);section.Enabled.setChecked(bool(state));del blocker
            for section_id,fields in values.items():
                section=self.Window.Properties._sections.get(section_id)
                if section is None:continue
                for name,value in fields.items():
                    editor=section._fields.get(name);setter=getattr(editor,"SetValue",None) if editor else None
                    focus=QApplication.focusWidget()
                    if preserve_editing and editor is not None and focus is not None and (focus is editor or editor.isAncestorOf(focus)):continue
                    if callable(setter):
                        blocker=QSignalBlocker(editor);setter(value);del blocker
        finally:self._updating_inspector=False

    def SelectSceneEntity(self,entity_id:str,additive:bool=False)->None:
        if entity_id and not self.Runtime.IsEditorSelectable(entity_id):return
        if additive:
            selected=list(self.SelectedEntities)
            if entity_id in selected:selected.remove(entity_id)
            else:selected.append(entity_id)
            self.SelectEntities(selected);self.Window.Hierarchy.SetSelectedData(selected)
        else:self.SelectEntity(entity_id);self.Window.Hierarchy.SetSelectedData([entity_id])

    def _ComponentIcon(self, component: str):
        names={"Camera":"comp_cam.svg","Light":"comp_light.svg","Mesh":"comp_mesh.svg","Primitive Object":"comp_mesh.svg",
               "Scene Query Bounds":"comp_sqb.svg","Gaussian Blur":"comp_gblur.svg","Vignette":"comp_vign.svg"}
        filename=names.get(component)
        return self.Window.Resources.Icon(f"icons/{filename}") if filename else None

    def _ComponentEditor(self, entity_id: str, component: str, name: str, value):  # type: ignore[no-untyped-def]
        if name=="Material Slots":return None  # Slot UUIDs are not numeric vector fields.
        if component=="Camera" and name=="Render Target":return self._AssetPicker(value,{".btexture"},lambda v:self._CommitComponent(entity_id,component,name,v or ZERO))
        if isinstance(value, bool):
            editor=BoolInput(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, int):
            if component=="Frame" and name in ("Mode","ScaleMode"):
                tr=self.Window.Localization.Translate
                options=((tr("gui.mode.viewport"),0),(tr("gui.mode.camera_bound"),1),(tr("gui.mode.spatial"),2)) if name=="Mode" else ((tr("gui.scale.constant"),0),(tr("gui.scale.responsive"),1))
                editor=EnumInput();editor.SetOptions(options);editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
            if component=="Light" and name=="Type":
                editor=EnumInput();editor.SetOptions((("Directional",0),("Sun",1),("Point",2),("Spot",3)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitLightType(entity_id,editor.GetValue()));return editor
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
            if component=="Primitive Object" and name=="Shape":
                editor=EnumInput();editor.SetOptions((("Cube",0),("Sphere",1),("Cylinder",2),("Capsule",3),("Plane",4),("Cone",5),("Torus",6)));editor.SetValue(value);editor.currentIndexChanged.connect(lambda _=0:self._CommitPrimitiveShape(entity_id,editor.GetValue()));return editor
            if value>2_147_483_647:
                editor=UIntInput(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
            editor=IntInput(value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, float):
            if component=="Light":
                bounds={"Intensity":(0,1e12),"Range":(.001,1e12),"Inner Cone":(.5,90),"Outer Cone":(.5,90),"Sun Angular Radius":(.25,20),"Sun Halo Size":(1,1e12),"Sun Halo Falloff":(1,1e12)}
                minimum,maximum=bounds.get(name,(0,1e12))
                editor=FloatInput(minimum=minimum,maximum=maximum,value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
            editor=FloatInput(value=value);editor.valueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, (tuple,list)) and len(value) in (2,3,4) and all(isinstance(v,(int,float)) for v in value):
            if "Color" in name:
                from PySide6.QtGui import QColor
                values=tuple(float(v) for v in value);alpha=values[3] if len(values)==4 else 1.0
                editor=ColorInput(QColor.fromRgbF(values[0],values[1],values[2],alpha));editor.ValueChanged.connect(lambda color:self._CommitComponent(entity_id,component,name,(color.redF(),color.greenF(),color.blueF(),color.alphaF()) if len(values)==4 else (color.redF(),color.greenF(),color.blueF())));return editor
            editor={2:Vec2Input,3:Vec3Input,4:Vec4Input}[len(value)](value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v));return editor
        if isinstance(value, str):
            if component in ("Frame","GuiImage") and name=="Camera":
                entities={str(item["uuid"]):item for item in self.Runtime.Entities()};tr=self.Window.Localization.Translate
                cameras={identity:item for identity,item in entities.items() if "Camera" in item.get("components",[])}
                editor=ObjectPickerInput(tr("gui.select_camera"),validator=lambda identity:identity in cameras)
                editor.SetValue(value if value in cameras else None,cameras.get(value,{}).get("name",""))
                def assign_camera(identity):
                    editor.Display.setText(cameras.get(str(identity),{}).get("name",""));self._CommitComponent(entity_id,component,name,identity or ZERO)
                def pick_camera():
                    dialog=QDialog(editor);dialog.setWindowTitle(tr("gui.select_camera"));layout=QVBoxLayout(dialog);items=QListWidget(dialog);layout.addWidget(items)
                    for identity,item in cameras.items():
                        row=QListWidgetItem(item.get("name",""));row.setData(Qt.ItemDataRole.UserRole,identity);items.addItem(row)
                    def accept_camera(item):editor.SetValue(item.data(Qt.ItemDataRole.UserRole));dialog.accept()
                    items.itemDoubleClicked.connect(accept_camera);dialog.exec()
                editor.ValueChanged.connect(assign_camera);editor.PickRequested.connect(pick_camera);return editor
            if component=="GuiImage" and name=="Texture":
                return self._AssetPicker(value,set(IMAGE_EXTENSIONS)|{".btexture"},lambda v:self._CommitComponent(entity_id,component,name,v or ZERO))
            if "Asset" in name:
                tr=self.Window.Localization.Translate;editor=AssetPickerInput(tr("properties.select_project_asset"),accepted_extensions=self._AssetExtensions(name),picker_title=tr("properties.select_project_asset"),search_placeholder=tr("properties.search_project_assets"),missing_label=tr("properties.missing_asset"));editor.ConfigureAssets(self._ProjectAssets());editor.SetValue(value);editor.ValueChanged.connect(lambda v:self._CommitComponent(entity_id,component,name,v or "0"));editor.PickRequested.connect(editor.OpenProjectPicker);return editor
            editor=StringInput(value);editor.editingFinished.connect(lambda:self._CommitComponent(entity_id,component,name,editor.GetValue()));return editor
        return None

    def _ScriptEditor(self,script:dict,name:str,value):
        editor=self._RawScriptEditor(script,name,value)
        descriptor=self._ScriptDescriptor(script)
        prop=next((p for p in descriptor.properties if p.name==name),None) if descriptor else None
        rules=prop.rules if prop else {}
        editor._inspector_rules=rules
        blocker=QSignalBlocker(editor)
        try:
            if isinstance(editor,(IntInput,FloatInput)):
                minimum=rules.get("Min",editor.minimum());maximum=rules.get("Max",editor.maximum())
                # Preserve invalid legacy values visibly; never clamp the saved model on display.
                editor.setRange(min(minimum,value),max(maximum,value))
                if "Step" in rules:editor.setSingleStep(int(rules["Step"]) if isinstance(editor,IntInput) else rules["Step"])
            elif hasattr(editor,"Inputs"):
                for control,current in zip(editor.Inputs,value):
                    control.setRange(min(rules.get("Min",control.minimum()),current),max(rules.get("Max",control.maximum()),current))
                    if "Step" in rules:control.setSingleStep(rules["Step"])
            elif isinstance(editor,StringInput) and "MaxLength" in rules:editor.setMaxLength(max(int(rules["MaxLength"]),len(str(value))))
            if isinstance(editor,(IntInput,FloatInput)) or hasattr(editor,"Inputs"):editor.SetValue(value)
            editor.setToolTip(rules.get("Tooltip",""))
        finally:del blocker
        return editor

    def _ScriptDescriptor(self,script):
        if not hasattr(self,"_script_descriptor_cache"):self._script_descriptor_cache={}
        source=script.get("source","")
        if source not in self._script_descriptor_cache:
            try:self._script_descriptor_cache[source]=ScriptCompiler.Inspect(source)
            except ScriptValidationError:self._script_descriptor_cache[source]=None
        return self._script_descriptor_cache[source]

    def _RawScriptEditor(self,script:dict,name:str,value):
        commit=lambda v:self._SetScriptProperty(script,name,v)
        descriptor=self._ScriptDescriptor(script)
        prop=next((p for p in descriptor.properties if p.name==name),None) if descriptor else None
        if prop and prop.type.replace("Bazzalt::","").strip() in ("EntityReference","Entity"):
            entities={str(item["uuid"]):item for item in self.Runtime.Entities()}
            editor=ObjectPickerInput(validator=lambda v:v in entities)
            def assign(v):
                editor.Display.setText(entities.get(str(v),{}).get("name", ""))
                commit(v or "00000000-0000-0000-0000-000000000000")
            editor.SetValue(value if value in entities else None,entities.get(value,{}).get("name",""))
            editor.ValueChanged.connect(assign)
            def pick():
                dialog=QDialog(editor);dialog.setWindowTitle(name);layout=QVBoxLayout(dialog);items=QListWidget(dialog);layout.addWidget(items)
                for uuid,entity in entities.items():
                    item=QListWidgetItem(entity.get("name",""));item.setData(Qt.ItemDataRole.UserRole,uuid);items.addItem(item)
                def accept(item):
                    editor.SetValue(item.data(Qt.ItemDataRole.UserRole));dialog.accept()
                items.itemDoubleClicked.connect(accept);dialog.exec()
            editor.PickRequested.connect(pick)
            return editor
        if prop and prop.type.replace("Bazzalt::","").strip() in ("Material","Shader"):
            return self._AssetPicker(value,{".matinst"} if "Material" in prop.type else {".mat",".shad",".bshader"},commit)
        if prop and prop.type.replace("Bazzalt::","").strip()=="Texture":return self._AssetPicker(value,set(IMAGE_EXTENSIONS)|{".btexture"},commit)
        if isinstance(value,bool):editor=BoolInput(value);editor.ValueChanged.connect(commit);return editor
        if isinstance(value,int):editor=IntInput(value=value);editor.valueChanged.connect(commit);return editor
        if isinstance(value,float):editor=FloatInput(value=value);editor.valueChanged.connect(commit);return editor
        if isinstance(value,(tuple,list)) and len(value) in (2,3,4):editor={2:Vec2Input,3:Vec3Input,4:Vec4Input}[len(value)](value);editor.ValueChanged.connect(commit);return editor
        editor=StringInput(str(value));editor.editingFinished.connect(lambda:commit(editor.GetValue()));return editor

    def _AssetExtensions(self,field_name:str)->set[str]:
        name=field_name.casefold()
        if "mesh" in name or "model" in name:return set(MODEL_EXTENSIONS)
        if "texture" in name or "image" in name:return set(IMAGE_EXTENSIONS)|set(ENVIRONMENT_EXTENSIONS)|{".btexture"}
        if "material" in name:return {".matinst"}
        if "shader" in name:return {".mat",".shad",".bshader"}
        if "scene" in name:return {".bscene"}
        return set()

    def _ProjectAssets(self)->list[dict]:
        root_text=self.Runtime.AssetDirectory()
        if not root_text:return []
        root=Path(root_text).resolve();result=[]
        try:paths=sorted((value for value in root.rglob("*") if value.is_file() and not value.name.endswith(".meta")),key=lambda value:str(value).casefold())
        except OSError:return []
        for path in paths:
            meta=path.with_name(path.name+".meta")
            if not meta.is_file():continue
            try:
                fields={}
                for line in meta.read_text(encoding="utf-8").splitlines():
                    if ":" not in line:continue
                    key,raw=line.split(":",1);fields[key.strip()]=raw.strip().strip("'\"")
                asset_id=fields.get("UUID",fields.get("uuid",""))
                if not asset_id:continue
                result.append({"uuid":asset_id,"name":path.name,"path":str(path),
                               "relative_path":path.relative_to(root).as_posix(),
                               "extension":path.suffix.lower(),"importer":fields.get("Importer",fields.get("importer",""))})
            except (OSError,UnicodeError,ValueError):continue
        return result

    def _CommitComponent(self, entity_id: str, component: str, name: str, value) -> None:  # type: ignore[no-untyped-def]
        if not self._updating_inspector and entity_id==self.SelectedEntity and self._Mutate(f"Edit {component}",lambda:self.Runtime.SetComponentProperty(entity_id,component,name,value)):
            self.SetDirty(True)
            if component=="Light" and name!="Type":self._RefreshInspectorValues()

    def _CommitLightType(self,entity_id:str,value:int)->None:
        self._CommitComponent(entity_id,"Light","Type",value)
        if entity_id==self.SelectedEntity:self.SelectEntity(entity_id,force=True)

    def _CommitPrimitiveShape(self,entity_id:str,value:int)->None:
        self._CommitComponent(entity_id,"Primitive Object","Shape",value)
        if entity_id==self.SelectedEntity:self.SelectEntity(entity_id,force=True)

    def _Rename(self, entity_id: str, name: str) -> None:
        if not self._updating_inspector and entity_id==self.SelectedEntity and name.strip() and self._Mutate("Rename entity",lambda:self.Runtime.Rename(entity_id,name.strip())):self.SetDirty(True);self.RefreshHierarchy()

    def SetDirty(self, dirty: bool = True, scene_ids=None) -> None:
        if dirty:self._material_scenes=None
        if not dirty:self._dirty_scenes.clear()
        else:
            ids={str(value) for value in (scene_ids or ()) if value}
            if not ids:
                for entity in self.SelectedEntities or ([self.SelectedEntity] if self.SelectedEntity else []):
                    scene_id=str(self.Runtime.EntityDetails(entity).get("scene_uuid",""))
                    if scene_id:ids.add(scene_id)
            if not ids:
                active=next((scene for scene in self.Runtime.LoadedScenes() if scene.get("active")),None)
                if active:ids.add(str(active.get("uuid","")))
            self._dirty_scenes.update(ids)
        self._SyncDirtyPresentation()

    def _SyncDirtyPresentation(self)->None:
        self.IsDirty=bool(self._dirty_scenes);title=self.Window.windowTitle().rstrip(" *")
        self.Window.setWindowTitle(title+(" *" if self.IsDirty else ""));self.Window.Hierarchy.SetDirtyScenes(self._dirty_scenes)

    def _GizmoModeChanged(self, mode) -> None: self.Window.Scene.Surface.SetGizmoMode(mode);self._UpdateGizmo()
    def _UpdateGizmo(self) -> None:
        self.Window.RefreshEditActions()
        self.Window.Scene.Surface._selected_entity_ids=set(self.SelectedEntities)
        self.Runtime.SetSelectionOutline(self.SelectedEntities)
        modes={GizmoMode.Select:0,GizmoMode.Translate:1,GizmoMode.Rotate:2,GizmoMode.Scale:3}
        if self.SelectedEntity and self.Runtime.IsEditorHidden(self.SelectedEntity):self.Window.Scene.Surface.SetSelection(None);self.Runtime.SetGizmo("",0);return
        if not self._gizmos_visible:self.Runtime.SetGizmo("",0);return
        if len(self.SelectedEntities)>1:
            positions=[]
            for value in self.SelectedEntities:
                details=self.Runtime.EntityPose(value)
                if details.get("scene_active",True):positions.append(details.get("world_position",details.get("position")))
            positions=[value for value in positions if value]
            if positions:
                center=tuple(sum(value[axis] for value in positions)/len(positions) for axis in range(3)) if self._pivot_center else tuple(positions[0]);self.Window.Scene.Surface.SetSelection(center);self.Runtime.SetGizmoPosition(center,modes[self.Window.Toolbar.GetGizmoMode()]);return
        if self.SelectedEntity:
            details=self.Runtime.EntityPose(self.SelectedEntity)
            if details and not details.get("scene_active",True):self.Window.Scene.Surface.SetSelection(None);self.Runtime.SetGizmo("",modes[self.Window.Toolbar.GetGizmoMode()]);return
            if details:self.Window.Scene.Surface.SetSelection(details.get("world_position",details["position"]))
        self.Runtime.SetGizmo(self.SelectedEntity, modes[self.Window.Toolbar.GetGizmoMode()])

    def SetGizmosVisible(self,visible:bool)->None:
        self._gizmos_visible=bool(visible)
        blocker=QSignalBlocker(self.Window.Scene.GizmoToggle);self.Window.Scene.GizmoToggle.setChecked(bool(visible));del blocker
        self.Window.MenuBar.GizmosAction.setChecked(bool(visible));self._UpdateGizmo()
    def FocusSelected(self)->None:
        points=[]
        snapshots={value["uuid"]:value for value in self.Runtime.Entities()}
        for entity in self.SelectedEntities or ([self.SelectedEntity] if self.SelectedEntity else []):
            bounds=snapshots.get(entity,{}).get("mesh_bounds")
            if bounds:points.extend(bounds);continue
            details=self.Runtime.EntityDetails(entity)
            if details:points.append(details.get("world_position",details.get("position")))
        if points:
            center=tuple(sum(value[i] for value in points)/len(points) for i in range(3));self.Window.Scene.Surface.Frame(center,max(1.,max((sum((value[i]-center[i])**2 for i in range(3)))**.5 for value in points)))
    def FrameAll(self)->None:
        points=[]
        for value in self.Runtime.Entities():
            if self.Runtime.IsEditorHidden(value.get("uuid","")):continue
            if value.get("mesh_bounds"):points.extend(value["mesh_bounds"])
            else:points.append(value.get("world_position",value.get("position")))
        points=[value for value in points if value]
        if not points:return
        center=tuple((min(value[i] for value in points)+max(value[i] for value in points))*.5 for i in range(3));radius=max(1.,max((sum((value[i]-center[i])**2 for i in range(3)))**.5 for value in points));self.Window.Scene.Surface.Frame(center,radius)
    def SetRenderMode(self,mode:str)->None:
        modes=("lit","unlit","wireframe","lighting_only","overdraw");success=self.Runtime.SetSceneRenderMode(mode);selected=mode if success else "lit"
        blocker=QSignalBlocker(self.Window.Scene.ShadingMode);self.Window.Scene.ShadingMode.setCurrentIndex(modes.index(selected));del blocker
        self.Window.MenuBar.ShadingActions[selected].setChecked(True)
        if not success:self.Window.Console.AddMessage(self.Window.Localization.Translate("renderer.mode_unavailable",mode=mode),ConsoleLevel.Warning,True,self.Window.Localization.Translate("renderer.source"))
    def SetOverlays(self,values)->None:
        values=set(values);self.Window.Scene.GridToggle.setChecked("grid" in values);self.Runtime.SetEditorIconsVisible("icons" in values);self.Window.Scene.StatsLabel.setVisible("stats" in values)
        self.Window.MenuBar.GridAction.setChecked("grid" in values);self.Window.MenuBar.IconsAction.setChecked("icons" in values);self.Window.MenuBar.StatsAction.setChecked("stats" in values)

    def ActivateAsset(self,path)->None:
        path=Path(path)
        if path.suffix.lower()==".bscene":self.LoadSceneAdditive(path)

    def LoadSceneAdditive(self,path)->None:
        self.Runtime.LoadSceneAdditive(path)

    def ActivateScene(self,scene_id:str)->None:
        if self.Runtime.ActivateScene(scene_id):
            self.ScenePath=str(self.Runtime.SceneInfo().get("path",""));self.SelectEntity(None);self.Window.Output.SetGameCameraAvailable(self.Runtime.HasGameOutput())

    def UnloadScene(self,scene_id:str)->None:
        if self.Runtime.UnloadScene(scene_id):self._dirty_scenes.discard(str(scene_id));self._SyncDirtyPresentation();self.SelectEntity(None)

    def RenameScene(self,scene_id:str,name:str)->None:
        if self.Runtime.RenameLoadedScene(scene_id,name):self.ScenePath=str(self.Runtime.SceneInfo().get("path",self.ScenePath));self.Window.AssetBrowser.Refresh()
        else:self.RefreshHierarchy()

    def RenameSceneAsset(self,old:Path,target:Path)->bool:
        target=target if target.suffix.lower()==".bscene" else target.with_name(target.name+".bscene")
        if self.Runtime.IsSceneLoaded(old):
            result=self.Runtime.RenameLoadedScene(str(old),target.name)
        else:
            try:
                old.rename(target);meta=old.with_name(old.name+".meta")
                if meta.exists():meta.rename(target.with_name(target.name+".meta"))
                result=True
            except OSError:result=False
        self.Window.AssetBrowser.Refresh(target if result else old)
        if result:self.ScenePath=str(self.Runtime.SceneInfo().get("path",self.ScenePath));self.RefreshHierarchy()
        return result

    def _FinishGizmoDrag(self) -> None:
        self.History.Commit()
        if self.SelectedEntity:self._RefreshInspectorValues();self._UpdateGizmo()

    def CreateEntity(self, parent) -> None:  # type: ignore[no-untyped-def]
        if self._Mutate("Create entity",lambda:bool(self.Runtime.CreateEntity(self.Window.Localization.Translate("entity.new"),str(parent or "")))):self.SetDirty(True,[self._SceneForParent(parent)])

    def CreateTypedEntity(self, component_type: str, parent) -> None:  # type: ignore[no-untyped-def]
        if component_type.startswith("GUI:"):
            self._CreateGuiEntity(component_type.partition(":")[2], parent)
            return
        component_name,_,shape_text=component_type.partition(":")
        primitive_keys=("cube","sphere","cylinder","capsule","plane","cone","torus")
        display_name=self.Window.Localization.Translate(f"primitive.{primitive_keys[int(shape_text)]}") if component_name=="Primitive Object" and shape_text else component_name
        self.History.Begin(f"Create {display_name}")
        entity_id = self.Runtime.CreateEntity(display_name if component_name != "Entity" else self.Window.Localization.Translate("entity.new"), str(parent or ""))
        if entity_id and component_name != "Entity":
            self.Runtime.AddComponent(entity_id, component_name)
            if component_name=="Primitive Object" and shape_text:self.Runtime.SetComponentProperty(entity_id,component_name,"Shape",int(shape_text))
        if entity_id:self.History.Commit();self.SetDirty(True,[self._SceneForParent(parent)])
        else:self.History.Cancel()

    def _CreateGuiEntity(self, kind: str, parent) -> None:
        """Create compositional widgets below a Frame, never on its root."""
        title=self.Window.Localization.Translate(f"hierarchy.gui.{kind}")
        self.History.Begin(f"Create {title}")
        scene_id=self._SceneForParent(parent)
        parent_id=str(parent or "")
        created_root=""
        try:
            if kind not in {"viewport", "camera_bound", "spatial"}:
                current=parent_id;visited=set();has_frame=False
                while current and current not in visited:
                    visited.add(current);details=self.Runtime.EntityDetails(current)
                    if "Frame" in details.get("components",()):has_frame=True;break
                    current=str(details.get("parent", "") or "")
                if not has_frame:
                    parent_id=self.Runtime.CreateEntity(self.Window.Localization.Translate("hierarchy.gui.viewport"),parent_id)
                    created_root=parent_id
                    if not parent_id or not self.Runtime.AddComponent(parent_id,"Frame"):raise RuntimeError("Could not create GUI Frame")
            entity_id=self.Runtime.CreateEntity(title,parent_id)
            if not created_root:created_root=entity_id
            if not entity_id:raise RuntimeError("Could not create GUI entity")
            components={"viewport":("Frame",),"camera_bound":("Frame",),"spatial":("Frame",),"container":("RectTransform",),"rectangle":("RectTransform","Rectangle"),"text":("RectTransform","GuiText"),"image":("RectTransform","GuiImage"),"button":("RectTransform","Rectangle","GuiText","GuiButton"),"text_input":("RectTransform","Rectangle","GuiTextInput")}[kind]
            for component in components:
                if not self.Runtime.AddComponent(entity_id,component):raise RuntimeError(f"Could not add {component}")
            if kind in {"camera_bound","spatial"}:
                if not self.Runtime.SetComponentProperty(entity_id,"Frame","Mode",1 if kind=="camera_bound" else 2):raise RuntimeError("Could not configure GUI Frame")
            if kind in {"text","button"}:self.Runtime.SetComponentProperty(entity_id,"GuiText","Value",title)
            self.History.Commit();self.SetDirty(True,[scene_id]);self.SelectEntity(entity_id,force=True)
        except Exception:
            if created_root:self.Runtime.DestroyEntity(created_root)
            self.History.Cancel()
            raise

    def ShowAddComponentMenu(self) -> None:
        if not self.SelectedEntity: return
        menu = QMenu(self.Window.Properties)
        search=QLineEdit(menu);search.setPlaceholderText(self.Window.Localization.Translate("properties.search_components"));search.setClearButtonEnabled(True)
        search_action=QWidgetAction(menu);search_action.setDefaultWidget(search);menu.addAction(search_action)
        targets=self.SelectedEntities or [self.SelectedEntity];existing=set.intersection(*(set(self.Runtime.EntityDetails(target).get("components",())) for target in targets))
        for component_type in self.Runtime.ComponentTypes():
            if component_type=="CameraRenderTarget":continue # Legacy scenes keep this component; new cameras use Texture assets.
            action = menu.addAction(DisplayName(self.Window.Localization,component_type)); action.setEnabled(component_type not in existing)
            action.setData(component_type)
            action.triggered.connect(lambda _=False, name=component_type: self._AddComponent(name))
        scripts=self.ScriptCompiler.Discover() if self.ScriptCompiler else []
        if self.ScriptCompiler and self.ScriptCompiler.Diagnostics:
            for diagnostic in self.ScriptCompiler.Diagnostics:self._ScriptValidationFailed(diagnostic.message)
        if scripts:
            menu.addSeparator();script_menu=menu
            for descriptor in scripts:
                action=script_menu.addAction(descriptor.name);action.setToolTip(str(descriptor.path));action.setEnabled(not self.ScriptAttachments or not any(value.get("type")==descriptor.name for value in self.ScriptAttachments.For(self.SelectedEntity)));action.triggered.connect(lambda _=False,d=descriptor:self._AttachScript(d))
        button = self.Window.Properties.AddComponentButton
        search.textChanged.connect(lambda text:[action.setVisible(all(word in action.text().casefold() for word in text.casefold().split())) for action in menu.actions() if action is not search_action and not action.isSeparator()])
        QTimer.singleShot(0,search,search.setFocus)
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
        if component_id.startswith("script.") and self.ScriptAttachments:
            if self.ScriptAttachments.Remove(self.SelectedEntity,component_id.removeprefix("script.")):
                self._SyncLuaScripts();self.SetDirty(True);self.SelectEntity(self.SelectedEntity,force=True)
            return
        prefix="multi." if component_id.startswith("multi.") else "runtime." if component_id.startswith("runtime.") else ""
        if not prefix:return
        component=component_id.removeprefix(prefix);targets=self.SelectedEntities or [self.SelectedEntity];self.History.Begin(f"Remove {component}");results=[self.Runtime.RemoveComponent(target,component) for target in targets]
        if any(results):self.History.Commit();self.SetDirty(True);self.SelectEntities(targets) if len(targets)>1 else self.SelectEntity(targets[0],force=True)
        else:self.History.Cancel()

    def InstantiateAsset(self,path:str,parent_id:str)->None:
        if Path(path).suffix.lower() in {".gltf",".glb",".obj",".fbx",".dae",".filamesh"}:
            self.History.Begin("Instantiate model");entity=self.Runtime.InstantiateModelPath(path,parent_id)
            if entity:self.History.Commit();self.SetDirty(True,[self._SceneForParent(parent_id)]);self.SelectEntity(entity)
            else:self.History.Cancel()
        elif Path(path).suffix.lower() in {".cpp",".lua"} and self.ScriptCompiler:
            try:descriptor=self.ScriptCompiler.Inspect(path)
            except ScriptValidationError as error:self._ScriptValidationFailed(error);return
            target=parent_id or self.SelectedEntity
            if descriptor and target:self._AttachScript(descriptor,target)

    def DeleteEntity(self, entity_id) -> None:  # type: ignore[no-untyped-def]
        targets=entity_id if isinstance(entity_id,(list,tuple)) else [entity_id]
        scenes={str(self.Runtime.EntityDetails(str(value)).get("scene_uuid","")) for value in targets if value}
        self.History.Begin("Delete entities");results=[self.Runtime.DestroyEntity(str(value)) for value in targets if value]
        if any(results):self.History.Commit();self.SetDirty(True,scenes)
        else:self.History.Cancel()

    def RenameHierarchyEntity(self,entity_id:str,name:str)->None:
        if name.strip() and self._Mutate("Rename entity",lambda:self.Runtime.Rename(entity_id,name.strip())):
            self.SetDirty(True);self.RefreshHierarchy()

    def CopyHierarchyEntities(self,entity_ids)->None:
        selected=list(dict.fromkeys(str(value) for value in entity_ids if value));entities=[entity for scene in self.Runtime.LoadedScenes() for entity in scene.get("entities",())]
        by_parent={}
        for entity in entities:by_parent.setdefault(str(entity.get("parent","")),[]).append(str(entity["uuid"]))
        roots=[value for value in selected if str(self.Runtime.EntityDetails(value).get("parent","")) not in selected]
        def snapshot(entity_id):
            details=dict(self.Runtime.EntityDetails(entity_id))
            return {"details":details,"children":[snapshot(child) for child in by_parent.get(entity_id,())]}
        self._hierarchy_clipboard=[snapshot(value) for value in roots]
        self.Window.RefreshEditActions()

    def DuplicateHierarchyEntities(self,entity_ids)->None:
        if not entity_ids:return
        previous=self._hierarchy_clipboard
        self.CopyHierarchyEntities(entity_ids);nodes=self._hierarchy_clipboard;self._hierarchy_clipboard=previous
        self.History.Begin("Duplicate entities");created=[];scenes=set()
        try:
            for node in nodes:
                details=node["details"];parent=str(details.get("parent") or "")
                if not parent or parent=="00000000-0000-0000-0000-000000000000":parent=str(details.get("scene_uuid") or "")
                value=self._CloneHierarchyNode(node,parent,True)
                if value:created.append(value);scenes.add(str(details.get("scene_uuid","")))
        except Exception:self.History.Cancel();raise
        if not created:self.History.Cancel();return
        self.History.Commit();self.SetDirty(True,scenes);self.RefreshHierarchy();self.SelectEntities(created)

    def PasteHierarchyEntities(self,parent_id)->None:
        if not self._hierarchy_clipboard:return
        self.History.Begin("Paste entities");created=[]
        try:
            for node in self._hierarchy_clipboard:
                value=self._CloneHierarchyNode(node,str(parent_id or ""),True)
                if value:created.append(value)
        except Exception:
            self.History.Cancel();raise
        if not created:self.History.Cancel();return
        self.History.Commit();self.SetDirty(True,[self._SceneForParent(parent_id)]);self.RefreshHierarchy();self.SelectEntities(created)

    def _CloneHierarchyNode(self,node:dict,parent_id:str,top_level:bool=False)->str:
        details=node.get("details",{});name=str(details.get("name","Entity"))+(" Copy" if top_level else "")
        entity=self.Runtime.CreateEntity(name,parent_id)
        if not entity:return ""
        self.Runtime.SetTransform(entity,details.get("position",(0,0,0)),details.get("rotation",(0,0,0,1)),details.get("scale",(1,1,1)))
        supported=set(self.Runtime.ComponentTypes())
        for component,fields in details.get("component_data",{}).items():
            if component not in supported or not self.Runtime.AddComponent(entity,component):continue
            for field,value in dict(fields).items():self.Runtime.SetComponentProperty(entity,component,field,value)
            self.Runtime.SetComponentEnabled(entity,component,bool(details.get("component_enabled",{}).get(component,True)))
        for child in node.get("children",()):self._CloneHierarchyNode(child,entity)
        return entity

    def ReparentEntity(self, entity_id: str, parent_id: str) -> None:
        scene_id=str(self.Runtime.EntityDetails(entity_id).get("scene_uuid",""))
        if self._Mutate("Reparent entity",lambda:self.Runtime.SetParent(entity_id,parent_id)):
            self.SetDirty(True,[scene_id])
            if self.Window.Hierarchy.ExpandNewItems:self.Window.Hierarchy.ExpandData(parent_id)
            if entity_id==self.SelectedEntity:self.SelectEntity(entity_id,force=True)

    def Play(self) -> None:
        if self.Runtime.IsPlaying():return
        if self.LuaAssets.IsBusy():self._lua_play_pending=True;return
        if not self._PrepareUsedMaterials():self.Window.Toolbar.SetPlayState(PlayState.Stopped);return
        if self.Window.PreferenceValue("console","clear_on_play",False):self.Window.Console.Clear()
        if self.ScriptCompiler and self.ScriptAttachments:
            tr=self.Window.Localization.Translate
            native_sources=[path for path in self.ScriptAttachments.UsedSources() if Path(path).suffix.lower()!=".lua"]
            progress=None
            if native_sources:
                progress=QProgressDialog(tr("scripting.compiling"),None,0,0,self.Window);progress.setWindowModality(Qt.WindowModality.WindowModal);progress.setCancelButton(None);progress.show();QApplication.processEvents()
            try:
                self.ScriptCompiler.Tools=self.Window.GetPreferences().get("tools",{})
                result=self.ScriptCompiler.Build(native_sources) if native_sources else BuildResult(True)
            finally:
                if progress is not None:progress.close()
            for diagnostic in result.diagnostics:
                level=ConsoleLevel.Error if diagnostic.level=="error" else ConsoleLevel.Warning if diagnostic.level=="warning" else ConsoleLevel.Info
                message=tr(diagnostic.localization_key) if diagnostic.localization_key else diagnostic.message
                self.Window.Console.AddMessage(message,level,True,tr("scripting.compiler_source"))
            if not result.success:self.Window.Toolbar.SetPlayState(PlayState.Stopped);return
            if result.outputs and self.Window.PreferenceValue("scripting","show_compile_success",True):self.Window.Console.AddMessage(tr("scripting.compile_success",count=len(result.outputs)),ConsoleLevel.Info,True,tr("scripting.compiler_source"))
            try:bindings=self.ScriptAttachments.RuntimeBindings(result.outputs)
            except ScriptValidationError as error:self._ScriptValidationFailed(error);self.Window.Toolbar.SetPlayState(PlayState.Stopped);return
            active_entities={str(entity["uuid"]) for entity in self.Runtime.Entities()}
            bindings=[binding for binding in bindings if binding["entity"] in active_entities]
            if not self.Runtime.ConfigureScripts(bindings):
                self.Window.Console.AddMessage(self.Runtime.LastError(),ConsoleLevel.Error,True,tr("scripting.runtime_source"));self.Window.Toolbar.SetPlayState(PlayState.Stopped);return
        authoring=(set(self._dirty_scenes),self.History.Checkpoint())
        if not self.Runtime.Play():self.Window.Toolbar.SetPlayState(PlayState.Stopped)
        else:
            self._play_authoring=authoring;self.History.Clear()
            self.Window.Output.SetGameCameraAvailable(self.Runtime.HasGameOutput())
            self.Window.Docking.activate_panel("output");self.GameInput.FocusGame()

    def _AttachScript(self,descriptor,entity_id:str|None=None)->None:
        entity_id=entity_id or self.SelectedEntity
        if self.Runtime.IsEditorLocked(entity_id):return
        if not entity_id or not self.ScriptAttachments:return
        try:
            if self.ScriptCompiler:descriptor=self.ScriptCompiler.ValidateDescriptor(descriptor)
            added=self.ScriptAttachments.Attach(entity_id,descriptor)
        except ScriptValidationError as error:self._ScriptValidationFailed(error);return
        if added:self._SyncLuaScripts();self.SetDirty(True);self.SelectEntity(entity_id,force=True)
        else:self._ScriptValidationFailed(self.Window.Localization.Translate("scripting.already_attached",name=descriptor.name))

    def _ScriptValidationFailed(self,error)->None:
        tr=self.Window.Localization.Translate
        self.Window.Console.AddMessage(tr("scripting.validation_failed",error=str(error)),ConsoleLevel.Error,True,tr("scripting.compiler_source"))

    def _SetScriptEnabled(self,script:dict,enabled:bool)->None:
        script["enabled"]=enabled
        if self.ScriptAttachments:self.ScriptAttachments.Save();self._SyncLuaScripts();self.SetDirty(True)

    def _RefreshScriptRules(self)->None:
        for script,name,section,editor,rules in getattr(self,"_script_rule_rows",()):
            try:
                visible,enabled=PropertyState(rules,script.get("properties",{}))
                section.SetFieldVisible(name,visible);editor.setEnabled(enabled)
                try:
                    ValidateProperty(rules,script.get("properties",{}),script.get("properties",{}).get(name))
                    editor.SetFieldState(FieldState.Default);editor.setToolTip(rules.get("Tooltip",""))
                except RuleError as error:
                    editor.SetFieldState(FieldState.Warning);editor.setToolTip(rules.get("Message",str(error)))
            except RuleError as error:
                section.SetFieldVisible(name,True)
                editor.setEnabled(False);editor.SetFieldState(FieldState.Error);editor.setToolTip(str(error))

    def _SetScriptProperty(self,script:dict,name:str,value)->bool:
        rows=[row for row in getattr(self,"_script_rule_rows",()) if row[0] is script and row[1]==name]
        rules=rows[0][4] if rows else {}
        try:
            visible,enabled=PropertyState(rules,script.get("properties",{}))
            if not visible or not enabled:raise RuleError("This inspector field is not editable")
            if isinstance(value,str) and rules.get("Trim"):value=value.strip()
            candidate=dict(script.get("properties",{}));candidate[name]=value
            value=ValidateProperty(rules,candidate,value)
        except RuleError as error:
            for _,_,_,editor,_ in rows:
                blocker=QSignalBlocker(editor)
                try:editor.SetValue(script.get("properties",{}).get(name))
                finally:del blocker
                editor.SetFieldState(FieldState.Error);editor.setToolTip(rules.get("Message",str(error)))
            return False
        script.setdefault("properties",{})[name]=value
        if self.ScriptAttachments:self.ScriptAttachments.Save();self._SyncLuaScripts();self.SetDirty(True)
        for _,_,_,editor,_ in rows:
            blocker=QSignalBlocker(editor)
            try:editor.SetValue(value)
            finally:del blocker
            editor.SetFieldState(FieldState.Default);editor.setToolTip(rules.get("Tooltip",""))
        self._RefreshScriptRules()
        return True

    def _SyncLuaScripts(self)->bool:
        if not self.ScriptAttachments:return True
        bindings=self.ScriptAttachments.LuaSceneBindings()
        self.LuaAssets.Track(value["source"] for value in bindings)
        if not bindings:return self.Runtime.SyncLuaScripts([]) if hasattr(self.Runtime,"SyncLuaScripts") else True
        if self.Runtime.SyncLuaScripts(bindings):return True
        if not self.Runtime.RefreshLuaAssets({value["source"] for value in bindings}):return False
        return self.Runtime.SyncLuaScripts(bindings)

    def _LuaImportFinished(self):
        if self._lua_play_pending:self._lua_play_pending=False;self.Play()
    def _LuaImportFailed(self,error):
        self._lua_play_pending=False;self._ScriptValidationFailed(error);self.Window.Toolbar.SetPlayState(PlayState.Stopped)

    def Stop(self) -> None:
        self._lua_play_pending=False
        self.Runtime.SetGameInputActive(False)
        self.Runtime.Stop()
        self._RestorePlayAuthoring()

    def _RestorePlayAuthoring(self)->None:
        if self._play_authoring is None:return
        dirty,history=self._play_authoring;self._play_authoring=None
        self._dirty_scenes=dirty;self.History.RestoreCheckpoint(history);self._SyncDirtyPresentation()
        selected=[value for value in self.SelectedEntities if self.Runtime.EntityDetails(value)]
        if len(selected)>1:self.SelectEntities(selected)
        else:self.SelectEntity(selected[0] if selected else None,force=True)
        self.Window.Toolbar.SetPlayState(PlayState.Stopped)

    def _SceneForParent(self,parent)->str:
        value=str(parent or "")
        for scene in self.Runtime.LoadedScenes():
            if str(scene.get("uuid",""))==value:return value
        details=self.Runtime.EntityDetails(value) if value else {}
        if details:return str(details.get("scene_uuid",""))
        active=next((scene for scene in self.Runtime.LoadedScenes() if scene.get("active")),{})
        return str(active.get("uuid",""))

    def ApplyGizmoTranslation(self, delta) -> bool:  # type: ignore[no-untyped-def]
        targets=self.SelectedEntities or ([self.SelectedEntity] if self.SelectedEntity else [])
        results=[self.Runtime.Translate(entity,(delta.X,delta.Y,delta.Z)) for entity in targets];changed=any(results)
        if changed:self.SetDirty(True)
        self._UpdateGizmo();return changed

    def ApplyGizmoRotation(self, axis, angle: float) -> bool:  # type: ignore[no-untyped-def]
        if len(self.SelectedEntities)>1:
            details=[self.Runtime.EntityDetails(value) for value in self.SelectedEntities]
            center=Vec3(*(sum(item["position"][index] for item in details)/len(details) for index in range(3)));unit=axis.Normalized();sine,cosine=sin(angle),cos(angle);half_sine=sin(angle*.5);changed=False
            for entity,item in zip(self.SelectedEntities,details):
                relative=Vec3(*item["position"])-center;rotated=relative*cosine+unit.Cross(relative)*sine+unit*(unit.Dot(relative)*(1-cosine));x,y,z,w=item["rotation"];dx,dy,dz,dw=unit.X*half_sine,unit.Y*half_sine,unit.Z*half_sine,cos(angle*.5);rotation=(dw*x+dx*w+dy*z-dz*y,dw*y-dx*z+dy*w+dz*x,dw*z+dx*y-dy*x+dz*w,dw*w-dx*x-dy*y-dz*z);position=(center.X+rotated.X,center.Y+rotated.Y,center.Z+rotated.Z) if self._pivot_center else item["position"];changed=self.Runtime.SetTransform(entity,position,rotation,item["scale"]) or changed
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
                position=tuple(center[i]+(item["position"][i]-center[i])*factors[i] for i in range(3)) if self._pivot_center else item["position"];scale=tuple(item["scale"][i]*factors[i] for i in range(3));changed=self.Runtime.SetTransform(entity,position,item["rotation"],scale) or changed
            if changed:self.SetDirty(True);self._UpdateGizmo()
            return changed
        details=self.Runtime.EntityDetails(self.SelectedEntity) if self.SelectedEntity else {}
        if not details:return False
        scale=tuple(a*b for a,b in zip(details["scale"],(factor.X,factor.Y,factor.Z)))
        changed=self.Runtime.SetTransform(self.SelectedEntity,details["position"],details["rotation"],scale)
        if changed:self.SetDirty(True);self._UpdateGizmo()
        return changed


__all__ = ["EditorController"]
