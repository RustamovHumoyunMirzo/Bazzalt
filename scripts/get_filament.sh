#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
FILAMENT_DIR="$PROJECT_DIR/deps/filament"
VERSION="v1.77.0"

if [ -f "$FILAMENT_DIR/include/filament/Engine.h" ]; then
    printf 'Filament is already available at %s\n' "$FILAMENT_DIR"
    exit 0
fi

case "$(uname -s)" in
    Linux*) PACKAGE="linux" ;;
    Darwin*) PACKAGE="mac" ;;
    *) printf 'Unsupported host platform. Use the platform package from Filament releases.\n' >&2; exit 1 ;;
esac

if [ -d "$FILAMENT_DIR" ]; then
    rmdir "$FILAMENT_DIR" 2>/dev/null || {
        printf 'Error: %s exists and is not empty.\n' "$FILAMENT_DIR" >&2; exit 1;
    }
fi
mkdir -p "$FILAMENT_DIR"
ARCHIVE="$PROJECT_DIR/deps/filament-$VERSION-$PACKAGE.tgz"
trap 'rm -f "$ARCHIVE"' EXIT
curl -fL "https://github.com/google/filament/releases/download/$VERSION/filament-$VERSION-$PACKAGE.tgz" -o "$ARCHIVE"
tar -xzf "$ARCHIVE" -C "$FILAMENT_DIR"
test -f "$FILAMENT_DIR/include/filament/Engine.h" || {
    printf 'Downloaded Filament package has an unexpected layout.\n' >&2; exit 1;
}
printf 'Installed Filament %s at %s\n' "$VERSION" "$FILAMENT_DIR"
