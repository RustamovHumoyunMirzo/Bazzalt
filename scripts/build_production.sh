#!/usr/bin/env sh
set -eu
VERSION="${1:-0.5.0}"
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
python "$ROOT/scripts/compile_resources.py" --build-directory "$BUILD/CompiledResources"
python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 \
  --nofollow-import-to=PySide6.QtWebEngineCore,PySide6.QtWebEngineWidgets,PySide6.QtWebChannel \
  --include-module=bazzalt._hub_resources_rc --include-module=bazzalt._branding_resources_rc --output-filename=BazzaltHub \
  --output-dir="$OUTPUT" "$ROOT/bazzalt_hub.py"
python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 \
  --include-module=bazzalt._editor_resources_rc --include-module=bazzalt._branding_resources_rc --output-filename=Bazzalt \
  --output-dir="$OUTPUT" "$ROOT/bazzalt_editor.py"
HUB="$OUTPUT/bazzalt_hub.dist"
python "$ROOT/scripts/compile_resources.py" --audit "$HUB"
python "$ROOT/scripts/compile_resources.py" --audit "$OUTPUT/bazzalt_editor.dist"
TARGET="$OUTPUT/bazzalt_editor.dist"
mkdir -p "$TARGET/Editor"
cp "$NATIVE" "$TARGET/Editor/"
cp -R "$BUILD/ScriptSDK" "$TARGET/"
for LIBRARY in "$BUILD/Editor/libBazzalt.so" "$BUILD/Editor/libBazzalt.dylib"; do
  if [ -f "$LIBRARY" ]; then cp "$LIBRARY" "$TARGET/Editor/"; fi
done
for LIBRARY in "$BUILD/Editor/libbshad.so" "$BUILD/Editor/libbshad.dylib"; do
  if [ -f "$LIBRARY" ]; then cp "$LIBRARY" "$TARGET/Editor/"; fi
done
printf '{\n  "version": "%s",\n  "executable": "Bazzalt",\n  "project_format_max": 1\n}\n' "$VERSION" > "$TARGET/editor.json"
python "$ROOT/scripts/compile_resources.py" --audit "$HUB"
printf 'Independent Hub: %s\nManaged Editor: %s\n' "$HUB" "$TARGET"
