#!/bin/sh
# Builds and runs the gl renderer benchmark on OSMesa (headless).
#   tools/gl_bench/run.sh [bench args...]     e.g. --scales 1,2,4 --feedback
# BFM_HOST_DEPS (default ~/opt/host-port-deps/usr) supplies the Khronos GL
# headers and libOSMesa; BFM_OSMESA overrides the library path.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
deps=${BFM_HOST_DEPS:-$HOME/opt/host-port-deps/usr}
lib=${BFM_OSMESA:-$deps/lib/x86_64-linux-gnu/libOSMesa.so.8}
out=${TMPDIR:-/tmp}/bfm_gl_bench
plat=$root/pc_port/platform
cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -D_POSIX_C_SOURCE=200809L \
    -I"$plat" -I"$deps/include" "$plat"/bfm_plat*.c "$plat/psyq/bfm_psyq_compat.c" \
    "$plat/psyq/bfm_psyq_libgs.c" "$plat/psyq/bfm_psyq_libgte.c" \
    "$plat/backends/gl/bfm_gl_core.c" "$plat/backends/gl/bfm_gl_exec.c" \
    "$root/tools/gl_bench/bfm_gl_bench.c" -o "$out" -ldl -lm
LD_LIBRARY_PATH="$deps/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    exec "$out" "$lib" "$@"
