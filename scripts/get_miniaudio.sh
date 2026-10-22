#!/usr/bin/env sh
set -eu
project_directory=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dependency_directory="$project_directory/deps/miniaudio"
if [ ! -f "$dependency_directory/miniaudio.h" ]; then
    test ! -e "$dependency_directory" || { echo 'Incomplete miniaudio directory.' >&2; exit 1; }
    git clone --branch 0.11.23 --depth 1 https://github.com/mackron/miniaudio.git "$dependency_directory"
fi
test "$(git -C "$dependency_directory" rev-parse HEAD)" = f40cf03f80cdb7e741d43e53b7e706e8c1394bcf || { echo 'miniaudio integrity check failed.' >&2; exit 1; }
echo 'miniaudio 0.11.23 is ready.'
