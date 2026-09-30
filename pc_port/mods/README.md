# Mods

The port loads mods from each directory in the `[mods] dirs` config key
(default `mods`, next to the binary). The loader is
`pc_port/platform/bfm_plat_mods.c`; the plugin SDK is
`pc_port/platform/bfm_plugin.h`. See `docs/ARCHITECTURE-PORT.md` for the
full contract.

```
mods/<folder>/mod.ini          [mod] name, version, priority, enabled, plugin, script
mods/<folder>/<plugin>.so      C plugin (.dll on Windows), exports bfm_plugin_init
mods/<folder>/cheats.ini       [cheat NAME] address, value, width, description, enabled
mods/<folder>/assets/textures/<hash16>.bfmi   VRAM upload replacement
mods/<folder>/assets/files/<hash16>.<ext>     disc file replacement
```

Replacement assets are keyed by the FNV-1a 64 hash of the original data, so
a mod never contains or needs the original bytes. Set `[mods] dump_textures = 1`
to have the port write the hashes (and your own disc's textures) to
`dump_dir` on your machine for making replacements. Don't redistribute those
dumps.

`examples/lua_hello` is the same sample in Lua (builds with
`BFM_PLAT_WITH_LUA`, after `tools/fetch_lua.sh`).

`examples/hello` is a sample mod: frame counter, room-enter log, a
`turbo_cross` cheat and a `hello` console command. Build it with
`examples/build_example.sh`.
