#!/bin/sh
# Fetch everything needed to build matching candidates: the candidate compilers
# and the maspsx assembler shim.
#
# The compilers are GPL rebuilds of old GCC from decompals/old-gcc, not Sony
# Psy-Q binaries, so they are freely redistributable. Nothing here is tracked,
# because it is large and this script reproduces it exactly.
#
# The registry in tools/build_candidate.py pairs each compiler with its ASPSX
# assembler version; keep the two in step when adding a compiler here.
#
# Run from the repository root:
#   ./tools/fetch_toolchains.sh [compiler-destination]   (default: tools/psyq)

set -eu

BASE="https://github.com/decompals/old-gcc/releases/download"
DEST="${1:-tools/psyq}"

# maspsx reproduces ASPSX macro expansion between cc1 and GNU as. Pinned so a
# change in its expansion behaviour cannot silently alter match results.
MASPSX_REPO="https://github.com/mkst/maspsx.git"
MASPSX_COMMIT="746b895f02929ecd148af7b1f4ff05b69f973878"
MASPSX_DIR="tools/maspsx"

# m2c turns a function's disassembly into C to start a match from. Pinned so
# its output does not shift under an in-progress decompilation.
M2C_REPO="https://github.com/matt-kempster/m2c.git"
M2C_COMMIT="a73bfba20a00db8a546ec84d6ebec76063ebcf60"
M2C_DIR="tools/m2c"

# PsyCross (MIT) reimplements the Psy-Q libraries on SDL2/OpenAL/OpenGL and is
# what the port links against. Pinned so the shim under the port cannot drift.
# Its own CMakeLists.txt is not used; the integration lives in CMakeLists.txt.
# Needs SDL2 and OpenAL-soft, e.g. from Homebrew:
#   brew install sdl2 openal-soft
PSYCROSS_REPO="https://github.com/OpenDriver2/PsyCross.git"
PSYCROSS_COMMIT="e56e4cde1c2b8a15e0d4e38b26cdd9202e0d17e6"
PSYCROSS_DIR="tools/third_party/psycross"

mkdir -p "$DEST"

fetch_pinned() {
    dir="$1"
    repo="$2"
    commit="$3"
    marker="$4"
    if [ -f "$dir/$marker" ]; then
        echo "$(basename "$dir") already present"
        return
    fi
    echo "fetching $(basename "$dir")"
    git clone --quiet "$repo" "$dir"
    git -C "$dir" checkout --quiet "$commit"
}

fetch_pinned "$MASPSX_DIR" "$MASPSX_REPO" "$MASPSX_COMMIT" maspsx.py
fetch_pinned "$M2C_DIR" "$M2C_REPO" "$M2C_COMMIT" m2c.py
fetch_pinned "$PSYCROSS_DIR" "$PSYCROSS_REPO" "$PSYCROSS_COMMIT" include/PsyX/PsyX_public.h
fetch_pinned "$PSYCROSS_DIR" "$PSYCROSS_REPO" "$PSYCROSS_COMMIT" CMakeLists.txt

fetch() {
    name="$1"
    url="$2"
    if [ -x "$DEST/$name/cc1" ]; then
        echo "$name already present"
        return
    fi
    echo "fetching $name"
    mkdir -p "$DEST/$name"
    tmp="$(mktemp)"
    if curl -fsSL "$url" -o "$tmp"; then
        tar xzf "$tmp" -C "$DEST/$name"
        rm -f "$tmp"
    else
        rm -f "$tmp"
        echo "ERROR: failed to download $name from $url" >&2
        return 1
    fi
}

# PSY-Q 3.5
fetch gcc-2.6.0-psx     "$BASE/0.12/gcc-2.6.0-psx.tar.gz"
# PSY-Q 4.0
fetch gcc-2.7.2-psx     "$BASE/0.13/gcc-2.7.2-psx.tar.gz"
# PSY-Q 4.1
fetch gcc-2.7.2-cdk-psx "$BASE/0.15/gcc-2.7.2-cdk.tar.gz"
# PSY-Q 4.3
fetch gcc-2.8.0-psx     "$BASE/0.12/gcc-2.8.0-psx.tar.gz"
# PSY-Q 4.4
fetch gcc-2.8.1-psx     "$BASE/0.12/gcc-2.8.1-psx.tar.gz"
# PSY-Q 4.5
fetch gcc-2.91.66-psx   "$BASE/0.12/gcc-2.91.66-psx.tar.gz"

echo
echo "compilers under $DEST:"
for dir in "$DEST"/*/; do
    name="$(basename "$dir")"
    if [ -x "$dir/cc1" ]; then
        echo "  $name"
    else
        echo "  $name (INCOMPLETE: no cc1)"
    fi
done
echo "assembler shim: $MASPSX_DIR @ $MASPSX_COMMIT"
echo "decompiler:     $M2C_DIR @ $M2C_COMMIT"
echo "psy-q for pc:   $PSYCROSS_DIR @ $PSYCROSS_COMMIT"
