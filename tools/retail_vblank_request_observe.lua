-- Read-only normal-boot observation of a request reached through VBlank slot 7.
local memory = PCSX.getMemPtr()
local function byte(a)
    assert(a >= 0x80000000 and a < 0x80200000, 'unsupported cached RAM')
    return tonumber(memory[a - 0x80000000])
end
local function word(a)
    return byte(a) + byte(a+1)*256 + byte(a+2)*65536 + byte(a+3)*16777216
end
local ready, in_callback, requested, entered = false, false, false, false
local target, argument
local visits = 0
local function capture_frame()
    local path = os.getenv('MUSASHI_REQUEST_FRAME')
    if not path then return end
    local existing = io.open(path, 'rb')
    if existing then existing:close(); error('refusing to overwrite frame') end
    local frame = PCSX.GPU.takeScreenShot()
    local bpp = tonumber(frame.bpp)
    assert(frame.width > 0 and frame.height > 0, 'empty framebuffer')
    assert(bpp == 0 or bpp == 1, 'unknown pixel format')
    local stride = bpp == 0 and 2 or 3
    assert(#frame.data == frame.width * frame.height * stride, 'invalid framebuffer length')
    local file = assert(io.open(path, 'wb'))
    file:write(string.format('P6\n%d %d\n255\n', frame.width, frame.height))
    for offset = 0, #frame.data - 1, stride do
        if stride == 2 then
            local pixel = frame.data[offset] + frame.data[offset+1] * 256
            file:write(string.char(math.floor((pixel % 32)*255/31),
                math.floor((math.floor(pixel/32) % 32)*255/31),
                math.floor((math.floor(pixel/1024) % 32)*255/31)))
        else
            file:write(string.char(frame.data[offset], frame.data[offset+1], frame.data[offset+2]))
        end
    end
    file:close()
    print(string.format('MUSASHI_REQUEST_FRAME path=%s width=%d height=%d bpp=%d visits=%d',
        path, frame.width, frame.height, bpp, visits))
end
local function observe(address, label, fn)
    return PCSX.addBreakpoint(address, 'Exec', 4, label, function()
        local ok, keep = pcall(fn)
        if not ok then
            print('MUSASHI_REQUEST_ERROR ' .. tostring(keep))
            PCSX.quit(3)
            return false
        end
        return keep
    end)
end
musashi_request_breakpoints = {
    observe(0x800427e0, 'initialized callback table', function()
        local table_address = word(0x8006cb84)
        if table_address < 0x80000000 or table_address > 0x801fffe0 then return true end
        if word(table_address + 4) ~= 0x80042f8c or word(table_address + 8) ~= 0x800429dc then return true end
        ready = true
        print('MUSASHI_REQUEST_INITIALIZATION_QUALIFIED')
        return false
    end),
    observe(0x800616d0, 'slot-seven callback entry', function()
        if not ready or tonumber(PCSX.getRegisters().GPR.n.ra) ~= 0x80042d38 then return true end
        assert(not in_callback, 'nested callback requires separate capture')
        in_callback = true
        visits = visits + 1
        if visits == 1 then
            print(string.format('MUSASHI_REQUEST_FIRST_CALLBACK index=%08x tick=%08x',
                word(0x80072a34), word(0x8006cbb8)))
        end
        return true
    end),
    observe(0x80062950, 'worker indirect call', function()
        if not in_callback then return true end
        local r = PCSX.getRegisters().GPR.n
        assert(tonumber(r.ra) == 0x800616f0, 'worker caller is not the VBlank callback')
        target = tonumber(r.v0)
        -- The call's delay slot adds v1 to a0. Verify at actual callee entry.
        argument = (tonumber(r.v1) + tonumber(r.a0)) % 4294967296
        requested = true
        print(string.format('MUSASHI_REQUEST_CALL target=%08x argument=%08x index=%08x tick=%08x',
            target, argument, word(0x80072a34), word(0x8006cbb8)))
        musashi_request_target = observe(target, 'request callee entry', function()
            local registers = PCSX.getRegisters().GPR.n
            if tonumber(registers.ra) ~= 0x80062958 then return true end
            assert(tonumber(registers.a0) == argument, 'delay-slot argument mismatch')
            entered = true
            print(string.format('MUSASHI_REQUEST_ENTERED target=%08x argument=%08x words=%08x,%08x,%08x,%08x',
                target, argument, word(argument), word(argument+4), word(argument+8), word(argument+12)))
            return false
        end)
        return true
    end),
    observe(0x80062958, 'request returned', function()
        if not in_callback or not requested then return true end
        assert(entered, 'request returned without observed callee entry')
        print(string.format('MUSASHI_REQUEST_RETURN result=%08x live_index=%08x',
            tonumber(PCSX.getRegisters().GPR.n.v0), word(0x80072a34)))
        return true
    end),
    observe(0x80061758, 'completion call', function()
        if not in_callback then return true end
        local r = PCSX.getRegisters().GPR.n
        print(string.format('MUSASHI_REQUEST_COMPLETION target=%08x first=%08x second=%08x',
            tonumber(r.a2), tonumber(r.a0), tonumber(r.a1)))
        return true
    end),
    observe(0x80061768, 'slot-seven callback return', function()
        if not in_callback then return true end
        in_callback = false
        if requested and entered then
            print(string.format('MUSASHI_REQUEST_CHAIN_RETURN index=%08x flag=%08x pending=%08x,%08x saved=%08x,%08x',
                word(0x80072a34), word(0x80078c88), word(0x80078c80), word(0x80078c84),
                word(0x80078cc4), word(0x80078cc8)))
            PCSX.quit(0)
        elseif visits >= 600 then
            print('MUSASHI_REQUEST_LIMIT_WITHOUT_REQUEST')
            capture_frame()
            PCSX.quit(2)
        end
        return true
    end)
}
print('MUSASHI_REQUEST_OBSERVER_ARMED')
