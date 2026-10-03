#!/usr/bin/env sh
set -eu
project_directory=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dependency_directory="$project_directory/deps/SDL3"
version=release-3.2.30
commit=f5e5f6588921eed3d7d048ce43d9eb1ff0da0ffc
if [ ! -f "$dependency_directory/CMakeLists.txt" ]; then
    if [ -e "$dependency_directory" ]; then
        echo "SDL3 directory exists but is incomplete; refusing to overwrite it." >&2
        exit 1
    fi
    git clone --branch "$version" --depth 1 https://github.com/libsdl-org/SDL.git "$dependency_directory"
fi
test "$(git -C "$dependency_directory" rev-parse HEAD)" = "$commit" || { echo "SDL3 integrity check failed." >&2; exit 1; }
echo "SDL3 $version is ready at $dependency_directory"
