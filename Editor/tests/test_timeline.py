import copy
import tempfile
import unittest
import uuid
import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from pathlib import Path
from PySide6.QtWidgets import QApplication
from Editor import timeline as Data
from Editor.gui.panels.timeline import TimelinePanel
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.theme import ThemeManager

TARGET = "00000000-0000-0001-0000-000000000001"


class Runtime:
    def __init__(self):
        self.PreviewCalls = []
        self.Stops = 0

    def IsPlaying(self):
        return False

    def PreviewTimeline(self, *args):
        self.PreviewCalls.append(args)

    def EndTimelinePreview(self):
        self.Stops += 1

    def CaptureTimelinePose(self, *args):
        return dict(position=[1, 2, 3], rotation=[0, 0, 0, 1], scale=[1, 1, 1])

    def EntityDetails(self, identity):
        return dict(name="Cube", components=["Transform"])

    def Entities(self):
        return [dict(uuid=TARGET, name="Cube")]


class TimelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def test_roundtrip_and_strict_track_validation(self):
        doc = Data.NewSequence()
        doc["tracks"] = [
            dict(
                name="Cube",
                target=TARGET,
                keys=[
                    dict(
                        time=0,
                        position=[0, 0, 0],
                        rotation=[0, 0, 0, 1],
                        scale=[1, 1, 1],
                    )
                ],
            )
        ]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "Scene.btimeline"
            self.assertTrue(Data.Save(path, doc))
            self.assertFalse(Data.Save(path, doc))
            self.assertEqual(Data.Load(path), doc)
            before = path.read_bytes()
            bad = copy.deepcopy(doc)
            bad["duration"] = float("nan")
            with self.assertRaises(ValueError):
                Data.Save(path, bad)
            self.assertEqual(path.read_bytes(), before)
        for edit in (
            lambda d: d["tracks"].append(copy.deepcopy(d["tracks"][0])),
            lambda d: d["tracks"][0]["keys"].append(
                copy.deepcopy(d["tracks"][0]["keys"][0])
            ),
            lambda d: d["tracks"][0]["keys"][0].update(scale=[0, 1, 1]),
            lambda d: d.update(frameRate=True),
        ):
            invalid = copy.deepcopy(doc)
            edit(invalid)
            with self.assertRaises(ValueError):
                Data.Validate(invalid)
        invalid = copy.deepcopy(doc)
        invalid["cameras"] = [
            dict(camera=TARGET, start=0, end=2),
            dict(camera=TARGET, start=1, end=3),
        ]
        with self.assertRaises(ValueError):
            Data.Validate(invalid)
        self.assertEqual(Data.Snap(0.11, doc), 3 / 30)

    def test_native_preview_restores_and_director_roundtrip(self):
        from Editor.runtime import RuntimeService

        runtime = RuntimeService()
        if not runtime.IsAvailable() or not hasattr(runtime._host, "timeline_preview"):
            runtime.Release()
            self.skipTest("Native timeline bridge unavailable")
        try:
            actor = runtime.CreateEntity("Actor")
            camera = runtime.CreateEntity("Camera")
            self.assertTrue(runtime.AddComponent(camera, "Camera"))
            self.assertTrue(runtime.AddComponent(actor, "Timeline Director"))
            self.assertTrue(
                runtime.SetComponentProperty(actor, "Timeline Director", "Loop", True)
            )
            document = Data.NewSequence()
            document["duration"] = 2
            document["tracks"] = [
                dict(
                    name="Actor",
                    target=actor,
                    keys=[
                        dict(
                            time=0,
                            position=[0, 0, 0],
                            rotation=[0, 0, 0, 1],
                            scale=[1, 1, 1],
                        ),
                        dict(
                            time=2,
                            position=[10, 0, 0],
                            rotation=[0, 0, 0, 1],
                            scale=[1, 1, 1],
                        ),
                    ],
                )
            ]
            document["cameras"] = [
                dict(name="Shot", camera=camera, start=0, end=2, blendIn=0)
            ]
            runtime.PreviewTimeline(Data.Source(document), 1, True)
            self.assertAlmostEqual(runtime.EntityDetails(actor)["position"][0], 5)
            runtime.EndTimelinePreview()
            self.assertAlmostEqual(runtime.EntityDetails(actor)["position"][0], 0)
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "Scene.bscene"
                self.assertTrue(runtime.SaveScene(path))
                self.assertIn(
                    "Bazzalt.TimelineDirector", path.read_text(encoding="utf-8")
                )
                self.assertTrue(runtime.LoadScene(path))
                self.assertTrue(
                    runtime.EntityDetails(actor)["component_data"]["Timeline Director"][
                        "Loop"
                    ]
                )
                runtime.Release()
        finally:
            runtime.Release()

    def test_typed_signal_arguments(self):
        document = Data.NewSequence()
        document["signals"] = [
            dict(
                time=0,
                name="Cue",
                event=Data.ZERO,
                arguments=[
                    dict(type="number", value=2),
                    dict(type="string", value="Start"),
                    dict(type="uuid", value=TARGET),
                ],
            )
        ]
        Data.Validate(document)
        for value in (float("inf"), True):
            bad = copy.deepcopy(document)
            bad["signals"][0]["arguments"][0]["value"] = value
            with self.assertRaises(ValueError):
                Data.Validate(bad)

    def test_panel_keys_history_preview_restore_and_frame_step(self):
        runtime = Runtime()
        resources = ResourceManager()
        panel = TimelinePanel(
            runtime, LocalizationManager(resources), resources, ThemeManager(self.App)
        )
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "Test.btimeline"
            Data.Save(path, Data.NewSequence())
            original = path.read_bytes()
            self.assertTrue(panel.Open(path))
            self.assertTrue(panel.Flush())
            self.assertEqual(path.read_bytes(), original)
            panel.SetEntity(TARGET)
            panel.AddTransform()
            self.assertEqual(len(panel.Document["tracks"]), 1)
            panel.Tracks.setCurrentItem(panel.Tracks.topLevelItem(0))
            panel.Time.setValue(1)
            panel.CaptureKey()
            self.assertEqual(len(panel.Document["tracks"][0]["keys"]), 2)
            panel.History.undo()
            self.assertEqual(len(panel.Document["tracks"][0]["keys"]), 1)
            panel.History.redo()
            self.assertEqual(len(panel.Document["tracks"][0]["keys"]), 2)
            first = next(
                item for item in panel.Scene.items() if item.data(0) == ("key", 0, 0)
            )
            first.setSelected(True)
            panel.MoveItems(copy.deepcopy(panel.Document), [(("key", 0, 0), 0)])
            self.assertEqual(panel.Selected(), [("key", 0, 0)])
            panel.MoveItems(copy.deepcopy(panel.Document), [(("key", 0, 0), 1.5)])
            self.assertEqual(panel.Selected(), [("key", 0, 1)])
            panel.Seek(0.5)
            self.assertTrue(runtime.PreviewCalls)
            calls = len(runtime.PreviewCalls)
            panel.Stop()
            self.assertEqual(len(runtime.PreviewCalls), calls)
            self.assertFalse(panel.Preview)
            panel.Step(1)
            self.assertAlmostEqual(panel.View.Time, 1 / 30)
            panel.Flush()
            self.assertEqual(Data.Load(path), panel.Document)
        panel.EndPreview()
        panel.deleteLater()
