import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
from pathlib import Path
import tempfile
import unittest
import uuid
import json
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QEvent
from PySide6.QtGui import QImage,QColor
from Editor.gui.application import Editor
from Editor.gui.widgets import InspectorViewport
from Editor.gui.panels.properties import ComponentSection
from Editor.gui.widgets import FloatInput,BoolInput,Vec3Input
from Editor.theme import Theme,BuildPalette
from Editor.environments import InspectEnvironment

ROOT=Path(__file__).resolve().parents[2]

class EnvironmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_shared_preview_resets_and_handles_missing_images_without_native_surface(self):
        widget=InspectorViewport(empty_text="No preview")
        image=QImage(64,32,QImage.Format.Format_RGB32);image.fill(QColor("blue"));widget.SetImage(image)
        self.assertTrue(widget.HasImage());widget._zoom=4;widget.ResetView();self.assertEqual(widget._zoom,1)
        widget.SetImage(None);self.assertFalse(widget.HasImage());widget.close();widget.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_preview_background_is_darker_in_both_themes(self):
        widget=InspectorViewport()
        for theme in (Theme.dark(),Theme.light()):
            widget.setPalette(BuildPalette(theme))
            self.assertLess(widget.BackgroundColor().lightness(),QColor(theme.surface).lightness())
            self.assertLess(widget.BackgroundColor().lightness(),QColor(theme.background).lightness())
        widget.close();widget.deleteLater()
    def test_component_rows_have_uniform_spacing_without_vertical_stretch(self):
        section=ComponentSection("Fields")
        for label,field in (("Value",FloatInput()),("Enabled",BoolInput()),("Position",Vec3Input())):section.AddField(label,field)
        section.resize(400,600);section.show();self.App.processEvents()
        rows=[section.Form.cellRect(row,0) for row in range(section.Form.rowCount()) if section.Form.itemAtPosition(row,0)]
        self.assertEqual(len(rows),3)
        self.assertEqual(len({row.height() for row in rows}),1)
        self.assertEqual([rows[i+1].top()-rows[i].bottom()-1 for i in range(2)],[6,6])
        self.assertLess(rows[-1].bottom(),150)
        section.close();section.deleteLater()
    def test_scene_environment_fields_persist_and_undo(self):
        window=Editor()
        try:
            scene=window.Runtime.SceneInfo()["uuid"];window.Controller.SelectEntity(scene,force=True)
            self.assertIn("scene.environment",window.Properties._sections)
            self.assertFalse(window.Properties.AddComponentButton.isVisible())
            window.Controller._SetSceneEnvironment(scene,"intensity",1234.)
            self.assertEqual(window.Runtime.SceneEnvironment(scene)["intensity"],1234.)
            self.assertTrue(window.Controller.History.Undo())
            self.assertEqual(window.Runtime.SceneEnvironment(scene)["intensity"],30000.)
            self.assertFalse(window.Runtime.SetSceneEnvironment(scene,{"intensity":float("nan")}))
            window.Runtime.Tick();self.assertFalse(window.Runtime.SceneEnvironment(scene)["lighting_ready"])
        finally:window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_environment_mode_shows_only_its_picker_and_preserves_legacy_material_mode(self):
        window=Editor()
        try:
            scene=window.Runtime.SceneInfo()["uuid"];window.Controller.SelectEntity(scene,force=True)
            tr=window.Localization.Translate;fields=window.Properties._sections["scene.environment"]._fields
            self.assertIn(tr("environment.source"),fields);self.assertNotIn(tr("environment.material"),fields)
            fields[tr("environment.mode")].SetValue(1);self.App.processEvents()
            self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],1)
            fields=window.Properties._sections["scene.environment"]._fields
            self.assertIn(tr("environment.material"),fields);self.assertNotIn(tr("environment.source"),fields)
            window.Controller.Undo();self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],0)
            self.assertIn(tr("environment.source"),window.Properties._sections["scene.environment"]._fields)
            window.Controller.Redo();self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],1)
            self.assertIn(tr("environment.material"),window.Properties._sections["scene.environment"]._fields)
            snapshot=window.Runtime.CaptureScene();self.assertIn(b"Mode: 1",snapshot)
            window.Runtime.SetSceneEnvironment(scene,{"mode":0});self.assertTrue(window.Runtime.RestoreScene(snapshot));self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],1)
            self.assertFalse(window.Runtime.SetSceneEnvironment(scene,{"mode":99}))
            legacy=snapshot.replace(b"Version: 2\n  Mode: 1",b"Version: 1");self.assertTrue(window.Runtime.RestoreScene(legacy));self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],0)
            legacy=legacy.replace(b'  Material: "00000000-0000-0000-0000-000000000000"',b'  Material: "00000000-0000-0001-0000-000000000001"');self.assertTrue(window.Runtime.RestoreScene(legacy));self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],1)
        finally:
            window.Controller.SetDirty(False);window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_hdr_import_preview_reimport_lighting_and_clear(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            root=Path(folder)/"Документы 日本語 project";root.mkdir();assets=root/"Assets";assets.mkdir()
            source=assets/"окружение 空.hdr"
            source.write_bytes(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n"+bytes([128,64,32,129])*8)
            project=root/"test.bproject";project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: "Test"\nAssetDirectory: "Assets"\nStartupScene: ""\nProperties:\n',encoding="utf-8")
            window=Editor()
            try:
                self.assertTrue(window.Runtime.LoadProject(project),window.Runtime.LastError())
                info=window.Runtime.AssetInfo(str(source));self.assertEqual(info["importer"],"Bazzalt.Environment")
                data=InspectEnvironment(source,info);self.assertEqual((data["width"],data["height"]),(4,2))
                self.assertTrue(data["preview"].is_file());self.assertTrue(data["ibl"].is_file(),str(list(Path(str(info["cache"])+".environment").rglob("*"))))
                window.Controller.SelectAsset(source);self.assertIn("asset.environment",window.Properties._sections)
                self.assertTrue(window.Properties.findChildren(InspectorViewport)[0].HasImage())
                scene=window.Runtime.SceneInfo()["uuid"]
                self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{"source":info["uuid"]}))
                self.assertTrue(window.Runtime.Play(),window.Runtime.LastError());window.Runtime.Tick()
                self.assertTrue(window.Runtime.SceneEnvironment(scene)["lighting_ready"])
                self.assertTrue(window.Runtime.SceneEnvironment(scene)["skybox_ready"])
                # The inactive map must never leak into material mode.
                self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{"mode":1}));window.Runtime.Tick()
                self.assertFalse(window.Runtime.SceneEnvironment(scene)["skybox_ready"])
                shader=assets/"sky.mat";shader.write_text('material {name:"SkyConfig",parameters:[{type:sampler2d,name:sky}]} fragment {void material(inout MaterialInputs material){prepareMaterial(material);material.baseColor=vec4(1.0);}}',encoding="utf-8")
                self.assertTrue(window.Runtime.RefreshAssets());shader_id=window.Runtime.AssetInfo(str(shader))["uuid"]
                material=assets/"sky.matinst";material.write_text(json.dumps({"version":1,"shader":shader_id,"properties":{"sky":info["uuid"]}}),encoding="utf-8")
                self.assertTrue(window.Runtime.RefreshAssets());material_id=window.Runtime.AssetInfo(str(material))["uuid"]
                window.Controller.MaterialCompiler.PrepareMaterial(material_id)
                self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{"mode":1,"material":material_id}));window.Runtime.Tick()
                self.assertTrue(window.Runtime.SceneEnvironment(scene)["skybox_ready"])
                self.assertEqual(window.Runtime.SceneEnvironment(scene)["resolved_source"],info["uuid"])
                self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{"mode":0}));window.Runtime.Tick()
                self.assertTrue(window.Runtime.SetEnvironmentImportSettings(info["uuid"],{"Resolution":"32","Samples":"64"}),window.Runtime.LastError())
                updated=window.Runtime.AssetInfo(info["uuid"]);self.assertNotEqual(updated["cache"],info["cache"])
                self.assertEqual(updated["settings"]["Resolution"],"32")
                self.assertFalse(window.Runtime.SetEnvironmentImportSettings(info["uuid"],{"Resolution":"3"}))
                self.assertEqual(window.Runtime.AssetInfo(info["uuid"])["settings"]["Resolution"],"32")
                regenerated_preview=InspectEnvironment(source,updated)["preview"]
                regenerated_preview.unlink()
                self.assertTrue(window.Runtime.RefreshAssets(),window.Runtime.LastError())
                self.assertTrue(regenerated_preview.is_file())
                window.Runtime.Tick();self.assertTrue(window.Runtime.SceneEnvironment(scene)["lighting_ready"])
                self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{"source":str(uuid.UUID(int=0)),"clear_color":[.1,.2,.3,1]}));window.Runtime.Tick()
                self.assertFalse(window.Runtime.SceneEnvironment(scene)["lighting_ready"]);self.assertFalse(window.Runtime.SceneEnvironment(scene)["skybox_ready"])
                snapshot=window.Runtime.CaptureScene();self.assertIn(b"Environment:",snapshot)
                window.Runtime.SetSceneEnvironment(scene,{"intensity":5.});self.assertTrue(window.Runtime.RestoreScene(snapshot))
                self.assertEqual(window.Runtime.SceneEnvironment(scene)["intensity"],30000.)
            finally:window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

if __name__=="__main__":unittest.main()
