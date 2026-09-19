#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
ENTT_DIR="$PROJECT_DIR/deps/entt"
ENTT_VERSION="v3.13.2"

if [ -f "$ENTT_DIR/CMakeLists.txt" ]; then
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
printf 'Installed EnTT %s at %s\n' "$ENTT_VERSION" "$ENTT_DIR"
