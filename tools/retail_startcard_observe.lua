-- Normal retail interpreter boot; CPU/RAM reads and observed guest writes
-- only. Never inject state/input or read MMIO from the observer.
local ffi, bit = require('ffi'), require('bit')
local ram, rom = PCSX.getMemPtr(), PCSX.getRomPtr()
local output = assert(os.getenv('MUSASHI_STARTCARD_OBSERVE_DIR'))
local active, returned, writes = false, false, 0

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
    print(string.format('MUSASHI_STARTCARD_CPU label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s',
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
            print('MUSASHI_STARTCARD_ERROR ' .. tostring(keep)); PCSX.quit(3)
            return false
        end
        return keep
    end)
end

musashi_startcard_breakpoints = {
    observe(0x800627e8, 'Exec', 4, 'StartCARD entry', function()
        if tonumber(PCSX.getRegisters().GPR.n.ra) ~= 0x80062340 then return true end
        assert(not active and not returned, 'duplicate StartCARD')
        active = true; snapshot('ENTRY', true); return false
    end),
    observe(0x80062340, 'Exec', 4, 'StartCARD return', function()
        if not active then return true end
        snapshot('RETURN', true); active, returned = false, true; return false
    end),
    observe(0x80061f38, 'Exec', 4, 'next A0:70 entry', function()
        if not returned then return true end
        assert(tonumber(PCSX.getRegisters().GPR.n.ra) == 0x8006188c, 'unexpected A0:70 caller')
        snapshot('NEXT_A0_70', true)
        print('MUSASHI_STARTCARD_COMPLETE writes=' .. writes)
        PCSX.quit(0); return false
    end),
    observe(0x1f801000, 'Write', 0x1000, 'StartCARD MMIO writes', function(address, width)
        if not active and not returned then return true end
        writes = writes + 1; assert(writes <= 128, 'write limit')
        local r = PCSX.getRegisters()
        local pc = tonumber(r.pc)
        local instruction = word(pc)
        local opcode = bit.rshift(instruction, 26)
        local expected_width = ({[0x28]=1, [0x29]=2, [0x2b]=4})[opcode]
        assert(expected_width == width, 'unsupported store decoding')
        local rt = bit.band(bit.rshift(instruction, 16), 31)
        local value = tonumber(r.GPR.r[rt]) % (2^(width*8))
        print(string.format('MUSASHI_STARTCARD_WRITE order=%d phase=%s address=%08x width=%d value=%08x pc=%08x instruction=%08x',
            writes, active and 'inside' or 'after', address, width, value, pc, instruction))
        return true
    end),
}

for _, point in ipairs({
    {0x00004c80, 'SERIAL_SETUP_RETURN'},
    {0x00004c88, 'INTERNAL_SYS1_RETURN'},
    {0x00004c98, 'DEQUEUE_RETURN'},
    {0x00004ca8, 'ENQUEUE_RETURN'},
    {0x00004cc8, 'PAD_POLICY_RETURN'},
    {0x00004cd4, 'TIMER_POLICY_RETURN'},
    {0x00004ce4, 'INTERNAL_SYS2_RETURN'},
    {0x80062348, 'FOLLOWING_PAD_POLICY_RETURN'},
    {0x80062350, 'FOLLOWING_SYS2_RETURN'},
}) do
    local address, label = point[1], point[2]
    musashi_startcard_breakpoints[#musashi_startcard_breakpoints+1] =
        observe(address, 'Exec', 4, label, function()
            if active or returned then snapshot(label, false) end
            return true
        end)
end
print('MUSASHI_STARTCARD_OBSERVER_ARMED')
