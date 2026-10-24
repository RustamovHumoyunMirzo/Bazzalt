#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dependency="$root/deps/freetype"
if [ ! -f "$dependency/CMakeLists.txt" ]; then
    test ! -e "$dependency" || { echo 'Incomplete FreeType directory.' >&2; exit 1; }
    git clone --depth 1 --branch VER-2-14-3 https://github.com/freetype/freetype.git "$dependency"
fi
test "$(git -C "$dependency" rev-parse HEAD)" = 0a0221a1347e2f1e07c395263540026e9a0aa7c7 || { echo 'FreeType integrity check failed.' >&2; exit 1; }
echo 'FreeType 2.14.3 source is ready.'
