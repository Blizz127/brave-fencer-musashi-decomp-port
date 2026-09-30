-- Normal retail interpreter boot. Observe CPU state and ordinary RAM only;
-- never write guest state, inject input, or read device registers.
local ffi = require('ffi')
local output = assert(os.getenv('MUSASHI_ENTRY_OBSERVE_DIR'))
local active, cd, count = false, false, 0
local seen = {}

local function snapshot(label, save_ram)
    local r = PCSX.getRegisters()
    local gpr, cp0 = {}, {}
    for i = 0, 31 do
        gpr[#gpr + 1] = string.format('%08x', tonumber(r.GPR.r[i]))
        cp0[#cp0 + 1] = string.format('%08x', tonumber(r.CP0.r[i]))
    end
    count = count + 1
    assert(count <= 80, 'snapshot limit')
    print(string.format('MUSASHI_ENTRY_SNAPSHOT label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s',
        label, tonumber(r.pc), tostring(PCSX.getCPUCycles()),
        tonumber(r.GPR.n.hi), tonumber(r.GPR.n.lo),
        table.concat(gpr, ','), table.concat(cp0, ',')))
    if save_ram then
        local file = assert(io.open(output .. '/' .. label .. '.ram', 'wb'))
        file:write(ffi.string(PCSX.getMemPtr(), 0x200000)); file:close()
    end
end

local function breakpoint(address, label, fn)
    return PCSX.addBreakpoint(address, 'Exec', 4, label, function()
        local ok, keep = pcall(fn)
        if not ok then
            print('MUSASHI_ENTRY_ERROR ' .. tostring(keep)); PCSX.quit(3)
            return false
        end
        return keep
    end)
end

musashi_entry_continuation_breakpoints = {}
for _, point in ipairs({
    {0x80010000, 'CRT_ENTRY', true},
    {0x80010178, 'CALLER_ENTRY', true},
    {0x800141f0, 'GRAPHICS_ENTRY'},
    {0x800101c8, 'GRAPHICS_RETURN', true},
    {0x8005fc68, 'CD_ENTRY'},
    {0x800622e8, 'B0_5B_RETURN'},
    {0x800622f0, 'SYS1_RETURN'},
    {0x80062304, 'B0_4A_CALL'},
    {0x8006230c, 'B0_4A_RETURN'},
    {0x80062314, 'SYS2_RETURN', true},
}) do
    local address, label, save_ram = point[1], point[2], point[3]
    musashi_entry_continuation_breakpoints[#musashi_entry_continuation_breakpoints + 1] =
        breakpoint(address, label, function()
            if label == 'CRT_ENTRY' then active = true end
            if not active or seen[label] then return true end
            seen[label] = true
            if label == 'CD_ENTRY' then cd = true end
            snapshot(label, save_ram)
            if label == 'SYS2_RETURN' then
                for _, required in ipairs({'CRT_ENTRY', 'CALLER_ENTRY',
                    'GRAPHICS_ENTRY', 'GRAPHICS_RETURN', 'CD_ENTRY',
                    'B0_5B_ENTRY', 'SYS1_ENTRY', 'B0_4A_ENTRY'}) do
                    assert(seen[required], 'missing ' .. required)
                end
                print('MUSASHI_ENTRY_COMPLETE snapshots=' .. count)
                PCSX.quit(0)
            end
            return false
        end)
end

for _, point in ipairs({
    {0x8005cf48, 'B0_5B_ENTRY'},
    {0x8005cf08, 'SYS1_ENTRY'},
    {0x800627d8, 'B0_4A_ENTRY'},
    {0x8005cf18, 'SYS2_ENTRY'},
}) do
    local address, label = point[1], point[2]
    musashi_entry_continuation_breakpoints[#musashi_entry_continuation_breakpoints + 1] =
        breakpoint(address, label, function()
            if not cd or seen[label] then return true end
            seen[label] = true; snapshot(label, false); return false
        end)
end
print('MUSASHI_ENTRY_OBSERVER_ARMED')
