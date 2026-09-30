-- Retail ground truth for the opening of the game: boot -> title -> first
-- playable scene. Runs a normal PCSX-Redux disc boot and, at every VSync,
-- records the scene/mode words and the player struct fields the native port
-- has been writing by hand, so the port can be compared against what retail
-- does on its own. Screenshots and whole-RAM images are taken on a schedule.
--
-- The observer changes no guest RAM, registers or MMIO. Its only stimulus is
-- the optional pad script, delivered through the emulated controller exactly
-- as a player's button presses would be.
--
-- Environment:
--   MUSASHI_L1_DIR     output directory (required)
--   MUSASHI_L1_FRAMES  VSyncs to run before quitting (default 3600)
--   MUSASHI_L1_EVERY   sample/screenshot period in VSyncs (default 30)
--   MUSASHI_L1_RAM     comma list of VSyncs at which to dump 2 MiB RAM
--   MUSASHI_L1_PAD     comma list of frame:button:length presses, e.g.
--                      "600:START:4,900:CROSS:4". Buttons are the PCSX names.
--   MUSASHI_L1_ON      comma list of state-triggered presses, fired once each
--                      in order: SCENE/A3B4+DELAY:BUTTON:LENGTH, hex state,
--                      e.g. "0002/0005+30:START:4". Retail load timing varies
--                      run to run, so presses keyed to the state the game is
--                      actually in reproduce where fixed frames do not.
--   MUSASHI_L1_REPEAT  SCENE:BUTTON:PERIOD, hex scene; while that scene is
--                      live, press BUTTON for 4 VSyncs every PERIOD VSyncs
--                      (advances dialogue the way a player mashing it would).
local ffi = require('ffi')

local out_dir = assert(os.getenv('MUSASHI_L1_DIR'), 'MUSASHI_L1_DIR required')
local total = tonumber(os.getenv('MUSASHI_L1_FRAMES') or '3600')
local every = tonumber(os.getenv('MUSASHI_L1_EVERY') or '30')

local BUTTON = {
    SELECT = 0, L3 = 1, R3 = 2, START = 3, UP = 4, RIGHT = 5, DOWN = 6, LEFT = 7,
    L2 = 8, R2 = 9, L1 = 10, R1 = 11, TRIANGLE = 12, CIRCLE = 13, CROSS = 14, SQUARE = 15,
}

local ram_frames = {}
for f in string.gmatch(os.getenv('MUSASHI_L1_RAM') or '', '[^,]+') do
    ram_frames[tonumber(f)] = true
end

-- press[frame] = list of {button, down}; a press of length n is a down edge at
-- its frame and an up edge n frames later.
local edges = {}
local function add_edge(frame, button, down)
    edges[frame] = edges[frame] or {}
    table.insert(edges[frame], {button, down})
end
for item in string.gmatch(os.getenv('MUSASHI_L1_PAD') or '', '[^,]+') do
    local f, b, n = string.match(item, '^(%d+):(%u+%d?):(%d+)$')
    assert(f and BUTTON[b], 'bad pad item ' .. item)
    add_edge(tonumber(f), BUTTON[b], true)
    add_edge(tonumber(f) + tonumber(n), BUTTON[b], false)
end

local triggers = {}
for item in string.gmatch(os.getenv('MUSASHI_L1_ON') or '', '[^,]+') do
    local sc, sub, d, b, n = string.match(item, '^(%x+)/(%x+)%+(%d+):(%u+%d?):(%d+)$')
    assert(sc and BUTTON[b], 'bad trigger ' .. item)
    table.insert(triggers, {scene = tonumber(sc, 16), sub = tonumber(sub, 16),
        delay = tonumber(d), button = BUTTON[b], len = tonumber(n)})
end
local next_trigger = 1
local repeat_scene, repeat_button, repeat_period
do
    local r = os.getenv('MUSASHI_L1_REPEAT')
    if r and r ~= '' then
        local sc, b, n = string.match(r, '^(%x+):(%u+%d?):(%d+)$')
        assert(sc and BUTTON[b], 'bad repeat ' .. r)
        repeat_scene, repeat_button, repeat_period = tonumber(sc, 16), BUTTON[b], tonumber(n)
    end
end

local log = assert(io.open(out_dir .. '/level1.log', 'w'))
local mem = ffi.cast('uint8_t *', PCSX.getMemPtr())

local function u8(a) return mem[bit.band(a, 0x1fffff)] end
local function u16(a) return u8(a) + u8(a + 1) * 256 end
local function u32(a) return u16(a) + u16(a + 2) * 65536 end
local function s16(a) local v = u16(a); if v >= 0x8000 then v = v - 0x10000 end; return v end
local function s32(a) local v = u32(a); if v >= 0x80000000 then v = v - 0x100000000 end; return v end

local PLAYER = 0x80126B58

local function sample(frame)
    local regs = PCSX.getRegisters()
    log:write(string.format(
        'L1 frame=%d cycles=%s pc=%08x scene=%04x a3b4=%04x a3b6=%04x mode=%04x fade=%04x ' ..
        'p_act=%04x p_sub=%04x p_4d=%02x p_a9=%02x p_aa=%04x p_ac=%04x p_ae=%04x ' ..
        'pad_dca=%04x pad_dd2=%04x b9a64=%02x p_pos=%d,%d,%d\n',
        frame, tostring(PCSX.getCPUCycles()), tonumber(regs.pc),
        u16(0x800B99DE), u16(0x800B99E4), u16(0x800B99E6), u16(0x800B99F0), u16(0x800AF7CE),
        u16(PLAYER), u16(PLAYER + 2), u8(PLAYER + 0x4D), u8(PLAYER + 0xA9),
        u16(PLAYER + 0xAA), u16(PLAYER + 0xAC), u16(PLAYER + 0xAE),
        u16(0x80078DCA), u16(0x80078DD2), u8(0x800B9A64),
        s32(PLAYER + 0x20), s32(PLAYER + 0x24), s32(PLAYER + 0x28)))
    log:flush()
end

local function screenshot(frame)
    local shot = PCSX.GPU.takeScreenShot()
    local bpp = tonumber(shot.bpp)
    if shot.width <= 0 or shot.height <= 0 then return end
    local path = string.format('%s/frame_%05d.ppm', out_dir, frame)
    local f = assert(io.open(path, 'wb'))
    f:write(string.format('P6\n%d %d\n255\n', shot.width, shot.height))
    local data = shot.data
    if bpp == 0 then
        -- PSX RGB555: red in bits 0-4. (tools/retail_frame_capture.lua reads
        -- bits 10-14 as red, which renders the red PlayStation "P" blue.)
        local px = {}
        for i = 0, #data - 1, 2 do
            local p = data[i] + data[i + 1] * 256
            px[#px + 1] = string.char(
                math.floor((p % 32) * 255 / 31),
                math.floor((math.floor(p / 32) % 32) * 255 / 31),
                math.floor((math.floor(p / 1024) % 32) * 255 / 31))
        end
        f:write(table.concat(px))
    else
        f:write(tostring(data))
    end
    f:close()
end

local function dump_ram(frame)
    local f = assert(io.open(string.format('%s/ram_%05d.bin', out_dir, frame), 'wb'))
    f:write(ffi.string(mem, 0x200000))
    f:close()
    log:write(string.format('L1_RAM frame=%d\n', frame))
end

local pad = PCSX.SIO0.slots[1].pads[1]
local frame = 0
L1_LISTENER = PCSX.Events.createEventListener('GPU::Vsync', function()
    frame = frame + 1
    local t = triggers[next_trigger]
    if t and u16(0x800B99DE) == t.scene and u16(0x800B99E4) == t.sub then
        add_edge(frame + t.delay, t.button, true)
        add_edge(frame + t.delay + t.len, t.button, false)
        log:write(string.format('L1_TRIGGER frame=%d index=%d scene=%04x a3b4=%04x\n',
            frame, next_trigger, t.scene, t.sub))
        next_trigger = next_trigger + 1
    end
    if repeat_scene and u16(0x800B99DE) == repeat_scene and frame % repeat_period == 0 then
        add_edge(frame, repeat_button, true)
        add_edge(frame + 4, repeat_button, false)
    end
    if edges[frame] then
        for _, e in ipairs(edges[frame]) do
            if e[2] then pad.setOverride(e[1]) else pad.clearOverride(e[1]) end
            log:write(string.format('L1_PAD frame=%d button=%d down=%s\n', frame, e[1], tostring(e[2])))
        end
    end
    sample(frame)
    if frame % every == 0 then screenshot(frame) end
    if ram_frames[frame] then dump_ram(frame) end
    if frame >= total then
        log:write(string.format('L1_DONE frames=%d\n', frame))
        log:close()
        PCSX.quit(0)
    end
end)
