#!/usr/bin/env bash
# Boot real retail code, then hold its naturally rendered start-screen frame.
# This is a paused inspection preview, not a gameplay/attract-mode launcher.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="${MUSASHI_NATIVE_BUILD:-$repo/build/menu-takeover}"
run_dir="${MUSASHI_NATIVE_RUN_DIR:-$repo/artifacts/menu-preview-$(date +%Y%m%d-%H%M%S)}"
mkdir -p -- "$run_dir"
cd -- "$run_dir"
export SDL_VIDEODRIVER=x11
export MUSASHI_HOLD_R1=0 MUSASHI_BOOT_AUTO_START=0
export MUSASHI_PAUSE_AT_START_SCREEN=1
export MUSASHI_HOLD_WINDOW_MS="${MUSASHI_HOLD_WINDOW_MS:-86400000}"
export SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR=0 SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0
export SDL_GRAB_KEYBOARD=0 SDL_MOUSE_AUTO_CAPTURE=0
export SDL_WINDOW_ACTIVATE_WHEN_RAISED=0 SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0
export LD_PRELOAD="$build/libmusashi_x11_nograb.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$build/musashi_native_boot" "$repo/extracted/disc/files/SLUS_007.26" \
    "$repo/extracted/disc/disc.cue" "$repo/extracted/disc/disc.bin" \
    </dev/null >stdout.log 2>stderr.log
