#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
ENTT_DIR="$PROJECT_DIR/deps/entt"
ENTT_VERSION="v3.13.2"
ENTT_COMMIT="78213075654a688e9da6bc49f7f873d25c26d12c"

if [ -f "$ENTT_DIR/CMakeLists.txt" ]; then
    [ "$(git -C "$ENTT_DIR" rev-parse HEAD)" = "$ENTT_COMMIT" ] || {
        printf 'Error: existing EnTT checkout failed integrity check.\n' >&2; exit 1;
    }
    printf 'EnTT is already available at %s\n' "$ENTT_DIR"
    exit 0
fi

if [ -d "$ENTT_DIR" ]; then
    rmdir "$ENTT_DIR" 2>/dev/null || {
        printf 'Error: %s exists and is not an empty directory.\n' "$ENTT_DIR" >&2
        exit 1
    }
fi

git clone --branch "$ENTT_VERSION" --depth 1 \
    https://github.com/skypjack/entt.git "$ENTT_DIR"
[ "$(git -C "$ENTT_DIR" rev-parse HEAD)" = "$ENTT_COMMIT" ] || {
    printf 'Error: EnTT integrity check failed.\n' >&2
    exit 1
}
printf 'Installed EnTT %s at %s\n' "$ENTT_VERSION" "$ENTT_DIR"
