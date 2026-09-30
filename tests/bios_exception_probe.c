/* Real selected ROM/EXE bytes; synthetic CPU context, real token/kernel/card. */
#include "musashi_bios_exception.h"
#include "musashi_bios_card.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static MusashiBootMemory ram, before;
static MusashiBiosKernel kernel;
static MusashiBiosExceptionImage image;
static MusashiCpuStatus *cpu;
static MusashiBiosCard card;
static uint64_t thread_id=1;
static int reenter;
static int selected;
static const MusashiCpuExceptionToken *active_token;
static uint64_t expected_hooks;
static int node_calls;
static int irq_node(void *p,uint16_t pending) {
    MusashiCpuStatusSnapshot snap;assert(p==&card && pending==1);
    assert(musashi_cpu_status_inspect(cpu,active_token,&snap) && snap.kernel_entered);
    assert(image.last_sequence==active_token->sequence && card.hook_calls==expected_hooks);
    node_calls++;return 1;
}
static uint64_t thread(void *p) {
    (void)p;
    if(reenter) {uint32_t v=0xdeadbeef;reenter=0;assert(!musashi_bios_exception_table(&image,&v));assert(v==0xdeadbeef);}
    return thread_id;
}
static uint32_t word(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void fresh(const uint8_t *rom,size_t size) {
    memset(&ram,0xa5,sizeof(ram));before=ram;memset(&image,0,sizeof(image));
    musashi_bios_kernel_init(&kernel);memset(&card,0,sizeof(card));cpu=NULL;thread_id=1;reenter=0;
    assert(musashi_cpu_status_open(&cpu,thread,NULL));
    assert(musashi_bios_card_init(&card,&kernel));int32_t result=99;
    assert(musashi_bios_card_init_service(&card,0,&result) && result==0);
    assert(selected?musashi_bios_exception_init_selected(&image,&ram,&kernel,cpu):
        musashi_bios_exception_init(&image,&ram,&kernel,cpu,rom,size));
    assert(!memcmp(ram.bytes+0x674,rom+0x10174,28));
    if(selected) {  /* no ROM: zero stand-in handler and continuation */
        for(unsigned i=0;i<112;i++)assert(ram.bytes[0xc80+i]==0);
    } else assert(!memcmp(ram.bytes+0xc80,rom+0x10780,112));
    assert(image.variant==MUSASHI_BIOS_EXCEPTION_ORIGINAL && image.generation==1 && !image.dirty);
    assert(kernel.early_hook==&card.early_hook && kernel.exception_required);
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;i++)
        if(!(i>=0x674&&i<0x690)&&!(i>=0xc80&&i<0xcf0))assert(ram.bytes[i]==before.bytes[i]);
}
static void close_all(void) {
    thread_id=1;reenter=0;
    assert(musashi_bios_exception_destroy(&image));
    assert(!kernel.exception_binding && kernel.exception_required);
    assert(!musashi_bios_kernel_before_exception(&kernel));
    assert(musashi_bios_card_shutdown(&card));assert(musashi_cpu_status_close(cpu));cpu=NULL;
}
static MusashiCpuExceptionToken begin(MusashiCpuExceptionKind kind,int host) {
    static uint32_t gpr[32];static unsigned identity;
    for(unsigned i=0;i<32;i++)gpr[i]=0x10203000u+i;
    gpr[0]=0;
    gpr[4]=kind==MUSASHI_CPU_EXCEPTION_SYS1?1:2;
    MusashiCpuContext context={.identity=&identity,.gpr=gpr,
        .provenance=host?MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE:MUSASHI_CPU_CONTEXT_SOURCE};
    if(!host) {context.pc=kind==MUSASHI_CPU_EXCEPTION_IRQ?0x80012340:(kind==MUSASHI_CPU_EXCEPTION_SYS1?0x8005cf0c:0x8005cf1c);
        context.npc=context.pc+4;context.instruction=kind==MUSASHI_CPU_EXCEPTION_IRQ?0:12;context.instruction_valid=1;}
    MusashiCpuExceptionToken token={0};
    assert(musashi_cpu_status_begin(cpu,kind,&context,kind==MUSASHI_CPU_EXCEPTION_IRQ?0x400:0,&token));
    return token;
}
static void sys(MusashiCpuExceptionKind kind,int host) {
    MusashiCpuExceptionToken token=begin(kind,host);uint64_t hooks=card.hook_calls;
    assert(musashi_bios_kernel_before_exception_with_context(&kernel,&token));
    assert(card.hook_calls==hooks+1);
    MusashiCpuStatusSnapshot s;assert(musashi_cpu_status_inspect(cpu,&token,&s));
    assert(s.kernel_entered && s.gpr[1]==0x10203001 && s.gpr[2]==0x10203002 && s.gpr[3]==0x10203003 && s.gpr[31]==0x1020301f);
    assert(s.cause_epc_valid==!host);if(!host)assert((s.cause&0x7c)==0x20 && s.epc==(kind==MUSASHI_CPU_EXCEPTION_SYS1?0x8005cf0cu:0x8005cf1cu));
    int32_t result=0x77777777;assert(musashi_cpu_status_apply_sys(cpu,&token,&result));
    if(kind==MUSASHI_CPU_EXCEPTION_SYS2)assert(result==0x77777777);
    assert(musashi_cpu_status_finish(cpu,&token));
}
static void copy_patch(const uint8_t *patch) {
    for(unsigned i=0;i<14;i++)assert(musashi_bios_exception_write32(&image,0xc80+4*i,word(patch+4*i)));
}
static void normal(const uint8_t *rom,size_t size,const uint8_t *patch) {
    fresh(rom,size);uint32_t v=99;
    const uint32_t aliases[]={0,0x80000000u,0xa0000000u};
    for(unsigned i=0;i<3;i++)assert(musashi_bios_exception_read32(&image,aliases[i]+0x68c,&v)&&v==0xc80);
    assert(musashi_bios_exception_table(&image,&v)&&v==0x674);
    sys(MUSASHI_CPU_EXCEPTION_SYS1,0);copy_patch(patch);
    assert(image.dirty && image.written_mask==0x3fff && image.writes==14);
    assert(!memcmp(ram.bytes+0xc80,patch,56));
    for(unsigned i=0;i<3;i++)assert(musashi_bios_exception_read32(&image,aliases[i]+0xcb0,&v)&&v==0x40026800);
    assert(musashi_bios_exception_publish(&image));
    assert(image.generation==2 && image.variant==MUSASHI_BIOS_EXCEPTION_GTE_PATCH && !image.dirty);
    sys(MUSASHI_CPU_EXCEPTION_SYS2,0);
    MusashiCpuExceptionToken irq=begin(MUSASHI_CPU_EXCEPTION_IRQ,0);uint64_t hooks=card.hook_calls;
    MusashiBiosIrqNode node={0};active_token=&irq;expected_hooks=hooks+1;node_calls=0;
    assert(musashi_bios_kernel_enqueue(&kernel,&node,2,1,irq_node,&card));
    assert(musashi_bios_kernel_dispatch_with_context(&kernel,1,&irq));
    assert(node_calls==1 && musashi_bios_kernel_dequeue(&kernel,&node));
    assert(card.hook_calls==hooks+1);assert(musashi_cpu_status_finish(cpu,&irq));
    sys(MUSASHI_CPU_EXCEPTION_SYS1,1);copy_patch(patch);assert(musashi_bios_exception_publish(&image));
    assert(image.generation==3 && image.writes==28);sys(MUSASHI_CPU_EXCEPTION_SYS2,1);
    irq=begin(MUSASHI_CPU_EXCEPTION_IRQ,1);assert(musashi_bios_kernel_dispatch_with_context(&kernel,0,&irq));
    assert(musashi_cpu_status_finish(cpu,&irq));assert(image.entries==6);
    assert(!memcmp(ram.bytes+0xcf0,before.bytes+0xcf0,0x20));
    close_all();
}
static void negatives(const uint8_t *rom,size_t size,const uint8_t *patch) {
    for(unsigned which=0;which<16;which++) {
        fresh(rom,size);uint32_t v=0xdeadbeef;
        if(which==0) {assert(musashi_bios_exception_write32(&image,0xc80,0));
            assert(!musashi_bios_exception_publish(&image));assert(image.generation==1 && image.dirty);}
        if(which==1) {copy_patch(patch);ram.bytes[0xc90]^=1;
            assert(!musashi_bios_exception_publish(&image));assert(image.generation==1);}
        if(which==2) {copy_patch(patch);MusashiCpuExceptionToken t=begin(MUSASHI_CPU_EXCEPTION_SYS2,0);uint64_t hooks=card.hook_calls;
            assert(!musashi_bios_kernel_before_exception_with_context(&kernel,&t));assert(card.hook_calls==hooks);
            assert(musashi_cpu_status_fault(cpu,&t));}
        if(which==3) {assert(!musashi_bios_exception_write32(&image,0xcf0,0));assert(ram.bytes[0xcf0]==0xa5);}
        if(which==4) {assert(!musashi_bios_exception_read32(&image,0x40000688,&v));assert(v==0xdeadbeef);}
        if(which==5) {ram.bytes[0x688]^=4;assert(!musashi_bios_exception_table(&image,&v));assert(v==0xdeadbeef);}
        if(which==6) {thread_id=2;assert(!musashi_bios_exception_table(&image,&v));assert(v==0xdeadbeef);thread_id=1;}
        if(which==7) {reenter=1;assert(!musashi_bios_exception_table(&image,&v));assert(v==0xdeadbeef);}
        if(which==8) {copy_patch(patch);assert(musashi_bios_exception_publish(&image));
            MusashiCpuExceptionToken t=begin(MUSASHI_CPU_EXCEPTION_SYS1,0),bad=t;bad.sequence++;
            uint64_t hooks=card.hook_calls;assert(!musashi_bios_kernel_before_exception_with_context(&kernel,&bad));assert(card.hook_calls==hooks);assert(musashi_cpu_status_fault(cpu,&t));}
        if(which==9) {sys(MUSASHI_CPU_EXCEPTION_SYS2,0);uint8_t old=ram.bytes[0xc80];
            assert(!musashi_bios_exception_write32(&image,0xc80,99));assert(ram.bytes[0xc80]==old);}
        if(which==10) {assert(!musashi_bios_exception_write32(&image,0xc81,99));assert(ram.bytes[0xc81]==0);}
        if(which==11) {assert(!musashi_bios_exception_write32(&image,0x674,99));assert(word(ram.bytes+0x674)==0x1508);}
        if(which==12) {ram.bytes[0xcb8]^=1;MusashiCpuExceptionToken t=begin(MUSASHI_CPU_EXCEPTION_SYS1,0);
            uint64_t hooks=card.hook_calls;assert(!musashi_bios_kernel_before_exception_with_context(&kernel,&t));
            assert(card.hook_calls==hooks);assert(musashi_cpu_status_fault(cpu,&t));}
        if(which==13) {MusashiCpuExceptionToken t=begin(MUSASHI_CPU_EXCEPTION_SYS1,0);
            assert(!musashi_bios_exception_destroy(&image));assert(image.initialized && kernel.exception_binding==&image.binding);
            assert(musashi_bios_kernel_before_exception_with_context(&kernel,&t));int32_t result;
            assert(musashi_cpu_status_apply_sys(cpu,&t,&result));assert(musashi_cpu_status_finish(cpu,&t));}
        if(which==14) {assert(!musashi_bios_exception_publish(&image));assert(image.generation==1);}
        if(which==15) {image.generation=UINT64_MAX;copy_patch(patch);assert(!musashi_bios_exception_publish(&image));
            assert(image.generation==UINT64_MAX && image.dirty && !memcmp(ram.bytes+0xc80,patch,56));}
        close_all();
    }
}
static void constructor_controls(uint8_t *rom,size_t size) {
    fresh(rom,size);assert(musashi_bios_exception_destroy(&image));
    memset(&image,0,sizeof(image));before=ram;
    assert(!musashi_bios_exception_init(&image,&ram,&kernel,cpu,rom,size-1));
    rom[0x12345]^=1;assert(!musashi_bios_exception_init(&image,&ram,&kernel,cpu,rom,size));rom[0x12345]^=1;
    assert(!memcmp(&ram,&before,sizeof(ram)) && !kernel.exception_binding && !image.initialized);
    reenter=1;assert(!musashi_bios_exception_init_selected(&image,&ram,&kernel,cpu));
    assert(image.faulted && !image.initialized && !memcmp(&ram,&before,sizeof(ram)) && !kernel.exception_binding);
    memset(&image,0,sizeof(image));assert(musashi_bios_exception_init_selected(&image,&ram,&kernel,cpu));
    assert(!memcmp(ram.bytes+0x674,rom+0x10174,28));
    for(unsigned i=0;i<112;i++)assert(ram.bytes[0xc80+i]==0);  /* selected: stand-ins */
    MusashiBiosExceptionImage other={0};before=ram;
    assert(!musashi_bios_exception_init_selected(&other,&ram,&kernel,cpu));
    assert(!other.initialized && !memcmp(&ram,&before,sizeof(ram)));
    close_all();
}
static uint8_t *load(const char *name,size_t *size) {FILE *f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);uint8_t *p=malloc((size_t)n);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));*size=(size_t)n;return p;}
int main(int argc,char **argv) {
    assert(argc==3);size_t size,n;uint8_t *rom=load(argv[1],&size),*exe=load(argv[2],&n);assert(n>0x3da0c);
    normal(rom,size,exe+0x3d9d4);selected=1;normal(rom,size,exe+0x3d9d4);
    negatives(rom,size,exe+0x3d9d4);constructor_controls(rom,size);
    assert(musashi_bios_exception_intersects(0xa0000673,4));assert(musashi_bios_exception_intersects(0x80000cb7,2));
    assert(!musashi_bios_exception_intersects(0x40000688,4));assert(!musashi_bios_exception_intersects(0xcf0,4));
    free(exe);free(rom);puts("BIOS_EXCEPTION_PASS source_rom=1 shared_token=1 publication=1 card_hook_preserved=1");return 0;
}
