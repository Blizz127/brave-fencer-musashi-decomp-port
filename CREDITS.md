# Credits

## The Brave Fencer Musashi decompilation

This port builds on the matching decompilation of *Brave Fencer Musashi*
(SLUS-00726) by **Druthulu (Drew T)**:
[Druthulu/BFM-decomp](https://github.com/Druthulu/BFM-decomp). That project
matched the main executable, the resident engine, every location overlay and
every streamed code module byte for byte, and its sources, symbol layout and
splat configuration were the reference this project's function boundaries,
overlay maps and recovered C were checked against. All commits in the
upstream history are by Druthulu; the upstream README also thanks the
sotn-decomp project, the Xenogears, Vagrant Story and Tomba! decompilations,
redump, TCRF, gamehacking.org and the Brave Fencer Musashi Archipelago world
(AegeusEvander).

Upstream licensing: its `tools/` and `docs/` are **AGPL-3.0**. Its
reimplemented game code (`src/`) asserts **no license** (see its
`src/NOTICE.md`), so it is not licensed for redistribution. **None of the
upstream code, and no file derived from it, is included here** (see
[EXCLUDED.md](EXCLUDED.md)). Config comments that name an upstream path, for
example `vendor/bfm-decomp/src/ov_SC01_000`, are attributions and not copies.
**Names:** the symbol and type names and the function boundaries in
`config/`, where they come from the upstream symbol files and splat layout,
are Druthulu's research. They are used here with thanks and credit.
The upstream work remains its author's. This project is re-deriving the game
code independently, from the game's own instructions checked against the
retail bytes. The release binary's native lane is built with
`MUSASHI_LANE_OWN_CODE_ONLY`, so it compiles only code written for this
project.

## Port, tools and documentation

Written by [Blizz127](https://github.com/Blizz127), with AI coding assistants
(Claude and Codex) under the owner's direction. The port follows the same
architecture as the owner's sibling ports
([xenogears-decomp-port](https://github.com/Blizz127/xenogears-decomp-port)
and a Parasite Eve port); no code from those projects' upstream
decompilations is included here.

## Libraries used by the port

None of these is included in this repository. Each one is fetched or
installed by the user. The release bundle ships SDL2, OpenAL Soft (dynamically
linked), libcrypto and zlib in `lib/`, with their license texts in
`licenses/`, and links PsyCross statically.

| Component | Use | License |
|---|---|---|
| [PsyCross](https://github.com/OpenDriver2/PsyCross) (REDRIVER2 Project) | Psy-Q API reimplementation on SDL2/OpenAL/OpenGL; fetched at the pinned commit `e56e4cd` by `tools/fetch_toolchains.sh` and patched with `tools/patches/psycross-local.patch` (the port's local edits, MIT like PsyCross). `pc_port/spu_voice_core.h` adapts its ADSR step machine and carries its MIT notice. | MIT |
| [SDL2](https://github.com/libsdl-org/SDL) | Window, input, audio device | zlib |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | Audio (PsyCross and the OpenAL backend) | LGPL-2.1-or-later (dynamic link) |
| OpenGL / Khronos headers (`GL/glcorearb.h`) | GL renderer backend | MIT-style (Khronos) |
| [zlib](https://zlib.net/) | PNG texture replacements (optional) | zlib |
| [OpenSSL](https://www.openssl.org/) (libcrypto) | SHA-256 verification of the user's files | Apache-2.0 |
| [Lua](https://www.lua.org/) 5.4 | Optional mod script runtime; fetched and hash-checked by `tools/fetch_lua.sh` | MIT |

## Tools

None of these is redistributed in this repository.

| Tool | Use | License |
|---|---|---|
| [splat](https://github.com/ethteck/splat) / [spimdisasm](https://github.com/Decompollaborate/spimdisasm) | Splitting and disassembling the user's own executable and overlays (`config/*.yaml`) | MIT |
| [maspsx](https://github.com/mkst/maspsx) (mkst) | ASPSX-compatible assembler wrapper for the matching build | MIT |
| [m2c](https://github.com/matt-kempster/m2c) (Matt Kempster and contributors) | Initial C drafts | GPL-3.0 |
| [old-gcc](https://github.com/decompals/old-gcc) (decompals) | GCC 2.6/2.7/2.8 PSX cross compilers used as matching candidates, fetched by `tools/fetch_toolchains.sh` | GPL (GCC) |
| [mkpsxiso](https://github.com/Lameguy64/mkpsxiso) (Lameguy64) | `dumpsxiso`, extracting the user's disc | GPL-2.0 |
| [MAME](https://github.com/mamedev/mame) `chdman` | Converting the user's CHD to BIN/CUE | BSD-3-Clause / GPL-2.0 |
| GNU binutils (`mips-linux-gnu-*`) | Assembling and linking the matching build | GPL-3.0 |
| [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) | Runtime reference emulator driven by the `tools/retail_*_observe.lua` scripts | GPL-2.0 |

## References

- [PSX-SPX](https://psx-spx.consoledev.net/) (Martin Korth's nocash PSX
  specifications and their maintainers): hardware behaviour. The SPU Gaussian
  interpolation table and reverb FIR taps in `pc_port/spu_voice_core.h` and
  `pc_port/spu_reverb_core.h` are the hardware constants documented there.
- [DuckStation](https://github.com/stenzek/duckstation) (Stenzek): consulted
  for the ordering of some hardware behaviours, cited in comments. No
  DuckStation code is copied.
- The Xenogears and Parasite Eve decompilations: some Sony-library functions
  in this project's unpublished matching tree were identified against their
  Psy-Q sources. None of that code is included here. The Sony library ranges
  in `config/psyq_ranges.txt` (addresses and segment names only) were derived
  from the upstream BFM-decomp splat layout.
