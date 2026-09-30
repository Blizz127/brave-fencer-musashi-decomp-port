-- Normal retail interpreter boot; observe A0:70 backup-unit/card filesystem init.
-- The historical filename/log prefix is retained for earlier artifact readers.
-- CPU/RAM reads and observed guest MMIO writes are allowed. Never inject
-- state/input, seed RAM, or read MMIO from the observer.
local ffi, bit = require('ffi'), require('bit')
local ram, rom = PCSX.getMemPtr(), PCSX.getRomPtr()
local output = assert(os.getenv('MUSASHI_CD_INIT_OBSERVE_DIR'))
local active, bios_seen, returned, writes = false, false, false, 0
local write_cap = 100000
local all_writes, summarized = nil, {}
local fasttrack = { [0x648c]=true, [0x64a0]=true, [0x64a8]=true,
    [0x6528]=true, [0x6540]=true, [0x6548]=true }

local function close_writes()
    if all_writes then all_writes:close(); all_writes = nil end
end

local function word(address)
    local physical = bit.band(address, 0x1fffffff)
    local memory, offset
    if physical < 0x200000 then memory, offset = ram, physical
    elseif physical >= 0x1fc00000 and physical < 0x1fc80000 then
        memory, offset = rom, physical - 0x1fc00000
    else error('observer read outside RAM/ROM') end
    return tonumber(memory[offset]) + tonumber(memory[offset+1])*256 +
        tonumber(memory[offset+2])*65536 + tonumber(memory[offset+3])*16777216
end

local function snapshot(label, save_ram)
    local r = PCSX.getRegisters()
    local gpr, cp0 = {}, {}
    for i = 0, 31 do
        gpr[#gpr+1] = string.format('%08x', tonumber(r.GPR.r[i]))
        cp0[#cp0+1] = string.format('%08x', tonumber(r.CP0.r[i]))
    end
    print(string.format('MUSASHI_CD_INIT_CPU label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s',
        label, tonumber(r.pc), tostring(PCSX.getCPUCycles()),
        tonumber(r.GPR.n.hi), tonumber(r.GPR.n.lo),
        table.concat(gpr, ','), table.concat(cp0, ',')))
    if save_ram then
        local file = assert(io.open(output .. '/' .. label .. '.ram', 'wb'))
        file:write(ffi.string(ram, 0x200000)); file:close()
    end
end

local function observe(address, kind, width, label, fn)
    return PCSX.addBreakpoint(address, kind, width, label, function(a, w)
        local ok, keep = pcall(fn, a, w)
        if not ok then
            close_writes()
            print('MUSASHI_CD_INIT_ERROR ' .. tostring(keep)); PCSX.quit(3)
            return false
        end
        return keep
    end)
end

musashi_cd_init_breakpoints = {
    observe(0x80061f38, 'Exec', 4, 'A0:70 entry', function()
        local r = PCSX.getRegisters()
        if tonumber(r.GPR.n.ra) ~= 0x8006188c then return true end
        assert(not active and not returned, 'duplicate A0:70 entry')
        all_writes = assert(io.open(output .. '/all-writes.tsv', 'wb'))
        all_writes:write('order\taddress\twidth\tvalue\tpc\tinstruction\n')
        active = true; snapshot('ENTRY', true); return true
    end),
    observe(0xbfc09914, 'Exec', 4, 'selected BIOS A0:70 target', function()
        if not active then return true end
        assert(not bios_seen, 'duplicate BIOS target')
        bios_seen = true; snapshot('BIOS_ENTRY', true); return true
    end),
    observe(0x8006188c, 'Exec', 4, 'A0:70 natural return', function()
        if not active or not bios_seen then return true end
        snapshot('RETURN', true); active, returned = false, true
        close_writes()
        for pc, count in pairs(summarized) do
            print(string.format('MUSASHI_CD_INIT_FASTTRACK pc=%08x count=%d', pc, count))
        end
        print('MUSASHI_CD_INIT_COMPLETE writes=' .. writes)
        PCSX.quit(0); return false
    end),
    observe(0x1f801000, 'Write', 0x1000, 'A0:70 MMIO writes', function(address, width)
        if not active then return true end
        writes = writes + 1; assert(writes <= write_cap, 'write limit')
        local r = PCSX.getRegisters()
        local pc = tonumber(r.pc)
        local instruction = word(pc)
        local opcode = bit.rshift(instruction, 26)
        local expected_width = ({[0x28]=1, [0x29]=2, [0x2b]=4})[opcode]
        assert(expected_width == width, 'unsupported store decoding')
        local rt = bit.band(bit.rshift(instruction, 16), 31)
        local value = tonumber(r.GPR.r[rt]) % (2^(width*8))
        local rs = bit.band(bit.rshift(instruction, 21), 31)
        local immediate = bit.band(instruction, 0xffff)
        if immediate >= 0x8000 then immediate = immediate - 0x10000 end
        local effective = (tonumber(r.GPR.r[rs]) + immediate) % 0x100000000
        assert(bit.band(effective, 0x1fffffff) == bit.band(address, 0x1fffffff),
            'store address mismatch')
        all_writes:write(string.format('%d\t%08x\t%d\t%08x\t%08x\t%08x\n',
            writes, address, width, value, pc, instruction))
        if fasttrack[bit.band(pc, 0x1fffffff)] then
            summarized[pc] = (summarized[pc] or 0) + 1
            return true
        end
        print(string.format('MUSASHI_CD_INIT_WRITE order=%d phase=%s address=%08x width=%d value=%08x pc=%08x instruction=%08x',
            writes, bios_seen and 'bios' or 'before-bios', address, width, value, pc, instruction))
        return true
    end),
}
for _, item in ipairs({
    {0xbfc08b3c, 'BU_PORT_ENTRY'},
    {0xbfc09934, 'BU_PORT0_RETURN'},
    {0xbfc0993c, 'BU_PORT1_RETURN'},
    {0xbfc0da00, 'CARD_READ_SECTOR'},
    {0xbfc0da20, 'CARD_WRITE_SECTOR'},
    {0x0000506c, 'CARD_VBLANK_TIMEOUT'},
}) do
    local address, label = item[1], item[2]
    musashi_cd_init_breakpoints[#musashi_cd_init_breakpoints+1] =
        observe(address, 'Exec', 4, label, function()
            if active then snapshot(label, false) end
            return true
        end)
end
print('MUSASHI_CD_INIT_OBSERVER_ARMED')
-- Optional host-only launch gate permits inspecting the configured peripheral
-- checkboxes while the emulator is still paused before normal boot. This is
-- debugger run control; it changes no guest register, RAM, MMIO, or input.
local launch_gate = os.getenv('MUSASHI_BU_HOST_LAUNCH_GATE')
if launch_gate then
    local uv = assert(luv, 'PCSX host event loop unavailable')
    musashi_bu_launch_timer = uv.new_timer()
    snapshot('HOST_PAUSED_BEFORE_BOOT', false)
    musashi_bu_launch_timer:start(50, 50, function()
        local file = io.open(launch_gate, 'rb')
        if file then
            file:close()
            musashi_bu_launch_timer:stop()
            musashi_bu_launch_timer:close()
            print('MUSASHI_BU_HOST_LAUNCH_GATE_OPEN')
            PCSX.resumeEmulator()
        end
    end)
end
