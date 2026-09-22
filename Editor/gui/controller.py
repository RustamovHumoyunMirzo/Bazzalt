"""Coordinates editor widgets with the private runtime bridge."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QObject, QTimer
from PySide6.QtWidgets import QFileDialog

from ..runtime import RuntimeService
from .panels import ConsoleLevel
from .widgets import PlayState, StringInput, Vec3Input, Vec4Input


class EditorController(QObject):
    def __init__(self, window, runtime: RuntimeService) -> None:  # type: ignore[no-untyped-def]
        super().__init__(window)
        self.Window = window
        self.Runtime = runtime
        self.SelectedEntity = ""
        self.ScenePath = ""
        self._updating_inspector = False
        self.Timer = QTimer(self)
        self.Timer.setInterval(16)
        self.Timer.timeout.connect(runtime.Tick)

        window.MenuBar.OpenProjectRequested.connect(self.OpenProjectDialog)
        window.MenuBar.NewProjectRequested.connect(self.NewProjectDialog)
        window.MenuBar.SaveProjectRequested.connect(lambda: runtime.SaveProject())
        window.MenuBar.SaveProjectAsRequested.connect(self.SaveProjectAsDialog)
        window.MenuBar.OpenSceneRequested.connect(self.OpenSceneDialog)
        window.MenuBar.SaveSceneRequested.connect(self.SaveScene)
        window.MenuBar.SaveSceneAsRequested.connect(self.SaveSceneAsDialog)
        window.Toolbar.PlayRequested.connect(self.Play)
        window.Toolbar.StopRequested.connect(self.Stop)
        window.Toolbar.PauseRequested.connect(runtime.Pause)
        window.Toolbar.StepRequested.connect(runtime.Step)
        window.Hierarchy.SelectionChanged.connect(self.SelectEntity)
        window.Hierarchy.CreateRequested.connect(self.CreateEntity)
        window.Hierarchy.DeleteRequested.connect(self.DeleteEntity)
        runtime.SceneChanged.connect(self.RefreshHierarchy)
        runtime.ProjectChanged.connect(self._ProjectLoaded)
        runtime.ErrorOccurred.connect(lambda text: window.Console.AddMessage(text, ConsoleLevel.Error))
        if not runtime.IsAvailable():
            window.Console.AddMessage(runtime.LastError(), ConsoleLevel.Warning)

    def OpenProjectDialog(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self.Window, self.Window.Localization.Translate("dialog.open_project"),
            "", self.Window.Localization.Translate("dialog.project_filter")
        )
        if path: self.Runtime.LoadProject(path)

    def NewProjectDialog(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self.Window, self.Window.Localization.Translate("dialog.new_project"),
            "", self.Window.Localization.Translate("dialog.project_filter"))
        if path: self.Runtime.CreateProject(path)

    def SaveProjectAsDialog(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self.Window, self.Window.Localization.Translate("dialog.save_project"),
            "", self.Window.Localization.Translate("dialog.project_filter"))
        if path: self.Runtime.SaveProject(path)

    def OpenSceneDialog(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self.Window, self.Window.Localization.Translate("dialog.open_scene"),
            self.Runtime.AssetDirectory(), self.Window.Localization.Translate("dialog.scene_filter"))
        if path and self.Runtime.LoadScene(path): self.ScenePath = path

    def SaveScene(self) -> None:
        if self.ScenePath: self.Runtime.SaveScene(self.ScenePath)
        else: self.SaveSceneAsDialog()

    def SaveSceneAsDialog(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self.Window, self.Window.Localization.Translate("dialog.save_scene"),
            self.Runtime.AssetDirectory(), self.Window.Localization.Translate("dialog.scene_filter"))
        if path and self.Runtime.SaveScene(path): self.ScenePath = path

    def _ProjectLoaded(self, path: str) -> None:
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
        for entity in entities:
            parent = items.get(entity["parent"])
            items[entity["uuid"]] = self.Window.Hierarchy.AddItem(
                entity["name"], entity["uuid"], parent
            )
        if selected in items:
            self.Window.Hierarchy.Tree.setCurrentItem(items[selected])
        elif selected:
            self.SelectEntity(None)

    def SelectEntity(self, entity_id) -> None:  # type: ignore[no-untyped-def]
        self.SelectedEntity = str(entity_id or "")
        self.Window.Properties.Clear()
        if not self.SelectedEntity: return
        details = self.Runtime.EntityDetails(self.SelectedEntity)
        if not details: return
        identity = self.Window.Properties.AddComponentSection("identity", "Entity")
        name = StringInput(str(details["name"])); name.setReadOnly(True)
        identity.AddField("Name", name)
        transform = self.Window.Properties.AddComponentSection("transform", "Transform")
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
        for component in details.get("components", ())[1:]:
            if component != "Transform":
                self.Window.Properties.AddComponentSection(
                    f"runtime.{component}", str(component), expanded=False
                )

    def CreateEntity(self, parent) -> None:  # type: ignore[no-untyped-def]
        self.Runtime.CreateEntity(self.Window.Localization.Translate("entity.new"), str(parent or ""))

    def DeleteEntity(self, entity_id) -> None:  # type: ignore[no-untyped-def]
        if entity_id: self.Runtime.DestroyEntity(str(entity_id))

    def Play(self) -> None:
        if self.Runtime.Play(): self.Timer.start()
        else: self.Window.Toolbar.SetPlayState(PlayState.Stopped)

    def Stop(self) -> None:
        self.Timer.stop(); self.Runtime.Stop()

    def ApplyGizmoTranslation(self, delta) -> bool:  # type: ignore[no-untyped-def]
        return bool(self.SelectedEntity and self.Runtime.Translate(
            self.SelectedEntity, (delta.X, delta.Y, delta.Z)
        ))


__all__ = ["EditorController"]
