# Modding the Brave Fencer Musashi PC port

This guide is for people writing mods: cheats, texture packs, scripts and C
plugins. The port's internals are in [ARCHITECTURE-PORT.md](ARCHITECTURE-PORT.md).
Everything here is also in the headers under `pc_port/platform/`, which are
the reference.

Three sample mods ship with the port and are the worked examples:

| Sample | Shows |
|---|---|
| [`examples/cheat_pack`](../pc_port/mods/examples/cheat_pack) | cheats.ini only: poke and copy cheats on the known state addresses |
| [`examples/hello`](../pc_port/mods/examples/hello) | a C plugin: events, a callback cheat that edits input, a console command, config |
| [`examples/lua_hello`](../pc_port/mods/examples/lua_hello) | the same plugin written in Lua |

**Ground rules**
- Mods must not contain anything from the game disc: no textures, models,
  audio, text or executable bytes. Replacements are keyed by a *hash* of
  the original data, so a mod never needs to include the original.
- Texture dumps you make from your own disc are for making replacements
  on your own machine. Don't redistribute them.
- Addresses in this guide are for the US release (SLUS-00726), the only
  one the port runs.

## 1. Installing and managing mods

- Mods live in the directories listed in the user config's `[mods] dirs`,
  `;`-separated. The default is `mods` next to the binary. The config
  file is `~/.config/bfm-port/config.ini` (Linux), `%APPDATA%\bfm-port\config.ini`
  (Windows), or `$BFM_PORT_CONFIG`.
- `--no-mods` or `[mods] enabled = 0` turns all mods off.
- In-game: the console (default key `` ` ``) runs `mods`, `cheats` and
  `cheat NAME on|off`. The cheat menu is on F1.

## 2. A mod folder

```
mods/my_mod/
  mod.ini                 required
  cheats.ini              optional: data-only cheats
  my_plugin.so            optional: C plugin (.dll on Windows, .dylib on macOS)
  main.lua                optional: Lua script (builds with Lua support)
  assets/textures/<hash16>.bfmi|.png   optional: texture replacements
  assets/files/<hash16>.<any>          optional: disc-file replacements
```

`mod.ini`:

```ini
[mod]
name = my_mod            ; unique; defaults to the folder name
version = 1.0.0
priority = 100           ; load order: lower first, then folder name; later mods win asset conflicts
enabled = 1
plugin = my_plugin       ; file name without extension, in this folder
script = main.lua        ; a file in this folder
description = What it does
```

A folder without `mod.ini` is ignored. A second mod with the same name is
ignored, with a log line. `plugin` and `script` must be plain file names in
the mod folder.

Per-mod settings go in the user config under `[mod.<name>]`. Plugins read
them with `config_get("mod.<name>.key")`, and Lua with `bfm.config(...)`.

## 3. Cheats without code: cheats.ini

Each `[cheat NAME]` section is one cheat. Enabled cheats run once per
game frame, at `frame_end`.

```ini
[cheat max_money]
description = Money held at 99999
address = 0x80078E8C     ; guest address (0x80xxxxxx)
value = 99999
width = 4                ; bytes: 1, 2 or 4 (little endian)
enabled = 0              ; start switched off (recommended)

[cheat infinite_hp]
description = HP stays at its maximum
address = 0x80078EB4     ; destination
copy_from = 0x80078EB2   ; copy `width` bytes from here each frame
width = 2
enabled = 0
```

- A **poke cheat** writes `value` each frame.
- A **copy cheat** copies from `copy_from` each frame, for "current = max"
  style cheats.
- Names can't contain spaces. A cheat with a bad width is skipped, with a
  log line.

[`examples/cheat_pack/cheats.ini`](../pc_port/mods/examples/cheat_pack/cheats.ini)
has infinite HP/BP, max money and max HP gauge, all off by default. The
addresses come from the table in section 8.

## 4. Replacing textures and files

### Keys

Every replacement is looked up by the **FNV-1a 64-bit hash** of the
original data, written as 16 lowercase hex digits.

- **Textures:** each VRAM upload the game makes is hashed over:
  `width` (le16), `height` (le16), then `width*height` 16-bit pixels (le16).
- **Files:** a file the port reads from the disc by name is hashed over its
  whole contents.

```python
def fnv1a64(data: bytes) -> str:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h
```

### Finding texture hashes: dump your own

Set this in the user config, then play through the scenes you want to
change:

```ini
[mods]
dump_textures = 1
dump_dir = dump
```

Every distinct upload is written once to `dump/textures/<hash>.bfmi` (see
the format below), with the hash as the file name. Convert a dump to PNG,
edit it, and save it under the same hash in your mod.

### Replacement formats

- `.bfmi`: the port's raw format. `"BFMI"`, then le16 width, le16 height,
  then width×height RGBA8888 pixels. Dumps use it.
- `.png`: when the port is built with PNG support (the default where zlib
  is available). Non-interlaced 8/16-bit grey, RGB, palette (with tRNS),
  grey+alpha or RGBA.
- Size: the original size, or an integer multiple of it (2×, 4×…) for HD.
  A same-size replacement works on every renderer. HD needs a renderer
  with an HD path. On one without, the port keeps the original. A
  mismatched size is ignored, with a log line.
- Transparency: alpha 0 becomes the PS1's transparent colour (0x0000).
  Opaque black keeps the PS1 "semi-transparency" bit, so it stays visible.

HD replacements on the `gl` renderer (`[backends] renderer = gl`):
- The image stands for the rect's **decoded texels**. A 4-bit texture
  uploaded as a 16x64 VRAM rect is 64x64 texels, so draw it as a 64x64
  picture or any multiple of that. The port samples it in coordinates
  normalised to the rect, so any size works, and bilinear filtering
  smooths it.
- Alpha below 50% is transparent, and vertex colour still tints the image,
  as on the PS1.
- The palette is ignored. If the game draws one texture with several
  CLUTs, every palette shows your image.
- The dump (`.bfmi`) holds the upload's raw 16-bit words. For 4-bit and
  8-bit textures that is index data, not a picture, so decode it with the
  palette you see in game before editing.
- When the game re-uploads different data to that rect, the replacement
  is dropped until the hashed upload comes back.

### File replacements

Put the replacement at `assets/files/<hash-of-the-original-file>.<any
extension>`. When the port reads that disc file by name, it serves your
bytes instead. Hash a file you extracted from your own disc with the
snippet above.

When two mods replace the same asset, the one later in load order wins.

## 5. C plugins

### Build

A plugin includes only the SDK headers, which are shipped in the package
under `include/`: `bfm_plugin.h`, `bfm_plat_types.h`, `bfm_plat_input.h`,
`bfm_plat_config.h`.

```sh
cc -std=c99 -shared -fPIC -I include my_plugin.c -o mods/my_mod/my_plugin.so
```

`pc_port/mods/examples/build_example.sh` builds the `hello` sample this way.

### Entry points

```c
#include "bfm_plugin.h"

BFM_PLUGIN_EXPORT int bfm_plugin_init(const BfmPluginHost *host, BfmPluginInfo *info) {
    if (host->abi_version < 1) return -1;
    info->abi_version = BFM_PLUGIN_ABI_VERSION;   /* the version you built against */
    info->name = "my_mod";
    info->version = "1.0.0";
    /* subscribe, register cheats/commands here */
    return 0;                                     /* non-zero = don't load me */
}

BFM_PLUGIN_EXPORT void bfm_plugin_shutdown(void) { /* optional */ }
```

- Keep `host`: it stays valid until shutdown.
- Register cheats and console commands inside `bfm_plugin_init`. They
  belong to your mod and are removed when it unloads.
- Subscriptions and watches can be made at any time. Everything a mod made
  is dropped when mods shut down, so an unloaded plugin is never called.

### ABI versioning rules

- `BFM_PLUGIN_ABI_VERSION` is the host's version. `host->abi_version` tells
  you what the running port provides, and `host->size` is
  `sizeof(BfmPluginHost)` on the host side.
- Fields are **only ever appended**: to `BfmPluginHost`, and to event
  payload structs. Event numbers never change; new events are appended.
- Before using a field added in version N, check `host->abi_version >= N`.
- A plugin whose `info->abi_version` is newer than the host's is refused.
  A plugin built against an older ABI keeps working.

| ABI | Added |
|---|---|
| 1 | `log`, `subscribe`/`unsubscribe`, `guest_read`/`guest_write`, `register_cheat`/`set_cheat`, `register_command`, `config_get`; events 0–14 |
| 2 | `watch_add`/`watch_remove`; events 15–19 (`projection`, `damage`, `bp_use`, `money`, `item_use`); payload fields `BfmEventItem.kind/.flags`, `BfmEventRoom.location`; `BfmEventStat`, `BfmEventProjection` |

### Host services

| Field | Use |
|---|---|
| `mod_name`, `mod_dir` | your mod's name and folder (for your own files) |
| `log(mod, level, msg)` | 0 error, 1 warning, 2 info |
| `subscribe(event, fn, user)` → id | `fn(user, event, payload)`; returns an id > 0 |
| `unsubscribe(id)` | |
| `guest_read(addr, out, n)` / `guest_write(addr, data, n)` | guest RAM by address; `BFM_PLAT_NOT_READY` until the game runs |
| `register_cheat(&cheat)` | `BfmCheat`: a poke (`address/value/width`) or `apply(user)` each frame, plus optional `on_toggle(user, on)` |
| `set_cheat(name, on)` | |
| `register_command(name, help, fn, user)` | `fn(user, argc, argv, out, out_size)`; write the reply into `out`, return 0 for success |
| `config_get("section.key")` | the user config, or NULL |
| `watch_add(addr, len, fn, user)` → id (ABI 2) | `fn(user, addr, len, old, new)` when those bytes change (≤ 64 bytes) |
| `watch_remove(id)` (ABI 2) | |

All functions return `BFM_PLAT_OK` (0) or a negative `BfmPlatResult`.

### Events

| # | Name | Payload | Source |
|---|---|---|---|
| 0 | `boot` | – | all mods loaded |
| 1 | `shutdown` | – | before mods unload |
| 2 | `frame_begin` | – | each game frame, after input |
| 3 | `frame_end` | – | each game frame (cheats run here) |
| 4 | `vsync` | – | each video field waited |
| 5 | `input` | `BfmEventInput` (pads and hotkeys, **editable**) | each poll |
| 6/7 | `before_present` / `after_present` | – | around each presented frame |
| 8/9 | `room_enter` / `room_exit` | `BfmEventRoom {area, room, location}` | code sites (enter: medium confidence; exit: low) |
| 10/11 | `battle_start` / `battle_end` | `BfmEventBattle` | **not wired yet** |
| 12 | `item_get` | `BfmEventItem {item, count, kind, flags}` | code site (inventory add) and derived (inventory slots, toy-shop figures) |
| 13/14 | `save` / `load` | `BfmEventSave {slot}` | code site on the memory-card state machine (medium) |
| 15 | `projection` | `BfmEventProjection {h, ofx, ofy}` | the game's GTE projection setters |
| 16 | `damage` | `BfmEventStat {value, previous, max, flags}` | code site (lethal HP subtract) and derived (any HP drop) |
| 17 | `bp_use` | `BfmEventStat` | code site and derived |
| 18 | `money` | `BfmEventStat` | derived |
| 19 | `item_use` | `BfmEventItem` | code site (item effect handler) |

- **Derived events** come from a change in memory rather than a code site,
  and have `BFM_EVENT_FLAG_DERIVED` in `flags`. Each change is reported
  once: when a code site already reported it, the derived event is
  suppressed.
- **`kind`** is `BFM_ITEM_KIND_INVENTORY` (item = id 1..119) or
  `BFM_ITEM_KIND_FIGURE` (item = figure index 0..42).
- Event names are also listed in the console (`events`).

### Worked example: `examples/hello`

[`hello_plugin.c`](../pc_port/mods/examples/hello/hello_plugin.c):
- counts `frame_end`
- logs `room_enter`
- registers a callback cheat `turbo_cross`. Its `on_toggle` flips a
  flag, and an `input` hook clears Cross on odd frames while the flag is on.
- registers a `hello` console command that reads
  `mod.hello.greeting` from the config

## 6. Lua scripts

Available when the port is built with Lua (5.4, fetched by
`tools/fetch_lua.sh`). Name the script in `mod.ini` (`script = main.lua`).
Each mod gets its own Lua state.

**Sandbox**
- Available: base (without `dofile`, `loadfile`, `load`, `require`,
  `collectgarbage`), `table`, `string`, `math`, `utf8`, `coroutine`.
- Not available: `io`, `os`, `package`, `debug`. Only text chunks load;
  precompiled bytecode doesn't.
- Every load and every callback runs under a budget of 5 million
  instructions. A script that exceeds it errors out, is logged, and the
  game continues.
- `bfm.cheat` and `bfm.command` work only while the script's main chunk
  runs (at load).

**API** (global `bfm`)

| Call | Notes |
|---|---|
| `bfm.mod_name`, `bfm.mod_dir`, `bfm.abi_version` | |
| `bfm.log(msg [, level])` | |
| `id = bfm.on(event, fn)` | event by name (`"frame_end"`) or number; `fn(event_name, payload)` |
| `bfm.off(id)` | |
| `bfm.read8/16/32(addr)`, `bfm.write8/16/32(addr, v)` | little endian; read returns nil if memory isn't available yet |
| `s = bfm.read(addr, n)`, `bfm.write(addr, s)` | byte strings (n ≤ 65536) |
| `bfm.cheat{name=, description=, address=, value=, width=}` | poke cheat |
| `bfm.cheat{name=, description=, apply=fn, on_toggle=fn(on)}` | callback cheat |
| `bfm.set_cheat(name, on)` | |
| `bfm.command(name, help, fn(...))` | `fn` gets the arguments as strings and returns the reply string |
| `bfm.config(key)` | string or nil |
| `id = bfm.watch(addr, len, fn(addr, old, new))`, `bfm.unwatch(id)` | `old`/`new` are byte strings |
| `bfm.PAD_CROSS` … `bfm.HOTKEY_PAUSE` … | button and hotkey masks |

**Payload tables:**
- `input`: `{pads = {{buttons, lx, ly, rx, ry, connected, analog}, …},
  hotkeys}`. Assigning to `buttons`, the sticks or `hotkeys` changes what
  the game sees.
- room: `area`, `room`, `location`
- item: `item`, `count`, `kind`, `derived`
- stat: `value`, `previous`, `max`, `derived`
- save: `slot`
- projection: `h`, `ofx`, `ofy`

Worked example: [`examples/lua_hello/main.lua`](../pc_port/mods/examples/lua_hello/main.lua)
does the same things as the C `hello` sample.

## 7. Console commands

| Command | |
|---|---|
| `help` | list commands (plugins add their own) |
| `mods` | loaded mods |
| `cheats`, `cheat NAME on\|off\|toggle` | |
| `peek ADDR [N]`, `poke ADDR VALUE [1\|2\|4]` | guest RAM (N ≤ 64) |
| `get SECTION.KEY`, `set SECTION.KEY VALUE` | runtime config |
| `ff [SPEED]` | fast-forward toggle / speed |
| `events` | event counts and registered hook sites |
| `pause`, `screenshot [PATH.png]`, `quit` | |

In the console: Enter runs a line, Up/Down browse history, Tab completes a
command name, PageUp/PageDown scroll, Esc closes. The game gets no input
while the console is open.

## 8. Known game state (US, SLUS-00726)

Found statically from the decompilation. The evidence for each row, down
to file and line, is in ARCHITECTURE-PORT.md ("Live game state") and is
checked by the test suite. Confidence is about the address and type; a
"label" note means the meaning of the value is inferred.

| What | Address | Type | Confidence |
|---|---|---|---|
| Player/progress struct | `0x80078E78` | 0x98 bytes (holds most rows below; saved with the game) | high |
| Money | `0x80078E8C` | s32, 0..99999 | high |
| HP current / max | `0x80078EB4` / `0x80078EB2` | s16 / u16 (max ≤ 500) | address high; "HP" label medium |
| BP current / max | `0x80078EB8` / `0x80078EB6` | s16 / u16 (max ≤ 0x662) | address high; "BP" label medium |
| Play time | `0x80078E7C` | 4 × u8: frames (÷30), s, min, h | high |
| Day | `0x80078EAC` | u16 | high |
| Time of day | `0x80078EB1` hour, `0x80078EB0` minute | u8 | high |
| Location | `0x800B9A08` | s16 (`currentLocationId`) | high (id meaning: medium) |
| Story flags | `0x800AE648` | 512 bits (flag n = byte n/8, bit n%8) | high |
| Script variables | `0x800BA1B8` | 256 bytes | high |
| Inventory slots | `0x800BA1E7`..`0x800BA1F2` (vars 0x2F..0x3A) | u8 item id, 0 = empty | high |
| Inventory stamps | vars `0x14 + 2*slot` (`0x800BA1CC`..) | u16 day acquired (items spoil/mature by days) | high |
| Toy-shop figures | vars `0x63`..`0x8D` (`0x800BA21B`..) | bit 0x40 owned, 0x80 in shop, low nibble price trend | high |
| Figure prices | `0x800A6588` | u16[64] | high |
| NPC timestamps | `0x800BA2B8` | 24 × {?, s16 day*24+hour} | medium |

Not known yet: equipment and key items, and battle boundaries (boss
fights). Writing to state the game doesn't expect (e.g. HP above its max)
can break things; the cheat pack stays inside the game's own limits.

## 9. Tips

- **Watches or events?** Prefer events: they come from code sites and carry
  meaning. Use `watch_add`/`bfm.watch` for memory the port has no event
  for.
- **Timing:** until the native lane reports every guest store, watches and
  derived events are checked once per frame. Several changes within one
  frame then arrive as one.
- **Testing without the game:** the port runs headless with the `null`
  backends. The test suite (`tests/test_bfm_plat.py`) drives plugins and
  scripts against simulated guest memory; the probes there show how.
