#!/bin/sh
# Fetch Lua (MIT) for the port's optional script runtime (BFM_PLAT_WITH_LUA).
#
# Pinned by version and the SHA-256 that lua.org publishes on
# https://www.lua.org/ftp/, and extracted under tools/third_party/lua, which is
# ignored by git like the PsyCross checkout. Nothing from Lua is committed.
#
# Run from the repository root:
#   ./tools/fetch_lua.sh

set -eu

LUA_VERSION="5.4.9"
LUA_SHA256="2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6"
LUA_URL="https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"
LUA_DIR="tools/third_party/lua"

if [ -f "$LUA_DIR/src/lua.h" ] && [ "$(cat "$LUA_DIR/.version" 2>/dev/null)" = "$LUA_VERSION" ]; then
    echo "lua $LUA_VERSION already present in $LUA_DIR"
    exit 0
fi

tmp="${TMPDIR:-/tmp}/bfm-lua.$$"
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
curl -sSfL -o "$tmp/lua.tar.gz" "$LUA_URL"
got=$(sha256sum "$tmp/lua.tar.gz" | cut -d' ' -f1)
if [ "$got" != "$LUA_SHA256" ]; then
    echo "lua-${LUA_VERSION}.tar.gz: sha256 $got, expected $LUA_SHA256" >&2
    exit 1
fi
tar -xzf "$tmp/lua.tar.gz" -C "$tmp"
rm -rf "$LUA_DIR"
mkdir -p "$(dirname "$LUA_DIR")"
mv "$tmp/lua-${LUA_VERSION}" "$LUA_DIR"
echo "$LUA_VERSION" > "$LUA_DIR/.version"
echo "lua $LUA_VERSION -> $LUA_DIR"
