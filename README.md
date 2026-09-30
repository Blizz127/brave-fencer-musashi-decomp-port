# brave-fencer-musashi-decomp-port

This is a native PC port of *Brave Fencer Musashi* (PlayStation, USA,
SLUS-00726) for Linux (x86-64), with the tools of the matching decompilation
it is built on. You play from **your own disc**. None of the game is
included.

> **Early preview.** The port boots through the logos, the title and the
> opening scene. The game hands you control and Musashi responds to input.
> More of the game is being brought up.

## About this project
This is a passion project. I'm working hard on it, but it's made for fun and for everyone's enjoyment — free, non-commercial, and made by a fan. If you enjoy it, that's the whole point.

## Milestones and progress

Public releases now happen at milestones; the work in between ships as private
checkpoint builds. The [Milestones](../../wiki/Milestones) wiki page has the
definition of each milestone, the status of every segment and a dated
changelog, and is updated at each checkpoint.

**Next milestone: Chapter 1 playable end to end, with saving.**

## How to play

### 1. Download

Get **the latest release** from the [Releases](../../releases) page:

- `bfm-r24-fe4a59820-linux-x86_64.tar.gz` (or newer) is the port.
- `SHA256SUMS` holds the checksums. Check them with `sha256sum -c SHA256SUMS`.

Unpack the port anywhere. It unpacks into its own subfolder. For example:

```sh
mkdir -p ~/Games/bfm-port
tar -xzf bfm-r24-fe4a59820-linux-x86_64.tar.gz -C ~/Games/bfm-port
```

It needs glibc 2.38 or newer, OpenGL, X11 or Wayland, and ALSA or
PulseAudio/PipeWire. A normal desktop Linux or a Steam Deck already has all of
these.

### 2. Your disc

You need a **.cue/.bin dump of your own Brave Fencer Musashi disc** (USA,
SLUS-00726).

- One-bin and per-track dumps both work.
- A 2048-byte `.iso` does **not** work, because it is missing the CD-XA audio
  and video data.
- You don't need a BIOS. The port provides its own kernel services.

Put the `.cue` and `.bin` files in `~/Games/brave-fencer-musashi/disc/`, or in
a `disc/` folder next to `launch.sh` (for example
`~/Games/bfm-port/bfm-r24-fe4a59820/disc/`). You can also pick the `.cue` when the
port asks on first start. The port remembers your choice in
`~/.config/bfm-port/disc`.

### 3. Run

```sh
~/Games/bfm-port/bfm-r24-fe4a59820/launch.sh
```

Any extra arguments are passed to the port. To start the port directly, run
`./musashi_native_boot --disc /path/to/your.cue` from that folder.

**Steam Deck / Game Mode:** in Desktop Mode, open Steam and choose *Add a
Game → Add a Non-Steam Game*. Browse to `~/Games/bfm-port/bfm-r24-fe4a59820/launch.sh` and add it. It then shows
up in your library in Game Mode.

### Controls

You can use a game controller (the first one connected) or the keyboard.

| PlayStation | Controller | Keyboard |
|---|---|---|
| D-pad | D-pad or left stick | Arrow keys or WASD |
| Cross | A | C (or K) |
| Circle | B | V (or I) |
| Square | X | X (or J) |
| Triangle | Y | Z (or U) |
| Start | Start | Enter |
| Select | Back | Space |
| L1 / R1 | LB / RB | Left Shift / Right Shift |
| L2 / R2 | LT / RT | Left Ctrl / Right Ctrl |

### Dev menu (off by default)

```sh
BFM_DEV_MENU=1 ~/Games/bfm-port/bfm-r24-fe4a59820/launch.sh
```

To open the menu, press **F1** on the keyboard. On a controller, press
**L3 + R3** (both sticks) or the **Guide** button.

- Move with up/down.
- Confirm with Enter or C (A on a controller).
- Go back with Esc or V (B on a controller).

The menu has opt-in cheats: Infinite HP, Infinite BP and Max Drans. Warps are
added to the menu once each one has been tested in the port.

### Troubleshooting

- Errors are shown on screen. The log of the last run is at
  `~/.local/state/bfm-port/last-run.log`. Please attach it to bug reports.
- **"No disc image found"**: put your `.cue` and `.bin` in
  `~/Games/brave-fencer-musashi/disc/` and start again.
- **"not the USA release"**: the port checks your files against the USA disc
  (SLUS-00726). This message means your dump is of a different release, or
  it has been modified or patched. Make a clean dump of the USA disc.
- **An `.iso` is refused**: use a `.cue/.bin` dump instead (see above).

### FAQ

- **Do I need a BIOS?** No.
- **Is the game included?** No, you bring your own disc. Nothing of the game
  is distributed here: no disc image, no game files and no assets.
- **Can I play it through?** Not yet. This is an early preview (see Status).
- **Windows or macOS?** Only Linux x86-64 for now.

## Status

This is an early preview. The port boots through the logos, the title and
the opening scene. The game hands you control and Musashi responds to input.
More of the game is being brought up.

The goal is a port that is 1:1 with the North American release. Mods, cheats
and enhancements are opt-in and off by default.
[docs/KNOWN-DIVERGENCES.md](docs/KNOWN-DIVERGENCES.md) lists everything known
to differ from the PlayStation original, and everything not compared yet.

## Build from source

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

The release binary is built with `-DMUSASHI_NATIVE_LANE=ON
-DMUSASHI_LANE_OWN_CODE_ONLY=ON`. With these options the native lane compiles
only owner-authored matched C listed in
[config/lane_own_code_allow.txt](config/lane_own_code_allow.txt) (guest
addresses only). Everything else runs interpreted from your own disc,
including every function whose recorded origin names the upstream
decompilation or Sony/Psy-Q code. This repository builds that binary with the
steps below (it compiles 210 functions natively; the release also compiles 12
more whose sources use an m2c-derived helper header that is not published
here).

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
