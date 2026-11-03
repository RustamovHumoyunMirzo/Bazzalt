#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=5.4.9
hash=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
if [ -f "$root/deps/lua/src/lua.h" ]; then
    [ "$(tr -d '\r\n' < "$root/deps/lua/.bazzalt-source-sha256")" = "$hash" ] || exit 1
    exit 0
fi
[ ! -e "$root/deps/lua" ] || { echo 'Lua destination exists' >&2; exit 1; }
mkdir -p "$root/deps"
downloaded=false
for host in https://www.lua.org/ftp https://www.tecgraf.puc-rio.br/lua/ftp https://lua.org/ftp; do
    if curl --fail --location --proto '=https' --retry 1 --retry-all-errors --connect-timeout 15 --max-time 90 "$host/lua-$version.tar.gz" -o "$root/deps/lua-source.tar.gz"; then
        downloaded=true
        break
    fi
done
[ "$downloaded" = true ] || { echo 'All pinned Lua download endpoints failed' >&2; exit 1; }
if command -v sha256sum >/dev/null 2>&1; then actual=$(sha256sum "$root/deps/lua-source.tar.gz" | cut -d ' ' -f 1); else actual=$(shasum -a 256 "$root/deps/lua-source.tar.gz" | cut -d ' ' -f 1); fi
[ "$actual" = "$hash" ] || { echo 'Lua checksum mismatch' >&2; exit 1; }
tar -xzf "$root/deps/lua-source.tar.gz" -C "$root/deps"
mv "$root/deps/lua-$version" "$root/deps/lua"
printf '%s\n' "$hash" > "$root/deps/lua/.bazzalt-source-sha256"
