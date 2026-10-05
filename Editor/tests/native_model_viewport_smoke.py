"""Manual real-driver model/present regression; never run on Qt offscreen CI.

python -m Editor.tests.native_model_viewport_smoke path/to/model.glb [vulkan]
Uses a temporary project, does not edit the supplied model or user settings.
"""
import faulthandler
import shutil
import sys
import tempfile
from time import perf_counter
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication, QWidget
from Editor.runtime import _LoadNativeModule


def main():
    faulthandler.enable()
    app=QApplication([])
    runtime=None
    if "--editor" in sys.argv:
        from Editor.runtime import RuntimeService
        runtime=RuntimeService();host=runtime._host
    else:
        module=_LoadNativeModule();assert module is not None;host=module.EditorHost()
    with tempfile.TemporaryDirectory(prefix="bazzalt-viewport-") as directory:
        root=Path(directory);(root/"Assets").mkdir()
        shutil.copyfile(sys.argv[1],root/"Assets"/"model.glb")
        project=root/"test.bproject"
        project.write_text('FormatVersion: 1\nProjectUUID: "00000000-0000-0001-0000-000000000001"\nName: Smoke\nAssetDirectory: Assets\nStartupScene: ""\nProperties:\n',encoding="utf-8")
        assert host is not None
        assert host.configure_rendering_backend(sys.argv[2] if len(sys.argv)>2 else "vulkan")
        assert host.load_project(str(project))
        if "--editor" in sys.argv:
            from Editor.gui.application import Editor
            from PySide6.QtTest import QTest
            window=Editor(runtime=runtime,settings={},settings_saver=lambda _:None)
            window.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen);window.show();app.processEvents()
            window.Controller._ProjectLoaded(str(project));window.Controller.Timer.stop()
            print("FULL EDITOR DROP",flush=True)
            window.Controller.InstantiateAsset(str(root/"Assets"/"model.glb"),"")
            window.Controller.Timer.stop()
            for frame in range(10):
                if frame==4:
                    mesh=next(entity for entity in runtime.Entities() if "Mesh" in entity.get("components",()))
                    window.Controller.SelectEntity(mesh["uuid"])
                started=perf_counter();window.Controller._Tick();elapsed=perf_counter()-started;QTest.qWait(10);print("EDITOR FRAME",frame,round(elapsed*1000,2),"ms",flush=True)
            window.Controller.IsDirty=False;window.Controller._dirty_scenes.clear()
            window.close();app.processEvents();runtime.Release();del host
            return 0
        surface=QWidget();surface.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
        surface.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen);surface.resize(640,480)
        surface.show();app.processEvents()
        print("CREATE VIEWPORT",flush=True)
        assert host.create_viewport(1,int(surface.winId()),True,640,480,1.)
        identity=host.instantiate_model_path(str(root/"Assets"/"model.glb"),"")
        print("MODEL",identity,flush=True)
        host.set_scene_camera(1,(6.,4.,8.),(0.,0.,0.))
        for frame in range(4):host.tick();app.processEvents();print("UNSELECTED",frame,flush=True)
        host.set_selection_outline([identity])
        for frame in range(4):host.tick();app.processEvents();print("SELECTED",frame,flush=True)
        host.set_selection_outline([])
        # Exercise long camera frustum edges near their apex: these must remain
        # clipped raster lines, never midpoint-scaled world-space boxes.
        camera=host.create_entity("Guide camera","");assert host.add_component(camera,"Camera")
        host.set_selection_outline([camera])
        from PySide6.QtTest import QTest
        for frame in range(4):host.tick();QTest.qWait(20);print("CAMERA GUIDES",frame,flush=True)
        host.resize_viewport(1,480,360,1.)
        for frame in range(4):host.tick();app.processEvents();print("RESIZED",frame,flush=True)
        host.destroy_viewport(1);del host;surface.close();app.processEvents()
    return 0


if __name__=="__main__":raise SystemExit(main())
