import tempfile,unittest
from pathlib import Path
from scripts.patch_sol2_clang18 import Patch as PatchSol,OLD as SOL_OLD,NEW as SOL_NEW
from scripts.patch_filament_linux_x86 import Patch as PatchBlue,Symbols

class DependencyCompatibilityTests(unittest.TestCase):
    def test_sol_patch_is_idempotent_and_rejects_partially_patched_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);header=root/'include/sol/function_types_stateless.hpp';header.parent.mkdir(parents=True)
            header.write_text(SOL_OLD+'\n'+SOL_OLD)
            self.assertTrue(PatchSol(root));stamp=header.stat().st_mtime_ns
            self.assertFalse(PatchSol(root));self.assertEqual(header.stat().st_mtime_ns,stamp)
            partial=SOL_OLD+'\n'+SOL_NEW;header.write_text(partial)
            with self.assertRaises(ValueError):PatchSol(root)
            self.assertEqual(header.read_text(),partial)

    def test_bluegl_preserves_all_entrypoints_and_other_architectures(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'libs/bluegl/src';source.mkdir(parents=True)
            universal=source/'BlueGLCoreLinuxUniversalImpl.S';old='#if defined(__aarch64__)\n#include "arm.S"\n#else\n#include "BlueGLCoreLinuxImpl.S"\n#endif\n';universal.write_text(old)
            names=['glProbe'+str(i) for i in range(2575)];upstream='/* upstream license */\n'+'\n'.join('bluegl_'+name+':' for name in names)
            assembly=source/'BlueGLCoreLinuxImpl.S';assembly.write_text(upstream)
            self.assertTrue(PatchBlue(root));generated=source/'BlueGLCoreLinuxI386Impl.S';stamp=generated.stat().st_mtime_ns
            self.assertEqual(Symbols(generated.read_text()),names);self.assertEqual(assembly.read_text(),upstream)
            self.assertIn('#elif defined(__aarch64__)',universal.read_text());self.assertIn('#include "arm.S"',universal.read_text())
            self.assertFalse(PatchBlue(root));self.assertEqual(generated.stat().st_mtime_ns,stamp)

    def test_bluegl_unknown_upstream_fails_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'libs/bluegl/src';source.mkdir(parents=True)
            universal=source/'BlueGLCoreLinuxUniversalImpl.S';old='#if defined(__aarch64__)\n#endif';universal.write_text(old)
            (source/'BlueGLCoreLinuxImpl.S').write_text('/* unknown version */\nbluegl_glProbe:')
            with self.assertRaises(ValueError):PatchBlue(root)
            self.assertEqual(universal.read_text(),old);self.assertFalse((source/'BlueGLCoreLinuxI386Impl.S').exists())
