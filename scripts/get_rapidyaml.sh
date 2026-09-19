#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
RYML_DIR="$PROJECT_DIR/deps/rapidyaml"
RYML_VERSION="v0.16.0"

if [ -f "$RYML_DIR/CMakeLists.txt" ]; then
    printf 'rapidyaml is already available at %s\n' "$RYML_DIR"
    exit 0
fi
if [ -d "$RYML_DIR" ]; then
    rmdir "$RYML_DIR" 2>/dev/null || {
        printf 'Error: %s exists and is not empty.\n' "$RYML_DIR" >&2
        exit 1
    }
fi
git clone --branch "$RYML_VERSION" --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/biojppm/rapidyaml.git "$RYML_DIR"
printf 'Installed rapidyaml %s at %s\n' "$RYML_VERSION" "$RYML_DIR"
