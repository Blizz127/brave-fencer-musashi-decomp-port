-- Read cached RAM and CPU registers only during normal interpreter disc boot.
local memory = PCSX.getMemPtr()
local function byte(address)
    assert(address >= 0x80000000 and address < 0x80200000, 'unsupported RAM address')
    return tonumber(memory[address - 0x80000000])
end
local function word(address)
    return byte(address) + byte(address + 1) * 256 +
           byte(address + 2) * 65536 + byte(address + 3) * 16777216
end
local entered = false
local registrations = 0
local pending_index
local function registration(label, index, callback, previous)
    local enabled = byte(0x8006bb2c) + byte(0x8006bb2d) * 256
    print(string.format('MUSASHI_REGISTRATION stage=%s index=%d callback=%08x previous=%08x slot=%08x enabled=%04x',
          label, index, callback, previous, word(0x8006bb00 + index * 4), enabled))
end
local function snapshot(label)
    local table_address = word(0x8006cb84)
    local guard = byte(0x8006bafc) + byte(0x8006bafd) * 256
    print(string.format('MUSASHI_CALLBACK_STATE stage=%s table=%08x guard=%04x saved_stack=%08x',
          label, table_address, guard, word(0x8006bb38)))
    for offset = 0, 0x1c, 4 do
        print(string.format('MUSASHI_CALLBACK_SLOT offset=%02x target=%08x',
                            offset, word(table_address + offset)))
    end
end
musashi_callback_breakpoints = {
    PCSX.addBreakpoint(0x80042718, 'Exec', 4, 'callback entry', function()
        entered = true
        snapshot('entry')
        return false
    end),
    PCSX.addBreakpoint(0x800427e0, 'Exec', 4, 'callback return', function()
        assert(entered, 'return observed without entry')
        snapshot('return')
        print(string.format('MUSASHI_CALLBACK_RETURN v0=%08x',
                            tonumber(PCSX.getRegisters().GPR.n.v0)))
        print(string.format('MUSASHI_REGISTRATIONS_OBSERVED count=%d', registrations))
        PCSX.quit(0)
        return false
    end),
    PCSX.addBreakpoint(0x800429dc, 'Exec', 4, 'registration entry', function()
        local r = PCSX.getRegisters().GPR.n
        pending_index = tonumber(r.a0)
        assert(pending_index >= 0 and pending_index < 11, 'unsupported callback index')
        registration('entry', pending_index, tonumber(r.a1), word(0x8006bb00 + pending_index * 4))
        return true
    end),
    PCSX.addBreakpoint(0x80042b0c, 'Exec', 4, 'registration return', function()
        local r = PCSX.getRegisters().GPR.n
        assert(pending_index == tonumber(r.s1), 'registration order mismatch')
        registration('return', pending_index, tonumber(r.s2), tonumber(r.v0))
        registrations = registrations + 1
        pending_index = nil
        return true
    end)
}
print('MUSASHI_CALLBACK_OBSERVER_ARMED')
