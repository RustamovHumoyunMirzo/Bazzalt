"""Pinned Android renderer/crypto builds. Heavy compilation belongs on CI."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

FILAMENT_COMMIT = 'b01d8a12f283876b337d678e4c74081ae14e20da'
OPENSSL_COMMIT = 'c1eeb9406b6142148f267594197d853403d10208'


def Run(*command, **kwargs):
    subprocess.run([str(value) for value in command], check=True, **kwargs)


def Checkout(folder, repository, tag, revision):
    if not folder.exists(): Run('git', 'clone', '--depth', '1', '--branch', tag, repository, folder)
    actual = subprocess.check_output(['git', '-C', str(folder), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != revision: raise ValueError('Android dependency revision mismatch: ' + str(folder))


def Build(abi, ndk):
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise ValueError('Build Android dependencies on GitHub Actions and download the SDK artifact')
    root = Path(__file__).resolve().parents[1]
    ndk = Path(ndk).resolve()
    work = root / 'build/android-bootstrap'
    work.mkdir(parents=True, exist_ok=True)
    source = work / 'filament-source'
    Checkout(source, 'https://github.com/google/filament.git', 'v1.77.0', FILAMENT_COMMIT)
    host = work / 'host'
    exports = 'out/bazzalt-tools'
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DFILAMENT_SKIP_SAMPLES=ON', '-DFILAMENT_BUILD_FILAMAT=ON',
              '-DFILAMENT_ENABLE_RTTI=ON', '-DFILAMENT_ENABLE_EXCEPTIONS=ON', '-DFILAMENT_SUPPORTS_WEBGPU=OFF',
              '-DFILAMENT_BUILD_FILAMESHIO=ON', '-DFILAMENT_BUILD_TESTING=OFF', '-DBUILD_TESTING=OFF']
    Run('cmake', '-S', source, '-B', host, *common, '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
        '-DCMAKE_CXX_FLAGS=-stdlib=libc++', '-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++', '-DIMPORT_EXECUTABLES_DIR=' + exports)
    Run('cmake', '--build', host, '--parallel', '2', '--target', 'matc', 'cmgen', 'filamesh', 'mipgen', 'resgen', 'uberz', 'glslminifier')
    arch = 'aarch64' if abi == 'arm64-v8a' else 'x86_64'
    target = work / abi
    destination = root / 'deps/filament-android' / abi
    Run('cmake', '-S', source, '-B', target, *common,
        '-DCMAKE_TOOLCHAIN_FILE=' + str(ndk / 'build/cmake/android.toolchain.cmake'), '-DANDROID_ABI=' + abi,
        '-DANDROID_PLATFORM=android-26', '-DANDROID_STL=c++_shared', '-DFILAMENT_ANDROID_PLATFORM_API_LEVEL=26',
        '-DIMPORT_EXECUTABLES_DIR=' + exports, '-DDIST_DIR=' + arch, '-DDIST_ARCH=' + arch,
        '-DCMAKE_INSTALL_PREFIX=' + str(destination), '-DFILAMENT_SUPPORTS_VULKAN=ON', '-DFILAMENT_SUPPORTS_OPENGL=ON',
        '-DFILAMENT_SUPPORTS_METAL=OFF', '-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384')
    Run('cmake', '--build', target, '--parallel', '2')
    Run('cmake', '--install', target)
    # CMake consumes the same normalized include/lib layout on every platform.
    libraries = destination / 'lib' / arch
    libraries.mkdir(parents=True, exist_ok=True)
    for file in destination.rglob('*.a'):
        if file.parent != libraries: shutil.copy2(file, libraries / file.name)
    tools = destination / 'bin'
    tools.mkdir(exist_ok=True)
    for name in ('matc', 'cmgen', 'filamesh'):
        candidates = list(host.rglob(name))
        executable = next((p for p in candidates if p.is_file() and os.access(p, os.X_OK)), None)
        if executable is None: raise ValueError('Host renderer tool is missing: ' + name)
        shutil.copy2(executable, tools / name)
    openssl = work / 'openssl-source'
    Checkout(openssl, 'https://github.com/openssl/openssl.git', 'openssl-3.5.4', OPENSSL_COMMIT)
    crypto_build = work / ('openssl-' + abi)
    crypto_build.mkdir(exist_ok=True)
    crypto = root / 'deps/openssl-android' / abi
    environment = dict(os.environ, ANDROID_NDK_ROOT=str(ndk),
                       PATH=str(ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin') + os.pathsep + os.environ['PATH'])
    Run('perl', openssl / 'Configure', 'android-arm64' if abi == 'arm64-v8a' else 'android-x86_64',
        '-D__ANDROID_API__=26', 'no-shared', 'no-tests', 'no-apps', '-fPIC', '--prefix=' + str(crypto), cwd=crypto_build, env=environment)
    Run('make', '-j2', cwd=crypto_build, env=environment)
    Run('make', 'install_sw', cwd=crypto_build, env=environment)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--abi', choices=('arm64-v8a', 'x86_64'), required=True)
    parser.add_argument('--ndk', required=True)
    arguments = parser.parse_args()
    Build(arguments.abi, arguments.ndk)
