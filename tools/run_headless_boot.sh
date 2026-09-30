#!/bin/sh
# Headless live boot of musashi_native_boot: no display, no sound card.
#
#   tools/run_headless_boot.sh --binary BUILD/musashi_native_boot \
#       --disc DIR/extracted/disc [--out DIR] [--timeout SECS]
#       [--capture-every N] [--host-clock] [--no-preload] [-- extra args]
#
# DIR/extracted/disc must hold files/SLUS_007.26, disc.cue and disc.bin (your
# own disc). Everything written goes to --out (default: headless-out/ in the
# repository, git-ignored); it must be outside the repository or ignored: egl.json, alsa.conf, the capture shim,
# out.log, trace/trace.counts, frames.log, frame_*.png, vram_*.png.
#
# What each setting does (see docs/ARCHITECTURE-PORT.md, "Headless live
# boot"). Two of them PAPER OVER REAL ISSUES and are marked WORKAROUND:
#   SDL_VIDEODRIVER=offscreen + Mesa EGL (egl.json, EGL_PLATFORM=surfaceless)
#                     a real GL context without a display (llvmpipe)
#   LIBGL_ALWAYS_SOFTWARE unset
#                     setting it crashes Mesa 25.0's EGL in SDL_CreateWindow
#   MUSASHI_AUDIO=null  explicit, logged null audio (port/native-lane
#                     8edc1d55d). Used when the binary has it; older builds
#                     (main before that merge) get the old WORKAROUND instead:
#                     an ALSA null PCM (alsa.conf, SDL_AUDIODRIVER=alsa), which
#                     satisfies an audio gate meant to require a real device
#   MUSASHI_CODE_IMAGE=<the EXE>
#                     harmless; needed only by builds before 7d483a31b, which
#                     refused at 80010000 without it
#   MUSASHI_GUEST_CLOCK=1 (default; --host-clock turns it off)
#                     on a loaded host the host-paced clock ends the run with
#                     an IRQ-dispatch refusal in the SPU poll loop (printed as
#                     IRQ_DISPATCH_REFUSED reason=source-overflow since
#                     8edc1d55d)
#   LD_PRELOAD Mesa libEGL/libEGL_mesa/libgallium
#                     only for builds without the ordered PsyCross shutdown
#                     (musashi_psyx_shutdown, 8edc1d55d): old WORKAROUND for
#                     GR_Shutdown segfaulting after Mesa's EGL libraries were
#                     unloaded. --no-preload skips it, --preload forces it
#   bfm_swap_capture.so (LD_PRELOAD, BFM_CAPTURE_*)
#                     per-frame non-black counts and periodic PNGs of the
#                     presented frame and PsyCross's VRAM
# BFM_HOST_DEPS (default ~/opt/host-port-deps/usr) supplies the Mesa/SDL/ALSA
# libraries.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
deps=${BFM_HOST_DEPS:-$HOME/opt/host-port-deps/usr}
lib=$deps/lib/x86_64-linux-gnu
binary= disc= out= timeout=600 every=600 guest=1 preload=auto
while [ $# -gt 0 ]; do
    case $1 in
    --binary) binary=$2; shift 2 ;;
    --disc) disc=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --timeout) timeout=$2; shift 2 ;;
    --capture-every) every=$2; shift 2 ;;
    --host-clock) guest=0; shift ;;
    --no-preload) preload=0; shift ;;
    --preload) preload=1; shift ;;
    --) shift; break ;;
    *) echo "run_headless_boot: unknown option $1" >&2; exit 2 ;;
    esac
done
[ -n "$out" ] || out=$root/headless-out
[ -n "$binary" ] && [ -n "$disc" ] || {
    echo "usage: $0 --binary PATH --disc EXTRACTED_DISC_DIR --out DIR [--timeout S]" >&2; exit 2; }
exe=$disc/files/SLUS_007.26
for f in "$binary" "$exe" "$disc/disc.cue" "$disc/disc.bin" "$lib/libEGL_mesa.so.0"; do
    [ -e "$f" ] || { echo "run_headless_boot: missing $f" >&2; exit 2; }
done
out=$(realpath -m "$out")
# never write into the tracked tree (checked before anything is created)
if git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    case $out/ in
    "$root"/*) git -C "$root" check-ignore -q "$out/" || {
        echo "run_headless_boot: --out is inside the repository and not ignored" >&2; exit 2; } ;;
    esac
fi
mkdir -p "$out"

printf '{"file_format_version":"1.0.0","ICD":{"library_path":"%s/libEGL_mesa.so.0"}}\n' "$lib" > "$out/egl.json"
printf 'pcm.!default {\n    type null\n}\n' > "$out/alsa.conf"

shim=$out/bfm_swap_capture.so
cc -std=c99 -O2 -shared -fPIC -I"$root/pc_port/platform" "$here/headless/bfm_swap_capture.c" \
    "$root/pc_port/platform/bfm_plat_image.c" -o "$shim" -ldl
vram_off=$(nm "$binary" 2>/dev/null | awk '$3 == "vram" && ($2 == "B" || $2 == "b") {print $1; exit}')

# what this binary already handles itself
has_null_audio=0
grep -aq "MUSASHI_AUDIO" "$binary" && has_null_audio=1
if [ "$preload" = auto ]; then
    preload=1
    nm "$binary" 2>/dev/null | grep -q " musashi_psyx_shutdown$" && preload=0
fi
if [ "$has_null_audio" = 1 ]; then
    audio_env="MUSASHI_AUDIO=${MUSASHI_AUDIO:-null}"   # e.g. MUSASHI_AUDIO=bfm_plat
else
    echo "run_headless_boot: this build has no MUSASHI_AUDIO; using the ALSA null PCM workaround" >&2
    audio_env="SDL_AUDIODRIVER=alsa ALSA_CONFIG_PATH=$out/alsa.conf"
fi

pre=$shim
if [ "$preload" = 1 ]; then
    gallium=$(ls "$lib"/libgallium-*.so 2>/dev/null | head -1)
    pre="$lib/libEGL.so.1:$lib/libEGL_mesa.so.0${gallium:+:$gallium}:$shim"
fi
clock=
[ "$guest" = 1 ] && clock=MUSASHI_GUEST_CLOCK=1

echo "run_headless_boot: $binary -> $out (timeout ${timeout}s, guest clock $guest, preload $preload)"
cd "$out"
set +e
env -u LIBGL_ALWAYS_SOFTWARE -u DISPLAY -u WAYLAND_DISPLAY \
    LD_LIBRARY_PATH="$lib" LD_PRELOAD="$pre" \
    SDL_VIDEODRIVER=offscreen EGL_PLATFORM=surfaceless __EGL_VENDOR_LIBRARY_FILENAMES="$out/egl.json" \
    $audio_env XDG_RUNTIME_DIR="$out" \
    MUSASHI_CODE_IMAGE="$exe" MUSASHI_BOOT_AUTO_START=1 MUSASHI_HOLD_WINDOW_MS=0 \
    MUSASHI_TRACE_REFUSAL=1 MUSASHI_TRACE_FUNCS="$out/trace" $clock \
    BFM_CAPTURE_DIR="$out" BFM_CAPTURE_EVERY="$every" BFM_CAPTURE_VRAM_OFFSET="$vram_off" \
    timeout "$timeout" "$binary" "$exe" "$disc/disc.cue" "$disc/disc.bin" "$@" > "$out/out.log" 2>&1
status=$?
set -e

echo "exit: $status$( [ $status = 124 ] && echo ' (timeout)')"
grep -m1 "EXECUTION_BOUNDARY" out.log || true
grep -m1 "RUN_REFUSED\|CHECKPOINT_REFUSED" out.log || true
grep -m1 "CPU_BOUNDARY" out.log | cut -c1-120 || true
for m in TITLE_LOAD SCENE1_REACHED RENDER_ENTER CALL_80159C84; do
    n=$(grep -c "native_boot: $m" out.log || true)
    [ "$n" -gt 0 ] && echo "marker $m: $n (first: $(grep -m1 "native_boot: $m" out.log | cut -c1-90))"
done
if [ -s frames.log ]; then
    echo "frames presented: $(wc -l < frames.log)"
    awk '$3 > 0 {print "first non-black frame: " $1 " at " $2 " s (" $3 " px of " $4 "x" $5 ")"; exit}' frames.log
fi
if [ -s trace.counts ]; then
    echo "traced functions: $(($(wc -l < trace.counts) - 1)), top:"
    sed -n 2,11p trace.counts
elif [ -s trace ]; then
    echo "traced functions (first entries): $(($(wc -l < trace) - 1))"
fi
exit 0
