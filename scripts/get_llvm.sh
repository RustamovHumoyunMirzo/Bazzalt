#!/usr/bin/env sh
set -eu
VERSION="${1:-22.1.0}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="$ROOT/toolchain/llvm"
[ -x "$DEST/bin/clang++" ] && { echo "LLVM already installed: $DEST"; exit 0; }
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) PATTERN='clang+llvm-.*x86_64-linux-gnu-ubuntu-.*tar.xz' ;;
  Darwin-arm64) PATTERN='clang+llvm-.*arm64-apple-darwin.*tar.xz' ;;
  Darwin-x86_64) PATTERN='clang+llvm-.*x86_64-apple-darwin.*tar.xz' ;;
  *) echo "Unsupported host; provide toolchain/llvm manually" >&2; exit 1 ;;
esac
URL=$(curl -fsSL "https://api.github.com/repos/llvm/llvm-project/releases/tags/llvmorg-$VERSION" | sed -n 's/.*"browser_download_url": "\([^"]*\)".*/\1/p' | grep -E "$PATTERN" | head -n1)
[ -n "$URL" ] || { echo "Official LLVM archive not found" >&2; exit 1; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
curl -fL "$URL" -o "$TMP/llvm.tar.xz"; mkdir -p "$TMP/out"; tar -xf "$TMP/llvm.tar.xz" -C "$TMP/out"
mkdir -p "$(dirname "$DEST")"; mv "$TMP/out"/* "$DEST"; echo "Installed private LLVM toolchain: $DEST"
