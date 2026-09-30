-- Normal GUI interpreter boot. Observer reads CPU/RAM/ROM only.
-- Never read MMIO or write guest memory/registers/input; no state loading.
local ffi,bit=require('ffi'),require('bit')
local ram,rom=PCSX.getMemPtr(),PCSX.getRomPtr()
local output=assert(os.getenv('MUSASHI_SPU_STARTUP_DIR'))
local active,returned=false,false
local order,writes,reads,prewrites,snapshots,saves=0,0,0,0,0,0
local unresolved,mandatory_unknown,pending_count,sites=0,0,0,0
local counts,bps,pending,installed,services={},{},{},{},{}
local dma_written={}
local first_return,second_entry,nested_return=false,false,false
local stream=assert(io.open(output..'/mmio.tsv','wb'))
local prelude=assert(io.open(output..'/prelude.tsv','wb'))
local header='order\tkind\tpc\taddress\twidth\tvalue\tcycles\tread_id\n'
stream:write(header);prelude:write(header)
musashi_spu_startup_breakpoints=bps
local function word(address)
    local p,m=bit.band(address,0x1fffffff),ram
    if p>=0x1fc00000 and p<=0x1fc7fffc then p,m=p-0x1fc00000,rom
    else assert(p>=0 and p<=0x1ffffc,'outside RAM/ROM') end
    return tonumber(m[p])+tonumber(m[p+1])*256+tonumber(m[p+2])*65536+tonumber(m[p+3])*16777216
end
local function emit(kind,pc,address,width,value,id,is_pre)
    order=order+1
    local f=is_pre and prelude or stream
    f:write(string.format('%d\t%s\t%08x\t%08x\t%d\t%08x\t%s\t%d\n',
        order,kind,pc,address,width,value,tostring(PCSX.getCPUCycles()),id or 0))
end
local function snapshot(label,save)
    snapshots=snapshots+1;assert(snapshots<=250,'snapshot cap')
    local r,g,c=PCSX.getRegisters(),{},{}
    for i=0,31 do g[#g+1]=string.format('%08x',tonumber(r.GPR.r[i]));c[#c+1]=string.format('%08x',tonumber(r.CP0.r[i])) end
    emit('CPU_MARK',tonumber(r.pc),0,0,snapshots)
    print(string.format('MUSASHI_SPU_STARTUP_CPU order=%d label=%s pc=%08x cycles=%s hi=%08x lo=%08x gpr=%s cp0=%s',
        order,label,tonumber(r.pc),tostring(PCSX.getCPUCycles()),tonumber(r.GPR.n.hi),tonumber(r.GPR.n.lo),table.concat(g,','),table.concat(c,',')))
    if active then
        local base,bytes=word(0x120),word(0x124)
        local physical=bit.band(base,0x1fffffff)
        assert(bytes==22*28 and physical<=0x200000-bytes and bit.band(base,3)==0,'event table bounds/layout')
        print(string.format('MUSASHI_SPU_STARTUP_TABLE order=%d base=%08x bytes=%d',order,base,bytes))
        for i=0,21 do
            local values={}
            for j=0,6 do values[#values+1]=string.format('%08x',word(base+i*28+j*4)) end
            print(string.format('MUSASHI_SPU_STARTUP_EVENT order=%d slot=%d words=%s',order,i,table.concat(values,',')))
        end
        for _,a in ipairs({0x8006b0e0,0x8006b548,0x8006b54c,0x8006b550,0x8006b554,
            0x8006b558,0x8006b55c,0x8006b560,0x8006b564,0x8006b568,
            0x8006bb0c,0x8006bb2c,0x8006bb34,0x8006bb38}) do
            print(string.format('MUSASHI_SPU_STARTUP_RAM order=%d address=%08x value=%08x',order,a,word(a)))
        end
    end
    if save then
        saves=saves+1;assert(saves<=16,'RAM snapshot cap')
        local f=assert(io.open(output..'/'..label..'.ram','wb'));f:write(ffi.string(ram,0x200000));f:close()
    end
end
local function observe(address,kind,width,label,fn)
    bps[#bps+1]=PCSX.addBreakpoint(address,kind,width,label,function(a,w)
        local ok,keep=pcall(fn,a,w)
        if not ok then
            stream:flush();prelude:flush()
            print('MUSASHI_SPU_STARTUP_ERROR '..tostring(keep));PCSX.quit(3);return false
        end
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
local function spu(address) return address>=0x1f801c00 and address<=0x1f801fff end
local function unknown(kind,pc,address,width,value,id)
    unresolved=unresolved+1
    if spu(address) then mandatory_unknown=mandatory_unknown+1 end
    emit(kind,pc,address,width,value,id)
end
observe(0x1f801000,'Write',0x1000,'decoded device stores',function(a,w)
    local selected=spu(a) or (a>=0x1f8010c0 and a<=0x1f8010cb) or
        a==0x1f8010f0 or a==0x1f8010f4 or a==0x1f801070 or a==0x1f801074
    if not active and not selected then return true end
    local pc,rt,value=decode(a,w,true);value=value%(2^(w*8))
    if not active then
        prewrites=prewrites+1;assert(prewrites<=1000000,'prelude write cap')
        emit('PREWRITE',pc,a,w,value,0,true)
    else
        writes=writes+1;assert(writes<=200000,'active write cap')
        emit('WRITE',pc,a,w,value)
    end
    if a==0x1f8010c0 or a==0x1f8010c4 or a==0x1f8010c8 then
        -- This is source-write provenance, not a read of current DMA state.
        dma_written[a]=w==4 and value or nil
        if active and a==0x1f8010c8 and w==4 and bit.band(value,0x01000000)~=0 then
            counts.DMA4_START=(counts.DMA4_START or 0)+1
            local function known(reg)
                return dma_written[reg] and string.format('%08x',dma_written[reg]) or 'UNKNOWN'
            end
            print(string.format('MUSASHI_SPU_STARTUP_DMA4_START order=%d madr_last_write=%s bcr_last_write=%s chcr_store=%08x',
                order,known(0x1f8010c0),known(0x1f8010c4),value))
            snapshot('DMA4_START_'..counts.DMA4_START,true)
        end
    end
    return true
end)
observe(0x1f801000,'Read',0x1000,'executed load observation',function(a,w)
    if not active then return true end
    local pc,rt,old,ins=decode(a,w,false)
    reads=reads+1;assert(reads<=300000,'read cap');emit('READ_BEGIN',pc,a,w,old,reads)
    local delay=word(pc+4)
    if not safe_delay(delay,rt) then unknown('UNRESOLVED_DELAY',pc,a,w,delay,reads);return true end
    assert(not pending[pc],'overlapping load invocation')
    pending_count=pending_count+1;assert(pending_count<=256,'pending load cap')
    pending[pc]={id=reads,address=a,width=w,rt=rt,stage=false,ins=ins,delay=delay}
    if not installed[pc] then
        sites=sites+1;assert(sites<=512,'read site cap');installed[pc]=true
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
                pending[pc]=nil;pending_count=pending_count-1
            end
            return true
        end)
    end
    return true
end)
observe(0x800101ec,'Exec',4,'CdInit returned; begin SPU window',function()
    assert(not active and not returned,'duplicate capture entry')
    active=true;snapshot('ENTRY_101EC',true);return true
end)
local stages={
    {0x8002c8f4,'OUTER',0x800101fc,true},
    {0x8003a424,'SPU_INIT',nil,false}, {0x8003a444,'SPU_BODY'},
    {0x8002c904,'FIRST_RETURN',nil,true}, {0x8003d518,'SECOND_ENTRY',0x8002c90c},
    {0x8003d530,'NESTED_RETURN'}, {0x8003d548,'SHADOW_UPLOAD'},
    {0x8003d538,'SHADOW_RETURN'}, {0x80042580,'IRQ_INIT'},
    {0x8003a5b8,'SPU_REG_INIT'}, {0x8003a53c,'EVENT_INIT'},
    {0x8003b14c,'DMA_REGISTER'}, {0x8003af04,'SPU_TRANSFER'},
    {0x8003aa18,'SPU_DMA_CALLBACK'}, {0x8003fe3c,'POST_SHADOW'},
    {0x80042e08,'DMA_IRQ'}, {0x800427f4,'GAME_IRQ'},
    {0x80045290,'CD_IRQ'}, {0x8005d734,'INPUT_IRQ'}
}
for _,item in ipairs(stages) do
    local pc,name,ra,save=item[1],item[2],item[3],item[4]
    observe(pc,'Exec',4,name,function()
        if not active then return true end
        counts[name]=(counts[name]or 0)+1;assert(counts[name]<=100000,'call cap')
        if ra then assert(tonumber(PCSX.getRegisters().GPR.n.ra)==ra,'unexpected stage caller') end
        if name=='SPU_INIT' then
            local actual=tonumber(PCSX.getRegisters().GPR.n.ra)
            assert(actual==(counts[name]==1 and 0x8002c904 or 0x8003d530) and counts[name]<=2,'unexpected SPU init call')
        elseif name=='FIRST_RETURN' then assert(not first_return,'duplicate first return');first_return=true
        elseif name=='SECOND_ENTRY' then assert(first_return and not second_entry,'second entry order');second_entry=true
        elseif name=='NESTED_RETURN' then assert(second_entry and counts.SPU_INIT==2,'nested return order');nested_return=true end
        emit('STAGE',pc,0,0,counts[name])
        if counts[name]<=4 then snapshot(name..'_'..counts[name],save) end
        return true
    end)
end
for _,item in ipairs({{0x8005ce78,'OPEN'},{0x8005ce88,'CLOSE'},{0x8005ce98,'WAIT'},
    {0x8005cea8,'TEST'},{0x8005ceb8,'ENABLE'},{0x8005cec8,'DISABLE'},
    {0x8005cf08,'SYS1'},{0x8005cf18,'SYS2'}}) do
    local pc,name=item[1],item[2]
    observe(pc,'Exec',4,name,function()
        if not active then return true end
        counts[name]=(counts[name]or 0)+1;assert(counts[name]<=32,'service count cap')
        local label=name..'_'..counts[name]
        local ra=tonumber(PCSX.getRegisters().GPR.n.ra)
        assert(bit.band(ra,3)==0 and bit.band(ra,0x1fffffff)<0x200000,'service return outside RAM')
        assert(#services<32,'service depth cap');services[#services+1]=label
        snapshot(label..'_ENTRY',false)
        observe(ra,'Exec',4,label..' return',function()
            assert(active and services[#services]==label,'service return order')
            snapshot(label..'_RETURN',false);services[#services]=nil;return false
        end)
        return true
    end)
end
for _,pc in ipairs({0x8005cf0c,0x8005cf1c}) do
    observe(pc,'Exec',4,'actual syscall',function()
        if active then snapshot(string.format('SYSCALL_%08x',pc),false) end
        return true
    end)
end
observe(0xb0,'Exec',4,'BIOS event vector',function()
    if not active then return true end
    local service=tonumber(PCSX.getRegisters().GPR.n.t1)
    if not ({[7]=true,[8]=true,[9]=true,[10]=true,[11]=true,[12]=true,[13]=true,[32]=true})[service] then return true end
    local name='B0_'..service;counts[name]=(counts[name]or 0)+1
    assert(counts[name]<=100000,'BIOS event vector cap')
    emit('BIOS_EVENT',0xb0,0,0,service)
    local r=PCSX.getRegisters()
    print(string.format('MUSASHI_SPU_STARTUP_BIOS_ARGS order=%d service=%02x a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x sr=%08x',
        order,service,tonumber(r.GPR.r[4]),tonumber(r.GPR.r[5]),tonumber(r.GPR.r[6]),tonumber(r.GPR.r[7]),
        tonumber(r.GPR.r[31]),tonumber(r.CP0.r[12])))
    if counts[name]<=4 then snapshot(name..'_'..counts[name],false) end
    return true
end)
for _,address in ipairs({0x8006b0e0,0x8006b548}) do
    observe(address,'Write',4,'SPU event owned RAM',function(a,w)
        if active then local pc,rt,value=decode(a,w,true);emit('RAM_WRITE',pc,a,w,value%(2^(w*8))) end
        return true
    end)
end
observe(0x8002c90c,'Exec',4,'second initial SPU call returned',function()
    if not active then return true end
    assert(first_return and second_entry and nested_return and counts.SPU_INIT==2 and
        counts.SHADOW_RETURN==1 and #services==0,'incomplete initial call graph')
    for pc,p in pairs(pending) do unknown('UNRESOLVED_RETURN',pc,p.address,p.width,p.ins,p.id) end
    snapshot('RETURN_2C90C',true)
    active,returned=false,true
    for name,n in pairs(counts) do print(string.format('MUSASHI_SPU_STARTUP_COUNT name=%s count=%d',name,n)) end
    print(string.format('MUSASHI_SPU_STARTUP_COMPLETE boundary_returned=1 authority_complete=%d writes=%d reads=%d prewrites=%d unresolved=%d mandatory_spu_unknown=%d snapshots=%d',
        unresolved==0 and 1 or 0,writes,reads,prewrites,unresolved,mandatory_unknown,snapshots))
    stream:close();prelude:close();PCSX.quit(mandatory_unknown==0 and 0 or 3);return false
end)
print('MUSASHI_SPU_STARTUP_ARMED');snapshot('HOST_PAUSED_BEFORE_BOOT',false)
musashi_spu_startup_timer=assert(luv).new_timer()
musashi_spu_startup_timer:start(50,50,function()
    local f=io.open(output..'/launch','rb')
    if f then f:close();musashi_spu_startup_timer:stop();musashi_spu_startup_timer:close();print('MUSASHI_SPU_STARTUP_HOST_RESUME');PCSX.resumeEmulator() end
end)
