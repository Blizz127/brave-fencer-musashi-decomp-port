-- Normal interpreter boot only. Observe A0:72's real memory/device effects;
-- do not inject registers, RAM, MMIO values, input, or a return address.
local ffi = require('ffi')
local bit = require('bit')
local ram, rom = PCSX.getMemPtr(), PCSX.getRomPtr()
local output = assert(os.getenv('MUSASHI_BIOS_OBSERVE_DIR'), 'output directory required')
local active, removed, before, writes = false, false, nil, 0
local prelude_calls = 0

local function word(address)
    local p = bit.band(address, 0x1fffffff)
    local mem, offset
    if p < 0x200000 then mem, offset = ram, p
    elseif p >= 0x1fc00000 and p < 0x1fc80000 then mem, offset = rom, p - 0x1fc00000
    else error('RAM/ROM read refused') end
    return tonumber(mem[offset]) + tonumber(mem[offset + 1]) * 256 +
           tonumber(mem[offset + 2]) * 65536 + tonumber(mem[offset + 3]) * 16777216
end

local function save(label)
    local data = ffi.string(ram, 0x200000)
    local file = assert(io.open(output .. '/' .. label .. '.ram', 'wb'))
    file:write(data)
    file:close()
    return data
end

local function observe(address, kind, width, label, fn)
    return PCSX.addBreakpoint(address, kind, width, label, function(a, w)
        local ok, keep = pcall(fn, a, w)
        if not ok then
            print('MUSASHI_BIOS_REMOVE_ERROR ' .. tostring(keep))
            PCSX.quit(3)
            return false
        end
        return keep
    end)
end

musashi_bios_remove_breakpoints = {
    observe(0x8005ce50, 'Exec', 4, 'game A0:72 thunk', function()
        local r = PCSX.getRegisters().GPR.n
        if tonumber(r.ra) ~= 0x800427d4 then return true end
        assert(not active, 'nested removal')
        active = true
        before = save('before')
        print(string.format('MUSASHI_BIOS_REMOVE_ENTRY a0=%08x ra=%08x sp=%08x sr=%08x a072=%08x c003=%08x',
            tonumber(r.a0), tonumber(r.ra), tonumber(r.sp),
            tonumber(PCSX.getRegisters().CP0.r[12]), word(0x800003c8), word(0x80000680)))
        return true
    end),
    observe(0x800427d4, 'Exec', 4, 'return before ExitCriticalSection', function()
        if not active then return true end
        local r = PCSX.getRegisters().GPR.n
        save('after')
        local changed = 0
        for offset = 0, 0x1fffff do
            if before:byte(offset + 1) ~= tonumber(ram[offset]) then changed = changed + 1 end
        end
        print(string.format('MUSASHI_BIOS_REMOVE_RETURN v0=%08x sp=%08x sr=%08x changed_bytes=%d mmio_writes=%d',
            tonumber(r.v0), tonumber(r.sp), tonumber(PCSX.getRegisters().CP0.r[12]), changed, writes))
        active, removed = false, true
        return false
    end),
    observe(0x800427dc, 'Exec', 4, 'return from ExitCriticalSection', function()
        if not removed then return true end
        local registers = PCSX.getRegisters()
        print(string.format('MUSASHI_BIOS_REMOVE_SYS2_RETURN sr=%08x v0=%08x sp=%08x',
            tonumber(registers.CP0.r[12]), tonumber(registers.GPR.n.v0),
            tonumber(registers.GPR.n.sp)))
        return false
    end),
    observe(0x8005ce38, 'Exec', 4, 'first GPU_cw after CD removal', function()
        if not removed then return true end
        local registers = PCSX.getRegisters()
        local r = registers.GPR.n
        print(string.format('MUSASHI_BIOS_REMOVE_NEXT_GPU command=%08x ra=%08x sr=%08x',
            tonumber(r.a0), tonumber(r.ra), tonumber(registers.CP0.r[12])))
        PCSX.quit(0)
        return false
    end),
    observe(0x1f801000, 'Write', 0x1000, 'MMIO writes during BIOS removal', function(address, width)
        if not active then return true end
        writes = writes + 1
        local registers = PCSX.getRegisters()
        local pc = tonumber(registers.pc)
        local parts = {}
        for i = 0, 31 do parts[#parts + 1] = string.format('%08x', tonumber(registers.GPR.r[i])) end
        print(string.format('MUSASHI_BIOS_REMOVE_MMIO address=%08x width=%d pc=%08x op=%08x prev=%08x regs=%s',
            address, width, pc, word(pc), word(pc - 4), table.concat(parts, ',')))
        return true
    end),
}

for _, vector in ipairs({0xa0, 0xb0, 0xc0}) do
    musashi_bios_remove_breakpoints[#musashi_bios_remove_breakpoints + 1] =
        observe(vector, 'Exec', 4, 'nested BIOS call', function()
            local r = PCSX.getRegisters().GPR.n
            local index = tonumber(r.t1)
            local prelude = not active and not removed and
                ((vector == 0xc0 and index == 2) or
                 (vector == 0xb0 and (index == 8 or index == 12)))
            if not active and not prelude then return true end
            if prelude then
                prelude_calls = prelude_calls + 1
                assert(prelude_calls <= 256, 'prelude call limit')
            end
            print(string.format('MUSASHI_BIOS_REMOVE_%s vector=%02x index=%02x a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x sr=%08x',
                prelude and 'PRELUDE' or 'SERVICE', vector, index,
                tonumber(r.a0), tonumber(r.a1), tonumber(r.a2), tonumber(r.a3),
                tonumber(r.ra), tonumber(PCSX.getRegisters().CP0.r[12])))
            return true
        end)
end

-- The selected BIOS installs C0:03 at RAM 1444. Record its actual branch
-- path during the two removal calls, including any uninitialized-stack read.
musashi_bios_remove_breakpoints[#musashi_bios_remove_breakpoints + 1] =
    observe(0x1444, 'Exec', 0xc4, 'selected BIOS C0:03 path', function()
        if not active then return true end
        local registers = PCSX.getRegisters()
        local r = registers.GPR.n
        print(string.format('MUSASHI_BIOS_REMOVE_DEQUEUE pc=%08x v0=%08x v1=%08x a1=%08x a2=%08x sp=%08x',
            tonumber(registers.pc), tonumber(r.v0), tonumber(r.v1),
            tonumber(r.a1), tonumber(r.a2), tonumber(r.sp)))
        return true
    end)
print('MUSASHI_BIOS_REMOVE_OBSERVER_ARMED')
