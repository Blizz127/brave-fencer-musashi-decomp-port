-- Observe normal retail GPU startup. No input injection, guest writes, or
-- observer MMIO reads. Loaded values are read from CPU registers instead.
local ffi, bit = require('ffi'), require('bit')
local ram, rom = PCSX.getMemPtr(), PCSX.getRomPtr()
local output = assert(os.getenv('MUSASHI_GPU_OBSERVE_DIR'))
local active, writes = false, 0

local function word(address)
    local p = bit.band(address, 0x1fffffff)
    local mem, offset
    if p < 0x200000 then mem, offset = ram, p
    elseif p >= 0x1fc00000 and p < 0x1fc80000 then mem, offset = rom, p - 0x1fc00000
    else error('not RAM or ROM') end
    return tonumber(mem[offset]) + tonumber(mem[offset+1])*256 +
        tonumber(mem[offset+2])*65536 + tonumber(mem[offset+3])*16777216
end

local function save(label)
    local file = assert(io.open(output .. '/' .. label .. '.ram', 'wb'))
    file:write(ffi.string(ram, 0x200000)); file:close()
end

local function regs(label)
    local r = PCSX.getRegisters()
    local parts = {}
    for i = 0, 31 do parts[#parts+1] = string.format('%08x', tonumber(r.GPR.r[i])) end
    print(string.format('MUSASHI_GPU_%s pc=%08x sr=%08x cycles=%s regs=%s', label,
        tonumber(r.pc), tonumber(r.CP0.r[12]), tostring(PCSX.getCPUCycles()), table.concat(parts, ',')))
end

local function observe(address, kind, width, label, fn)
    return PCSX.addBreakpoint(address, kind, width, label, function(a, w)
        local ok, keep = pcall(fn, a, w)
        if not ok then print('MUSASHI_GPU_ERROR ' .. tostring(keep)); PCSX.quit(3); return false end
        return keep
    end)
end

musashi_gpu_startup_breakpoints = {
    observe(0x8005ce38, 'Exec', 4, 'GPU_cw entry', function()
        if tonumber(PCSX.getRegisters().GPR.n.ra) ~= 0x800592dc then return true end
        assert(not active, 'duplicate startup'); active = true; save('before'); regs('ENTRY')
        print(string.format('MUSASHI_GPU_BIOS_TARGET a049=%08x', word(0x80000324)))
        return true
    end),
    observe(0x1f801000, 'Write', 0x1000, 'startup device writes', function(address, width)
        if not active then return true end
        writes = writes + 1; assert(writes < 500, 'write limit')
        local pc = tonumber(PCSX.getRegisters().pc)
        print(string.format('MUSASHI_GPU_WRITE address=%08x width=%d op=%08x prev=%08x',
            address, width, word(pc), word(pc-4)))
        regs('WRITE_REGS'); return true
    end),
    observe(0x8005b684, 'Exec', 4, 'following graphics reset', function()
        if not active then return true end
        save('after'); regs('NEXT_GRAPHICS_RESET')
        print(string.format('MUSASHI_GPU_COMPLETE writes=%d', writes))
        PCSX.quit(0); return false
    end),
}

-- BIOS read results at load+8 (after the MIPS load delay). Other checkpoints
-- expose the game's actual GPU-info/capability branch and ResetGraph return.
for _, pair in ipairs({
    {0xbfc04150, 'SYNC_STATUS_FIRST'}, {0xbfc04164, 'SYNC_STATUS_DIRECT'},
    {0xbfc041c0, 'SYNC_DMA_CHCR'}, {0xbfc04208, 'SYNC_STATUS_COMMAND'},
    {0x800592dc, 'BIOS_RETURN'}, {0x800592e4, 'DEVICE_RESET_RETURN'},
    {0x8005c1c0, 'QUERY_ENTRY'}, {0x8005c1f0, 'QUERY_INFO'},
    {0x8005c210, 'QUERY_MODE'}, {0x8005c234, 'QUERY_STATUS'},
    {0x8005c294, 'QUERY_RETURN'},
}) do
    local address, label = pair[1], pair[2]
    musashi_gpu_startup_breakpoints[#musashi_gpu_startup_breakpoints+1] =
        observe(address, 'Exec', 4, label, function()
            if active then regs(label) end
            return true
        end)
end
print('MUSASHI_GPU_OBSERVER_ARMED')
