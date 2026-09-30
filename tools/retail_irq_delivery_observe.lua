-- Read-only normal-boot observation of one VBlank IRQ delivery end to end.
-- It observes cached RAM and registers only; it never supplies input, writes
-- guest state, or reads MMIO. The accepted chain is 800427F4 -> 80042CE8 ->
-- 8005CED8, so separate parent/child observations cannot be mistaken for one
-- delivery.
local memory = PCSX.getMemPtr()

local function byte(address)
    assert(address >= 0x80000000 and address < 0x80200000,
           'unsupported RAM address')
    return tonumber(memory[address - 0x80000000])
end

local function half(address)
    return byte(address) + byte(address + 1) * 256
end

local function word(address)
    return half(address) + half(address + 2) * 65536
end

local function observe(address, label, fn)
    return PCSX.addBreakpoint(address, 'Exec', 4, label, function()
        local ok, keep = pcall(fn)
        if not ok then
            print('MUSASHI_IRQ_DELIVERY_ERROR ' .. tostring(keep))
            PCSX.quit(3)
            return false
        end
        return keep
    end)
end

local visits, phase, entry_tick = 0, 0, 0
musashi_irq_delivery_breakpoints = {
    observe(0x800427f4, 'IRQ parent dispatch entry', function()
        local r = PCSX.getRegisters().GPR.n
        if half(0x8006bafc) ~= 1 or word(0x8006bb00) ~= 0x80042ce8 then
            return true
        end
        visits = visits + 1
        phase = 1
        entry_tick = word(0x8006cbb8)
        print(string.format(
            'MUSASHI_IRQ_DELIVERY_PARENT visit=%d ra=%08x sp=%08x active=%04x enabled=%04x tick=%08x slot0=%08x',
            visits, tonumber(r.ra), tonumber(r.sp), half(0x8006bafe),
            half(0x8006bb2c), entry_tick, word(0x8006bb00)))
        return true
    end),
    observe(0x80042ce8, 'VBlank child entry', function()
        local r = PCSX.getRegisters().GPR.n
        if phase ~= 1 or tonumber(r.ra) ~= 0x800428dc then
            return true
        end
        phase = 2
        print(string.format(
            'MUSASHI_IRQ_DELIVERY_CHILD visit=%d ra=%08x active=%04x tick=%08x slot7=%08x',
            visits, tonumber(r.ra), half(0x8006bafe), word(0x8006cbb8),
            word(0x8006cbb4)))
        return true
    end),
    observe(0x8005ced8, 'B0:17 return-from-exception thunk', function()
        local r = PCSX.getRegisters().GPR.n
        if phase ~= 2 or tonumber(r.ra) ~= 0x800429b8 then
            return true
        end
        print(string.format(
            'MUSASHI_IRQ_DELIVERY_RETURN visit=%d ra=%08x active=%04x tick_before=%08x tick_after=%08x counter=%08x',
            visits, tonumber(r.ra), half(0x8006bafe), entry_tick,
            word(0x8006cbb8), word(0x8006cb94)))
        print('MUSASHI_IRQ_DELIVERY_OBSERVED')
        PCSX.quit(0)
        return false
    end)
}
print('MUSASHI_IRQ_DELIVERY_OBSERVER_ARMED')
