#!/usr/bin/env sh
set -eu
destination="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)/deps/pybind11}"
if [ -f "$destination/CMakeLists.txt" ]; then printf '%s\n' "pybind11 already exists at $destination"; exit 0; fi
git clone --depth 1 --branch v3.0.1 https://github.com/pybind/pybind11.git "$destination"
