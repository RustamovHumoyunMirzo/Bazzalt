import json
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import tempfile
import unittest
import wave
from contextlib import ExitStack
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.gui.controller import EditorController
from Editor.gui.widgets.audio import AudioCurveInput,AudioPreview
from Editor.gui.panels.assets import AssetBrowserPanel,AUDIO_EXTENSIONS
from Editor.gui.panels.properties import PropertiesPanel
from Editor.lua_declarations import ReadDeclarations

APP=QApplication.instance() or QApplication([])

class AudioTests(unittest.TestCase):
    def test_audio_component_bridge_and_clip_import(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime is not built')
        try:
            with tempfile.TemporaryDirectory() as folder, ExitStack() as cleanup:
                cleanup.callback(runtime.Release)
                root=Path(folder);(root/'Assets').mkdir();clip=root/'Assets'/'音声.wav'
                with wave.open(str(clip),'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(48000);f.writeframes(b'\x00\x00'*48000)
                project=root/'Audio.bproject';project.write_text('FormatVersion: 1\nProjectUUID: "12345678-1234-4234-8234-123456789abc"\nName: Audio\nAssetDirectory: Assets\nStartupScene: ""\nProperties:\n',encoding='utf-8')
                self.assertTrue(runtime.LoadProject(str(project)),runtime.LastError());info=runtime.AudioClipInfo(clip);self.assertEqual(info['frames'],48000);self.assertEqual(info['duration'],1)
                entity=runtime.CreateEntity('Speaker');self.assertTrue(runtime.AddComponent(entity,'Audio Source'));self.assertTrue(runtime.AddComponent(entity,'Audio Listener'))
                self.assertTrue(runtime.SetComponentProperty(entity,'Audio Source','Clip',info['uuid']))
                for name,value in [('Volume',.7),('Pitch',1.2),('Spatial Blend',.5),('Rolloff Curve',2),('Custom Rolloff',[(0,1),(.5,.7),(1,0)])]:self.assertTrue(runtime.SetComponentProperty(entity,'Audio Source',name,value))
                for name,value in [('Volume',2),('Pitch',.1),('Priority',-1),('Min Distance',200),('Custom Rolloff',[(0,1),(0,.5),(1,0)])]:self.assertFalse(runtime.SetComponentProperty(entity,'Audio Source',name,value))
                snapshot=runtime.CaptureScene();self.assertTrue(runtime.RestoreScene(snapshot));data=runtime.EntityDetails(entity)['component_data']['Audio Source'];self.assertAlmostEqual(data['Volume'],.7,places=5);self.assertEqual(len(data['Custom Rolloff']),3)
                self.assertTrue(runtime.SetComponentEnabled(entity,'Audio Listener',False));self.assertFalse(runtime.EntityDetails(entity)['component_enabled']['Audio Listener'])
                self.assertTrue(runtime.RemoveComponent(entity,'Audio Source'));self.assertNotIn('Audio Source',runtime.EntityDetails(entity)['components'])
                locale=LocalizationManager(ResourceManager());browser=AssetBrowserPanel(locale,ResourceManager());browser.SetProjectRoot(root/'Assets');browser._Create('audio_mixer');self.assertTrue(runtime.RefreshAssets())
                mixer=root/'Assets'/'New Audio Mixer.bmixer';mixer_info=runtime.AssetInfo(str(mixer));buses=runtime.AudioBuses(mixer_info['uuid']);self.assertEqual(len(buses),3)
                buses[1]['volume']=.3;self.assertTrue(runtime.SaveAudioMixer(mixer_info['uuid'],buses));self.assertAlmostEqual(runtime.AudioBuses(mixer_info['uuid'])[1]['volume'],.3,places=5)
                properties=PropertiesPanel(locale);controller=EditorController.__new__(EditorController);controller.Runtime=runtime;controller.Window=SimpleNamespace(Localization=locale,Properties=properties,Console=SimpleNamespace(AddMessage=Mock()));controller._inspected_asset=mixer
                controller._InspectAudioMixer(mixer);self.assertEqual(len(properties._sections),1);self.assertIn('mixer_preview',properties._sections);properties.deleteLater();browser.deleteLater()
        finally:runtime.Release()

    def test_preview_and_curve_controls(self):
        resources=ResourceManager();locale=LocalizationManager(resources)
        runtime=SimpleNamespace(PreviewAudio=Mock(return_value=4),AudioStatus=Mock(return_value={'valid':True,'state':1,'time':.2}),AudioControl=Mock(return_value=True),StopAudioPreview=Mock())
        preview=AudioPreview(runtime,'clip',locale,1);preview.Play();runtime.PreviewAudio.assert_called_once_with('clip');preview.Pause();runtime.AudioControl.assert_called_with(4,'pause');preview.Stop();runtime.StopAudioPreview.assert_called_once()
        curve=AudioCurveInput([(0,1),(1,0)],locale);curve.Add();self.assertEqual(curve.GetValue(),[(0,1),(.5,.5),(1,0)]);curve.Edit(1,(.7,.8));self.assertEqual(curve.GetValue()[1],(.7,.8));curve.Edit(0,(.1,1));self.assertEqual(curve.GetValue()[0],(0,1));curve.Remove(1);self.assertEqual(len(curve.GetValue()),2)
        preview.deleteLater();curve.deleteLater()

    def test_audio_browser_icons_and_lua_declarations(self):
        resources=ResourceManager();locale=LocalizationManager(resources);panel=AssetBrowserPanel(locale,resources)
        for extension in AUDIO_EXTENSIONS:self.assertFalse(panel._Icon(Path('clip'+extension)).isNull())
        with tempfile.TemporaryDirectory() as directory:
            panel.SetProjectRoot(directory);panel._Create('audio_mixer');self.assertTrue((Path(directory)/'New Audio Mixer.bmixer').is_file())
        panel.deleteLater()
        result=ReadDeclarations('local B=Bazzalt\nlocal Sound=COMPONENT("Sound")\nSound.Clip=PROPERTY("AudioClip",B.AudioClip.new())\nreturn Sound')
        self.assertEqual(result[1][0][0],'AudioClip')
