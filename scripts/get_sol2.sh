#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ ! -d "$root/deps/sol2/.git" ]; then
    [ ! -e "$root/deps/sol2" ] || exit 1
    git clone --depth 1 --branch v3.3.1 https://github.com/ThePhD/sol2.git "$root/deps/sol2"
fi
[ "$(git -C "$root/deps/sol2" rev-parse HEAD)" = dca62a0f02bb45f3de296de3ce00b1275eb34c25 ] || { echo 'Sol2 pin mismatch' >&2; exit 1; }
