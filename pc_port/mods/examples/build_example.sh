#!/bin/sh
# Builds the sample plugin into a ready-to-copy mod folder.
#   pc_port/mods/examples/build_example.sh [OUT_MODS_DIR]   (default: mods)
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-mods}
mkdir -p "$out/hello"
cp "$root/pc_port/mods/examples/hello/mod.ini" \
   "$root/pc_port/mods/examples/hello/cheats.ini" "$out/hello/"
${CC:-cc} -std=c99 -Wall -Wextra -shared -fPIC -I "$root/pc_port/platform" \
    "$root/pc_port/mods/examples/hello/hello_plugin.c" \
    -o "$out/hello/hello_plugin.so"
echo "built $out/hello"
