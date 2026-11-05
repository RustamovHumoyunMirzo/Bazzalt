"""Run real Cocoa/Metal checks in isolated, bounded processes with hang diagnostics."""
import os
from pathlib import Path
import signal
import subprocess
import sys


CASES = (
    "Editor.tests.test_macos_native.MacNativeTests.test_native_menu_without_duplicate_client_strip",
    "Editor.tests.test_macos_native.MacNativeTests.test_metal_viewport_attach_resize_and_release",
    "Compiler.tests.test_macos_build",
    "Editor.tests.test_shader_graph.ShaderGraphWidgetTests.test_actual_offscreen_material_sphere",
)


def Run(command, timeout=240, diagnostic=None):
    process = subprocess.Popen(command, start_new_session=os.name != "nt")
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"::error::Native check timed out after {timeout}s: {command}", flush=True)
        try:
            if sys.platform == "darwin" and diagnostic is not None:
                subprocess.run(["/usr/bin/sample", str(process.pid), "5", "-file", str(diagnostic)],
                               timeout=15, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"Could not collect native hang sample: {error}", flush=True)
        finally:
            # Kill this check's process group, including any packaged game it spawned.
            # Never leave a timed-out native process running into the next check.
            if os.name == "nt":
                process.kill()
            else:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait()
        return 124


def main():
    Path("build").mkdir(exist_ok=True)
    status = 0
    for index, case in enumerate(CASES):
        print(f"Native check: {case}", flush=True)
        result = Run([sys.executable, "-X", "faulthandler", "-u", "-m", "unittest", case, "-v"],
                     diagnostic=Path(f"build/macos-gpu-hang-{index}.log"))
        if result and not status:
            status = result
    return status


if __name__ == "__main__":
    sys.exit(main())
