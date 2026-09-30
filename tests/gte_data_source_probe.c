#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
/* Exact retail transfer leaf plus the camera sqrt tail, explicit fixture
 * caller frame and real shared CPU/image/GTE owners. No native claim. */
#include <stdint.h>
/* Retail result for input 90000 is the approximation 298, not a refined
 * integer sqrt: table halfword 0x12A7 (pinned EXE file 0x5E11A) shifted by
 * the computed t1=8 gives (0x12A7<<8)>>12 = 298. */
#define SQRT90000 298u
#define main prior_gte_fixture_main
#define MUSASHI_GTE_REAL_OPERATOR 1
#include "gte_init_source_probe.c"
#undef main

static unsigned data_writes, data_reads;
static int refuse_write, refuse_read;
static int data_write(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t v) {
    Fixture *x=p;data_writes++;
    return !refuse_write && musashi_gte_owner_write_data(&x->gte,c,r,v);
}
static int data_read(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    Fixture *x=p;data_reads++;
    if(refuse_read){*v=0xbadc0ffe;return 0;}
    return musashi_gte_owner_read_data(&x->gte,c,r,v);
}
static void setup_data(const uint8_t *exe,size_t size,uint32_t input) {
    for(unsigned i=0;i<32;++i)gteRegs.CP2D.p[i].d=0x76540000u+i;
    init(exe,size);assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(f.cpu.pc==0x80052618);
    f.transfer.write_data=data_write;f.transfer.read_data=data_read;
    data_writes=data_reads=0;refuse_write=refuse_read=0;
    /* Test-owned argument bank at the actual JAL and arithmetic delay slot.
     * This does not claim to have run the preceding camera computation. */
    f.cpu.pc=0x80054048;f.cpu.npc=f.cpu.pc+4;
    f.cpu.r[4]=input;f.cpu.r[3]=0;f.cpu.r[2]=300;
    before=f.memory;
    assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.pc==0x8005404c && f.cpu.r[31]==0x80054050 && f.cpu.delay_slot);
    assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.pc==0x80047d3c && f.cpu.r[4]==input && !f.cpu.delay_slot);
}
static void step_at(uint32_t pc) {
    unsigned cycles=f.cycles,calls=f.clock_calls;
    assert(f.cpu.pc==pc && formatter_step(&f.memory,&f.cpu));
    assert(f.cycles==cycles+2 && f.clock_calls==calls+1);
}
static void step_mem(uint32_t pc) {
    unsigned cycles=f.cycles,calls=f.clock_calls;
    assert(f.cpu.pc==pc && formatter_step(&f.memory,&f.cpu));
    assert(f.cycles==cycles+3 && f.clock_calls==calls+1);
}
static void to_pending(uint32_t input,uint32_t count) {
    step_at(0x80047d3c);
    assert(gteRegs.CP2D.p[30].d==input && gteRegs.CP2D.p[31].d==count);
    step_at(0x80047d40);step_at(0x80047d44);step_at(0x80047d48);
    assert(f.cpu.pc==0x80047d4c && f.cpu.r[2]==300 && f.cpu.gte_load_pending);
    assert(f.cpu.gte_load_reg==2 && f.cpu.gte_load_value==count && f.cpu.gte_load_next_pc==0x80047d4c);
    assert(data_writes==1 && data_reads==1);
}
static void unchanged_refusal(int clock_charged) {
    FormatterCpu saved=f.cpu;unsigned cycles=f.cycles,calls=f.clock_calls;
    assert(!formatter_step(&f.memory,&f.cpu));
    assert(!memcmp(&saved,&f.cpu,sizeof(saved)));
    assert(f.cycles==cycles && f.clock_calls==calls+(unsigned)clock_charged);
    assert(!memcmp(&before,&f.memory,sizeof(before)));
}
static void data_case(const uint8_t *exe,size_t size,uint32_t input,uint32_t count,int irq) {
    setup_data(exe,size,input);uint32_t controls[32];
    for(unsigned i=0;i<32;++i)controls[i]=gteRegs.CP2C.p[i].d;
    to_pending(input,count);
    if(irq) {
        FormatterCpu saved=f.cpu;MusashiCpuContext context;
        MusashiCpuExceptionToken token={0};MusashiCpuStatusSnapshot snapshot;
        assert(musashi_boot_cpu_context(&f.cpu,MUSASHI_CPU_CONTEXT_SOURCE,&context));
        assert(musashi_cpu_status_begin(f.status,MUSASHI_CPU_EXCEPTION_IRQ,&context,0x400,&token));
        assert(musashi_bios_kernel_before_exception_with_context(&f.kernel,&token));
        assert(musashi_cpu_status_inspect(f.status,&token,&snapshot));
        assert(snapshot.gpr[2]==300 && snapshot.epc==0x80047d4c && snapshot.kernel_entered);
        assert(musashi_cpu_status_finish(f.status,&token));
        assert(!memcmp(&saved,&f.cpu,sizeof(saved))); /* private pending survives real frame */
    }
    f.refuse_clock_pc=0x80047d4c;unchanged_refusal(1);f.refuse_clock_pc=0;
    FormatterCpu pending=f.cpu;
    f.cpu.npc+=4;unchanged_refusal(0);f.cpu=pending;
    f.cpu.merge_pending=1;unchanged_refusal(0);f.cpu=pending;
    step_at(0x80047d4c);
    assert(f.cpu.r[1]==32 && f.cpu.r[2]==count && !f.cpu.gte_load_pending);
    step_at(0x80047d50);step_at(0x80047d54);
    if(count==32) {
        step_at(0x80047db8);step_at(0x80047dbc);
        assert(f.cpu.pc==0x80054050 && f.cpu.r[2]==0 && f.cpu.r[29]==STACK);
    } else {
        step_at(0x80047d58);step_at(0x80047d5c);step_at(0x80047d60);step_at(0x80047d64);
        step_at(0x80047d68);step_at(0x80047d6c);step_at(0x80047d70);step_at(0x80047d74);
        step_at(0x80047d78);step_at(0x80047d84);step_at(0x80047d88);step_at(0x80047d8c);
        step_at(0x80047d90);step_at(0x80047d94);step_at(0x80047d98);step_at(0x80047d9c);
        {unsigned cycles=f.cycles,calls=f.clock_calls; /* LH takes the data-access cycle */
         assert(f.cpu.pc==0x80047da0 && formatter_step(&f.memory,&f.cpu));
         assert(f.cycles==cycles+3 && f.clock_calls==calls+1);}
        step_at(0x80047da4);step_at(0x80047da8);step_at(0x80047dac);
        step_at(0x80047db0);step_at(0x80047db4);
        assert(input==90000 && f.cpu.pc==0x80054050 && f.cpu.r[2]==SQRT90000 && f.cpu.r[29]==STACK);
    }
    assert(!memcmp(&before,&f.memory,sizeof(before)));
    for(unsigned i=0;i<32;++i) {
        assert(gteRegs.CP2C.p[i].d==controls[i]);
        if(i<30)assert(gteRegs.CP2D.p[i].d==0x76540000u+i);
    }
    close_fixture();
}
static void data_refusals(const uint8_t *exe,size_t size) {
    for(unsigned which=0;which<8;++which) {
        setup_data(exe,size,90000);
        /* SquareRoot0 is caller independent: the native lane calls it
         * with its sentinel RA (func_80013294 after the opening unlock). */
        if(which==0){f.cpu.r[31]=0xffffffffu;step_at(0x80047d3c);
            assert(gteRegs.CP2D.p[30].d==90000 && gteRegs.CP2D.p[31].d==15);close_fixture();continue;}
        if(which==1){f.transfer.write_data=NULL;unchanged_refusal(0);}
        if(which==2){f.refuse_clock_pc=f.cpu.pc;unchanged_refusal(1);}
        if(which==3){refuse_write=1;FormatterCpu saved=f.cpu;assert(!formatter_step(&f.memory,&f.cpu));assert(!memcmp(&saved,&f.cpu,sizeof(saved)));}
        if(which>=4) {
            step_at(0x80047d3c);step_at(0x80047d40);step_at(0x80047d44);
            if(which==4){f.refuse_clock_pc=f.cpu.pc;unchanged_refusal(1);}
            else if(which==6){f.transfer.read_data=NULL;unchanged_refusal(0);}
            else if(which==7){f.cpu.r[31]=0xffffffffu;step_at(0x80047d48);
                assert(f.cpu.gte_load_pending && f.cpu.gte_load_value==15);close_fixture();continue;}
            else {refuse_read=1;FormatterCpu saved=f.cpu;assert(!formatter_step(&f.memory,&f.cpu));assert(!memcmp(&saved,&f.cpu,sizeof(saved)));}
            assert(gteRegs.CP2D.p[30].d==90000 && gteRegs.CP2D.p[31].d==15);
        } else assert(gteRegs.CP2D.p[30].d==0x7654001e && gteRegs.CP2D.p[31].d==0x7654001f);
        assert(!f.cpu.gte_load_pending && !memcmp(&before,&f.memory,sizeof(before)));
        close_fixture();
    }
}
static void to_sqrt_tail(const uint8_t *exe,size_t size) {
    setup_data(exe,size,90000);
    step_at(0x80047d3c);step_at(0x80047d40);step_at(0x80047d44);
    step_at(0x80047d48);step_at(0x80047d4c);step_at(0x80047d50);step_at(0x80047d54);
    step_at(0x80047d58);step_at(0x80047d5c);step_at(0x80047d60);step_at(0x80047d64);
}
static void sub_overflow_traps(const uint8_t *exe,size_t size) {
    to_sqrt_tail(exe,size);
    assert(f.cpu.pc==0x80047d68);
    f.cpu.r[9]=0x7fffffffu;f.cpu.r[10]=0xffffffffu; /* INT32_MAX-(-1) overflows */
    {FormatterCpu saved=f.cpu;assert(!formatter_step(&f.memory,&f.cpu));assert(!memcmp(&saved,&f.cpu,sizeof(saved)));}
    close_fixture();
}
static void second_call(const uint8_t *exe,size_t size) {
    /* Explicit second-caller frame: JAL540D0 plus delay NEGU reaches the
     * leaf with RA540D8. Only the call mechanics and admitted RA are new;
     * the leaf itself is the same checked sequence, input 90000. */
    uint32_t controls[32];unsigned i;
    setup_data(exe,size,90000);
    for(i=0;i<32;++i)controls[i]=gteRegs.CP2C.p[i].d;
    f.cpu.pc=0x800540d0;f.cpu.npc=0x800540d4;f.cpu.r[31]=0x80053208u;
    f.cpu.r[4]=90000;
    assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.pc==0x800540d4 && f.cpu.r[31]==0x800540d8 && f.cpu.delay_slot);
    assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.pc==0x80047d3c && !f.cpu.delay_slot);
    to_pending(90000,15);
    step_at(0x80047d4c);
    assert(f.cpu.r[1]==32 && f.cpu.r[2]==15 && !f.cpu.gte_load_pending);
    step_at(0x80047d50);step_at(0x80047d54);
    step_at(0x80047d58);step_at(0x80047d5c);step_at(0x80047d60);step_at(0x80047d64);
    step_at(0x80047d68);step_at(0x80047d6c);step_at(0x80047d70);step_at(0x80047d74);
    step_at(0x80047d78);step_at(0x80047d84);step_at(0x80047d88);step_at(0x80047d8c);
    step_at(0x80047d90);step_at(0x80047d94);step_at(0x80047d98);step_at(0x80047d9c);
    {unsigned cycles=f.cycles,calls=f.clock_calls;
     assert(f.cpu.pc==0x80047da0 && formatter_step(&f.memory,&f.cpu));
     assert(f.cycles==cycles+3 && f.clock_calls==calls+1);}
    step_at(0x80047da4);step_at(0x80047da8);step_at(0x80047dac);
    step_at(0x80047db0);step_at(0x80047db4);
    assert(f.cpu.pc==0x800540d8 && f.cpu.r[2]==SQRT90000 && f.cpu.r[29]==STACK);
    assert(!memcmp(&before,&f.memory,sizeof(before)));
    for(i=0;i<32;++i)assert(gteRegs.CP2C.p[i].d==controls[i]);
    close_fixture();
}
static void div_site_at(const uint8_t *exe,size_t size,uint32_t pc,unsigned rs) {
    /* Admitted DIV idiom sites share one shape; the helper's own arithmetic
     * is proven by test_mips_signed_div, this checks admission/writeback. */
    setup_data(exe,size,90000);
    f.cpu.pc=pc;f.cpu.npc=pc+4u;
    f.cpu.r[rs]=0x00100000u;f.cpu.r[18]=298u;f.cpu.r[31]=0x800540d8u;
    {unsigned cycles=f.cycles,calls=f.clock_calls;
     assert(formatter_step(&f.memory,&f.cpu));
     assert(f.cycles==cycles+2 && f.clock_calls==calls+1);}
    assert(f.cpu.hi==0x00100000u%298u && f.cpu.lo==0x00100000u/298u);
    assert(f.cpu.pc==pc+4u);
    close_fixture();
}
static void div_site(const uint8_t *exe,size_t size) {
    div_site_at(exe,size,0x80054070u,16);
}
static void div_zero_pin(const uint8_t *exe,size_t size) {
    /* Retail executes DIV before its own zero check, then breaks; the owner
     * records the hardware-conventional result and the break stays refused. */
    setup_data(exe,size,90000);
    f.cpu.pc=0x80054070;f.cpu.npc=0x80054074;
    f.cpu.r[16]=5;f.cpu.r[18]=0;f.cpu.r[31]=0x80014468u;
    assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.hi==5 && f.cpu.lo==0xffffffffu);
    close_fixture();
}
static void div540e0_site(const uint8_t *exe,size_t size) {
    div_site_at(exe,size,0x800540e0u,6);
}
static void div54154_site(const uint8_t *exe,size_t size) {
    div_site_at(exe,size,0x80054154u,5);
}
static void div54194_site(const uint8_t *exe,size_t size) {
    div_site_at(exe,size,0x80054194u,6);
}
static void srav_keeps_sign(const uint8_t *exe,size_t size) {
    to_sqrt_tail(exe,size);
    step_at(0x80047d68);step_at(0x80047d6c);step_at(0x80047d70);step_at(0x80047d74);
    step_at(0x80047d78);step_at(0x80047d84);step_at(0x80047d88);
    assert(f.cpu.pc==0x80047d8c);
    f.cpu.r[11]=4;f.cpu.r[4]=0x80000001u;
    step_at(0x80047d8c);
    assert(f.cpu.r[12]==0xf8000000u);
    close_fixture();
}
static unsigned commands, control_reads;
static int refuse_command, refuse_control_read;
static int gte_command(void *p,const MusashiCpuContext *c,uint32_t w) {
    Fixture *x=p;commands++;
    return !refuse_command && musashi_gte_owner_command(&x->gte,c,w);
}
static int control_read(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    Fixture *x=p;control_reads++;
    if(refuse_control_read){*v=0xbadc0ffe;return 0;}
    return musashi_gte_owner_read_control(&x->gte,c,r,v);
}
/* Explicit 80048D9C frame: identity rotation at a0, three fixture vertices
 * at a1, caller-chosen audited RA. MVMVA sf=1 with cv=3 adds no
 * translation, so each IR triple equals its V0 triple exactly. */
static void setup_camera(const uint8_t *exe,size_t size,uint32_t ra) {
    uint32_t matrix, verts;
    for(unsigned i=0;i<32;++i)gteRegs.CP2D.p[i].d=0x76540000u+i;
    init(exe,size);assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(f.cpu.pc==0x80052618);
    f.transfer.write_data=data_write;f.transfer.read_data=data_read;
    f.transfer.command=gte_command;f.transfer.read_control=control_read;
    data_writes=data_reads=commands=control_reads=0;
    refuse_write=refuse_read=refuse_command=refuse_control_read=0;
    matrix=f.cpu.r[29]-0x80u;verts=matrix+0x20u;
    assert(musashi_boot_write32(&f.memory,matrix+0u,0x00001000u));
    assert(musashi_boot_write32(&f.memory,matrix+4u,0x00000000u));
    assert(musashi_boot_write32(&f.memory,matrix+8u,0x00001000u));
    assert(musashi_boot_write32(&f.memory,matrix+12u,0x00000000u));
    assert(musashi_boot_write32(&f.memory,matrix+16u,0x00001000u));
    assert(musashi_boot_write32(&f.memory,verts+0u,0x00400010u));
    assert(musashi_boot_write32(&f.memory,verts+4u,0x00020000u));
    assert(musashi_boot_write32(&f.memory,verts+8u,0x00090007u));
    assert(musashi_boot_write32(&f.memory,verts+12u,0x00000030u));
    assert(musashi_boot_write32(&f.memory,verts+16u,0x00000025u));
    gteRegs.CP2C.p[11].d=0x55667788u;
    f.cpu.pc=0x80048d9c;f.cpu.npc=f.cpu.pc+4;
    f.cpu.r[4]=matrix;f.cpu.r[5]=verts;f.cpu.r[31]=ra;
    before=f.memory;
}
static void camera_body(uint32_t ra) {
    uint32_t matrix=f.cpu.r[4],verts=f.cpu.r[5],w;
    step_mem(0x80048d9c);step_mem(0x80048da0);step_mem(0x80048da4);
    step_mem(0x80048da8);step_mem(0x80048dac);
    step_at(0x80048db0);step_at(0x80048db4);step_at(0x80048db8);
    step_at(0x80048dbc);step_at(0x80048dc0);
    assert(gteRegs.CP2C.p[0].d==0x00001000u && gteRegs.CP2C.p[1].d==0 &&
           gteRegs.CP2C.p[2].d==0x00001000u && gteRegs.CP2C.p[3].d==0 &&
           gteRegs.CP2C.p[4].d==0x00001000u);
    step_mem(0x80048dc4);step_mem(0x80048dc8);step_mem(0x80048dcc);
    step_at(0x80048dd0);step_at(0x80048dd4);step_at(0x80048dd8);
    step_at(0x80048ddc);step_at(0x80048de0);step_at(0x80048de4);step_at(0x80048de8);
    assert(gteRegs.CP2D.p[9].sw.l==16 && gteRegs.CP2D.p[10].sw.l==2 && gteRegs.CP2D.p[11].sw.l==48);
    step_mem(0x80048dec);step_mem(0x80048df0);step_mem(0x80048df4);
    step_at(0x80048df8);step_at(0x80048dfc);
    step_at(0x80048e00);step_at(0x80048e04);step_at(0x80048e08);
    /* Architectural MFC2 IR reads sign-extend the low signed halfword;
     * seeded shared-bank high halves must not reach the CPU. */
    /* E08's own load is still live here; it publishes when E0C executes. */
    assert(f.cpu.r[11]==0x00000010u && f.cpu.r[12]==0x00000002u &&
           f.cpu.gte_load_pending && f.cpu.gte_load_reg==13 &&
           f.cpu.gte_load_value==0x00000030u && f.cpu.gte_load_next_pc==0x80048e0c);
    step_at(0x80048e0c);
    assert(f.cpu.r[13]==0x00000030u && !f.cpu.gte_load_pending);
    step_at(0x80048e10);step_at(0x80048e14);step_at(0x80048e18);
    /* lh(+14) reads the upper half of Z0, which the fixture leaves zero. */
    assert(gteRegs.CP2D.p[9].sw.l==64 && gteRegs.CP2D.p[10].sw.l==7 && gteRegs.CP2D.p[11].sw.l==0);
    step_mem(0x80048e1c);step_mem(0x80048e20);step_mem(0x80048e24);
    step_at(0x80048e28);step_at(0x80048e2c);step_at(0x80048e30);
    step_at(0x80048e34);step_at(0x80048e38);step_at(0x80048e3c);
    step_at(0x80048e40);step_at(0x80048e44);step_at(0x80048e48);step_at(0x80048e4c);
    assert(gteRegs.CP2D.p[9].sw.l==0 && gteRegs.CP2D.p[10].sw.l==9 && gteRegs.CP2D.p[11].sw.l==37);
    step_at(0x80048e50);step_at(0x80048e54);step_at(0x80048e58);step_mem(0x80048e5c);
    step_at(0x80048e60);step_at(0x80048e64);step_at(0x80048e68);step_mem(0x80048e6c);
    step_at(0x80048e70);step_at(0x80048e74);
    step_at(0x80048e78);step_at(0x80048e7c);step_at(0x80048e80);step_mem(0x80048e84);
    step_at(0x80048e88);step_at(0x80048e8c);step_at(0x80048e90);step_mem(0x80048e94);
    step_mem(0x80048e98);
    assert(musashi_boot_read32(&f.memory,matrix+0u,&w)&&w==0x00400010u);
    assert(musashi_boot_read32(&f.memory,matrix+4u,&w)&&w==0x00020000u);
    assert(musashi_boot_read32(&f.memory,matrix+8u,&w)&&w==0x00090007u);
    assert(musashi_boot_read32(&f.memory,matrix+12u,&w)&&w==0x00000030u);
    assert(musashi_boot_read32(&f.memory,matrix+16u,&w)&&w==0x55667788u);
    step_at(0x80048e9c);step_at(0x80048ea0);step_at(0x80048ea4);
    assert(f.cpu.pc==ra && f.cpu.r[2]==matrix && f.cpu.r[29]==STACK);
    assert(f.cpu.r[4]==matrix && f.cpu.r[5]==verts);
    for(unsigned i=0;i<5;++i)
        assert(musashi_boot_read32(&f.memory,verts+i*4u,&w) &&
               w==((uint32_t[]){0x00400010u,0x00020000u,0x00090007u,0x00000030u,0x00000025u})[i]);
    assert(data_writes==6 && data_reads==8 && commands==3 && control_reads==1);
}
static void camera_case(const uint8_t *exe,size_t size) {
    setup_camera(exe,size,0x80052edcu);
    camera_body(0x80052edcu);
    close_fixture();
}
static void camera_jal_case(const uint8_t *exe,size_t size) {
    /* Real call edge: JAL at 80054130 with s3=verts reaches the leaf with
     * RA=54138, runs the identical body, and returns to the caller. The
     * delay slot (addu a1,s3,zero) re-establishes a1 from s3. */
    uint32_t verts;
    setup_camera(exe,size,0x80054138u);
    verts=f.cpu.r[5];
    f.cpu.pc=0x80054130u;f.cpu.npc=0x80054134u;
    f.cpu.r[19]=verts;f.cpu.r[31]=0x80050000u;
    step_at(0x80054130);step_at(0x80054134);
    assert(f.cpu.pc==0x80048d9c && f.cpu.r[31]==0x80054138u &&
           !f.cpu.delay_slot && f.cpu.r[5]==verts);
    camera_body(0x80054138u);
    close_fixture();
}
static void camera_refusals(const uint8_t *exe,size_t size) {
    /* Wrong RA at the first CTC2. */
    setup_camera(exe,size,0x80052edcu);
        f.cpu.pc=0x80048db0;f.cpu.npc=0x80048db4;f.cpu.r[31]=0x80050000u;
    unchanged_refusal(0);
    assert(gteRegs.CP2C.p[0].d==0xa5000000u);
    close_fixture();
    /* Unbound MVMVA dispatch at the first vector core. */
    setup_camera(exe,size,0x80052edcu);
        f.cpu.pc=0x80048de8;f.cpu.npc=0x80048dec;f.transfer.command=NULL;
    unchanged_refusal(0);
    close_fixture();
    /* Unbound control read at the SWC2. */
    setup_camera(exe,size,0x80052edcu);
        f.cpu.pc=0x80048e98;f.cpu.npc=0x80048e9c;f.transfer.read_control=NULL;
    unchanged_refusal(0);
    close_fixture();
    /* Refused owner command leaves the bank untouched. */
    setup_camera(exe,size,0x80052edcu);
        f.cpu.pc=0x80048de8;f.cpu.npc=0x80048dec;refuse_command=1;
    {FormatterCpu saved=f.cpu;assert(!formatter_step(&f.memory,&f.cpu));assert(!memcmp(&saved,&f.cpu,sizeof(saved)));}
    close_fixture();
}
/* Explicit 800484EC frame: identity rotation at a0, sign/magnitude input
 * triple [65535,-65536,16] at a1, outputs at a2, RA=52F64 (audited JAL at
 * 52F5C). The abs idiom splits each input into (magnitude & 7FFF, sign>>15);
 * MVMVA#1 (sf=0, v=IR) yields MAC=(4096,-8192,0), the second abs pass scales
 * by 8 to (32768,-65536,0), MVMVA#2 (sf=1) yields (32767,0,16), and the
 * outputs compose to [65535,-65536,16]. Branch mix: taken,untaken,taken in
 * both halves. */
static void setup_camera2(const uint8_t *exe,size_t size,uint32_t ra) {
    uint32_t matrix, verts, out;
    for(unsigned i=0;i<32;++i)gteRegs.CP2D.p[i].d=0x76540000u+i;
    init(exe,size);assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(f.cpu.pc==0x80052618);
    f.transfer.write_data=data_write;f.transfer.read_data=data_read;
    f.transfer.command=gte_command;f.transfer.read_control=control_read;
    data_writes=data_reads=commands=control_reads=0;
    refuse_write=refuse_read=refuse_command=refuse_control_read=0;
    matrix=f.cpu.r[29]-0x80u;verts=matrix+0x20u;out=matrix+0x40u;
    assert(musashi_boot_write32(&f.memory,matrix+0u,0x00001000u));
    assert(musashi_boot_write32(&f.memory,matrix+4u,0x00000000u));
    assert(musashi_boot_write32(&f.memory,matrix+8u,0x00001000u));
    assert(musashi_boot_write32(&f.memory,matrix+12u,0x00000000u));
    assert(musashi_boot_write32(&f.memory,matrix+16u,0x00001000u));
    assert(musashi_boot_write32(&f.memory,verts+0u,0x0000ffffu));
    assert(musashi_boot_write32(&f.memory,verts+4u,0xffff0000u));
    assert(musashi_boot_write32(&f.memory,verts+8u,0x00000010u));
    assert(musashi_boot_write32(&f.memory,out+0u,0xdeadbeefu));
    assert(musashi_boot_write32(&f.memory,out+4u,0xdeadbeefu));
    assert(musashi_boot_write32(&f.memory,out+8u,0xdeadbeefu));
    f.cpu.pc=0x800484ec;f.cpu.npc=f.cpu.pc+4;
    f.cpu.r[4]=matrix;f.cpu.r[5]=verts;f.cpu.r[6]=out;f.cpu.r[31]=ra;
    before=f.memory;
}
static void camera2_body(uint32_t ra) {
    uint32_t matrix=f.cpu.r[4],verts=f.cpu.r[5],out=f.cpu.r[6],w;
    step_mem(0x800484ec);step_mem(0x800484f0);step_mem(0x800484f4);
    step_mem(0x800484f8);step_mem(0x800484fc);
    step_at(0x80048500);step_at(0x80048504);step_at(0x80048508);
    step_at(0x8004850c);step_at(0x80048510);
    assert(gteRegs.CP2C.p[0].d==0x00001000u && gteRegs.CP2C.p[1].d==0 &&
           gteRegs.CP2C.p[2].d==0x00001000u && gteRegs.CP2C.p[3].d==0 &&
           gteRegs.CP2C.p[4].d==0x00001000u);
    step_mem(0x80048514);step_mem(0x80048518);step_mem(0x8004851c);
    step_at(0x80048520);step_at(0x80048524);step_at(0x80048544);
    assert(f.cpu.r[8]==0x00007fffu && f.cpu.r[11]==1);
    step_at(0x80048548);step_at(0x8004854c);
    step_at(0x80048550);step_at(0x80048554);step_at(0x80048558);
    step_at(0x8004855c);step_at(0x80048560);step_at(0x80048564);
    assert(f.cpu.r[9]==0 && f.cpu.r[12]==0xfffffffeu);
    step_at(0x80048570);step_at(0x80048574);step_at(0x80048594);
    assert(f.cpu.r[10]==0x00000010u && f.cpu.r[13]==0);
    step_at(0x80048598);step_at(0x8004859c);step_at(0x800485a0);
    assert(gteRegs.CP2D.p[9].d==1 && gteRegs.CP2D.p[10].d==0xfffffffeu &&
           gteRegs.CP2D.p[11].d==0);
    step_at(0x800485a4);step_at(0x800485a8);
    assert(gteRegs.CP2D.p[25].sd==4096 && gteRegs.CP2D.p[26].sd==-8192 &&
           gteRegs.CP2D.p[27].sd==0);
    step_at(0x800485ac);step_at(0x800485b0);step_at(0x800485b4);
    assert(f.cpu.r[11]==0x00001000u && f.cpu.r[12]==0xffffe000u &&
           f.cpu.gte_load_pending && f.cpu.gte_load_reg==13 &&
           f.cpu.gte_load_value==0 && f.cpu.gte_load_next_pc==0x800485b8u);
    step_at(0x800485b8);
    assert(f.cpu.r[13]==0 && !f.cpu.gte_load_pending);
    step_at(0x800485bc);step_at(0x800485c0);
    assert(gteRegs.CP2D.p[9].d==0x00007fffu && gteRegs.CP2D.p[10].d==0 &&
           gteRegs.CP2D.p[11].d==0x00000010u);
    step_at(0x800485c4);step_at(0x800485c8);
    assert(gteRegs.CP2D.p[25].sd==32767 && gteRegs.CP2D.p[26].sd==0 &&
           gteRegs.CP2D.p[27].sd==16);
    step_at(0x800485cc);step_at(0x800485d0);step_at(0x800485e4);
    assert(f.cpu.r[11]==0x00008000u);
    step_at(0x800485e8);step_at(0x800485ec);
    step_at(0x800485f0);step_at(0x800485f4);step_at(0x800485f8);
    step_at(0x800485fc);
    assert((int32_t)f.cpu.r[12]==-65536);
    step_at(0x80048604);step_at(0x80048608);step_at(0x8004861c);
    assert(f.cpu.r[13]==0);
    step_at(0x80048620);step_at(0x80048624);step_at(0x80048628);
    assert(f.cpu.r[8]==0x00007fffu && f.cpu.r[9]==0 &&
           f.cpu.gte_load_pending && f.cpu.gte_load_reg==10 &&
           f.cpu.gte_load_value==16 && f.cpu.gte_load_next_pc==0x8004862cu);
    step_at(0x8004862c);
    assert(f.cpu.r[10]==16 && !f.cpu.gte_load_pending);
    step_at(0x80048630);step_at(0x80048634);
    assert(f.cpu.r[8]==0x0000ffffu && f.cpu.r[9]==0xffff0000u);
    step_mem(0x80048638);step_mem(0x8004863c);step_mem(0x80048640);
    assert(musashi_boot_read32(&f.memory,out+0u,&w)&&w==0x0000ffffu);
    assert(musashi_boot_read32(&f.memory,out+4u,&w)&&w==0xffff0000u);
    assert(musashi_boot_read32(&f.memory,out+8u,&w)&&w==0x00000010u);
    step_at(0x80048644);step_at(0x80048648);
    assert(f.cpu.pc==ra && f.cpu.r[2]==out && f.cpu.r[29]==STACK);
    assert(f.cpu.r[4]==matrix && f.cpu.r[5]==verts && f.cpu.r[6]==out);
    for(unsigned i=0;i<5;++i)
        assert(musashi_boot_read32(&f.memory,matrix+i*4u,&w) &&
               w==((uint32_t[]){0x00001000u,0u,0x00001000u,0u,0x00001000u})[i]);
    for(unsigned i=0;i<3;++i)
        assert(musashi_boot_read32(&f.memory,verts+i*4u,&w) &&
               w==((uint32_t[]){0x0000ffffu,0xffff0000u,0x00000010u})[i]);
    assert(data_writes==6 && data_reads==6 && commands==2);
}
static void camera2_case(const uint8_t *exe,size_t size) {
    setup_camera2(exe,size,0x80052f64u);
    camera2_body(0x80052f64u);
    close_fixture();
}
static void camera2_refusals(const uint8_t *exe,size_t size) {
    /* Wrong RA at the first CTC2. */
    setup_camera2(exe,size,0x80052f64u);
    f.cpu.pc=0x80048500;f.cpu.npc=0x80048504;f.cpu.r[31]=0x80050000u;
    unchanged_refusal(0);
    assert(gteRegs.CP2C.p[0].d==0xa5000000u);
    close_fixture();
    /* Wrong RA at an MTC2-IR site. */
    setup_camera2(exe,size,0x80052f64u);
    f.cpu.pc=0x80048598;f.cpu.npc=0x8004859c;f.cpu.r[31]=0x80050000u;
    unchanged_refusal(0);
    close_fixture();
    /* Unbound MVMVA dispatch at the second vector core. */
    setup_camera2(exe,size,0x80052f64u);
    f.cpu.pc=0x800485c8;f.cpu.npc=0x800485cc;f.transfer.command=NULL;
    unchanged_refusal(0);
    close_fixture();
    /* Foreign RA at an MFC2-MAC site with live pending load. */
    setup_camera2(exe,size,0x80052f64u);
    f.cpu.pc=0x80048624;f.cpu.npc=0x80048628;f.cpu.r[31]=0x80052f64u;
    f.cpu.gte_load_pending=1;f.cpu.gte_load_reg=9;
    f.cpu.gte_load_value=0x12345678u;f.cpu.gte_load_next_pc=0x80047000u;
    unchanged_refusal(0);
    close_fixture();
}
static void light_dispatch(const uint8_t *exe,size_t size) {
    /* Explicit 53E28 frame: a0=test stack buffer, a1/a2 explicit halves,
     * a3=0x78 (the caller loads it per pinned EXE word at 5411C), ra=54128.
     * Only the taken jump-table case (entry32->53EA0) runs; the source copy
     * is checked against a seeded test pattern, never retail state. Note the
     * source base is 0x800AE620 from the raw immediates (LUI 0x800B plus
     * signed -0x19E0). v1 is an explicit input: the body bases stores at
     * a0+v1, so the frame pins v1=0 and dest=a0. */
    uint32_t buf,i,pattern[8];
    setup_data(exe,size,90000);
    buf=f.cpu.r[29]-0x80u;
    for(i=0;i<8;++i){pattern[i]=0xa5000000u+i;assert(musashi_boot_write32(&f.memory,0x800ae620u+i*4u,pattern[i]));}
    f.cpu.pc=0x80053e28;f.cpu.npc=0x80053e2c;f.cpu.r[31]=0x80054128u;
    f.cpu.r[3]=0;f.cpu.r[4]=buf;f.cpu.r[5]=0x1234u;f.cpu.r[6]=0x5678u;f.cpu.r[7]=0x78u;
    step_at(0x80053e28);step_at(0x80053e2c);step_at(0x80053e30);
    step_mem(0x80053e34);step_mem(0x80053e38);step_mem(0x80053e3c);
    step_mem(0x80053e40);step_mem(0x80053e44);step_mem(0x80053e48);
    step_mem(0x80053e4c);step_mem(0x80053e50);step_mem(0x80053e54);
    step_mem(0x80053e58);step_mem(0x80053e5c);step_mem(0x80053e60);
    step_mem(0x80053e64);step_mem(0x80053e68);
    step_mem(0x80053e6c);step_mem(0x80053e70);
    step_at(0x80053e74);step_at(0x80053e78);step_at(0x80053e7c);
    step_at(0x80053e80);step_at(0x80053e84);step_at(0x80053e88);step_at(0x80053e8c);
    step_mem(0x80053e90);step_at(0x80053e94);step_at(0x80053e98);step_at(0x80053e9c);
    assert(f.cpu.pc==0x80053ea0);
    step_at(0x80053ea0);step_mem(0x80053ea4);step_mem(0x80053ea8);step_mem(0x80053eac);
    step_at(0x80053eb0);step_mem(0x80053eb4);
    step_at(0x80053ee4);step_at(0x80053ee8);
    assert(f.cpu.pc==0x80054128 && f.cpu.r[29]==STACK);
    for(i=0;i<8;++i){uint32_t got=0;
     if(i==2||i==3||i==4)continue; /* case stores below overwrite these */
     assert(musashi_boot_read32(&f.memory,buf+i*4u,&got)&&got==pattern[i]);}
    {uint32_t w=0;
     assert(musashi_boot_read32(&f.memory,buf+8u,&w)&&w==0xedcc5678u);
     assert(musashi_boot_read32(&f.memory,buf+12u,&w)&&w==0x12340003u);
     assert(musashi_boot_read32(&f.memory,buf+16u,&w)&&w==0xa5005678u);}
    {uint16_t h=0;
     assert(musashi_boot_read16(&f.memory,buf+8u,&h)&&h==0x5678u);
     assert(musashi_boot_read16(&f.memory,buf+16u,&h)&&h==0x5678u);
     assert(musashi_boot_read16(&f.memory,buf+10u,&h)&&h==0xedccu);
     assert(musashi_boot_read16(&f.memory,buf+14u,&h)&&h==0x1234u);}
    close_fixture();
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0);rewind(file);
    uint8_t *exe=malloc((size_t)size);assert(exe&&fread(exe,1,(size_t)size,file)==(size_t)size);assert(!fclose(file));
        data_case(exe,(size_t)size,0,32,0);data_case(exe,(size_t)size,UINT32_MAX,32,0);
    data_case(exe,(size_t)size,90000,15,1);data_refusals(exe,(size_t)size);
    sub_overflow_traps(exe,(size_t)size);srav_keeps_sign(exe,(size_t)size);
    div_site(exe,(size_t)size);div_zero_pin(exe,(size_t)size);
    second_call(exe,(size_t)size);div540e0_site(exe,(size_t)size);
    div54154_site(exe,(size_t)size);div54194_site(exe,(size_t)size);
    light_dispatch(exe,(size_t)size);    camera_case(exe,(size_t)size);camera_jal_case(exe,(size_t)size);camera_refusals(exe,(size_t)size);    camera2_case(exe,(size_t)size);camera2_refusals(exe,(size_t)size);
    free(exe);puts("GTE_DATA_SOURCE_PASS fixture_only=1 positive_sqrt=298 second_caller=540D8 light_case=53EA0 camera_out0=00400010 camera2_out0=0000ffff");return 0;
}
