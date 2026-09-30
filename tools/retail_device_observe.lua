-- Read-only register observation during normal disc boot. Use the interpreter.
-- Device registers are never read by this observer (reads may have effects).
musashi_device_breakpoints = {}
local seen = 0
local expected = {'startup', 'callback_init_enter', 'callback_init_return',
                  'device_query_enter', 'device_query_return'}
local stage = 1
local function observe(address, name, finish, intermediate)
    local bp = PCSX.addBreakpoint(address, 'Exec', 4, name, function()
        local r = PCSX.getRegisters()
        if tonumber(r.pc) ~= address or (not intermediate and expected[stage] ~= name) then
            print('MUSASHI_DEVICE_OBSERVATION_FAILED unexpected event or PC')
            PCSX.quit(2)
            return false
        end
        if not intermediate then stage = stage + 1 end
        print(string.format('MUSASHI_DEVICE_EVENT name=%s pc=%08x a0=%08x v0=%08x v1=%08x ra=%08x cycles=%s',
              name, tonumber(r.pc), tonumber(r.GPR.n.a0), tonumber(r.GPR.n.v0),
              tonumber(r.GPR.n.v1), tonumber(r.GPR.n.ra), tostring(PCSX.getCPUCycles())))
        seen = seen + 1
        if finish then
            print(string.format('MUSASHI_DEVICE_OBSERVATION_COMPLETE events=%d', seen))
            PCSX.quit(0)
        end
        return false
    end)
    table.insert(musashi_device_breakpoints, bp)
end
observe(0x800141f0, 'startup', false)
observe(0x80042718, 'callback_init_enter', false)
observe(0x800427e0, 'callback_init_return', false)
observe(0x8005c1c0, 'device_query_enter', false)
observe(0x8005c1f0, 'gpu_info_masked', false, true)
observe(0x8005c210, 'draw_mode_before_or', false, true)
observe(0x8005c234, 'gpu_status_before_mask', false, true)
observe(0x8005c294, 'device_query_return', true)
print('MUSASHI_DEVICE_OBSERVER_ARMED')
