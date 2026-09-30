#!/bin/sh
# Package musashi_native_boot for the owner's launcher (Linux x86_64, Legion Go).
#
#   tools/package_legion.sh --binary BUILD/musashi_native_boot --run N \
#       --out DIR --status TEXT [--known-issue TEXT ...] \
#       [--exe YOUR/SLUS_007.26 --extracted YOUR/extracted/disc]
#
# Stages bfm-r<N>-<commit>/ (binary, runtime libraries, launch.sh, README,
# licenses, manifest.json with the size and SHA-256 of every file), runs
# tools/retail_guard.py over it and writes bfm-r<N>-<commit>.tar.gz beside it.
# No disc image, BIOS or game data is copied; --exe/--extracted feed the
# guard only. The game is read at run time from BFM_DISC_DIR (default
# ~/Games/brave-fencer-musashi/disc).

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
binary=""
run=""
out=""
status=""
issues=""
guard_args=""
nl='
'

while [ $# -gt 0 ]; do
    case "$1" in
        --binary) binary=$2; shift 2 ;;
        --run) run=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --status) status=$2; shift 2 ;;
        --known-issue) issues="$issues$2$nl"; shift 2 ;;
        --exe) guard_args="$guard_args --exe $2"; shift 2 ;;
        --extracted) guard_args="$guard_args --extracted $2"; shift 2 ;;
        *) echo "package_legion: unknown option $1" >&2; exit 2 ;;
    esac
done
[ -f "$binary" ] && [ -n "$run" ] && [ -n "$out" ] && [ -n "$status" ] || {
    echo "usage: $0 --binary FILE --run N --out DIR --status TEXT [--known-issue TEXT ...]" >&2; exit 2; }

commit=$(git -C "$root" rev-parse --short=9 HEAD)
branch=$(git -C "$root" rev-parse --abbrev-ref HEAD)
name="bfm-r$run-$commit"
bindir=$(cd "$(dirname "$binary")" && pwd)
stage="$out/$name"
[ -e "$stage" ] && { echo "package_legion: $stage already exists" >&2; exit 2; }
mkdir -p "$stage/lib" "$stage/licenses"

cp "$binary" "$stage/musashi_native_boot"
chmod 755 "$stage/musashi_native_boot"
cp "$bindir/libmusashi_x11_nograb.so" "$stage/lib/"
for soname in libSDL2-2.0.so.0 libopenal.so.1 libcrypto.so.3 libz.so.1; do
    path=$(ldd "$binary" | awk -v s="$soname" '$1 == s {print $3}')
    [ -f "$path" ] || { echo "package_legion: $soname not resolved" >&2; exit 2; }
    cp -L "$path" "$stage/lib/"
done
for pkg in libsdl2-2.0-0 libopenal1 libssl3t64 zlib1g; do
    [ -f "/usr/share/doc/$pkg/copyright" ] && cp "/usr/share/doc/$pkg/copyright" "$stage/licenses/$pkg-copyright"
done
cp "$root/LICENSE-NOTES.md" "$stage/licenses/"
[ -f "$root/tools/third_party/psycross/LICENSE" ] && cp "$root/tools/third_party/psycross/LICENSE" "$stage/licenses/PsyCross-LICENSE"
cat > "$stage/licenses/THIRD-PARTY.txt" <<'TXT'
PsyCross (MIT)            Psy-Q library reimplementation; see PsyCross-LICENSE
SDL2 (zlib)               window, input, audio; lib/libSDL2-2.0.so.0
OpenAL Soft (LGPL-2.1)    audio; lib/libopenal.so.1, dynamically linked
OpenSSL libcrypto (Apache-2.0)   SHA-256 of the disc; lib/libcrypto.so.3
zlib (zlib)               PNG encoder; lib/libz.so.1
The game is copyrighted by its rights holders and is not included.
TXT

cat > "$stage/launch.sh" <<'SH'
#!/bin/sh
# Brave Fencer Musashi native port. Plays from your own disc image (.cue/.bin
# of the USA disc, SLUS-00726); nothing from the game is in this package.
# The disc is found in this order: $BFM_DISC (a .cue or .bin), the one you
# picked last time, then ./disc next to this script,
# ~/Games/brave-fencer-musashi/disc ($BFM_DISC_DIR too);
# otherwise a file picker asks once and the choice is remembered.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CONF=${XDG_CONFIG_HOME:-$HOME/.config}/bfm-port
STATE=${XDG_STATE_HOME:-$HOME/.local/state}/bfm-port
mkdir -p "$CONF" "$STATE"
LOG="$STATE/last-run.log"

say() {  # message box when a desktop tool exists, always stderr
    echo "bfm: $1" >&2
    if command -v zenity >/dev/null 2>&1; then zenity --error --title="Brave Fencer Musashi" --text="$1" 2>/dev/null
    elif command -v kdialog >/dev/null 2>&1; then kdialog --title "Brave Fencer Musashi" --error "$1" 2>/dev/null
    fi
}

find_in() {  # first .cue, else first .bin, in a folder
    [ -d "$1" ] || return 1
    for f in "$1"/*.cue "$1"/*.CUE "$1"/*.bin "$1"/*.BIN; do
        [ -f "$f" ] && { echo "$f"; return 0; }
    done
    return 1
}

DISC=${BFM_DISC:-}
[ -z "$DISC" ] && [ -f "$CONF/disc" ] && DISC=$(cat "$CONF/disc")
[ -n "$DISC" ] && [ ! -f "$DISC" ] && DISC=
if [ -z "$DISC" ]; then
    for d in ${BFM_DISC_DIR:+"$BFM_DISC_DIR"} "$HERE/disc" "$HOME/Games/brave-fencer-musashi/disc"; do
        DISC=$(find_in "$d") && break
        DISC=
    done
fi
if [ -z "$DISC" ]; then
    if command -v zenity >/dev/null 2>&1; then
        DISC=$(zenity --file-selection --title="Choose your Brave Fencer Musashi (USA) .cue or .bin" \
               --file-filter="Disc image | *.cue *.CUE *.bin *.BIN" 2>/dev/null) || DISC=
    elif command -v kdialog >/dev/null 2>&1; then
        DISC=$(kdialog --title "Choose your Brave Fencer Musashi (USA) .cue or .bin" \
               --getopenfilename "$HOME" "*.cue *.CUE *.bin *.BIN" 2>/dev/null) || DISC=
    fi
fi
if [ -z "$DISC" ]; then
    say "No disc image found. Put your Brave Fencer Musashi (USA) .cue and .bin in ~/Games/brave-fencer-musashi/disc/ and start again."
    exit 66
fi
printf '%s\n' "$DISC" > "$CONF/disc"

export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Playable window, real-time guest clock, errors shown on screen.
export MUSASHI_INTERACTIVE=${MUSASHI_INTERACTIVE:-1}
export MUSASHI_GUEST_CLOCK=${MUSASHI_GUEST_CLOCK:-1}
export MUSASHI_HOLD_WINDOW_MS=${MUSASHI_HOLD_WINDOW_MS:-3000}
"$HERE/musashi_native_boot" --disc "$DISC" "$@" > "$LOG" 2>&1
status=$?
# A refused image is not worth remembering.
[ "$status" = 2 ] && grep -q "could not start from your disc image" "$LOG" && rm -f "$CONF/disc"
exit "$status"
SH
chmod 755 "$stage/launch.sh"

{
    echo "Brave Fencer Musashi native port $name (Linux x86_64, Legion Go)"
    echo
    echo "Status: $status"
    [ -n "$issues" ] && { echo; echo "Known issues:"; printf '%s' "$issues" | sed 's/^/  - /'; }
    cat <<'TXT'

Your disc (none of the game is included). Put the .cue and .bin dump of your
Brave Fencer Musashi (USA, SLUS-00726) disc in ~/Games/brave-fencer-musashi/disc/
(or next to launch.sh in disc/), or pick it when asked on first start; the
choice is remembered in ~/.config/bfm-port/disc. One-bin and per-track dumps
both work. A 2048-byte .iso does not (it lacks the CD-XA data).
No BIOS is needed: the port provides its own kernel services.

Controls: a game controller (first connected SDL GameController) or keyboard.
  Controller: A Cross, B Circle, X Square, Y Triangle, Start, Back Select,
  LB/RB L1/R1, d-pad or left stick.
  Keyboard: arrows or WASD d-pad; C Cross, X Square, V Circle, Z Triangle
  (or K/J/I/U); Return Start, Space Select; left/right Shift L1/R1,
  left/right Ctrl L2/R2.

Requires glibc 2.38+ and the system's OpenGL, X11 or Wayland, and ALSA or
PulseAudio/PipeWire libraries. Run ./launch.sh; extra arguments go to the port.
Log of the last run: ~/.local/state/bfm-port/last-run.log.
Dev menu (off by default): BFM_DEV_MENU=1 ./launch.sh, then F1 or L3+R3.
TXT
} > "$stage/README.txt"

python3 - "$stage" "$name" "$commit" "$branch" "$status" "$issues" <<'PY'
import hashlib, json, os, sys
stage, name, commit, branch, status, issues = sys.argv[1:]
files = []
for dirpath, _, names in os.walk(stage):
    for n in names:
        p = os.path.join(dirpath, n)
        rel = os.path.relpath(p, stage)
        if rel == "manifest.json":
            continue
        files.append({"path": rel, "size_bytes": os.path.getsize(p),
                      "sha256": hashlib.sha256(open(p, "rb").read()).hexdigest()})
files.sort(key=lambda f: f["path"])
disc = "~/Games/brave-fencer-musashi/disc"
manifest = {
    "package": name, "target": "Linux x86_64 / Legion Go",
    "source_commit": commit, "source_branch": branch,
    "build_type": "RelWithDebInfo", "native_lane": True,
    "playback": status,
    "known_issues": [i for i in issues.splitlines() if i],
    "disc_dir_default": disc, "disc_dir_env": "BFM_DISC_DIR",
    "disc_inputs": ["your own .cue/.bin of Brave Fencer Musashi (USA, SLUS-00726); "
                    "searched in " + disc + ", ./disc" +
                    ", or picked on first run"],
    "excluded": ["disc image and all other game data", "BIOS"],
    "launch": "./launch.sh [native port options]",
    "files": files,
}
json.dump(manifest, open(os.path.join(stage, "manifest.json"), "w"), indent=2)
open(os.path.join(stage, "manifest.json"), "a").write("\n")
PY

# shellcheck disable=SC2086
python3 "$root/tools/retail_guard.py" --stage "$stage" $guard_args || {
    echo "package_legion: retail guard failed; no tarball written" >&2; exit 1; }
tar -C "$out" -czf "$out/$name.tar.gz" "$name"
sha256sum "$out/$name.tar.gz"
stat -c '%s bytes' "$out/$name.tar.gz"
