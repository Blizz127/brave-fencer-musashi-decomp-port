#!/bin/sh
# Package the native port for Linux without any game data.
#
# Stages the port binary, the open-source libraries you pass with --lib,
# license texts, docs and the sample mods, then runs tools/retail_guard.py
# over the staging tree. The tarball is written only if the guard passes.
# The game itself comes from the user's own disc at runtime.
#
#   tools/package_port.sh --binary build/linux/bfm-port \
#       --exe /path/to/your/SLUS_007.26 --extracted /path/to/extracted/disc \
#       [--lib /usr/lib/.../libSDL2-2.0.so.0 ...] [--extra FILE ...] \
#       [--out dist] [--version 0.1.0] [--allow-no-reference]
#
# --exe / --extracted point at YOUR files (for the retail-content guard
# only); nothing from them is copied.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
binary=""
out="dist"
version="dev"
guard_args=""
libs=""
extras=""

while [ $# -gt 0 ]; do
    case "$1" in
        --binary) binary=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --exe) guard_args="$guard_args --exe $2"; shift 2 ;;
        --extracted) guard_args="$guard_args --extracted $2"; shift 2 ;;
        --run-source) guard_args="$guard_args --run-source $2"; shift 2 ;;
        --allow-no-reference) guard_args="$guard_args --allow-no-reference"; shift ;;
        --lib) libs="$libs $2"; shift 2 ;;
        --extra) extras="$extras $2"; shift 2 ;;
        *) echo "package_port: unknown option $1" >&2; exit 2 ;;
    esac
done

[ -n "$binary" ] && [ -f "$binary" ] || { echo "package_port: --binary FILE is required" >&2; exit 2; }

arch=$(uname -m)
name="bfm-port-${version}-linux-${arch}"
stage="${TMPDIR:-/tmp}/bfm-package.$$/$name"
rm -rf "${stage%/*}"
mkdir -p "$stage/bin" "$stage/lib" "$stage/docs" "$stage/licenses" "$stage/mods/examples"
trap 'rm -rf "${stage%/*}"' EXIT

cp "$binary" "$stage/bin/bfm-port"
chmod 755 "$stage/bin/bfm-port"
for l in $libs; do cp -L "$l" "$stage/lib/"; done
for e in $extras; do cp "$e" "$stage/"; done

# Docs and samples (source only: the sample plugin is built by the user).
cp "$root/docs/ARCHITECTURE-PORT.md" "$root/docs/MODDING.md" "$stage/docs/"
cp "$root/pc_port/mods/README.md" "$stage/mods/"
for m in hello lua_hello cheat_pack; do
    [ -d "$root/pc_port/mods/examples/$m" ] && cp -R "$root/pc_port/mods/examples/$m" "$stage/mods/examples/"
done
cp "$root/pc_port/mods/examples/build_example.sh" "$stage/mods/examples/" 2>/dev/null || true
mkdir -p "$stage/include"
cp "$root/pc_port/platform/bfm_plugin.h" "$root/pc_port/platform/bfm_plat_types.h" \
   "$root/pc_port/platform/bfm_plat_input.h" "$root/pc_port/platform/bfm_plat_config.h" "$stage/include/"

# Licenses.
cp "$root/LICENSE-NOTES.md" "$stage/licenses/"
[ -f "$root/tools/third_party/psycross/LICENSE" ] && cp "$root/tools/third_party/psycross/LICENSE" "$stage/licenses/PsyCross-LICENSE"
if [ -f "$root/tools/third_party/lua/src/lua.h" ]; then
    sed -n '/Copyright (C) 1994/,/\*\*\*\*\*/p' "$root/tools/third_party/lua/src/lua.h" > "$stage/licenses/Lua-LICENSE"
fi
cat > "$stage/licenses/THIRD-PARTY.txt" <<'TXT'
Third-party components of the Brave Fencer Musashi native port

PsyCross (MIT, REDRIVER2 Project)       Psy-Q library reimplementation; see PsyCross-LICENSE
Lua 5.4 (MIT, Lua.org, PUC-Rio)         optional script runtime; see Lua-LICENSE when present
SDL2 (zlib)                             windowing, input, audio; bundled in lib/ if present
OpenAL Soft (LGPL-2.1, dynamic link)    PsyCross audio; bundled in lib/ if present
zlib (zlib)                             PNG texture decoder
OpenSSL libcrypto (Apache-2.0)          optional pinned-disc backend
Khronos GL headers (MIT)                gl renderer build-time API definitions
                                        (glcorearb.h, khrplatform.h); the system
                                        OpenGL driver is loaded at runtime

The game (executable and disc data) is copyrighted by its rights holders and
is not included. The port reads it from your own disc at runtime.
TXT

cat > "$stage/README.txt" <<TXT
Brave Fencer Musashi native port ($version, $arch)

This package contains no game data. You need your own copy of the US disc
(SLUS-00726) as a .cue/.bin or .iso image:

  1. Put the image in a folder named "disc" next to bin/, or pass
     --disc /path/to/game.cue, or set [disc] path in
     ~/.config/bfm-port/config.ini.
  2. Run bin/bfm-port. The port checks the disc (the boot executable's
     SHA-256) and tells you what is wrong if it is not the US release.

Mods go in mods/ (see docs/MODDING.md and mods/README.md).
TXT
mkdir -p "$stage/disc"
printf 'Put your own disc image (.cue + .bin, or .iso) here.\n' > "$stage/disc/PUT-YOUR-DISC-HERE.txt"

rc=0
# shellcheck disable=SC2086
python3 "$root/tools/retail_guard.py" --stage "$stage" $guard_args || rc=$?
if [ "$rc" -ne 0 ]; then
    if [ "$rc" -eq 1 ]; then
        echo "package_port: retail content found; no package written" >&2
    else
        echo "package_port: the retail guard could not run; no package written" >&2
    fi
    exit "$rc"
fi

mkdir -p "$out"
tar -C "${stage%/*}" -czf "$out/$name.tar.gz" "$name"
echo "package_port: wrote $out/$name.tar.gz"
