#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fetch() {
    directory="$root/deps/$1"
    if [ ! -e "$directory" ]; then git clone --depth 1 --branch "$2" "$4" "$directory"; fi
    [ "$(git -C "$directory" rev-parse HEAD)" = "$3" ] || { echo "Dependency integrity check failed: $1" >&2; exit 1; }
}
fetch harfbuzz 11.5.0 bf8929fbfb623703cf1522b372cab80002c17c95 https://github.com/harfbuzz/harfbuzz.git
fetch sheenbidi v3.0.0 cfe430e7375a7845b679adae9d51dac6deaa8858 https://github.com/Tehreer/SheenBidi.git
