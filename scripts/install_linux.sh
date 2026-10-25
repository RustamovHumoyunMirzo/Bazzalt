#!/usr/bin/env sh
# Install an extracted standalone Hub for the current user, without root.
set -eu
SOURCE=${1:?Usage: install_linux.sh /path/to/extracted-hub}
TARGET="${XDG_DATA_HOME:-$HOME/.local/share}/BAZZALT/Hub"
[ -x "$SOURCE/BazzaltHub" ] || { echo 'Select an extracted Linux Hub package' >&2; exit 1; }
[ ! -e "$TARGET" ] || { echo 'Hub is already installed; close it and move the old directory before updating' >&2; exit 1; }
mkdir -p "$TARGET" "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
cp -R "$SOURCE/." "$TARGET/"
python3 - "$TARGET" "${XDG_DATA_HOME:-$HOME/.local/share}/applications/bazzalt-hub.desktop" <<'PY'
import sys
from pathlib import Path
root=Path(sys.argv[1]);escape=lambda s:str(s).replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$')
Path(sys.argv[2]).write_text('[Desktop Entry]\nType=Application\nName=Bazzalt Hub\nExec="'+escape(root/'BazzaltHub')+'"\nIcon='+str(root/'share/BazzaltLogo.svg')+'\nCategories=Development;IDE;\nTerminal=false\n',encoding='utf-8')
PY
printf 'Installed Hub: %s/BazzaltHub\n' "$TARGET"
