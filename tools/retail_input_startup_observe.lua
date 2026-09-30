-- Read-only controller-startup trace during normal retail interpreter boot.
-- Captures vector selectors and callback arguments; it does not read MMIO.
local expected = {
    {0x8005D8B4, 'input_start'},
    {0x8005E178, 'c0_3'},
    {0x8005E168, 'c0_2'},
    {0x8005CF58, 'c0_0a'},
    {0x8005CF18, 'exit_critical'},
    {0x8005F228, 'record_0_callback'},
    {0x8005F228, 'record_1_callback'},
}

local stage = 1
musashi_input_startup_breakpoints = {}
for _, item in ipairs(expected) do
    local address, name = item[1], item[2]
    local bp = PCSX.addBreakpoint(address, 'Exec', 4, name, function()
        local r = PCSX.getRegisters()
        local expected_item = expected[stage]
        if not expected_item or tonumber(r.pc) ~= expected_item[1] then
            return false
        end
        print(string.format(
            'MUSASHI_INPUT_EVENT name=%s pc=%08x a0=%08x a1=%08x ra=%08x cycles=%s',
            expected_item[2], tonumber(r.pc), tonumber(r.GPR.n.a0),
            tonumber(r.GPR.n.a1), tonumber(r.GPR.n.ra),
            tostring(PCSX.getCPUCycles())))
        stage = stage + 1
        if stage > #expected then
            print('MUSASHI_INPUT_OBSERVATION_COMPLETE')
            PCSX.quit(0)
        end
        return false
    end)
    table.insert(musashi_input_startup_breakpoints, bp)
end

print('MUSASHI_INPUT_OBSERVER_ARMED')
