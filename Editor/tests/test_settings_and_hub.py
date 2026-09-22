from __future__ import annotations

from pathlib import Path
import unittest

from bazzalt.settings import ReadProjectMetadata, SettingsStore, Version
from Launcher.catalog import HubCatalog


class SettingsAndHubTests(unittest.TestCase):
    def test_project_metadata_and_versions_are_compatible(self) -> None:
        path = Path(__file__).parent / "fixtures" / "Alpha.bproject"
        metadata = ReadProjectMetadata(path)
        self.assertEqual(metadata["format_version"], 1)
        self.assertEqual(metadata["properties"]["engine.version"], "1.0.0")
        self.assertLess(Version.Parse("1.0.0"), Version.Parse("1.1.0"))

    def test_settings_are_atomic_and_versioned(self) -> None:
        path = Path.cwd() / "build" / "editor-settings-test.json"
        path.parent.mkdir(exist_ok=True)
        store = SettingsStore("test", 2, lambda: {"schema_version": 2, "value": 0},
                              {1: lambda value: {**value, "migrated": True}})
        store.Path = path
        try:
            store.Save({"value": 7})
            self.assertEqual(store.Load()["schema_version"], 2)
            self.assertEqual(store.Load()["value"], 7)
        finally:
            path.unlink(missing_ok=True)

    def test_hub_filters_incompatible_editor_versions(self) -> None:
        catalog = HubCatalog.__new__(HubCatalog)
        catalog.Data = {"editors": [
            {"version": "1.0.0", "project_format_max": 1},
            {"version": "2.0.0", "project_format_max": 2},
        ]}
        compatible = catalog.CompatibleEditors({"format_version": 2})
        self.assertEqual([item["version"] for item in compatible], ["2.0.0"])


if __name__ == "__main__": unittest.main()
