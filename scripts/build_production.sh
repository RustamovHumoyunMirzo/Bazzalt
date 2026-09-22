#!/usr/bin/env sh
set -eu
VERSION="${1:-1.0.0}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build"
OUTPUT="$ROOT/dist/production"
VERSION_FOLDER="bazzalt_$(printf '%s' "$VERSION" | tr . _)"
export NUITKA_CACHE_DIR="$BUILD/NuitkaCache"

python -c 'import nuitka' >/dev/null 2>&1 || { echo 'Install build dependencies: python -m pip install -r requirements-build.txt' >&2; exit 1; }
cmake --build "$BUILD" --config Release --target _bazzalt_runtime
NATIVE=$(find "$BUILD" -type f \( -name '_bazzalt_runtime*.so' -o -name '_bazzalt_runtime*.pyd' \) | head -n 1)
[ -n "$NATIVE" ] || { echo 'Could not find _bazzalt_runtime' >&2; exit 1; }
mkdir -p "$OUTPUT"
python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 \
  --include-data-file="$ROOT/Launcher/index.html=Launcher/index.html" --output-filename=BazzaltHub \
  --output-dir="$OUTPUT" "$ROOT/bazzalt_hub.py"
python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 \
  --include-data-dir="$ROOT/Editor/assets=Editor/assets" --output-filename=Bazzalt \
  --output-dir="$OUTPUT" "$ROOT/bazzalt_editor.py"
HUB="$OUTPUT/bazzalt_hub.dist"
TARGET="$HUB/versions/$VERSION_FOLDER"
mkdir -p "$TARGET/Editor"
cp -R "$OUTPUT/bazzalt_editor.dist/." "$TARGET/"
cp "$NATIVE" "$TARGET/Editor/"
printf '{\n  "version": "%s",\n  "executable": "Bazzalt",\n  "project_format_max": 1\n}\n' "$VERSION" > "$TARGET/editor.json"
printf 'Production bundle: %s\n' "$HUB"
