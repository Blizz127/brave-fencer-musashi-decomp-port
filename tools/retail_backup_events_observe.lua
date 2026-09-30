-- Normal retail boot: observe the natural backup-unit event registration.
-- CPU/RAM reads only; never mutate guest state/input or read MMIO.
local ffi, bit = require('ffi'), require('bit')
local ram = PCSX.getMemPtr()
local output = assert(os.getenv('MUSASHI_BACKUP_EVENTS_DIR'))
local active, returned, current = false, false, nil
local counts, snapshots, writes = {}, 0, 0
local breakpoints = {}
musashi_backup_events_breakpoints = breakpoints

local function word(address)
    local offset = bit.band(address, 0x1fffffff)
    assert(offset >= 0 and offset <= 0x1ffffc, 'RAM read outside bounds')
    return tonumber(ram[offset]) + tonumber(ram[offset+1])*256 +
        tonumber(ram[offset+2])*65536 + tonumber(ram[offset+3])*16777216
end

local function snapshot(label, save_ram)
    local r, gpr, cp0 = PCSX.getRegisters(), {}, {}
    snapshots = snapshots + 1
    assert(snapshots <= 100, 'snapshot cap')
    for i=0,31 do
        gpr[#gpr+1] = string.format('%08x', tonumber(r.GPR.r[i]))
        cp0[#cp0+1] = string.format('%08x', tonumber(r.CP0.r[i]))
    end
    print(string.format('MUSASHI_BACKUP_EVENTS_CPU label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s',
        label, tonumber(r.pc), tostring(PCSX.getCPUCycles()),
        tonumber(r.GPR.n.hi), tonumber(r.GPR.n.lo),
        table.concat(gpr, ','), table.concat(cp0, ',')))
    if active then
        local table_address, bytes = word(0x120), word(0x124)
        assert(bytes == 22*28, 'unexpected selected event capacity')
        print(string.format('MUSASHI_BACKUP_EVENTS_TABLE label=%s base=%08x bytes=%d', label, table_address, bytes))
        for i=0,21 do
            local values = {}
            for j=0,6 do values[#values+1] = string.format('%08x', word(table_address+i*28+j*4)) end
            print(string.format('MUSASHI_BACKUP_EVENTS_RECORD label=%s slot=%d words=%s', label, i, table.concat(values, ',')))
        end
        local values = {}
        for i=0,15 do values[#values+1] = string.format('%08x', word(0x80078c3c+i*4)) end
        print(string.format('MUSASHI_BACKUP_EVENTS_GAME label=%s base=80078c3c words=%s',label,table.concat(values, ',')))
    end
    if save_ram then
        local f = assert(io.open(output .. '/' .. label .. '.ram','wb'))
        f:write(ffi.string(ram,0x200000)); f:close()
    end
end

local function observe(address, kind, width, label, fn)
    local bp = PCSX.addBreakpoint(address, kind, width, label, function(a,w)
        local ok, keep = pcall(fn,a,w)
        if not ok then print('MUSASHI_BACKUP_EVENTS_ERROR '..tostring(keep)); PCSX.quit(3); return false end
        return keep
    end)
    breakpoints[#breakpoints+1] = bp
end

observe(0x8006189c,'Exec',4,'backup event registration entry',function()
    if tonumber(PCSX.getRegisters().GPR.n.ra) ~= 0x8005fcf8 then return true end
    assert(not active and not returned,'duplicate registration entry')
    active=true; snapshot('ENTRY',true); return true
end)
observe(0x8005fcf8,'Exec',4,'backup event registration return',function()
    if not active then return true end
    assert(not current,'return with active service')
    for _, name in ipairs({'OPEN','ENABLE','TEST'}) do assert(counts[name]==8,'unexpected '..name..' count') end
    assert(counts.SYS1==1 and counts.SYS2==1,'unexpected critical path')
    snapshot('RETURN',true)
    active,returned=false,true
    print(string.format('MUSASHI_BACKUP_EVENTS_COMPLETE snapshots=%d game_writes=%d',snapshots,writes))
    PCSX.quit(0); return false
end)
for _, item in ipairs({
    {0x8005ce78,'OPEN'}, {0x8005ceb8,'ENABLE'}, {0x8005cea8,'TEST'},
    {0x8005cf08,'SYS1'}, {0x8005cf18,'SYS2'},
}) do
    local address, name = item[1], item[2]
    observe(address,'Exec',4,name,function()
        if not active then return true end
        assert(not current,'nested unexpected service')
        counts[name]=(counts[name] or 0)+1
        assert(counts[name]<=8,'service cap')
        local label=name..'_'..counts[name]
        current=label
        snapshot(label..'_ENTRY',false)
        local ra=tonumber(PCSX.getRegisters().GPR.n.ra)
        assert(ra>=0x8006189c and ra<=0x80061c60,'unexpected service return')
        observe(ra,'Exec',4,label..' return',function()
            assert(active and current==label,'service return mismatch')
            snapshot(label..'_RETURN',false); current=nil; return false
        end)
        return true
    end)
end
observe(0x80078c3c,'Write',64,'event handle and counter stores',function(address,width)
    if not active then return true end
    local r=PCSX.getRegisters()
    local instruction=word(tonumber(r.pc))
    assert(width==4 and bit.rshift(instruction,26)==0x2b,'unsupported guest store')
    local rt=bit.band(bit.rshift(instruction,16),31)
    local rs=bit.band(bit.rshift(instruction,21),31)
    local offset=bit.band(instruction,0xffff)
    if offset>=0x8000 then offset=offset-0x10000 end
    assert(bit.band(tonumber(r.GPR.r[rs])+offset,0x1fffffff)==bit.band(address,0x1fffffff),'store address mismatch')
    writes=writes+1; assert(writes<=64,'guest store cap')
    print(string.format('MUSASHI_BACKUP_EVENTS_WRITE order=%d pc=%08x address=%08x value=%08x',
        writes,tonumber(r.pc),address,tonumber(r.GPR.r[rt])))
    return true
end)

print('MUSASHI_BACKUP_EVENTS_ARMED')
local gate=assert(os.getenv('MUSASHI_BACKUP_EVENTS_LAUNCH'))
snapshot('HOST_PAUSED_BEFORE_BOOT',false)
musashi_backup_events_timer=assert(luv).new_timer()
musashi_backup_events_timer:start(50,50,function()
    local f=io.open(gate,'rb')
    if f then
        f:close();musashi_backup_events_timer:stop();musashi_backup_events_timer:close()
        print('MUSASHI_BACKUP_EVENTS_HOST_RESUME');PCSX.resumeEmulator()
    end
end)
