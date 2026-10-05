"""Private Python facade for the optional pybind11 editor runtime module."""

from __future__ import annotations

import importlib
import importlib.util
import os
import json
import sys
from pathlib import Path

from PySide6.QtCore import QObject, Signal


_NATIVE_LOAD_ERROR = ""
_DLL_DIRECTORIES: list[object] = []


def _LoadExtension(path: Path):
    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        for directory in (Path(sys.executable).resolve().parent, path.parent):
            if directory.is_dir(): _DLL_DIRECTORIES.append(os.add_dll_directory(str(directory)))
    spec = importlib.util.spec_from_file_location("_bazzalt_runtime", path)
    if spec is None or spec.loader is None: raise ImportError(f"cannot create loader for {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _LoadNativeModule():
    global _NATIVE_LOAD_ERROR
    errors: list[str] = []
    try:
        return importlib.import_module("Editor._bazzalt_runtime")
    except ImportError as error:
        errors.append(str(error))
        source_root = Path(__file__).resolve().parent.parent
        program_root = Path(sys.executable).resolve().parent
        candidates = (program_root / "Editor", program_root, source_root / "Editor",
                      source_root / "build" / "Editor", source_root / "build" / "Editor" / "Release",
                      source_root / "build" / "Editor" / "Debug")
        for directory in candidates:
            for path in directory.glob("_bazzalt_runtime*.pyd" if os.name == "nt" else "_bazzalt_runtime*.so"):
                try: return _LoadExtension(path)
                except (ImportError, OSError) as native_error: errors.append(f"{path}: {native_error}")
        try:
            return importlib.import_module("_bazzalt_runtime")
        except ImportError as fallback_error:
            errors.append(str(fallback_error))
            _NATIVE_LOAD_ERROR = "; ".join(errors)
            return None


class RuntimeService(QObject):
    SceneChanged = Signal()
    ProjectChanged = Signal(str)
    ErrorOccurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self.EditorEntityState={};self.EditorParents={}
        self._tick_error = ""
        module = _LoadNativeModule()
        self._host = module.EditorHost() if module is not None else None

    def IsAvailable(self) -> bool:
        return self._host is not None

    def LastError(self) -> str:
        if self._tick_error: return self._tick_error
        if self._host is not None: return self._host.last_error()
        return f"Native editor runtime could not be loaded: {_NATIVE_LOAD_ERROR or 'module not found'}"

    def LoadProject(self, path: str | Path) -> bool:
        if self._host is None or not self._host.load_project(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        self.ProjectChanged.emit(str(path)); self.SceneChanged.emit(); return True

    def LoadScene(self, path: str | Path) -> bool:
        if self._host is None or not self._host.load_scene(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        self.SceneChanged.emit(); return True

    def ProjectInfo(self) -> dict:
        return dict(self._host.project_info()) if self._host and hasattr(self._host, "project_info") else {}

    def SupportedRenderingBackends(self) -> list[str]:
        return list(self._host.supported_rendering_backends()) if self._host and hasattr(self._host, "supported_rendering_backends") else ["automatic"]

    def SetEditorOrientationVisible(self, visible: bool)->None:
        if self._host and hasattr(self._host,"set_editor_orientation_visible"):self._host.set_editor_orientation_visible(visible)

    def ConfigureRenderingBackend(self, backend: str) -> bool:
        return bool(self._host and hasattr(self._host, "configure_rendering_backend") and self._host.configure_rendering_backend(backend))

    def Release(self) -> None:
        self._host = None

    def UpdateProjectInfo(self, name: str, properties: dict) -> bool:
        return bool(self._host and hasattr(self._host, "update_project_info") and self._host.update_project_info(name, properties))

    def LoadSceneAdditive(self, path: str | Path) -> bool:
        if self._host is None or not hasattr(self._host,"load_scene_additive") or not self._host.load_scene_additive(str(path)):
            self.ErrorOccurred.emit(self.LastError());return False
        self.SceneChanged.emit();return True

    def ActivateScene(self, scene_id: str) -> bool:
        result=bool(self._host and hasattr(self._host,"activate_scene") and self._host.activate_scene(scene_id))
        if result:self.SceneChanged.emit()
        return result

    def UnloadScene(self, scene_id: str) -> bool:
        result=bool(self._host and hasattr(self._host,"unload_scene") and self._host.unload_scene(scene_id))
        if result:self.SceneChanged.emit()
        return result

    def IsSceneLoaded(self, path: str | Path) -> bool:
        return bool(self._host and hasattr(self._host,"is_scene_loaded") and self._host.is_scene_loaded(str(path)))

    def RenameLoadedScene(self, scene_or_path: str, name: str) -> bool:
        result=bool(self._host and hasattr(self._host,"rename_loaded_scene") and self._host.rename_loaded_scene(str(scene_or_path),name))
        if result:self.SceneChanged.emit()
        return result

    def LoadedScenes(self) -> list[dict]:
        return [dict(scene) for scene in self._host.loaded_scenes()] if self._host is not None and hasattr(self._host,"loaded_scenes") else [dict(self.SceneInfo(),active=True,entities=self.Entities())]

    def SaveScene(self, path: str | Path) -> bool:
        if self._host is None or not self._host.save_scene(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        return True

    def NewScene(self) -> None:
        if self._host is not None: self._host.new_scene(); self.SceneChanged.emit()

    def Entities(self) -> list[dict]:
        return list(self._host.entities()) if self._host is not None else []

    def EntityDetails(self, entity_id: str) -> dict:
        return dict(self._host.entity_details(entity_id)) if self._host is not None else {}

    def EntityPose(self,entity_id:str)->dict:
        if self._host is not None and hasattr(self._host,"entity_pose"):return dict(self._host.entity_pose(entity_id))
        return self.EntityDetails(entity_id)

    def EntityCount(self)->int:
        if self._host is not None and hasattr(self._host,"entity_count"):return int(self._host.entity_count())
        return len(self.Entities())

    def HasActiveCamera(self) -> bool:
        return bool(self._host and hasattr(self._host, "has_active_camera") and
                    self._host.has_active_camera())

    def Rename(self, entity_id: str, name: str) -> bool:
        if self.IsEditorLocked(entity_id):return False
        return bool(self._host and self._host.rename(entity_id, name))

    def CreateEntity(self, name: str, parent: str = "") -> str:
        if self.IsEditorLocked(parent):return ""
        value = self._host.create_entity(name, parent) if self._host is not None else ""
        self.SceneChanged.emit(); return value

    def InstantiateModelPath(self, path: str | Path, parent: str = "") -> str:
        if self.IsEditorLocked(parent):return ""
        value=self._host.instantiate_model_path(str(path),parent) if self._host is not None and hasattr(self._host,"instantiate_model_path") else ""
        if value:self.SceneChanged.emit()
        return value

    def DestroyEntity(self, entity_id: str) -> bool:
        if self.IsEditorLocked(entity_id) or any(self.IsEditorLocked(child) and self._EditorDescendant(child,entity_id) for child in self.EditorParents):return False
        result = bool(self._host and self._host.destroy_entity(entity_id))
        if result: self.SceneChanged.emit()
        return result

    def SetTransform(self, entity_id: str, position, rotation, scale) -> bool:
        if self.IsEditorLocked(entity_id):return False
        return bool(self._host and self._host.set_transform(entity_id, position, rotation, scale))

    def Translate(self, entity_id: str, delta) -> bool:
        if self.IsEditorLocked(entity_id):return False
        return bool(self._host and self._host.translate(entity_id, delta))

    def AssetInfo(self, reference) -> dict:
        return dict(self._host.asset_info(str(reference))) if self._host else {}

    def RefreshAssets(self) -> bool:
        return bool(self._host and self._host.refresh_assets())

    def UsedShaderAssets(self) -> list[str]:
        return list(self._host.used_shader_assets()) if self._host and hasattr(self._host,"used_shader_assets") else []

    def AssetDirectory(self) -> str:
        return self._host.asset_directory() if self._host is not None else ""

    def SceneInfo(self) -> dict:
        if self._host is not None and hasattr(self._host, "scene_info"):
            return dict(self._host.scene_info())
        return {"uuid": "0", "name": "Untitled"}

    def CaptureScene(self) -> bytes:
        return bytes(self._host.capture_scene()) if self._host is not None and hasattr(self._host,"capture_scene") else b""

    def CaptureTransforms(self,entities)->bytes:
        if self._host is None or not hasattr(self._host,"restore_transforms"):return b""
        values={}
        for entity in entities:
            pose=self.EntityPose(entity)
            if not all(key in pose for key in ("local_position","local_rotation","local_scale")):return b""
            values[entity]={key:pose[key] for key in ("local_position","local_rotation","local_scale")}
        return json.dumps(values,sort_keys=True,separators=(",",":"),allow_nan=False).encode()

    def RestoreTransforms(self,snapshot:bytes)->bool:
        return bool(self._host and hasattr(self._host,"restore_transforms") and self._host.restore_transforms(json.loads(snapshot)))

    def SceneEnvironment(self,scene_id:str)->dict:
        return dict(self._host.scene_environment(scene_id)) if self._host is not None and hasattr(self._host,"scene_environment") else {}

    def SetEnvironmentImportSettings(self,asset_id:str,values:dict)->bool:
        return bool(self._host and hasattr(self._host,"set_environment_import_settings") and self._host.set_environment_import_settings(asset_id,values))

    def SetSceneEnvironment(self,scene_id:str,values:dict)->bool:
        return bool(self._host and hasattr(self._host,"set_scene_environment") and self._host.set_scene_environment(scene_id,values))

    def RestoreScene(self, snapshot: bytes) -> bool:
        result=bool(self._host and hasattr(self._host,"restore_scene") and self._host.restore_scene(snapshot))
        if result:self.SceneChanged.emit()
        return result

    def ComponentTypes(self) -> list[str]:
        return list(self._host.component_types()) if self._host is not None and hasattr(self._host, "component_types") else []

    def AddComponent(self, entity_id: str, component_type: str) -> bool:
        if self.IsEditorLocked(entity_id):return False
        result = bool(self._host and hasattr(self._host, "add_component") and self._host.add_component(entity_id, component_type))
        if result: self.SceneChanged.emit()
        return result

    def RemoveComponent(self, entity_id: str, component_type: str) -> bool:
        if self.IsEditorLocked(entity_id):return False
        result=bool(self._host and hasattr(self._host,"remove_component") and self._host.remove_component(entity_id,component_type))
        if result:self.SceneChanged.emit()
        return result

    def SetComponentEnabled(self,entity_id:str,component_type:str,enabled:bool)->bool:
        if self.IsEditorLocked(entity_id):return False
        # This changes component state, not scene structure. Emitting
        # SceneChanged here rebuilds the hierarchy during the checkbox click
        # and transiently clears its selection.
        return bool(self._host and hasattr(self._host,"set_component_enabled") and self._host.set_component_enabled(entity_id,component_type,enabled))

    def SetParent(self, entity_id: str, parent_id: str = "") -> bool:
        if self.IsEditorLocked(entity_id) or self.IsEditorLocked(parent_id):return False
        result = bool(self._host and self._host.set_parent(entity_id, parent_id))
        if result: self.SceneChanged.emit()
        return result

    def CreateViewport(self, viewport_id: int, handle: int, scene: bool,
                       width: int, height: int, pixel_ratio: float = 1.0) -> bool:
        return bool(self._host and hasattr(self._host, "create_viewport") and self._host.create_viewport(
            viewport_id, handle, scene, width, height, pixel_ratio))

    def ResizeViewport(self, viewport_id: int, width: int, height: int, pixel_ratio: float = 1.0) -> None:
        if self._host is not None and hasattr(self._host, "resize_viewport"): self._host.resize_viewport(viewport_id, width, height, pixel_ratio)

    def DestroyViewport(self, viewport_id: int) -> None:
        if self._host is not None and hasattr(self._host, "destroy_viewport"): self._host.destroy_viewport(viewport_id)

    def SetSceneCamera(self, viewport_id: int, eye, target) -> None:
        if self._host is not None and hasattr(self._host, "set_scene_camera"): self._host.set_scene_camera(viewport_id, eye, target)

    def SetComponentProperty(self, entity_id: str, component: str,
                             property_name: str, value) -> bool:
        if self.IsEditorLocked(entity_id):return False
        return bool(self._host and hasattr(self._host, "set_component_property") and
                    self._host.set_component_property(entity_id, component, property_name, value))

    def SetGizmo(self, entity_id: str, mode: int) -> None:
        if self._host is not None and hasattr(self._host, "set_gizmo"):
            self._host.set_gizmo(entity_id, mode)

    def SetGizmoPosition(self, position, mode: int) -> None:
        if self._host is not None and hasattr(self._host,"set_gizmo_position"):
            self._host.set_gizmo_position(position,mode)

    def SetGizmoHover(self, axis: int) -> None:
        if self._host is not None and hasattr(self._host, "set_gizmo_hover"):
            self._host.set_gizmo_hover(axis)

    def PickPrimitive(self,origin,direction)->str:
        return self._host.pick_primitive(origin,direction) if self._host is not None and hasattr(self._host,"pick_primitive") else ""

    def SetObjectHover(self,entity_id:str,eye)->None:
        if self._host is not None and hasattr(self._host,"set_object_hover"):self._host.set_object_hover(entity_id,eye)

    def RaycastEditor(self,origin,direction,excluded=()):
        return self._host.raycast_editor(origin,direction,list(excluded)) if self._host is not None and hasattr(self._host,"raycast_editor") else None

    def SetSelectionOutline(self,entity_ids)->None:
        if self._host is not None and hasattr(self._host,"set_selection_outline"):self._host.set_selection_outline(entity_ids)

    def SetGrid(self, visible: bool, plane: int = 1) -> None:
        if self._host is not None and hasattr(self._host, "set_grid"):
            self._host.set_grid(visible, plane)

    def SetEditorIconsVisible(self,visible:bool)->None:
        if self._host is not None and hasattr(self._host,"set_editor_icons_visible"):self._host.set_editor_icons_visible(visible)

    def _EditorDescendant(self,entity_id,parent)->bool:
        seen=set();current=str(entity_id)
        while current and current not in seen:
            if current==str(parent):return True
            seen.add(current);current=self.EditorParents.get(current,"")
        return False

    def _EditorFlag(self,entity_id,flag)->bool:
        return any(values.get(flag,False) and self._EditorDescendant(entity_id,parent) for parent,values in self.EditorEntityState.items())

    def IsEditorLocked(self,entity_id)->bool:return self._EditorFlag(entity_id,"locked")
    def IsEditorHidden(self,entity_id)->bool:return self._EditorFlag(entity_id,"hidden")
    def IsEditorSelectable(self,entity_id)->bool:return not self.IsEditorLocked(entity_id) and not self.IsEditorHidden(entity_id)

    def SyncEditorEntityState(self,scenes)->None:
        self.EditorParents={str(entity["uuid"]):str(entity.get("parent", "")) for scene in scenes for entity in scene.get("entities",())}
        if self._host is not None and hasattr(self._host,"set_editor_entity_state"):
            hidden=[entity for entity in self.EditorParents if self.IsEditorHidden(entity)];locked=[entity for entity in self.EditorParents if self.IsEditorLocked(entity)]
            state=(self.SceneInfo().get("uuid",""),tuple(hidden),tuple(locked))
            if getattr(self,"_synced_editor_flags",None)!=state:
                self._host.set_editor_entity_state(hidden,locked);self._synced_editor_flags=state

    def SetSceneRenderMode(self,mode:str)->bool:
        return bool(self._host and hasattr(self._host,"set_scene_render_mode") and self._host.set_scene_render_mode(mode))

    def Play(self) -> bool:
        result=bool(self._host and self._host.play())
        if not result:self.ErrorOccurred.emit(self.LastError())
        return result

    def IsPlaying(self)->bool:return bool(self._host and self._host.is_playing())
    def RenameAssetDirectory(self,source,target)->bool:
        if not self._host or not hasattr(self._host,"rename_asset_directory"):return False
        result=bool(self._host.rename_asset_directory(str(Path(source).resolve()),str(Path(target).resolve())))
        if result:self.SceneChanged.emit()
        return result
    def IsPaused(self)->bool:return bool(self._host and self._host.is_paused())
    def SetGameInputActive(self,active:bool)->None:
        if self._host is not None and hasattr(self._host,"set_game_input_active"):self._host.set_game_input_active(active)
    def GameKey(self,key:int,down:bool,repeat:bool=False)->None:
        if self._host is not None and hasattr(self._host,"game_key"):self._host.game_key(key,down,repeat)
    def GameButton(self,button:int,down:bool)->None:
        if self._host is not None and hasattr(self._host,"game_button"):self._host.game_button(button,down)
    def GameMotion(self,x:float,y:float,dx:float,dy:float)->None:
        if self._host is not None and hasattr(self._host,"game_motion"):self._host.game_motion(x,y,dx,dy)
    def GameScroll(self,x:float,y:float)->None:
        if self._host is not None and hasattr(self._host,"game_scroll"):self._host.game_scroll(x,y)

    def ConfigureScripts(self,bindings:list[dict])->bool:
        return bool(self._host and hasattr(self._host,"configure_scripts") and self._host.configure_scripts(bindings))

    def Pause(self, paused: bool) -> None:
        if self._host is not None: self._host.pause(paused)

    def Step(self) -> None:
        if self._host is not None: self._host.step()

    def Tick(self) -> bool:
        if self._tick_error: return False
        try:
            if self._host is not None: self._host.tick()
        except Exception as error:
            # Circuit-break a faulty native frame. Keep save/close and the Qt UI
            # responsive instead of throwing and retrying at timer frequency.
            self._tick_error = str(error) or type(error).__name__
            self.ErrorOccurred.emit(self._tick_error)
            return False
        return True

    def Stop(self) -> None:
        if self._host is not None: self._host.stop(); self.SceneChanged.emit()


__all__ = ["RuntimeService"]
