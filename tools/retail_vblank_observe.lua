-- Normal interpreter disc boot only. Read cached RAM/registers; never MMIO.
-- Stop after the first nonempty VBlank dispatch returns, or 600 empty visits.
local memory = PCSX.getMemPtr()
local function byte(address)
    assert(address >= 0x80000000 and address < 0x80200000, 'unsupported RAM address')
    return tonumber(memory[address - 0x80000000])
end
local function half(address)
    return byte(address) + byte(address + 1) * 256
end
local function word(address)
    return half(address) + half(address + 2) * 65536
end
local visits, calls = 0, 0
local in_handler = false
local nonempty = false
local entry_tick
local ready = false
local function observe(address, label, fn)
    return PCSX.addBreakpoint(address, 'Exec', 4, label, function()
        local ok, keep = pcall(fn)
        if not ok then
            print('MUSASHI_VBLANK_OBSERVER_ERROR ' .. tostring(keep))
            PCSX.quit(3)
            return false
        end
        return keep
    end)
end
musashi_vblank_breakpoints = {
    observe(0x800427e0, 'callback initialization qualified', function()
        local table_address = word(0x8006cb84)
        if half(0x8006bafc) ~= 1 or table_address < 0x80000000 or
           table_address > 0x801fffe0 then return true end
        if word(table_address + 4) ~= 0x80042f8c or
           word(table_address + 8) ~= 0x800429dc then return true end
        ready = true
        print('MUSASHI_VBLANK_INITIALIZATION_QUALIFIED')
        return false
    end),
    observe(0x80042ce8, 'VBlank entry', function()
        if not ready or tonumber(PCSX.getRegisters().GPR.n.ra) ~= 0x800428dc then
            return true
        end
        assert(not in_handler, 'nested VBlank handler requires separate capture')
        in_handler = true
        visits = visits + 1
        calls = 0
        entry_tick = word(0x8006cbb8)
        nonempty = false
        for index = 0, 7 do
            if word(0x8006cb98 + index * 4) ~= 0 then nonempty = true end
        end
        if visits <= 3 or nonempty then
            print(string.format('MUSASHI_VBLANK_ENTRY visit=%d ra=%08x tick=%08x active=%04x enabled=%04x nonempty=%s',
                visits, tonumber(PCSX.getRegisters().GPR.n.ra), entry_tick,
                half(0x8006bafe), half(0x8006bb2c), tostring(nonempty)))
            for index = 0, 7 do
                print(string.format('MUSASHI_VBLANK_SLOT index=%d target=%08x',
                    index, word(0x8006cb98 + index * 4)))
            end
        end
        return true
    end),
    observe(0x80042d30, 'VBlank indirect call', function()
        if not in_handler then return true end
        local r = PCSX.getRegisters().GPR.n
        local index = tonumber(r.s1)
        assert(index >= 0 and index < 8, 'invalid handler index')
        calls = calls + 1
        print(string.format('MUSASHI_VBLANK_CALL visit=%d index=%d target=%08x slot=%08x tick=%08x',
            visits, index, tonumber(r.v0), word(0x8006cb98 + index * 4), word(0x8006cbb8)))
        return true
    end),
    observe(0x80042d58, 'VBlank return transfer', function()
        if not in_handler then return true end
        in_handler = false
        if visits <= 3 or nonempty then
            print(string.format('MUSASHI_VBLANK_RETURN visit=%d ra=%08x tick_before=%08x tick_after=%08x calls=%d',
                visits, tonumber(PCSX.getRegisters().GPR.n.ra), entry_tick,
                word(0x8006cbb8), calls))
        end
        if nonempty and calls > 0 then
            print('MUSASHI_VBLANK_NONEMPTY_RETURN_OBSERVED')
            PCSX.quit(0)
        elseif visits >= 600 then
            print('MUSASHI_VBLANK_LIMIT_WITHOUT_NONEMPTY_RETURN')
            PCSX.quit(2)
        end
        return true
    end)
}
print('MUSASHI_VBLANK_OBSERVER_ARMED')
