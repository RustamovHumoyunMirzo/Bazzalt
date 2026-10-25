#!/usr/bin/env sh
# Only CI builds the large renderer dependency. Never run implicitly in editor.
set -eu
[ "${GITHUB_ACTIONS:-}" = true ] || { echo 'Run the Linux x86 dependency workflow on GitHub Actions.' >&2; exit 1; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK="$ROOT/build/linux32-bootstrap"
SOURCE="$WORK/filament-source"
PIN=b01d8a12f283876b337d678e4c74081ae14e20da
mkdir -p "$WORK"
if [ ! -d "$SOURCE" ]; then git clone --depth 1 --branch v1.77.0 https://github.com/google/filament.git "$SOURCE"; fi
[ "$(git -C "$SOURCE" rev-parse HEAD)" = "$PIN" ] || { echo 'Filament source integrity mismatch' >&2; exit 1; }
python3 "$ROOT/scripts/patch_filament_win32.py" "$SOURCE"
# Linux x86 uses the distribution's multilib libstdc++; the official x64 SDK uses libc++.
# Build native 32-bit tools too: the x64 Linux CI host supports IA32 executables.
CC=clang CXX=clang++ cmake -S "$SOURCE" -B "$WORK/filament-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$WORK/filament-x86" \
  -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 \
  -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_SHARED_LINKER_FLAGS=-m32 \
  -DCMAKE_LIBRARY_PATH=/usr/lib/i386-linux-gnu \
  -DDIST_ARCH=x86 -DDIST_DIR=x86 -DUSE_STATIC_LIBCXX=OFF \
  -DFILAMENT_SKIP_SAMPLES=ON -DFILAMENT_SKIP_SDL2=ON \
  -DFILAMENT_BUILD_TESTING=OFF -DSPIRV_SKIP_TESTS=ON \
  -DFILAMENT_BUILD_FILAMAT=ON -DFILAMENT_SUPPORTS_VULKAN=ON \
  -DFILAMENT_SUPPORTS_WEBGPU=OFF
cmake --build "$WORK/filament-build" --target install --parallel "${BAZZALT_BUILD_JOBS:-2}"
test -f "$WORK/filament-x86/lib/x86/libfilament.a"
