"""Android SDK/NDK discovery and validation. Engine binaries come from the SDK artifact."""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess

NDK_VERSION = "29.0.14206865"
MIN_API = 26
ABIS = {"arm64": ("arm64-v8a", "aarch64-linux-android"), "x64": ("x86_64", "x86_64-linux-android")}


def Version(text):
    if not re.fullmatch(r"\d+(?:\.\d+){1,3}", text):
        raise ValueError("Invalid Android tool version")
    return tuple(map(int, text.split(".")))


def Properties(path):
    values = {}
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key.strip()] = value.strip()
    return values


def HostTag():
    return "windows-x86_64" if os.name == "nt" else "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"


def Discover(sdk="", ndk=""):
    home = Path.home()
    roots = [Path(sdk)] if sdk else []
    roots += [Path(os.environ[key]) for key in ("ANDROID_SDK_ROOT", "ANDROID_HOME") if os.environ.get(key)]
    roots += [home / "AppData/Local/Android/Sdk", home / "Library/Android/sdk", home / "Android/Sdk",
              Path("C:/Android/Sdk"), Path("C:/Android"), Path("D:/Android/Sdk"), Path("D:/Android")]
    adb = shutil.which("adb")
    if adb:
        roots.append(Path(adb).resolve().parent.parent)
    selected = next((root.resolve() for root in roots if (root / "platforms").is_dir() and
                     any((root / "platforms").glob("android-*/android.jar")) and (root / "build-tools").is_dir()), None)
    candidates = [Path(ndk)] if ndk else []
    candidates += [Path(os.environ[key]) for key in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT") if os.environ.get(key)]
    if selected:
        candidates += sorted((selected / "ndk").glob("*"), key=lambda p: tuple(int(n) for n in re.findall(r"\d+", p.name)), reverse=True)
        candidates.append(selected / "ndk-bundle")
    candidates += list(Path("C:/").glob("android-ndk-r*")) if os.name == "nt" else []
    compatible = None
    for candidate in candidates:
        try:
            revision = Properties(candidate / "source.properties")["Pkg.Revision"]
            if Version(revision) >= Version(NDK_VERSION) and (candidate / "toolchains/llvm/prebuilt" / HostTag()).is_dir():
                if compatible is None:
                    compatible = candidate.resolve()
                if Version(revision) == Version(NDK_VERSION):
                    compatible = candidate.resolve()
                    break
        except (OSError, KeyError, ValueError):
            continue
    return {"sdk_path": str(selected) if selected else "", "ndk_path": str(compatible) if compatible else ""}


@dataclass(frozen=True)
class AndroidTools:
    Sdk: Path
    Ndk: Path
    BuildTools: Path
    Jar: Path
    Java: Path
    Javac: Path
    Compiler: Path
    Sysroot: Path
    Triple: str
    Abi: str
    Revision: str


def Validate(profile, architecture, runtime=None):
    if architecture not in ABIS:
        raise ValueError("Android output supports ARM64 and x86_64")
    found = Discover(profile.get("sdk_path", ""), profile.get("ndk_path", ""))
    sdk_text = profile.get("sdk_path") or found["sdk_path"]
    ndk_text = profile.get("ndk_path") or found["ndk_path"]
    if not sdk_text or not ndk_text:
        raise ValueError("Select an installed Android SDK and compatible NDK in Build Settings")
    sdk, ndk = Path(sdk_text).resolve(), Path(ndk_text).resolve()
    minimum, target, compile_api = profile["min_sdk"], profile["target_sdk"], profile["compile_sdk"]
    if not MIN_API <= minimum <= target <= compile_api <= 36:
        raise ValueError("Android requires 26 <= minimum SDK <= target SDK <= compile SDK <= 36")
    jar = sdk / "platforms" / f"android-{compile_api}" / "android.jar"
    revision = Properties(ndk / "source.properties").get("Pkg.Revision", "")
    if Version(revision) < Version(profile["minimum_ndk"]):
        raise ValueError("Selected NDK is older than the configured minimum NDK")
    limits = json.loads((ndk / "meta/platforms.json").read_text(encoding="utf-8"))
    if not limits["min"] <= minimum <= limits["max"]:
        raise ValueError("The selected NDK does not support the minimum Android API")
    build_tools = sdk / "build-tools" / profile["build_tools"]
    suffix = ".exe" if os.name == "nt" else ""
    for file in (jar, build_tools / ("aapt2" + suffix), build_tools / ("zipalign" + suffix),
                 build_tools / "lib/apksigner.jar", build_tools / "lib/d8.jar"):
        if not file.is_file():
            raise ValueError("Android SDK component is missing: " + str(file))
    java_home = Path(os.environ["JAVA_HOME"]) if os.environ.get("JAVA_HOME") else None
    java = java_home / ("bin/java" + suffix) if java_home else Path(shutil.which("java") or "")
    javac = java_home / ("bin/javac" + suffix) if java_home else Path(shutil.which("javac") or "")
    if not java.is_file() or not javac.is_file():
        raise ValueError("Install a JDK and set JAVA_HOME before building Android output")
    version = subprocess.run([str(java), "-version"],capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=15,
                             creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
    match=re.search(r'version "(\d+)',version.stderr+version.stdout)
    if version.returncode or not match or int(match[1])<17:
        raise ValueError("Android packaging requires JDK 17 or later")
    llvm = ndk / "toolchains/llvm/prebuilt" / HostTag()
    compiler = llvm / ("bin/clang++" + suffix)
    if not compiler.is_file():
        raise ValueError("The NDK does not contain a compiler for this host")
    if runtime is not None:
        manifest = json.loads((Path(runtime) / "runtime.json").read_text(encoding="utf-8"))
        if manifest.get("android_ndk") != revision or manifest.get("minimum_api", 999) > minimum:
            raise ValueError(f"The Android runtime SDK requires NDK {manifest.get('android_ndk', 'unknown')} and API {manifest.get('minimum_api', 'unknown')} or later. Selected NDK {revision}, API {minimum}")
    abi, triple = ABIS[architecture]
    return AndroidTools(sdk, ndk, build_tools, jar, java, javac, compiler, llvm / "sysroot", triple, abi, revision)
