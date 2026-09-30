# Notice

## Ownership

*Brave Fencer Musashi* (PlayStation, 1998; USA release SLUS-00726) and all of
its code, data, artwork, music, sound, text and trademarks are the property of
Square Enix Co., Ltd. (originally developed and published by Square Co., Ltd.).
This project is not affiliated with, endorsed by or sponsored by Square Enix or
Sony Interactive Entertainment. The PlayStation hardware, BIOS and Psy-Q SDK
are the property of Sony Interactive Entertainment (formerly Sony Computer
Entertainment). "PlayStation" is a trademark of Sony Interactive
Entertainment.

## What this repository contains

Only the owner's original work: the native PC port and its host/platform
layer, analysis and build tools, scripts, tests, split/symbol configuration
(addresses, sizes, names and hashes only), provenance metadata and
documentation. The MIT license in [LICENSE](LICENSE) applies to that original
work only.

This repository does **not** contain:

- any game content: no disc image, no executable, no file extracted from the
  disc, no disassembly, no audio, textures, movies, models, text or other
  assets, and no tables of retail bytes or words;
- any BIOS image or bytes from one;
- any Sony Psy-Q SDK code, headers, libraries or tools, and no decompiled Sony
  library code;
- the decompiled game code (`src/`), or any file derived from the upstream
  decompilation [Druthulu/BFM-decomp](https://github.com/Druthulu/BFM-decomp).
  Its tools and documentation are AGPL-3.0, and its decompiled game code
  (`src/`) asserts no license, so it is not licensed for redistribution.
  None of it is included. It is credited in [CREDITS.md](CREDITS.md) as the
  reference this work was checked against. The symbol and type names that
  its research established, and which appear in `config/` and in comments,
  are used with credit to Druthulu/BFM-decomp. The owner is re-deriving the
  game code independently, from the game's own instructions checked against
  the retail bytes;
- prebuilt third-party binaries or third-party source trees.

Every excluded path and the reason for it is listed in
[EXCLUDED.md](EXCLUDED.md). Before publishing, every file was checked against
the retail executable, every file on the disc, every locally decoded overlay,
and the BIOS image; against Sony/Psy-Q markers; and for secrets and personal
data. The commands and results are in [SCANS.md](SCANS.md). The only matches
of retail content are the documented address/metadata class (addresses,
sizes, offsets and digests: the same facts a symbol map records), not game
data.

## Your own disc

To run anything built from this code you need your own legally obtained copy
of *Brave Fencer Musashi* (USA, SLUS-00726). The port reads all game code and
data at runtime from files you extract from that disc; its tools verify your
files by SHA-256 and never write retail bytes into the source tree. Nothing
here lets you play the game without it. Do not share disc images, extracted
files, BIOS images, save files or screenshots of game content through this
repository.

## Purpose

The port and its tooling are made for preservation, research and
interoperability: keeping the game playable on current hardware for people who
own it.

## Third-party components

Third-party components keep their own licenses and are fetched by the user,
not redistributed here; see [CREDITS.md](CREDITS.md).
