-- Normal GUI retail boot. CPU/RAM/ROM reads and decoded guest MMIO stores only;
-- never read MMIO, alter guest state/registers/input, or supply completions.
local ffi,bit=require('ffi'),require('bit')
local ram,rom=PCSX.getMemPtr(),PCSX.getRomPtr()
local output=assert(os.getenv('MUSASHI_CD_DEVICE_DIR'))
local active,returned=false,false
local writes,reads,snapshots,order,unresolved=0,0,0,0,0
local counts,bps,pending,installed,prewrites={},{},{},{},{}
local bank=-1
local preconfig=0
local stream=assert(io.open(output..'/mmio.tsv','wb'))
stream:write('order\tkind\tpc\taddress\twidth\tvalue\tbank\tcycles\tread_id\n')
musashi_cd_device_breakpoints=bps
local function word(address)
    local p,m=bit.band(address,0x1fffffff),ram
    if p>=0x1fc00000 and p<=0x1fc7fffc then p,m=p-0x1fc00000,rom
    else assert(p>=0 and p<=0x1ffffc,'outside RAM/ROM') end
    return tonumber(m[p])+tonumber(m[p+1])*256+tonumber(m[p+2])*65536+tonumber(m[p+3])*16777216
end
local function emit(kind,pc,address,width,value,id)
    order=order+1
    stream:write(string.format('%d\t%s\t%08x\t%08x\t%d\t%08x\t%d\t%s\t%d\n',
        order,kind,pc,address,width,value,bank,tostring(PCSX.getCPUCycles()),id or 0))
end
local function snapshot(label,save)
    snapshots=snapshots+1;assert(snapshots<=250,'snapshot cap')
    local r,g,c=PCSX.getRegisters(),{},{}
    for i=0,31 do g[#g+1]=string.format('%08x',tonumber(r.GPR.r[i]));c[#c+1]=string.format('%08x',tonumber(r.CP0.r[i])) end
    print(string.format('MUSASHI_CD_DEVICE_CPU label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s bank=%d',
        label,tonumber(r.pc),tostring(PCSX.getCPUCycles()),tonumber(r.GPR.n.hi),tonumber(r.GPR.n.lo),table.concat(g,','),table.concat(c,','),bank))
    if active then
        for _,a in ipairs({0x8006cf4c,0x8006cf50,0x8006cf54,0x8006cf58,0x8006cf5c,0x8006cf60,
            0x8006cf64,0x8006cc84,0x8006cc88,0x8006cc94,0x8006cc98,0x8006bb08,0x8006cbb8}) do
            print(string.format('MUSASHI_CD_DEVICE_RAM label=%s address=%08x value=%08x',label,a,word(a)))
        end
    end
    if save then local f=assert(io.open(output..'/'..label..'.ram','wb'));f:write(ffi.string(ram,0x200000));f:close() end
end
local function observe(address,kind,width,label,fn)
    bps[#bps+1]=PCSX.addBreakpoint(address,kind,width,label,function(a,w)
        local ok,keep=pcall(fn,a,w)
        if not ok then stream:flush();print('MUSASHI_CD_DEVICE_ERROR '..tostring(keep));PCSX.quit(3);return false end
        return keep
    end)
end
local function decode(address,width,store)
    local r=PCSX.getRegisters();local pc=tonumber(r.pc);local ins=word(pc)
    local op=bit.rshift(ins,26);local expected
    if store then expected=({[0x28]=1,[0x29]=2,[0x2b]=4})[op]
    else expected=({[0x20]=1,[0x24]=1,[0x21]=2,[0x25]=2,[0x23]=4})[op] end
    assert(expected==width,'unsupported MMIO opcode')
    local rs,rt=bit.band(bit.rshift(ins,21),31),bit.band(bit.rshift(ins,16),31)
    local offset=bit.band(ins,0xffff);if offset>=0x8000 then offset=offset-0x10000 end
    assert(bit.band(tonumber(r.GPR.r[rs])+offset,0x1fffffff)==bit.band(address,0x1fffffff),'effective address mismatch')
    return pc,rt,tonumber(r.GPR.r[rt]),ins
end
-- Only linear delay instructions with known destination are eligible. At
-- loadPC+4 the load is pending; at loadPC+8 its GPR value has committed.
local function safe_delay(ins,rt)
    local op=bit.rshift(ins,26);local dest
    if op==0 then
        local fn=bit.band(ins,63)
        if not ({[0]=true,[2]=true,[3]=true,[4]=true,[6]=true,[7]=true,
            [0x20]=true,[0x21]=true,[0x22]=true,[0x23]=true,[0x24]=true,[0x25]=true,
            [0x26]=true,[0x27]=true,[0x2a]=true,[0x2b]=true})[fn] then return false end
        dest=bit.band(bit.rshift(ins,11),31)
    elseif op>=8 and op<=15 then dest=bit.band(bit.rshift(ins,16),31)
    elseif op==0x20 or op==0x21 or op==0x23 or op==0x24 or op==0x25 then
        -- The following load stages into the other delayed slot. Even when
        -- both target the same GPR, the first is visible at loadPC+8, before
        -- the second commits after one more instruction (r3000a.h340..366,
        -- psxinterpreter.cc1636..1638). No observer device read is involved.
        return rt~=0
    elseif op==0x28 or op==0x29 or op==0x2b then dest=0
    else return false end
    return rt~=0 and dest~=rt
end
observe(0x1f801000,'Write',0x1000,'decoded device stores',function(a,w)
    local initial_config=a==0x1f801120 or a==0x1f801124 or a==0x1f801128 or a==0x1f801074
    if not active and not initial_config and (a<0x1f801800 or a>0x1f801803) then return true end
    local pc,rt,value=decode(a,w,true);value=value%(2^(w*8))
    if not active then
        if initial_config then
            preconfig=preconfig+1;assert(preconfig<=2000,'pre-entry configuration cap')
            print(string.format('MUSASHI_CD_DEVICE_PRECONFIG pc=%08x address=%08x width=%d value=%08x cycles=%s',pc,a,w,value,tostring(PCSX.getCPUCycles())))
            if a~=0x1f801074 then snapshot('TIMER2_CONFIG_'..preconfig,false) end
        else
            if #prewrites==128 then table.remove(prewrites,1) end
            prewrites[#prewrites+1]=string.format('pc=%08x address=%08x width=%d value=%08x bank=%d cycles=%s',pc,a,w,value,bank,tostring(PCSX.getCPUCycles()))
        end
    else
        writes=writes+1;assert(writes<=200000,'write cap');emit('WRITE',pc,a,w,value)
        if a>=0x1f801800 and a<=0x1f801803 or a>=0x1f801d80 and a<=0x1f801dba or a==0x1f801020 then
            print(string.format('MUSASHI_CD_DEVICE_WRITE pc=%08x address=%08x width=%d value=%08x bank=%d',pc,a,w,value,bank))
        end
    end
    if a==0x1f801800 then bank=bit.band(value,3) end
    return true
end)
observe(0x1f801000,'Read',0x1000,'observe loads without reading devices',function(a,w)
    if not active then return true end
    local pc,rt,old,ins=decode(a,w,false)
    reads=reads+1;assert(reads<=300000,'read cap');emit('READ_BEGIN',pc,a,w,old,reads)
    local delay=word(pc+4)
    if not safe_delay(delay,rt) then
        unresolved=unresolved+1;emit('UNRESOLVED_DELAY',pc,a,w,delay,reads);return true
    end
    assert(not pending[pc],'unresolved overlapping load')
    pending[pc]={id=reads,address=a,width=w,rt=rt,stage=false,ins=ins,delay=delay}
    if not installed[pc] then
        installed[pc]=true
        observe(pc+4,'Exec',4,'load delay entered',function()
            local p=pending[pc]
            if p then assert(word(pc+4)==p.delay,'delay source changed');p.stage=true end
            return true
        end)
        observe(pc+8,'Exec',4,'committed load GPR',function()
            local p=pending[pc]
            if p then
                assert(p.stage,'missing linear load delay')
                emit('READ_RESULT',pc,p.address,p.width,tonumber(PCSX.getRegisters().GPR.r[p.rt]),p.id)
                pending[pc]=nil
            end
            return true
        end)
    end
    return true
end)
observe(0x80044d38,'Exec',4,'CD init entry',function()
    if tonumber(PCSX.getRegisters().GPR.n.ra)~=0x80043480 then return true end
    assert(not active and not returned,'duplicate init')
    active=true
    for i,v in ipairs(prewrites) do print('MUSASHI_CD_DEVICE_PREWRITE '..i..' '..v) end
    snapshot('ENTRY',true);return true
end)
for _,item in ipairs({{0x80043480,'LOW_INIT_RETURN'},{0x80044bf4,'SPU_ENTRY'},
    {0x8004349c,'SPU_RETURN'},{0x80044670,'COMMAND'},{0x80045290,'CD_IRQ'},
    {0x80043b9c,'CD_DRAIN'},{0x8005d734,'INPUT_IRQ'},{0x80042e08,'DMA_IRQ'}}) do
    local pc,name=item[1],item[2]
    observe(pc,'Exec',4,name,function()
        if active then
            counts[name]=(counts[name]or 0)+1
            if counts[name]<=12 then snapshot(name..'_'..counts[name],name=='LOW_INIT_RETURN'or name=='SPU_ENTRY'or name=='SPU_RETURN') end
            assert(counts[name]<=100000,'call cap')
        end
        return true
    end)
end
observe(0x800101ec,'Exec',4,'outer CdInit return',function()
    if not active then return true end
    for pc,p in pairs(pending) do unresolved=unresolved+1;emit('UNRESOLVED_RETURN',pc,p.address,p.width,p.ins,p.id) end
    snapshot('RETURN',true);active,returned=false,true
    for name,n in pairs(counts)do print('MUSASHI_CD_DEVICE_COUNT name='..name..' count='..n)end
    print(string.format('MUSASHI_CD_DEVICE_COMPLETE writes=%d reads=%d unresolved=%d snapshots=%d',writes,reads,unresolved,snapshots))
    stream:close();PCSX.quit(0);return false
end)
print('MUSASHI_CD_DEVICE_ARMED');snapshot('HOST_PAUSED_BEFORE_BOOT',false)
local gate=assert(os.getenv('MUSASHI_CD_DEVICE_LAUNCH'))
musashi_cd_device_timer=assert(luv).new_timer()
musashi_cd_device_timer:start(50,50,function()
    local f=io.open(gate,'rb')
    if f then f:close();musashi_cd_device_timer:stop();musashi_cd_device_timer:close();print('MUSASHI_CD_DEVICE_HOST_RESUME');PCSX.resumeEmulator()end
end)
