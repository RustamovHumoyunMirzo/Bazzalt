from pathlib import Path
import csv
import io
import json
import re
import tempfile
import unittest
import zipfile
import shutil
import subprocess

from scripts.package_pyside_win32 import Wheel
from Launcher.tests.test_downloads import PE
from scripts.patch_filament_win32 import Patch, OLD, NEW


class Win32PackagingTests(unittest.TestCase):
    def setUp(self):
        base=Path(__file__).resolve().parents[2]/"build";base.mkdir(exist_ok=True)
        self.Temp=tempfile.TemporaryDirectory(dir=base);self.addCleanup(self.Temp.cleanup);self.Root=Path(self.Temp.name)
        self.Package=self.Root/"shiboken6";self.Package.mkdir();(self.Package/"__init__.py").write_text("# unit test fixture\n")

    def test_wheel_has_win32_tag_metadata_and_complete_record(self):
        (self.Package/"Shiboken.pyd").write_bytes(PE(0x14c))
        wheel=Wheel(self.Package,self.Root/"wheels","6.10.2")
        with zipfile.ZipFile(wheel) as archive:
            names=archive.namelist();dist="shiboken6-6.10.2.dist-info/"
            self.assertIn("Tag: cp313-cp313-win32",archive.read(dist+"WHEEL").decode())
            self.assertIn("Version: 6.10.2",archive.read(dist+"METADATA").decode())
            rows=list(csv.reader(io.StringIO(archive.read(dist+"RECORD").decode())))
            self.assertEqual(set(names),{row[0] for row in rows})
            self.assertTrue(all(row[1].startswith("sha256=") for row in rows if row[0] != dist+"RECORD"))

    def test_wheel_rejects_any_x64_binary(self):
        (self.Package/"Shiboken.pyd").write_bytes(PE(0x8664))
        with self.assertRaises(ValueError):Wheel(self.Package,self.Root/"wheels","6.10.2")
        self.assertFalse((self.Root/"wheels").exists())

    def test_dependency_pins_are_consistent(self):
        root=Path(__file__).resolve().parents[2]
        pins=json.loads((root/"releases/windows-x86.json").read_text());versions=json.loads((root/"releases/versions.json").read_text())
        self.assertEqual(pins["qt_version"],versions["pyside"])
        for name in ("qtbase_commit","qtsvg_commit","pyside_commit","filament_commit","llvm_commit"):
            self.assertRegex(pins[name],r"^[a-f0-9]{40}$")
        self.assertRegex(pins["python_package_sha256"],r"^[a-f0-9]{64}$")

    def test_qt_source_builds_disable_unused_sql_module(self):
        root=Path(__file__).resolve().parents[2]
        script=(root/"scripts/build_windows_x86_dependencies.ps1").read_text()
        configurations=[line for line in script.splitlines() if line.startswith("Build $QtBase ")]
        self.assertEqual(len(configurations),2)
        for configuration in configurations:
            self.assertIn("'-DFEATURE_sql=OFF'",configuration)
        self.assertNotIn(";Sql;",script)

    def test_ci_splits_dependencies_from_product_build_and_caches_installs(self):
        root=Path(__file__).resolve().parents[2]
        workflow=(root/".github/workflows/windows-product.yml").read_text()
        matches=list(re.finditer(r"(?m)^  ([a-z0-9-]+):\s*$",workflow))
        jobs={match[1]:workflow[match.end():matches[index+1].start() if index+1<len(matches) else len(workflow)] for index,match in enumerate(matches)}
        for name,stage in (("x86-qt","qt"),("x86-gui","gui"),("x86-filament","filament")):
            self.assertIn(f"-Stage {stage} -Parallel 2",jobs[name])
            self.assertIn("actions/cache@",jobs[name])
            self.assertIn("steps.toolchain.outputs.version",jobs[name])
            self.assertIn("hashFiles(",jobs[name])
            self.assertIn("cache-hit != 'true'",jobs[name])
            self.assertNotIn("restore-keys",jobs[name]) # Do not restore mismatched binaries.
            self.assertIn("retention-days: 1",jobs[name])
        self.assertIn("needs: x86-qt",jobs["x86-gui"])
        self.assertNotIn("needs:",jobs["x86-filament"])
        product=jobs["x86"]
        self.assertIn("needs: [x86-qt, x86-gui, x86-filament]",product)
        self.assertIn("always() && !cancelled()",product)
        self.assertIn("inputs.product == 'core' || needs.x86-gui.result == 'success'",product)
        self.assertIn("inputs.product == 'hub' || needs.x86-filament.result == 'success'",product)
        self.assertNotIn("build_windows_x86_dependencies.ps1",product)
        self.assertIn("timeout-minutes: 120",product)
        self.assertIn("dependency-python-x86-${{ inputs.product }}",product)

    def test_split_gui_build_reuses_completed_qt_and_svg_installations(self):
        root=Path(__file__).resolve().parents[2]
        script=(root/"scripts/build_windows_x86_dependencies.ps1").read_text()
        self.assertLess(script.index("Build $Svg "),script.index("if ($Stage -eq 'qt')"))
        self.assertIn("Reusing verified completed installation",script)
        self.assertLess(script.index("Checked cmake @('--install'"),script.index("[IO.File]::WriteAllText($Stamp"))
        self.assertIn("Large dependency builds belong in GitHub Actions",script)

    def test_filament_handle_patch_is_exact_idempotent_and_part_of_cache(self):
        source=self.Root/"filament/backend/src/vulkan/VulkanAsyncHandles.cpp";source.parent.mkdir(parents=True)
        helper=source.parent/"utils/Helper.h";helper.parent.mkdir()
        helper.write_text("VkResult (*first)\nVkResult (*second)\nVkResult (*third)\n",encoding="utf-8")
        source.write_text("before\n"+OLD+"\nafter\n",encoding="utf-8")
        self.assertTrue(Patch(self.Root));self.assertIn(NEW,source.read_text());self.assertFalse(Patch(self.Root))
        self.assertEqual(helper.read_text().count("VkResult (VKAPI_PTR *"),3)
        source.write_text("upstream changed",encoding="utf-8")
        with self.assertRaises(ValueError):Patch(self.Root)
        root=Path(__file__).resolve().parents[2];script=(root/"scripts/build_windows_x86_dependencies.ps1").read_text()
        self.assertIn("patch_filament_win32.py",script);self.assertIn("$SourceDiff",script)
        self.assertIn("scripts/patch_filament_win32.py",(root/".github/workflows/windows-product.yml").read_text())

    def test_enumerate_calling_convention_compiles_for_win32_vulkan_functions(self):
        root=Path(__file__).resolve().parents[2];powershell=shutil.which("pwsh")
        if not powershell:self.skipTest("MSVC environment unavailable")
        source=self.Root/"enumerate.cpp"
        source.write_text("""
#define VKAPI_PTR __stdcall
using uint32_t=unsigned int;
enum VkResult { Success };
template<class Out> void enumerate(VkResult (VKAPI_PTR *fn)(uint32_t*,Out*)) {}
template<class In,class Out> void enumerate(VkResult (VKAPI_PTR *fn)(In,uint32_t*,Out*),In) {}
template<class A,class B,class Out> void enumerate(VkResult (VKAPI_PTR *fn)(A,B,uint32_t*,Out*),A,B) {}
VkResult VKAPI_PTR first(uint32_t*,int*);
VkResult VKAPI_PTR second(int,uint32_t*,int*);
VkResult VKAPI_PTR third(int,int,uint32_t*,int*);
void test(){enumerate(first);enumerate(second,1);enumerate(third,1,2);}
""",encoding="utf-8")
        toolchain=str(root/"scripts/windows_toolchain.ps1").replace("'","''");filename=str(source).replace("'","''")
        for arch in ("x86","x64"):
            result=subprocess.run([powershell,"-NoProfile","-Command",f". '{toolchain}'; Import-BazzaltMsvc {arch}; & cl /nologo /Zs /std:c++17 '{filename}'; exit $LASTEXITCODE"],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_patched_vulkan_handle_conversion_compiles_for_x86_and_x64(self):
        root=Path(__file__).resolve().parents[2];local=root/"toolchain/llvm/bin/clang++.exe"
        compiler=str(local) if local.exists() else shutil.which("clang++")
        powershell=shutil.which("pwsh") if not compiler else None
        if not compiler and not powershell:self.skipTest("No compiler available for small syntax checks")
        for target,pointers in (("i686-pc-windows-msvc",0),("x86_64-pc-windows-msvc",1)):
            with self.subTest(target=target):
                source=self.Root/"handles.cpp"
                source.write_text(f"#define VK_USE_64_BIT_PTR_DEFINES {pointers}\n"+"""
using uint64_t=unsigned long long;
#if VK_USE_64_BIT_PTR_DEFINES
using VkShaderModule=struct ShaderModule*;
#else
using VkShaderModule=uint64_t;
#endif
enum VkObjectType {VK_OBJECT_TYPE_SHADER_MODULE};
struct DebugUtils {void setName(VkObjectType,uint64_t,char const*) const;};
struct Context {DebugUtils const& getDebugUtils() const;};
struct Name {char const* c_str() const;};
void test(Context const& context,VkShaderModule module,Name const& name) {
"""+NEW+"\n}\n",encoding="utf-8")
                if compiler:
                    command=[compiler,"--target="+target,"-std=c++17","-fsyntax-only",str(source)]
                else:
                    toolchain=str(root/"scripts/windows_toolchain.ps1").replace("'","''");filename=str(source).replace("'","''")
                    arch="x64" if pointers else "x86"
                    command=[powershell,"-NoProfile","-Command",f". '{toolchain}'; Import-BazzaltMsvc {arch}; & cl /nologo /Zs /std:c++17 '{filename}'; exit $LASTEXITCODE"]
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_pinned_llvm_download_does_not_require_release_api(self):
        root=Path(__file__).resolve().parents[2];script=(root/"scripts/get_llvm.ps1").read_text()
        pinned=script.split("if ($Sha256) {",1)[1].split("} else {",1)[0]
        self.assertIn("releases/download/llvmorg-$Version/clang+llvm-$Version",pinned)
        self.assertNotIn("Invoke-RestMethod",pinned)
        self.assertIn("Invoke-WebRequest $DownloadUrl",script)
        self.assertIn("LLVM archive SHA-256 mismatch",script)
        self.assertIn('$Headers.Authorization="Bearer $Token"',script)


if __name__=="__main__":unittest.main()
