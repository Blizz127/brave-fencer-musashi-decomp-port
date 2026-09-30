/* Source-path fixture for the six GTE initialization exports.
 * It executes the actual startup runner with real Status/image/kernel/GTE
 * owners and an explicit synthetic initial CPU/stack. This is fixture evidence only;
 * it makes no native boot or retail lifecycle claim. */
#include "../pc_port/mips_formatter.c"
#include "musashi_gte_owner.h"
#include "musashi_bios_exception.h"
#include "musashi_bios_card.h"
#include "psx/gtereg.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Probes that never dispatch a GTE command keep this self-contained stub
 * bank: any operator entry aborts, and LIM ignores FLAG side effects.
 * A probe that executes real vector cores defines
 * MUSASHI_GTE_REAL_OPERATOR before including this file and links the real
 * PsyCross operator TU instead, which provides all four symbols below. */
#ifndef MUSASHI_GTE_REAL_OPERATOR
GTERegisters gteRegs;

int GTE_operator(int op) { (void)op; abort(); }
int LIM(int value, int max, int min, unsigned flag) {
    (void)flag;
    return value > max ? max : (value < min ? min : value);
}
unsigned int gte_leadingzerocount(unsigned int value) {
    unsigned count = 0;
    while (count < 32u && !(value & 0x80000000u)) { value <<= 1; ++count; }
    return count;
}
#endif

/* The refusal matrix below intentionally stops before these fallible helper
 * bodies. Keep the fixture link self-contained so it cannot accidentally turn
 * an image-overlap refusal into a host-side memset/search implementation. */
int musashi_boot_call_8001903c(MusashiBootMemory *memory) {
    (void)memory; return 0;
}
int musashi_boot_call_8005c29c(MusashiBootMemory *memory, uint32_t address,
                               int32_t value, uint32_t count) {
    (void)memory; (void)address; (void)value; (void)count; return 0;
}
int musashi_boot_call_80016714(MusashiBootMemory *memory, uint32_t address,
                               uint32_t count) {
    (void)memory; (void)address; (void)count; return 0;
}
int musashi_boot_call_80029044(MusashiBootMemory *memory) {
    (void)memory; return 0;
}
int musashi_boot_call_8002906c(MusashiBootMemory *memory) {
    (void)memory; return 0;
}
int musashi_boot_call_800425b0(MusashiBootMemory *memory, const MusashiCallbackRegistrationDevice *device,
                               int32_t index, uint32_t callback,
                               uint32_t *previous) {
    (void)memory; (void)device; (void)index; (void)callback; (void)previous;
    return 0;
}
void func_8005C388_native(int32_t value, uint8_t *classification,
                          int32_t *column, void *userdata) {
    (void)value; (void)classification; (void)column; (void)userdata; abort();
}

/* Link traps for unrelated compiled services retained by run_startup_cpu.
 * Any accidental entry fails the process, including alleged successful no-ops. */
int musashi_boot_call_80042c64(MusashiBootMemory *m,uint32_t a,uint32_t n) {(void)m;(void)a;(void)n;abort();}
int musashi_boot_call_80043060(MusashiBootMemory *m,int32_t a,int32_t *v) {(void)m;(void)a;(void)v;abort();}
int musashi_boot_call_8005bd7c(MusashiBootMemory *m,const MusashiGpuDevice *d,int32_t a,int32_t *v) {(void)m;(void)d;(void)a;(void)v;abort();}
int musashi_boot_call_8005b684(MusashiBootMemory *m,const MusashiGpuDevice *d,uint32_t a) {(void)m;(void)d;(void)a;abort();}
int musashi_boot_console_string(MusashiBootMemory *m,uint32_t a,MusashiConsoleSink s,void *u) {(void)m;(void)a;(void)s;(void)u;abort();}

enum { STACK=0x801ff000u };
typedef struct Fixture {
    MusashiBootMemory memory;
    FormatterCpu cpu;
    MusashiCpuStatus *status;
    MusashiBiosKernel kernel;
    MusashiBiosCard card;
    MusashiBiosExceptionImage image;
    MusashiGteOwner gte;
    MusashiExecutionClock clock;
    MusashiCpuTransferDevice transfer;
    MusashiBiosImageDevice image_device;
    MusashiCallbackDevice callbacks;
    MusashiCdDevice cd;
    unsigned cycles, clock_calls, syscalls, reads, status_writes, controls, tables, publishes;
    unsigned sys_entry_cycles, sys_entry_calls;
    uint32_t refuse_clock_pc, checkpoint_pc, change_pc, change_value;
    uint32_t store_addresses[28];
    int refuse_publish, refuse_status, change_kind, changed;
    unsigned returns;
    unsigned boundary_cycles, boundary_calls, boundary_reads, boundary_controls;
    uint32_t observe_boundary;
} Fixture;
static Fixture f;
static MusashiBootMemory before;
static uint64_t thread_token(void *p) {(void)p;return 1;}
static int advance(void *p,uint32_t cost) {
    Fixture *x=p;assert(cost==2 || cost==3);
    x->clock_calls++;
    if(x->cpu.pc==x->refuse_clock_pc)return 0;
    x->cycles+=cost;return 1;
}
static int checkpoint(void *p,const void *continuation) {
    Fixture *x=p;assert(continuation==&x->cpu);
    if(x->cpu.pc==0x8005cf0c || x->cpu.pc==0x8005cf1c) {
        x->sys_entry_cycles=x->cycles;x->sys_entry_calls=x->clock_calls;
    }
    if(x->cpu.pc==x->observe_boundary) {
        x->boundary_cycles=x->cycles;x->boundary_calls=x->clock_calls;
        x->boundary_reads=x->reads;x->boundary_controls=x->controls;
    }
    if(x->cpu.pc==x->change_pc && !x->changed) {
        x->changed=1;
        if(x->change_kind==1)x->cpu.r[4]=x->change_value;
        if(x->change_kind==2)x->cpu.r[31]=x->change_value;
    }
    if(x->cpu.pc==0x80052618u)x->returns++;
    return x->cpu.pc!=x->checkpoint_pc;
}
static int sys(Fixture *x,MusashiCpuExceptionKind kind,int32_t *result) {
    MusashiCpuContext context;MusashiCpuExceptionToken token={0};int32_t local;
    assert(x->cycles==x->sys_entry_cycles+2 && x->clock_calls==x->sys_entry_calls+1);
    assert(musashi_boot_cpu_context(&x->cpu,MUSASHI_CPU_CONTEXT_SOURCE,&context));
    if(!musashi_cpu_status_begin(x->status,kind,&context,0,&token))return 0;
    if(!musashi_bios_kernel_before_exception_with_context(&x->kernel,&token) ||
       !musashi_cpu_status_apply_sys(x->status,&token,kind==MUSASHI_CPU_EXCEPTION_SYS1?&local:NULL) ||
       !musashi_cpu_status_finish(x->status,&token)) {
        assert(musashi_cpu_status_fault(x->status,&token));return 0;
    }
    x->syscalls++;if(kind==MUSASHI_CPU_EXCEPTION_SYS1)*result=local;
    return 1;
}
static int enter_sys(void *p,int32_t *v){return sys(p,MUSASHI_CPU_EXCEPTION_SYS1,v);}
static int exit_sys(void *p){return sys(p,MUSASHI_CPU_EXCEPTION_SYS2,NULL);}
static int image_read(void *p,uint32_t a,uint32_t *v){return musashi_bios_exception_read32(&((Fixture*)p)->image,a,v);}
static int image_write(void *p,uint32_t a,uint32_t v){
    Fixture *x=p;unsigned n=(unsigned)x->image.writes;
    if(!musashi_bios_exception_write32(&x->image,a,v))return 0;
    assert(n<28);x->store_addresses[n]=a;return 1;
}
static int image_table(void *p,uint32_t *v){Fixture*x=p;x->tables++;return musashi_bios_exception_table(&x->image,v);}
static int publish(void *p){Fixture*x=p;x->publishes++;return !x->refuse_publish && musashi_bios_exception_publish(&x->image);}
static int read_status(void*p,const MusashiCpuContext*c,uint32_t*v){Fixture*x=p;assert(c->identity==&x->cpu);x->reads++;return musashi_cpu_status_read(x->status,v);}
static int write_status(void*p,const MusashiCpuContext*c,uint32_t v){Fixture*x=p;x->status_writes++;return !x->refuse_status && musashi_cpu_status_write_source(x->status,c,v);}
static int control(void*p,const MusashiCpuContext*c,uint32_t r,uint32_t v){Fixture*x=p;if(!musashi_gte_owner_write_control(&x->gte,c,r,v))return 0;x->controls++;return 1;}
static void init(const uint8_t *exe,size_t size) {
    memset(&f,0,sizeof(f));assert(musashi_boot_map_exe(&f.memory,exe,size));
    musashi_bios_kernel_init(&f.kernel);assert(musashi_cpu_status_open(&f.status,thread_token,&f));
    assert(musashi_bios_card_init(&f.card,&f.kernel));int32_t ignored;
    assert(musashi_bios_card_init_service(&f.card,0,&ignored));
    assert(musashi_bios_exception_init_selected(&f.image,&f.memory,&f.kernel,f.status));
    f.cpu.pc=0x80053cf8;f.cpu.npc=f.cpu.pc+4;f.cpu.r[29]=STACK;f.cpu.r[31]=0x80052618;
    f.cpu.r[30]=STACK+64;f.cpu.r[2]=0x12345678;
    f.clock=(MusashiExecutionClock){&f,advance};f.cpu.clock=&f.clock;
    f.callbacks.userdata=&f;f.callbacks.checkpoint=checkpoint;f.callbacks.exit_critical_section=exit_sys;
    f.cd.userdata=&f;f.cd.enter_critical_result=enter_sys;
    f.cpu.callback_device=&f.callbacks;f.cpu.cd_device=&f.cd;
    f.image_device=(MusashiBiosImageDevice){&f,musashi_bios_exception_intersects,image_read,image_write,image_table,publish};
    f.cpu.bios_image=&f.image_device;
    f.transfer=(MusashiCpuTransferDevice){.userdata=&f,.read_status=read_status,.write_status=write_status,.write_control=control};f.cpu.cpu_transfer=&f.transfer;
    /* Test-owned preexisting shared-bank sentinel, installed before lease.
     * No captured control registers, SR or RAM post-state is supplied. */
    for(unsigned i=0;i<32;i++)gteRegs.CP2C.p[i].d=0xa5000000u+i;
    assert(musashi_gte_owner_init(&f.gte,f.status,thread_token,&f,&f.cpu));
    f.checkpoint_pc=0x80052618;before=f.memory;
}
static MusashiResetGraphPrefixStatus run(void) {
    MusashiResetGraphPrefixStop stop={0};MusashiEntryRunStop entry={0};int32_t result=0;
    return run_startup_cpu(&f.memory,&f.cpu,NULL,NULL,&result,&stop,&entry,NULL,NULL);
}
static void close_fixture(void) {
    assert(musashi_gte_owner_close(&f.gte));
    assert(musashi_bios_card_shutdown(&f.card));
    assert(musashi_bios_exception_destroy(&f.image));assert(musashi_cpu_status_close(f.status));
}
static void success(const uint8_t *exe,size_t size) {
    init(exe,size);uint32_t value;
    assert(musashi_bios_exception_read32(&f.image,0x688,&value)&&value==0x1f88);
    assert(musashi_bios_exception_read32(&f.image,0x68c,&value)&&value==0xc80);
    assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT); /* Test checkpoint stops at actual return. */
    assert(f.cpu.pc==0x80052618 && f.returns==1 && f.cpu.r[29]==STACK && f.cpu.r[30]==STACK+64 && f.cpu.r[31]==0x80052618);
    assert(f.cpu.r[2]==0x40000401 && f.syscalls==2 && f.card.hook_calls==2);
    assert(f.image.writes==14 && f.image.generation==2 && f.image.variant==MUSASHI_BIOS_EXCEPTION_GTE_PATCH && !f.image.dirty && !f.image.faulted);
    assert(f.image.entries==2 && f.tables==1 && f.publishes==1);
    MusashiCpuStatusSnapshot state;assert(musashi_cpu_status_snapshot(f.status,&state));
    assert(state.sr==0x40000401 && !state.active && !state.faulted && state.sequence==2);
    assert(state.cause_epc_valid && state.epc==0x8005cf1c && state.gpr[4]==2);
    MusashiGteSnapshot snapshot;assert(musashi_gte_owner_snapshot(&f.gte,&snapshot));
    const uint32_t tail[]={0,0,0,0,0,0x3e8,0xffffef9e,0x01400000,0x155,0x100};
    assert(f.reads==1 && f.status_writes==1 && f.controls==12 && snapshot.write_count==12 && !snapshot.faulted);
    for(unsigned i=0;i<32;i++)assert(snapshot.control[i]==(i>=21&&i<=30?tail[i-21]:0xa5000000u+i));
    for(unsigned i=0;i<14;i++)assert(f.store_addresses[i]==0xc80+4*i);
    assert(!memcmp(f.memory.bytes+0xc80,exe+0x3d9d4,56));
    assert(!memcmp(f.memory.bytes+0xcf0,before.bytes+0xcf0,0x20));
    /* Exact whole-RAM effects: source stack, its two saved-RA globals and
     * the14 accepted instruction-image words. No guest TCB shadow. */
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;i++)
        if(!(i>=0xc80&&i<0xcb8)&&!(i>=0x1fefe8&&i<0x1ff000)&&
           !(i>=0x6d8dc&&i<0x6d8e0)&&!(i>=0x74754&&i<0x74758))
            assert(f.memory.bytes[i]==before.bytes[i]);
    close_fixture();
}
/* Two source invocations on one set of real persistent owners. The switch
 * between source entrypoints is explicitly fixture scheduling: it does not
 * claim the intervening full native renderer/caller path executed here. */
static void second_geom(const uint8_t *exe,size_t size) {
    for(unsigned which=0;which<7;++which) {
        init(exe,size);
        assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
        assert(f.cpu.pc==0x80052618 && f.cpu.r[29]==STACK);
        MusashiCpuStatus *same_status=f.status;
        uint64_t same_thread=f.gte.owner_thread;
        MusashiGteSnapshot initial;
        assert(musashi_gte_owner_snapshot(&f.gte,&initial));
        assert(initial.write_count==12 && f.image.generation==2);
        before=f.memory;
        /* Actual14278 JAL/14444 save/47CB4 saved-global ancestry. No fakeRA,
         * register values, kernel publication, or GTE state are installed. */
        assert(!f.cpu.delay_slot && !f.cpu.branch_pc);
        f.cpu.pc=0x80014278;f.cpu.npc=f.cpu.pc+4;
        f.checkpoint_pc=0x80053218;
        if(which==1 || which==2 || which==3) {
            f.change_pc=which==1?0x80047cd0:which==2?0x80047ce8:0x8004923c;
            f.change_kind=2;f.change_value=which==3?0x80014470:0x80053d24;
            f.observe_boundary=f.change_pc;
        }
        if(which==4 || which==5) {
            f.refuse_clock_pc=which==4?0x80047cd0:0x8004923c;
            f.observe_boundary=f.refuse_clock_pc;
        }
        if(which==6) {
            f.cpu.cpu_transfer=NULL;
            f.observe_boundary=0x80047cd0;
        }
        assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
        assert(f.status==same_status && f.gte.owner_thread==same_thread);
        assert(f.image.writes==28 && f.image.generation==3 && !f.image.dirty);
        assert(f.tables==2 && f.publishes==2 && f.syscalls==4 && f.card.hook_calls==4);
        for(unsigned i=0;i<28;++i)assert(f.store_addresses[i]==0xc80+4*(i%14));
        MusashiCpuStatusSnapshot state;
        assert(musashi_cpu_status_snapshot(f.status,&state));
        assert(state.sr==0x40000401 && state.sequence==4 && !state.active && !state.faulted);
        assert(!f.image.faulted);
        MusashiGteSnapshot final;
        assert(musashi_gte_owner_snapshot(&f.gte,&final));
        if(which==0) {
            uint32_t saved;
            assert(f.cpu.pc==0x80053218 && f.cpu.r[31]==0x80014470);
            assert(f.cpu.r[29]==STACK-0x18 && f.cpu.r[30]==STACK+64);
            assert(musashi_boot_read32(&f.memory,STACK-4,&saved)&&saved==0x80014280);
            assert(musashi_boot_read32(&f.memory,0x8006d8dc,&saved)&&saved==0x80014454);
            assert(musashi_boot_read32(&f.memory,0x80074754,&saved)&&saved==0x80047cc4);
            assert(musashi_boot_read32(&f.memory,0x80126950,&saved)&&saved==1000);
            assert(f.reads==2 && f.status_writes==2 && f.controls==20);
            assert(final.write_count==20 && final.control[26]==1000);
            /* Second InitGeom writes the same seven values, no bankreset. */
            for(unsigned i=0;i<32;++i)assert(final.control[i]==initial.control[i]);
            for(unsigned i=0;i<MUSASHI_RAM_SIZE;++i)
                if(!(i>=0xc80&&i<0xcb8)&&!(i>=0x1fefe8&&i<0x1ff000)&&
                   !(i>=0x6d8dc&&i<0x6d8e0)&&!(i>=0x74754&&i<0x74758)&&
                   !(i>=0x126950&&i<0x126954))
                    assert(f.memory.bytes[i]==before.bytes[i]);
        } else {
            uint32_t expected=f.observe_boundary;
            assert(f.cpu.pc==expected && f.cycles==f.boundary_cycles);
            assert(f.clock_calls==f.boundary_calls+(which==4||which==5));
            assert(f.controls==f.boundary_controls && f.reads==f.boundary_reads);
            assert(final.write_count==(which==3||which==5?19u:12u));
            for(unsigned i=0;i<32;++i)assert(final.control[i]==initial.control[i]);
        }
        close_fixture();
    }
}
static void failures(const uint8_t *exe,size_t size) {
    for(unsigned which=0;which<7;which++) {
        init(exe,size);
        if(which==0){f.change_pc=0x8005cf0c;f.change_kind=1;f.change_value=2;}
        if(which==1)f.refuse_clock_pc=0x8005cf0c;
        if(which==2)f.cpu.cpu_transfer=NULL;
        if(which==3)f.refuse_publish=1;
        if(which==4)f.refuse_status=1;
        if(which==5){f.change_pc=0x80047ce8;f.change_kind=2;f.change_value=0x80053d24;}
        if(which==6)f.refuse_clock_pc=0x8004d1ac;
        assert(run()!=MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        if(which<=1){assert(f.cpu.pc==0x8005cf0c && !f.syscalls && !f.image.writes);assert(f.cycles==f.sys_entry_cycles);assert(f.clock_calls==f.sys_entry_calls+(which==1));}
        if(which==2)assert(f.cpu.pc==0x80047cd0 && f.image.generation==2 && f.syscalls==2 && !f.controls);
        if(which==3)assert(f.cpu.pc==0xa0 && f.image.dirty && f.image.generation==1 && f.image.writes==14 && f.syscalls==1 && !f.controls);
        if(which==4)assert(f.cpu.pc==0x80047cdc && f.image.generation==2 && !f.controls);
        if(which==5)assert(f.cpu.pc==0x80047ce8 && !f.controls);
        if(which==6)assert(f.cpu.pc==0x8004d1ac && !f.image.writes && f.image.generation==1);
        close_fixture();
    }
}
static void intrinsic_refusal_cases(const uint8_t *exe,size_t size) {
    const uint32_t targets[]={0x8005c29c,0x80042c64,0x80042d8c,0x80043034,0x80016714,0x8005ccc8,0x8005cd18};
    const uint32_t addresses[]={0x80000c80,0x674,0xa0000c80,0x80000c70,0xc80,0x80000674,0xa0000674};
    const uint32_t counts[]={4,1,1,0x20,4,4,1};init(exe,size);
    for(unsigned i=0;i<7;i++){
        f.cpu.pc=targets[i];f.cpu.npc=targets[i]+4;f.cpu.r[31]=UINT32_MAX;
        f.cpu.r[2]=0xdeadbeef;f.cpu.r[4]=addresses[i];f.cpu.r[5]=0x7f;f.cpu.r[6]=counts[i];
        assert(!formatter_call(&f.memory,&f.cpu,NULL,NULL));assert(f.cpu.pc==targets[i] && f.cpu.r[2]==0xdeadbeef);
        assert(!memcmp(&f.memory,&before,sizeof(before)));
    }
    close_fixture();
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0);rewind(file);
    uint8_t *exe=malloc((size_t)size);assert(exe&&fread(exe,1,(size_t)size,file)==(size_t)size);assert(!fclose(file));
    success(exe,(size_t)size);second_geom(exe,(size_t)size);failures(exe,(size_t)size);intrinsic_refusal_cases(exe,(size_t)size);
    free(exe);puts("GTE_INIT_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED");return 0;
}
