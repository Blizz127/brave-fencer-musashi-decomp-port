# Setup for public r38

These instructions match **bfm-r38-61c60516d**, published on 2026-10-02,
using runtime source `61c60516d18bf6402e6dbd937d8c8d18ebd1af4b`.
[Download the release](https://github.com/Blizz127/brave-fencer-musashi-decomp-port/releases/tag/bfm-r38-61c60516d).
This is an experimental **hybrid** port: substantial game code still runs
through a CPU interpreter. Full native execution and whole-game completion
are not established.

| Platform | Package and verification |
|---|---|
| Linux x86_64 | Published tarball; forest, village, recorded attract endpoint, isolated inn save and cold Continue checked. Automated gameplay checks used null audio. |
| Windows | Package pending. A small platform adapter cross-compiled; the complete game did not build, and no Windows gameplay test has passed. See [Windows status](#windows-status). |

## Linux prerequisites

- Linux x86_64 with glibc **2.38 or newer** (`ldd --version` shows the installed version).
- System `libstdc++.so.6` exporting **GLIBCXX_3.4.32** or newer.
- Working OpenGL drivers, an X11 or Wayland desktop, and audio support. The bundled SDL library links both X11 and Wayland library families; it also needs ALSA/PulseAudio, libsamplerate, DRM/GBM, libxkbcommon and libdecor. OpenSSL needs system libzstd. Missing dependencies are reported by the system loader.
- Your own legally obtained USA disc, **SLUS-00726**, dumped as `.cue`/`.bin` files.
- A keyboard or an SDL-compatible game controller. In Steam Game Mode, keep Steam Input enabled for the shortcut.

The package includes the required SDL2, OpenAL, libcrypto and zlib shared
libraries. Keep its `lib/` folder with the executable. No BIOS is needed.

## Download, install and run on Linux

1. Download `bfm-r38-61c60516d-linux-x86_64.tar.gz` and `SHA256SUMS` from the
   release above into the same directory.
2. Check and unpack them:

   ```sh
   sha256sum -c SHA256SUMS
   mkdir -p "$HOME/Games/bfm-port"
   tar -xzf bfm-r38-61c60516d-linux-x86_64.tar.gz -C "$HOME/Games/bfm-port"
   ```

3. Keep your own `.cue` file and all referenced `.bin` tracks together in
   `~/Games/brave-fencer-musashi/disc/`, or in a `disc/` directory beside
   `launch.sh`. One-bin and per-track dumps are supported. A 2048-byte `.iso`
   is unsupported because it lacks the required CD-XA data. The normal disc
   route checks the combined raw track bytes and the USA boot executable;
   other regions and modified dumps are rejected.
4. Run:

   ```sh
   "$HOME/Games/bfm-port/bfm-r38-61c60516d/launch.sh"
   ```

If the disc is elsewhere, set its path explicitly:

```sh
BFM_DISC="/path/to/your/game.cue" "$HOME/Games/bfm-port/bfm-r38-61c60516d/launch.sh"
```

A desktop file picker is offered when no disc is found and `zenity` or
`kdialog` is available. Otherwise use `BFM_DISC`. The wrapper also searches
`BFM_DISC_DIR`, its adjacent `disc/` directory, the folder above, and the
existing Banshee library's `games/brave-fencer-musashi/disc/` folder. It
remembers the selected file in the configuration directory listed below.

For Steam Deck/Game Mode, add this `launch.sh` as a non-Steam game from
Desktop Mode. The launcher chooses the optimized x86-64-v3 executable when
the CPU supports it. `BFM_BASELINE=1 ./launch.sh` selects the baseline build.
Hardware performance and audio fidelity vary; the headless regression gate
is not an audio or handheld certification.

Install new versions in separate folders. Finish playing and quit normally
before backing up a card or choosing another build. Installing this package
does not require replacing your disc, saves or settings.

The supported raw disc stream has **416,021,760 bytes** and SHA-256
`0a53702937d74e20d99fee9a29a80f7e91da762e66958819d531173939a50879`.
For a per-track dump, this applies to the concatenated track bytes, not to
the cue text. A cue may differ in filenames without changing the disc data.

## Controls

The port follows the controller you use. Button names below use the SDL/Xbox
layout; other controllers may print different symbols.

| PlayStation | Controller | Keyboard |
|---|---|---|
| D-pad | D-pad or left stick | Arrows or WASD |
| Cross | A | C or K |
| Circle | B | V or I |
| Square | X | X or J |
| Triangle | Y | Z or U |
| Start | Start | Enter |
| Select | Back/Select | Space |
| L1 / R1 | LB / RB | Left Shift / Right Shift |
| L2 / R2 | LT / RT | Left Ctrl / Right Ctrl |

The packaged dev menu is enabled by default: **F8** or **Select+Start** on
one controller opens it. Menu navigation uses **Up/Down**, **Enter**, **Esc**,
or the controller's D-pad/left stick, **A**, **B**. The game keeps running
while this menu is open. See the [Dev Menu wiki](https://github.com/Blizz127/brave-fencer-musashi-decomp-port/wiki/Dev-Menu)
for all options, shortcuts and save precautions.

## Saves, settings and files on Linux

| File | Default location |
|---|---|
| Virtual memory card, port 0 | `~/.local/state/bfm-port/bfm_card0.mcd` |
| Last run log | `~/.local/state/bfm-port/last-run.log` |
| Local route journal | `~/.local/state/bfm-port/route.jsonl` |
| Remembered disc path | `~/.config/bfm-port/disc` |
| Disable-menu marker | `~/.config/bfm-port/dev-menu-off` |
| Locally extracted boot executable cache | `~/.cache/bfm-port/SLUS_007.26` |
| Dev-menu PNG screenshots | `~/Pictures/bfm-port/` |

`XDG_STATE_HOME`, `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` replace the respective
base directories. An exported `XDG_PICTURES_DIR` selects the screenshots'
base directory. Files created from your disc, cards and screenshots remain
local; do not upload them to this repository or wiki.

The game uses a 128 KiB virtual card and its ordinary save/Continue menus,
not emulator save states. An isolated inn save and cold Continue passed for
this runtime; every save location and later-game state are not yet verified.
Keep a backup of `bfm_card0.mcd`, especially before cheats or warps. To use a
separate card directory, start a later session with:

```sh
BFM_SAVE_DIR="$HOME/Games/bfm-test-saves" ./launch.sh
```

This directory contains a different card; an empty directory starts a fresh
one. Do not point simultaneous game processes at the same card. The override
has priority over `XDG_STATE_HOME` and the normal default.

**Configuration limitation:** source APIs refer to
`~/.config/bfm-port/config.ini` (or `BFM_PORT_CONFIG`), but r38 does not
reliably load that file in its production startup. Do not rely on it to move
saves or change graphics/audio settings. Use the documented launcher
environment variables; disc selection and the menu-off marker work through
the launcher. The release uses Original rendering; general configuration
and enhanced rendering options are not promised here.

## Windows status

**There is no Windows game package for r38 yet.** The Linux archive is not a
Windows executable. No Windows download filename, installer, runnable `.exe`,
minimum Windows version or verified Wine/Proton procedure is available to
recommend at this checkpoint.

Windows remains requested for this same release. Current evidence is limited
to cross-compiling the platform-name adapter and detecting the C/C++
compiler. Full game configuration stops at missing Windows SDL2 development
files. The production runtime still needs Windows memory mapping, fault and
filesystem handling, Windows native-code generation and target libraries
(SDL2, OpenAL, OpenSSL Crypto and zlib). Installing a compiler alone does not
produce a working game package.

You can prepare a lawful `.cue`/`.bin` dump of your own USA disc now: keep
the cue and every referenced track together in a folder you control, retaining
the filenames referenced by the cue. No Windows automatic discovery or
installation directory has been verified. The Windows install/run steps,
prerequisites and package checksum will be added here after a complete build
qualifies; Linux instructions above remain the available supported route.

For developers, the source intends `%APPDATA%\bfm-port\bfm_card0.mcd` for
cards and `%APPDATA%\bfm-port\config.ini` for configuration, with explicit
save/config overrides. These are **unverified source conventions**, not
working Windows release instructions. No Windows save migration, controller,
audio or gameplay compatibility claim is made.

## Troubleshooting

| Symptom | Action |
|---|---|
| No disc found / no file picker | Keep cue and tracks together; set `BFM_DISC` to the cue's absolute path. |
| Wrong disc / boot executable rejected | Use an unmodified USA SLUS-00726 dump. Check the log and referenced track filenames; an ISO or different region is unsupported. |
| `GLIBC_2.38` or `GLIBCXX_3.4.32` missing | Use a system meeting the libc/C++ runtime requirements; the baseline CPU option does not lower these library requirements. |
| Missing shared library | Keep `lib/` beside the executable and start with `launch.sh`; check desktop graphics/audio dependencies. |
| Controller ignored in Game Mode | Enable Steam Input for this shortcut and press a button on the controller you want to use. Include controller details from the log in a bug report. |
| Dev menu will not open | Use F8 or Back/Select+Start on the same controller; check `BFM_DEV_MENU`, the menu-off marker and `BFM_CHEATS=0`. F1 is help, not the open key. |
| Save directory unavailable / unreadable card | Check the logged card directory and permissions. Back up the card before investigation; the runtime refuses to replace an unreadable existing card. |
| Game stops on unsupported code | Record the build, room/actions and refusal address. Attract-mode refusal `800495ec` is known; whole-game coverage remains incomplete. |
| Windows download missing | Windows qualification is pending; see the status section above. |

For reports, include the release tag and relevant lines from `last-run.log`
after reviewing it for personal paths or other details you do not want to
share. Keep disc files, extracted data, memory cards and game screenshots
out of the public repository and wiki.
