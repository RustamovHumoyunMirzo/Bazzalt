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
    def test_local_fog_component_inspector_validation_and_enable(self):
        from Editor.runtime import RuntimeService
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest(runtime.LastError())
        window=Editor(runtime=runtime,settings={},settings_saver=lambda _:None)
        try:
            entity=runtime.CreateEntity("Fog bank")
            self.assertTrue(runtime.AddComponent(entity,"Local Fog"))
            self.assertTrue(runtime.SetComponentProperty(entity,"Local Fog","Size",(12,4,8)))
            self.assertTrue(runtime.SetComponentProperty(entity,"Local Fog","Mode",1))
            self.assertFalse(runtime.SetComponentProperty(entity,"Local Fog","Size",(-1,4,8)))
            details=runtime.EntityDetails(entity)
            self.assertEqual(tuple(details['component_data']['Local Fog']['Size']),(12,4,8))
            window.Controller.SelectEntity(entity,force=True)
            section=window.Properties._sections['runtime.Local Fog']
            self.assertIn('Mode',section._fields);self.assertIn('Size',section._fields)
            self.assertEqual(section._fields['Mode'].GetValue(),1)
            self.assertTrue(runtime.SetComponentEnabled(entity,"Local Fog",False))
            self.assertFalse(runtime.EntityDetails(entity)['component_enabled']['Local Fog'])
            self.assertTrue(runtime.RemoveComponent(entity,"Local Fog"))
            self.assertNotIn('Local Fog',runtime.EntityDetails(entity)['components'])
        finally:
            window.Controller.SetDirty(False);window.close();window.deleteLater()
            self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete);runtime.Release()

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
    def test_volumetric_mode_initializes_visible_defaults_and_keeps_authored_ranges(self):
        window=Editor()
        try:
            window.Controller.Timer.stop()
            scene=window.Runtime.SceneInfo()["uuid"]
            # Older scenes can already have Volumetric selected with analytic defaults.
            self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{"fog_mode":2}))
            window.Controller.SelectEntity(scene,force=True)
            mode=window.Properties._sections["scene.fog"]._fields[window.Localization.Translate("fog.mode")]
            mode.activated.emit(mode.currentIndex())
            self.App.processEvents()
            settings=window.Runtime.SceneEnvironment(scene)
            self.assertEqual(settings['fog_mode'],2)
            self.assertAlmostEqual(settings['fog_Density'],.002)
            self.assertEqual(settings['fog_StartDistance'],0)
            self.assertEqual(settings['fog_MaxDistance'],1000)
            self.assertTrue(settings['fog_sky'])
            self.assertEqual(settings['fog_HeightFalloff'],0)
            window.Controller.Undo()
            settings=window.Runtime.SceneEnvironment(scene)
            self.assertEqual(settings['fog_mode'],2)
            self.assertAlmostEqual(settings['fog_Density'],.002)
            self.assertEqual(settings['fog_StartDistance'],10)
            self.assertFalse(settings['fog_sky'])
            self.assertAlmostEqual(settings['fog_HeightFalloff'],.05)
            # Upgrade the previous editor profile on re-select, in one undo step.
            self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{'fog_Density':.02,'fog_StartDistance':0,'fog_MaxDistance':200,'fog_sky':True}))
            window.Controller.SelectEntity(scene,force=True)
            mode=window.Properties._sections['scene.fog']._fields[window.Localization.Translate('fog.mode')]
            mode.activated.emit(mode.currentIndex());self.App.processEvents()
            self.assertEqual(window.Runtime.SceneEnvironment(scene)['fog_HeightFalloff'],0)
            window.Controller.Undo()
            self.assertAlmostEqual(window.Runtime.SceneEnvironment(scene)['fog_HeightFalloff'],.05)
            self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{'fog_mode':1,'fog_Density':.035,'fog_StartDistance':3,'fog_MaxDistance':350,'fog_sky':False,'fog_HeightFalloff':.3}))
            window.Controller.SelectEntity(scene,force=True)
            mode=window.Properties._sections['scene.fog']._fields[window.Localization.Translate('fog.mode')]
            mode.SetValue(2)
            self.App.processEvents()
            settings=window.Runtime.SceneEnvironment(scene)
            self.assertAlmostEqual(settings['fog_Density'],.035)
            self.assertEqual(settings['fog_StartDistance'],3)
            self.assertEqual(settings['fog_MaxDistance'],350)
            self.assertAlmostEqual(settings['fog_HeightFalloff'],.3)
            self.assertFalse(settings['fog_sky'])
        finally:
            window.Controller.SetDirty(False);window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

    def test_missing_environment_reference_allows_fog_edits(self):
        window=Editor()
        try:
            window.Controller.Timer.stop()
            missing=str(uuid.uuid4())
            snapshot=window.Runtime.CaptureScene().replace(b'Source: "00000000-0000-0000-0000-000000000000"',('Source: "'+missing+'"').encode())
            self.assertTrue(window.Runtime.RestoreScene(snapshot))
            scene=window.Runtime.SceneInfo()['uuid']
            self.assertEqual(window.Runtime.SceneEnvironment(scene)['source'],missing)
            self.assertTrue(window.Runtime.SetSceneEnvironment(scene,{'fog_mode':2,'fog_Density':.04,'fog_StartDistance':0,'fog_sky':True}))
            settings=window.Runtime.SceneEnvironment(scene)
            self.assertEqual(settings['source'],missing)
            self.assertEqual(settings['fog_mode'],2)
            self.assertAlmostEqual(settings['fog_Density'],.04)
            settings['fog_Density']=.06
            self.assertTrue(window.Runtime.SetSceneEnvironment(scene,settings))
            self.assertAlmostEqual(window.Runtime.SceneEnvironment(scene)['fog_Density'],.06)
            self.assertFalse(window.Runtime.SetSceneEnvironment(scene,{'source':str(uuid.uuid4())}))
        finally:
            window.Controller.SetDirty(False);window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

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
            legacy=snapshot.replace(b"Version: 3\n  Mode: 1",b"Version: 1");self.assertTrue(window.Runtime.RestoreScene(legacy));self.assertEqual(window.Runtime.SceneEnvironment(scene)["mode"],0)
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
