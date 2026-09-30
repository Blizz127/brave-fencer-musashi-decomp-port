-- Sample Lua mod for the Brave Fencer Musashi port.
-- Everything goes through the `bfm` table (see pc_port/platform/backends/script_lua.c).

local frames, rooms = 0, 0
local turbo = false

bfm.on("frame_end", function()
  frames = frames + 1
end)

bfm.on("room_enter", function(_, room)
  rooms = rooms + 1
  bfm.log(string.format("entered area %d room %d", room.area, room.room))
end)

-- Turbo: while the cheat is on, a held Cross is released on odd frames.
bfm.on("input", function(_, input)
  local pad = input.pads[1]
  if turbo and (pad.buttons & bfm.PAD_CROSS) ~= 0 and frames % 2 == 1 then
    pad.buttons = pad.buttons & ~bfm.PAD_CROSS
  end
end)

bfm.cheat{
  name = "lua_turbo",
  description = "Hold Cross to auto-fire it (Lua)",
  apply = function() end,
  on_toggle = function(on) turbo = on end,
}

bfm.command("lua_hello", "lua_hello - sample Lua mod status", function(...)
  local greeting = bfm.config("mod.lua_hello.greeting") or "hello from lua"
  local extra = select("#", ...) > 0 and (" " .. table.concat({...}, ",")) or ""
  return string.format("%s: %d frames, %d rooms%s", greeting, frames, rooms, extra)
end)

bfm.log("lua_hello loaded (abi " .. bfm.abi_version .. ")")
