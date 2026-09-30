-- Read-only normal-boot observation for the first call after 80018918.
local hit = false
PCSX.addBreakpoint(0x80043300, 'Exec', 4, 'post_input_startup', function()
    local r = PCSX.getRegisters()
    if tonumber(r.GPR.n.ra) ~= 0x800101ec then
        return false
    end
    print(string.format(
        'MUSASHI_POST_INPUT_OBSERVED pc=%08x a0=%08x ra=%08x cycles=%s',
        tonumber(r.pc), tonumber(r.GPR.n.a0), tonumber(r.GPR.n.ra),
        tostring(PCSX.getCPUCycles())))
    hit = true
    PCSX.quit(0)
    return false
end)
print('MUSASHI_POST_INPUT_OBSERVER_ARMED')
