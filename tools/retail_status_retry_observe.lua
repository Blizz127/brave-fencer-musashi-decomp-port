-- Read-only capture of the retail retry-exhausted 80044198 poll frame.
-- It neither changes registers/RAM nor supplies a result. Run only with the
-- interpreter; this exits when the counter reaches the retail 0x003c0000 gate.
local memory = PCSX.getMemPtr()
local threshold = 0x003c0000
local first_hit = true
local unrelated_hit = true
local entry_hit = true
local entry_unqualified = true
local status_call_armed = false
local status_poll_active = false

local function byte(address)
    assert(address >= 0x80000000 and address < 0x80200000, 'unsupported RAM address')
    return tonumber(memory[address - 0x80000000])
end

local function word(address)
    return byte(address) + byte(address + 1) * 256 +
           byte(address + 2) * 65536 + byte(address + 3) * 16777216
end

musashi_status_call_bp = PCSX.addBreakpoint(
    0x80044734, 'Exec', 4, 'Musashi status poll caller', function()
        status_call_armed = true
        return false
    end)

musashi_status_entry_bp = PCSX.addBreakpoint(
    0x80044124, 'Exec', 4, 'Musashi status poll entry', function()
        local registers = PCSX.getRegisters()
        local r = registers.GPR.n
        if status_call_armed then
            if entry_hit then
                print(string.format(
                    'MUSASHI_STATUS_ENTRY_QUALIFIED caller=80044734 pc=%08x a0=%08x a1=%08x cycles=%s',
                    tonumber(registers.pc), tonumber(r.a0),
                    tonumber(r.a1), tostring(PCSX.getCPUCycles())))
                entry_hit = false
            end
            status_call_armed = false
            status_poll_active = true
        elseif entry_unqualified then
            print(string.format(
                'MUSASHI_STATUS_ENTRY_UNQUALIFIED pc=%08x a0=%08x a1=%08x cycles=%s',
                tonumber(registers.pc), tonumber(r.a0),
                tonumber(r.a1), tostring(PCSX.getCPUCycles())))
            entry_unqualified = false
        end
        return false
    end)

musashi_status_retry_bp = PCSX.addBreakpoint(
    0x80044198, 'Exec', 4, 'Musashi retry-exhausted poll', function()
        local retry = word(0x800763f4)
        local registers = PCSX.getRegisters()
        local r = registers.GPR.n
        -- The label is shared. Accept only a path whose immediate 80044734
        -- caller was observed; PCSX execution breakpoints expose pre-link RA.
        if not status_poll_active then
            if unrelated_hit then
                print(string.format(
                    'MUSASHI_STATUS_POLL_UNQUALIFIED pc=%08x ra=%08x retry=%08x cycles=%s',
                    tonumber(registers.pc), tonumber(r.ra), retry,
                    tostring(PCSX.getCPUCycles())))
                unrelated_hit = false
            end
            return false
        end
        if first_hit then
            print(string.format(
                'MUSASHI_STATUS_POLL_FIRST pc=80044198 cycles=%s retry=%08x tick=%08x deadline=%08x ra=%08x',
                tostring(PCSX.getCPUCycles()), retry, tonumber(r.v0),
                word(0x800763f0), tonumber(r.ra)))
            first_hit = false
        end
        if retry < threshold then
            return false
        end
        local r = PCSX.getRegisters().GPR.n
        print(string.format(
            'MUSASHI_STATUS_RETRY_EXHAUSTED pc=80044198 cycles=%s retry=%08x tick=%08x deadline=%08x',
            tostring(PCSX.getCPUCycles()), retry, tonumber(r.v0), word(0x800763f0)))
        print(string.format(
            'MUSASHI_STATUS_FRAME sp=%08x ra=%08x s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x s5=%08x s6=%08x',
            tonumber(r.sp), tonumber(r.ra), tonumber(r.s0), tonumber(r.s1),
            tonumber(r.s2), tonumber(r.s3), tonumber(r.s4), tonumber(r.s5),
            tonumber(r.s6)))
        print(string.format(
            'MUSASHI_STATUS_RAM cf4c=%08x cf54=%08x cf58=%08x cf5c=%08x cf64=%08x cc84=%08x cc88=%08x cc90=%08x',
            word(0x8006cf4c), word(0x8006cf54), word(0x8006cf58), word(0x8006cf5c),
            word(0x8006cf64), word(0x8006cc84), word(0x8006cc88), word(0x8006cc90)))
        PCSX.quit(0)
        return false
    end)

print(string.format('MUSASHI_STATUS_RETRY_OBSERVER_ARMED pc=80044198 threshold=%08x', threshold))
