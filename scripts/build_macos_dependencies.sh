#!/usr/bin/env sh
# CI-only native SDK: stock archives omit Intel and disable RTTI, which can poison Darwin exception matching.
set -eu
[ "${GITHUB_ACTIONS:-}" = true ] || { echo 'Build renderer dependencies on GitHub Actions.' >&2; exit 1; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ARCH=${BAZZALT_MAC_ARCH:-$(uname -m)}
[ "$ARCH" = x86_64 ] || [ "$ARCH" = arm64 ] || exit 1
[ ! -f "$ROOT/deps/filament/lib/$ARCH/libgltfio.a" ] || [ ! -f "$ROOT/deps/filament/bazzalt-rtti-$ARCH" ] || exit 0
WORK="$ROOT/build/macos-bootstrap-$ARCH"
SOURCE="$WORK/filament-source"
mkdir -p "$WORK"
if [ ! -d "$SOURCE" ]; then git clone --depth 1 --branch v1.77.0 https://github.com/google/filament.git "$SOURCE"; fi
[ "$(git -C "$SOURCE" rev-parse HEAD)" = b01d8a12f283876b337d678e4c74081ae14e20da ] || { echo 'Filament source integrity mismatch' >&2; exit 1; }
cmake -S "$SOURCE" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$ROOT/deps/filament" -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DDIST_ARCH="$ARCH" -DDIST_DIR="$ARCH" -DFILAMENT_SKIP_SAMPLES=ON -DFILAMENT_SKIP_SDL2=ON \
  -DFILAMENT_BUILD_TESTING=OFF -DSPIRV_SKIP_TESTS=ON -DFILAMENT_BUILD_FILAMAT=ON -DFILAMENT_ENABLE_RTTI=ON -DFILAMENT_ENABLE_EXCEPTIONS=ON \
  -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=OFF -DFILAMENT_SUPPORTS_WEBGPU=OFF
cmake --build "$WORK/build" --target install --parallel "${BAZZALT_BUILD_JOBS:-3}"
test -f "$ROOT/deps/filament/lib/$ARCH/libgltfio.a"

printf "%s\n" "b01d8a12f283876b337d678e4c74081ae14e20da" > "$ROOT/deps/filament/bazzalt-rtti-$ARCH"
