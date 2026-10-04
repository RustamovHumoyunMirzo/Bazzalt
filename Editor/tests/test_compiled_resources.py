from __future__ import annotations
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QFile
from PySide6.QtWidgets import QApplication
from bazzalt.resources import Package, ResourcePackage
from bazzalt.branding import LogoIcon
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.theme import BuildStyleSheet, Theme
from scripts.compile_resources import Audit, Compile

ROOT = Path(__file__).resolve().parents[2]


class CompiledResourcesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])
        Compile()

    def test_every_editor_asset_is_embedded_byte_for_byte(self):
        resources=Package("editor",compiled=True)
        for path in (ROOT/"Editor/assets").rglob("*"):
            if path.is_file():
                alias=path.relative_to(ROOT/"Editor/assets").as_posix()
                with self.subTest(asset=alias):
                    self.assertEqual(resources.ReadBytes(alias),path.read_bytes())

    def test_production_icons_localization_styles_and_branding(self):
        with patch.dict(os.environ,{"BAZZALT_COMPILED_RESOURCES":"1"}):
            resources=ResourceManager()
            self.assertTrue(resources.Path("locales/en.json").startswith(":/bazzalt/editor/"))
            self.assertEqual(LocalizationManager(resources).Translate("action.save_project"),"Save Project")
            self.assertFalse(resources.Icon("icons/scene_cam.svg").isNull())
            for theme in (Theme.dark(),Theme.light()):
                style=BuildStyleSheet(theme)
                self.assertIn(":/bazzalt/editor/icons/",style)
                self.assertNotIn(str(ROOT),style)
            self.assertFalse(LogoIcon().isNull())

    def test_compiled_packages_do_not_need_source_assets(self):
        resources=ResourcePackage("editor",ROOT/"build/nonexistent-resource-root",compiled=True)
        self.assertIn("Save Project",resources.ReadText("locales/en.json"))

    def test_native_hub_branding_is_compiled_without_html(self):
        hub=Package("hub",compiled=True)
        url=hub.Url("BazzaltLogo.svg")
        self.assertEqual(url.scheme(),"qrc")
        self.assertTrue(QFile.exists(":"+url.path()))
        self.assertEqual(hub.ReadBytes("BazzaltLogo.svg"),(ROOT/"Launcher/BazzaltLogo.svg").read_bytes())
        with self.assertRaises(FileNotFoundError):hub.ReadText("index.html")

    def test_unsafe_and_missing_paths_fail_in_both_modes(self):
        for compiled in (False,True):
            resources=Package("editor",compiled=compiled)
            for name in ("../outside.svg","/absolute.svg","C:/outside.svg","..\\outside.svg",":/other"):
                with self.subTest(compiled=compiled,name=name),self.assertRaises(ValueError):resources.Path(name)
            with self.assertRaises(FileNotFoundError):resources.ReadBytes("missing.svg")

    def test_custom_development_root_remains_supported(self):
        resources=ResourceManager(ROOT/"Editor/assets")
        self.assertIsInstance(resources.Resolve("locales/en.json"),Path)

    def test_production_audit_rejects_raw_assets_in_nested_versions(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            root=Path(folder)
            Audit(root)
            (root/"versions/v1/Editor/assets").mkdir(parents=True)
            with self.assertRaises(RuntimeError):Audit(root)
        with self.assertRaises(FileNotFoundError):Audit(ROOT/"build/nonexistent-distribution")


if __name__ == "__main__":unittest.main()
