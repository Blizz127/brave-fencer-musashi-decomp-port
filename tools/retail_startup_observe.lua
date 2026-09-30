-- PCSX-Redux interpreter observer. Boot the registered disc normally with
-- --interpreter --debugger --run --dofile <this file>.
-- This observes entry only: no native/menu/visual acceptance is implied.
-- Retain the breakpoint object so Lua GC cannot remove it before execution.
musashi_entry_bp = PCSX.addBreakpoint(0x800141f0, 'Exec', 4, 'Musashi startup', function()
    local r = PCSX.getRegisters()
    print(string.format('MUSASHI_RETAIL_STARTUP_OBSERVED pc=%08x cycles=%s',
                        tonumber(r.pc), tostring(PCSX.getCPUCycles())))
    PCSX.quit(0)
    return false
end)
print('MUSASHI_RETAIL_OBSERVER_ARMED target=800141f0')
