-- Natural retail input setup, with disconnected cards selected in a private GUI
-- profile. Reads CPU/RAM/ROM and observes guest stores; never writes guest state,
-- reads MMIO, or supplies game input. The launch file only resumes the debugger.
local ffi, bit = require('ffi'), require('bit')
local ram, rom = PCSX.getMemPtr(), PCSX.getRomPtr()
local output = assert(os.getenv('MUSASHI_INPUT_KERNEL_DIR'))
local active, returned, snapshots, writes = false, false, 0, 0
local counts, breakpoints = {}, {}
musashi_input_kernel_breakpoints = breakpoints
local function word(address)
    local offset, memory = bit.band(address, 0x1fffffff), ram
    if offset >= 0x1fc00000 and offset <= 0x1fc7fffc then
        offset, memory = offset - 0x1fc00000, rom
    else assert(offset >= 0 and offset <= 0x1ffffc, 'read outside RAM/ROM') end
    return tonumber(memory[offset]) + tonumber(memory[offset+1])*256 +
        tonumber(memory[offset+2])*65536 + tonumber(memory[offset+3])*16777216
end
local function snapshot(label, save_ram)
    snapshots = snapshots + 1; assert(snapshots <= 160, 'snapshot cap')
    local r, gpr, cp0 = PCSX.getRegisters(), {}, {}
    for i=0,31 do
        gpr[#gpr+1] = string.format('%08x', tonumber(r.GPR.r[i]))
        cp0[#cp0+1] = string.format('%08x', tonumber(r.CP0.r[i]))
    end
    print(string.format('MUSASHI_INPUT_KERNEL_CPU label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s',
        label,tonumber(r.pc),tostring(PCSX.getCPUCycles()),tonumber(r.GPR.n.hi),tonumber(r.GPR.n.lo),
        table.concat(gpr,','),table.concat(cp0,',')))
    if active then
        local base=word(0x100)
        assert(word(0x104)==32,'unexpected priority table size')
        for priority=0,3 do
            local node,seen=word(base+priority*8),{}
            print(string.format('MUSASHI_INPUT_KERNEL_HEAD label=%s priority=%d slot=%08x head=%08x auxiliary=%08x',
                label,priority,base+priority*8,node,word(base+priority*8+4)))
            local depth=0
            while node~=0 do
                depth=depth+1; assert(depth<=16 and not seen[node],'invalid priority list')
                seen[node]=true
                print(string.format('MUSASHI_INPUT_KERNEL_NODE label=%s priority=%d depth=%d address=%08x words=%08x,%08x,%08x,%08x',
                    label,priority,depth,node,word(node),word(node+4),word(node+8),word(node+12)))
                node=word(node)
            end
        end
        for _, address in ipairs({0x860c,0x8914,0x74b8,0x74bc,0x7295c,0x72960,0x72984,0x72990,0x72994,
                0x7299c,0x729a0,0x729a4,0x729ac,0x729b0,0x729b4,0x729b8,0x729bc,0x729c0,0x729c4,
                0x78988,0x7898c,0x78990,0x78994,0x78998,0x7899c}) do
            print(string.format('MUSASHI_INPUT_KERNEL_RAM label=%s address=%08x value=%08x',label,address,word(address)))
        end
        local record=word(0x72990)
        print(string.format('MUSASHI_INPUT_KERNEL_RECORDS label=%s base=%08x byte49=%02x,%02x',
            label,record,tonumber(ram[bit.band(record,0x1fffffff)+0x49]),tonumber(ram[bit.band(record,0x1fffffff)+0xf0+0x49])))
    end
    if save_ram then
        local f=assert(io.open(output..'/'..label..'.ram','wb'))
        f:write(ffi.string(ram,0x200000));f:close()
    end
end
local function observe(address,kind,width,label,fn)
    breakpoints[#breakpoints+1]=PCSX.addBreakpoint(address,kind,width,label,function(a,w)
        local ok,keep=pcall(fn,a,w)
        if not ok then print('MUSASHI_INPUT_KERNEL_ERROR '..tostring(keep));PCSX.quit(3);return false end
        return keep
    end)
end
observe(0x8005d8b4,'Exec',4,'input setup entry',function()
    if tonumber(PCSX.getRegisters().GPR.n.ra)~=0x8005d108 then return true end
    assert(not active and not returned,'duplicate entry')
    active=true;snapshot('ENTRY',true);return true
end)
for _,item in ipairs({
    {0x8005cf08,'SYS1',0x8005d8cc}, {0x8005e178,'C003',0x8005d8e0},
    {0x8005e168,'C002',0x8005d8ec}, {0x8005cf58,'C00A',0x8005d914},
    {0x8005cf18,'SYS2',0x8005d91c}, {0x8005f228,'RECORD0',0x8005d938},
    {0x8005f228,'RECORD1',0x8005d954},
}) do
    local address,name,return_pc=item[1],item[2],item[3]
    observe(address,'Exec',4,name,function()
        if not active or tonumber(PCSX.getRegisters().GPR.n.ra)~=return_pc then return true end
        counts[name]=(counts[name] or 0)+1;assert(counts[name]==1,'duplicate service')
        snapshot(name..'_ENTRY',true);return true
    end)
    observe(return_pc,'Exec',4,name..' return',function()
        if active then assert(counts[name]==1,'missing service entry');snapshot(name..'_RETURN',true) end
        return true
    end)
end
for _,item in ipairs({{0x1420,'BIOS_C002'}, {0x1444,'BIOS_C003'}, {0x15d8,'BIOS_C00A'},
        {0x8005d6cc,'GAME_VERIFIER'}, {0x8005d734,'GAME_HANDLER'}, {0x49bc,'BIOS_SIO_HANDLER'},
        {0x4a4c,'BIOS_SIO_VERIFIER'}, {0x8005d9c4,'GAME_SIO_START'}, {0x8005dbd8,'GAME_SIO_STEP'}}) do
    local address,name=item[1],item[2]
    observe(address,'Exec',4,name,function()
        if active then
            counts[name]=(counts[name] or 0)+1;assert(counts[name]<=16,'handler observation cap')
            snapshot(name..'_'..counts[name],false)
        end
        return true
    end)
end
observe(0x8005d108,'Exec',4,'input prefix return',function()
    if active then snapshot('PREFIX_RETURN',true) end
    return true
end)
observe(0x800101e4,'Exec',4,'outer input caller return',function()
    if not active then return true end
    for _,name in ipairs({'SYS1','C003','C002','C00A','SYS2','RECORD0','RECORD1'}) do
        assert(counts[name]==1,'missing '..name)
    end
    snapshot('RETURN',true);active,returned=false,true
    print(string.format('MUSASHI_INPUT_KERNEL_COMPLETE snapshots=%d writes=%d',snapshots,writes))
    PCSX.quit(0);return false
end)
observe(0x1f801000,'Write',0x1000,'input MMIO stores',function(address,width)
    if not active then return true end
    local r=PCSX.getRegisters();local instruction=word(tonumber(r.pc))
    assert(({[0x28]=1,[0x29]=2,[0x2b]=4})[bit.rshift(instruction,26)]==width,'unsupported store')
    local rt,rs=bit.band(bit.rshift(instruction,16),31),bit.band(bit.rshift(instruction,21),31)
    local offset=bit.band(instruction,0xffff);if offset>=0x8000 then offset=offset-0x10000 end
    assert(bit.band(tonumber(r.GPR.r[rs])+offset,0x1fffffff)==bit.band(address,0x1fffffff),'store address mismatch')
    writes=writes+1;assert(writes<=5000,'MMIO write cap')
    print(string.format('MUSASHI_INPUT_KERNEL_WRITE order=%d pc=%08x address=%08x width=%d value=%08x',
        writes,tonumber(r.pc),address,width,tonumber(r.GPR.r[rt])%(2^(width*8))))
    return true
end)
print('MUSASHI_INPUT_KERNEL_ARMED')
snapshot('HOST_PAUSED_BEFORE_BOOT',false)
local gate=assert(os.getenv('MUSASHI_INPUT_KERNEL_LAUNCH'))
musashi_input_kernel_timer=assert(luv).new_timer()
musashi_input_kernel_timer:start(50,50,function()
    local f=io.open(gate,'rb')
    if f then
        f:close();musashi_input_kernel_timer:stop();musashi_input_kernel_timer:close()
        print('MUSASHI_INPUT_KERNEL_HOST_RESUME');PCSX.resumeEmulator()
    end
end)
