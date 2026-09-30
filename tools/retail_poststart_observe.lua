-- Confirm the two post-startup direct calls during a normal interpreter boot.
-- This is retail execution evidence only; it is not native/menu acceptance.
local seen_2c8f4 = false

musashi_poststart_breakpoints = {
    PCSX.addBreakpoint(0x8002c8f4, 'Exec', 4, 'post-start initializer', function()
        local r = PCSX.getRegisters()
        seen_2c8f4 = true
        print(string.format('MUSASHI_RETAIL_POSTSTART target=8002c8f4 pc=%08x cycles=%s',
                            tonumber(r.pc), tostring(PCSX.getCPUCycles())))
        return false
    end),
    PCSX.addBreakpoint(0x8001971c, 'Exec', 4, 'post-start resource initialization', function()
        local r = PCSX.getRegisters()
        assert(seen_2c8f4, '8001971C observed before 8002C8F4')
        print(string.format('MUSASHI_RETAIL_POSTSTART target=8001971c pc=%08x cycles=%s',
                            tonumber(r.pc), tostring(PCSX.getCPUCycles())))
        PCSX.quit(0)
        return false
    end)
}

print('MUSASHI_RETAIL_POSTSTART_OBSERVER_ARMED')
