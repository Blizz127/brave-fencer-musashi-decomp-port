# brave-fencer-musashi-decomp-port

An experimental PC port of *Brave Fencer Musashi* (PlayStation, USA,
SLUS-00726). You play from **your own disc**; no game data or BIOS is included.
The current runtime is **hybrid**: compiled host code plus a CPU interpreter.
The fully native port remains a development goal.

> **Public r38 — 2026-10-02:** Linux x86_64 is available. Forest rendering
> fixes and isolated inn save/cold Continue checks passed. Windows is still
> pending: the game has not built or run on Windows. Whole-game completion
> and full graphics/audio fidelity are unverified.

## About this project
This is a passion project. I'm working hard on it, but it's made for fun and for everyone's enjoyment — free, non-commercial, and made by a fan. If you enjoy it, that's the whole point.

## Milestones and progress

Public releases mark selected improvements; an entire milestone may still
be in progress. The [Milestones](https://github.com/Blizz127/brave-fencer-musashi-decomp-port/wiki/Milestones)
wiki page defines each milestone and records segment status and a dated
changelog.

**Next milestone: Chapter 1 playable end to end, with saving.**

## How to play

These instructions match [bfm-r38-61c60516d](https://github.com/Blizz127/brave-fencer-musashi-decomp-port/releases/tag/bfm-r38-61c60516d).
Read the [setup guide](docs/SETUP.md) for prerequisites, controls, saves and troubleshooting.

### Linux x86_64

Download `bfm-r38-61c60516d-linux-x86_64.tar.gz` and `SHA256SUMS` from the
release page into the same directory, then:

```sh
sha256sum -c SHA256SUMS
mkdir -p "$HOME/Games/bfm-port"
tar -xzf bfm-r38-61c60516d-linux-x86_64.tar.gz -C "$HOME/Games/bfm-port"
"$HOME/Games/bfm-port/bfm-r38-61c60516d/launch.sh"
```

Requires glibc **2.38+**, a C++ runtime with **GLIBCXX_3.4.32**, OpenGL
and the desktop graphics/audio libraries listed in the setup guide.
Put a `.cue`/`.bin` dump of **your own USA SLUS-00726 disc** in
`~/Games/brave-fencer-musashi/disc/` or a `disc/` folder beside `launch.sh`.
Keep all referenced tracks together. One-bin and per-track dumps work; a
2048-byte `.iso` does not. A file picker is offered when available, or set
`BFM_DISC="/path/to/your/game.cue"` when launching.

For Steam Deck/Game Mode, add `launch.sh` as a non-Steam game and keep Steam
Input enabled. Preserve earlier version folders and back up saves after
quitting normally; no running game needs to be updated or restarted.

### Windows

**No Windows game package is available yet.** Windows is a separate
host-port project with no release date. The platform-name adapter cross-compiled, but the
complete game failed configuration and still needs Windows host/ABI work,
libraries and runtime qualification. There is no verified Windows install or
run procedure yet. See [Windows status and game-data preparation](docs/SETUP.md#windows-status)
for the exact limits and source-intended paths. Wine/Proton is unverified.

### Controls, saves and dev menu

- Gameplay: D-pad/left stick or arrows/WASD; controller A/B/X/Y correspond to
  Cross/Circle/Square/Triangle, or keyboard C/V/X/Z. Enter is Start; Space is
  Select. [Full controls](docs/SETUP.md#controls).
- Save using the game's menus. The default Linux card is
  `~/.local/state/bfm-port/bfm_card0.mcd`; `BFM_SAVE_DIR` selects a separate
  card directory. Inn save and cold Continue passed isolated tests.
  [Save/config locations and limitations](docs/SETUP.md#saves-settings-and-files-on-linux).
- The packaged dev menu is enabled by default, with cheats initially off.
  Open with **F8** or **Back/Select+Start** on one controller. Navigate with
  Up/Down or D-pad/left stick; Enter/A selects, Esc/B goes back. **F1 is help.**
  The game keeps running while the menu is open.
- The menu offers 29 warp destinations, an empty Finish Area page, HP/BP
  refills, Drans/time actions, fast-forward and screenshots. See the
  [current Dev Menu wiki](https://github.com/Blizz127/brave-fencer-musashi-decomp-port/wiki/Dev-Menu)
  for exact options, effects and precautions.

The log is `~/.local/state/bfm-port/last-run.log` by default. See
[troubleshooting](docs/SETUP.md#troubleshooting). General `config.ini`
settings are not reliably loaded by r38; the setup guide lists working
launcher controls instead.

## Status

r38 fixes forest frames going dark and includes tested inn saving and cold
Continue. Forest, village and the recorded four-scene attract route passed
scoped checks. Attract still stops at known refusal `800495ec`; later
unsupported code may also stop. Automated checks used null audio, and do
not establish hardware audio fidelity or whole-game completion.

The goal remains a faithful, fully native port. Original rendering is the
release default. Cheats start off, even though the dev menu is enabled.
[Milestones](https://github.com/Blizz127/brave-fencer-musashi-decomp-port/wiki/Milestones)
and [known divergences](docs/KNOWN-DIVERGENCES.md) distinguish completed
checks from remaining work.

## Build from source

**Source snapshot limitation:** the public r38 tag points to sanitized
snapshot `ec620c78dd5f68a166f6565f7c2d8539e9c378a0`; its runtime binaries
were built from `61c60516d18bf6402e6dbd937d8c8d18ebd1af4b`. GitHub's automatic
source archives and the historical build instructions below do not
reproduce the newer r38 binary. Windows compilation is not yet supported
by this recipe. Public player instructions above describe the release
artifact, not the older source snapshot's menu behavior.

This repository holds the port (`pc_port/`), its tools (`tools/`), tests,
configuration and documentation, and the owner-authored matched C that the
port compiles natively (`src/main/`, 252 functions, with their entries in
`provenance/matches.json`: addresses, sizes and source paths only). All of it
is the owner's original work. See [EXCLUDED.md](EXCLUDED.md) for what is left
out and why.

Matched C is a reimplementation of the game's own code, written so that the
original compiler reproduces the original machine code. It is published for
study, interoperability and preservation, like other matching
decompilations; it does not grant any rights in *Brave Fencer Musashi*.

The r38 release binary is built with `-DMUSASHI_NATIVE_LANE=ON
-DMUSASHI_LANE_OWN_CODE_ONLY=ON`. With these options the native lane compiles
only owner-authored matched C listed in
[config/lane_own_code_allow.txt](config/lane_own_code_allow.txt) (guest
addresses only). Everything else runs interpreted from your own disc,
including every function whose recorded origin names the upstream
decompilation or Sony/Psy-Q code. The commands below describe the older
sanitized source snapshot. Its historical function counts do not describe
r38; the current release's admitted sources passed the no-m2c-macro guard.

A build needs the following:

1. **Linux build dependencies:** CMake, a C/C++ compiler and clang, Python 3,
   SDL2, OpenAL Soft, OpenGL, OpenSSL (libcrypto), and optionally zlib.
2. **PsyCross** (MIT). `./tools/fetch_toolchains.sh` fetches it at the pinned
   commit `e56e4cd` into `tools/third_party/psycross/`. The same script also
   fetches maspsx, m2c and the old-gcc compilers used by the matching tools.
   Then apply the port's local PsyCross edits (MIT):
   `git -C tools/third_party/psycross apply ../../patches/psycross-local.patch`
   (the patch is `tools/patches/psycross-local.patch`, made against
   `e56e4cde`). `./tools/fetch_lua.sh` fetches Lua for mod
   scripts. This step is optional.
3. **Your own disc, at build time.** `tools/native_lane_gen.py` reads
   `extracted/disc/files/SLUS_007.26` and `extracted/overlays/main/0003.bin`
   to verify what it compiles. The tools create both files from your disc:

   ```sh
   # dumpsxiso from https://github.com/Lameguy64/mkpsxiso, built into
   # tools/third_party/mkpsxiso/build/; chdman (MAME) on PATH
   python3 tools/register_retail.py "/path/to/Brave Fencer Musashi (USA).chd"
   python3 tools/extract_retail.py "/path/to/Brave Fencer Musashi (USA).chd"
   python3 tools/extract_cd.py extracted/disc/files/MAIN.CD --output extracted/overlays/main
   ```

   These commands check your CHD against the pinned size and SHA-256 of the
   USA release. They write the BIN/CUE and the disc files to the ignored
   `extracted/` directory.
4. **Configure and build:**

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DMUSASHI_NATIVE_LANE=ON -DMUSASHI_LANE_OWN_CODE_ONLY=ON
   cmake --build build -j"$(nproc)"
   build/musashi_native_boot --disc extracted/disc/disc.cue
   ```

Full build instructions will be in BUILD.md (to be added). Also see
[docs/PC-PORT.md](docs/PC-PORT.md) and
[docs/ARCHITECTURE-PORT.md](docs/ARCHITECTURE-PORT.md).
`tools/package_legion.sh` builds the release bundle with `launch.sh`, and runs
the retail-data guard before it writes the tarball.

## License

- The owner's original work in this repository is under the **MIT License**
  ([LICENSE](LICENSE)). That license covers only this work.
- The upstream decompilation
  [Druthulu/BFM-decomp](https://github.com/Druthulu/BFM-decomp) is credited
  as the reference this project was checked against. Its symbol and type
  names are used in `config/`, with credit. None of its code is included (see
  [CREDITS.md](CREDITS.md)).
- Third-party libraries and tools keep their own licenses (see
  [CREDITS.md](CREDITS.md)).
- *Brave Fencer Musashi* is © Square Enix (originally Square Co., Ltd.). This
  project is not affiliated with or endorsed by Square Enix or Sony
  Interactive Entertainment. See [NOTICE.md](NOTICE.md).
