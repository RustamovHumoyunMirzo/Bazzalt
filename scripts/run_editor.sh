#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project=${1:-"$root/Editor/tests/fixtures/Alpha.bproject"}
build="$root/build"

if [ ! -f "$project" ]; then
    echo "Project file was not found: $project" >&2
    exit 2
fi

runtime=$(find "$build/Editor" -maxdepth 2 -type f \( -name '_bazzalt_runtime*.so' -o -name '_bazzalt_runtime*.pyd' \) 2>/dev/null | head -n 1 || true)
if [ -z "$runtime" ]; then
    cmake -S "$root" -B "$build" -DBAZZALT_BUILD_EDITOR_BRIDGE=ON
    cmake --build "$build" --config Release --target _bazzalt_runtime
fi

PYTHONPATH="$root${PYTHONPATH:+:$PYTHONPATH}" python "$root/bazzalt_editor.py" --project "$project" --editor-version 1.0.0
