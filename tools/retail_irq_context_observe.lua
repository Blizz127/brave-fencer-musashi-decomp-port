-- Read-only normal-boot observation of the parent IRQ dispatch exit.
-- It accepts only 800427F4's call to the B0:17 thunk. No input, RAM, or
-- device state is supplied by this script.
local memory = PCSX.getMemPtr()

local function byte(address)
    assert(address >= 0x80000000 and address < 0x80200000, 'unsupported RAM address')
    return tonumber(memory[address - 0x80000000])
end

local function word(address)
    return byte(address) + byte(address + 1) * 256 +
           byte(address + 2) * 65536 + byte(address + 3) * 16777216
end

local function observe(address, label, fn)
    return PCSX.addBreakpoint(address, 'Exec', 4, label, function()
        local ok, keep = pcall(fn)
        if not ok then
            print('MUSASHI_IRQ_CONTEXT_ERROR ' .. tostring(keep))
            PCSX.quit(3)
            return false
        end
        return keep
    end)
end

local calls = 0
musashi_irq_context_breakpoints = {
    observe(0x8005ced8, 'B0:17 return-from-exception thunk', function()
        local r = PCSX.getRegisters().GPR.n
        -- jal at 800429B0, whose return address is 800429B8.
        if tonumber(r.ra) ~= 0x800429b8 then return true end
        calls = calls + 1
        print(string.format(
            'MUSASHI_IRQ_CONTEXT_CALL count=%d ra=%08x sp=%08x gp=%08x s0=%08x s1=%08x active=%04x counter=%08x',
            calls, tonumber(r.ra), tonumber(r.sp), tonumber(r.gp), tonumber(r.s0),
            tonumber(r.s1), byte(0x8006bafe) + byte(0x8006baff) * 256,
            word(0x8006cb94)))
        print(string.format(
            'MUSASHI_IRQ_CONTEXT_RECORD ra=%08x sp=%08x fp=%08x s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x s5=%08x s6=%08x s7=%08x gp=%08x',
            word(0x8006bb34), word(0x8006bb38), word(0x8006bb3c),
            word(0x8006bb40), word(0x8006bb44), word(0x8006bb48),
            word(0x8006bb4c), word(0x8006bb50), word(0x8006bb54),
            word(0x8006bb58), word(0x8006bb5c), word(0x8006bb60)))
        PCSX.quit(0)
        return false
    end)
}
print('MUSASHI_IRQ_CONTEXT_OBSERVER_ARMED')
