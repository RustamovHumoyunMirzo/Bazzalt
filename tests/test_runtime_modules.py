"""Exercise module provider seams without downloading engine dependencies."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from bazzalt.runtime_libraries import LibraryNames

ROOT = Path(__file__).resolve().parents[1]


class RuntimeLibraryTests(unittest.TestCase):
    def test_inventory_includes_new_modules_and_deduplicates_aliases(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "runtime-libraries.json"
            path.write_text(json.dumps(dict(format_version=1, platform="linux",
                libraries=["libBazzalt.so", "libstudio.so", "libstudio.so"])))
            self.assertEqual(LibraryNames(folder, "linux"), ["libBazzalt.so", "libstudio.so"])

    def test_old_sdk_fallback_and_lua_aliases(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "libbazzalt_lua.so.5.4").touch()
            self.assertIn("libbazzalt_lua.so.5.4", LibraryNames(folder, "linux"))
            self.assertIn("bazzalt_ai.dll", LibraryNames(folder, "windows"))
            self.assertIn("libbazzalt_physics.dylib", LibraryNames(folder, "macos"))

    def test_inventory_rejects_bad_versions_platforms_and_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "runtime-libraries.json"
            for updates in (dict(format_version=2), dict(platform="linux"),
                            dict(libraries=[]), dict(libraries=["../escape.dll"]),
                            dict(libraries=["C:\\escape.dll"]), dict(libraries=[False]),
                            dict(libraries=["libwrong.so"])):
                data = dict(format_version=1, platform="windows", libraries=["Bazzalt.dll"])
                data.update(updates)
                path.write_text(json.dumps(data))
                with self.subTest(updates=updates), self.assertRaises(ValueError):
                    LibraryNames(folder, "windows")
            path.write_text("[]")
            with self.assertRaises(ValueError):
                LibraryNames(folder, "windows")


@unittest.skipUnless(shutil.which("cmake"), "CMake is needed for provider configuration checks")
class RuntimeProviderTests(unittest.TestCase):
    def Configure(self, folder, body):
        folder = Path(folder)
        (folder / "module.cpp").write_text("int Example() { return 1; }\n")
        (folder / "original.cpp").write_text("#error The replaced implementation must stay excluded\n")
        (folder / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(ProviderTest LANGUAGES CXX)\n'
            f'include("{ROOT.as_posix()}/cmake/RuntimeModules.cmake")\n' + body)
        return subprocess.run(["cmake", "-S", str(folder), "-B", str(folder / "build")],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")

    def test_replacement_owns_sources_preserves_name_and_stages_sdk(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "provider.cmake").write_text(
                'bazzalt_add_runtime_module(${BAZZALT_MODULE_TARGET} '
                'OUTPUT_NAME ${BAZZALT_MODULE_OUTPUT_NAME} SOURCES module.cpp)\n')
            result = self.Configure(path, '''
set(BAZZALT_TEST_PROVIDER "${PROJECT_SOURCE_DIR}/provider.cmake" CACHE FILEPATH "")
bazzalt_load_runtime_module(TEST TestModule studio "missing-default.cmake" original.cpp)
get_property(excluded GLOBAL PROPERTY BAZZALT_RUNTIME_EXCLUDED_SOURCES)
get_property(owned GLOBAL PROPERTY BAZZALT_RUNTIME_IMPLEMENTATION_SOURCES)
get_filename_component(expected_original "${PROJECT_SOURCE_DIR}/original.cpp" REALPATH)
get_filename_component(expected_implementation "${PROJECT_SOURCE_DIR}/module.cpp" REALPATH)
if(NOT expected_original IN_LIST excluded OR NOT expected_implementation IN_LIST owned)
    message(FATAL_ERROR "Provider source ownership was lost: excluded=${excluded}, owned=${owned}, expected=${expected_original},${expected_implementation}")
endif()
foreach(core Bazzalt BazzaltCrypto BazzaltLua)
    add_library(${core} SHARED module.cpp)
endforeach()
bazzalt_finalize_runtime()
''')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            built = subprocess.run(["cmake", "--build", str(path / "build"), "--config", "Release",
                                    "--target", "TestModule"],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace")
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            self.assertTrue(list((path / "build/ScriptSDK/lib").glob("*studio*")))
            platform = "windows" if sys.platform == "win32" else "macos" if sys.platform == "darwin" else "linux"
            inventory = next((path / "build").rglob("runtime-libraries.json"))
            self.assertTrue(any("studio" in name for name in LibraryNames(inventory.parent, platform)))

    def test_conflicting_ownership_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.Configure(folder, '''
bazzalt_add_runtime_module(First OUTPUT_NAME first SOURCES module.cpp)
bazzalt_add_runtime_module(Second OUTPUT_NAME second SOURCES module.cpp)
''')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("multiple owners", result.stdout + result.stderr)

    def test_provider_must_preserve_runtime_filename(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "provider.cmake").write_text(
                'bazzalt_add_runtime_module(TestModule OUTPUT_NAME wrong SOURCES module.cpp)\n')
            result = self.Configure(path, '''
bazzalt_load_runtime_module(TEST TestModule expected "${PROJECT_SOURCE_DIR}/provider.cmake" module.cpp)
''')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must preserve runtime name", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
