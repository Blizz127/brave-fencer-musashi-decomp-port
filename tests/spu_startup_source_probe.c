/* Synthetic source-execution fixture, NOT a native boot or captured RAM replay.
 * EXE supplies source data; the test explicitly owns a stack, an initialized
 * IRQ guard, a preinstalled
 * DMA dispatch slot, fresh events/IRQ/DMA and a copying PCM sink. */
#include "../pc_port/mips_formatter.c"
#include "musashi_bios_kernel.h"
#include "musashi_dma_controller.h"
#include "musashi_spu_cd_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture {
    MusashiBootMemory memory;
    FormatterCpu cpu;
    MusashiBiosKernel kernel;
    MusashiIrqController irq;
    MusashiDmaController dma;
    MusashiSpuCdAudio *audio;
    MusashiCallbackDevice callbacks;
    MusashiGpuDevice dma_device;
    MusashiCdSpuDevice spu;
    MusashiExecutionClock clock;
    uint64_t cycles;
    unsigned steps, opens, enables, enters, exits, init_calls, first_returns;
    unsigned writes, status_reads, transfers, frames;
    int enabled, refuse_queue, refuse_status;
} Fixture;
static Fixture f;
static uint8_t exe[0x65000];
static uint32_t word(uint32_t address) {
    uint32_t value;
    assert(musashi_boot_read32(&f.memory,address,&value));
    return value;
}
static void put(uint32_t address,uint32_t value) {
    assert(musashi_boot_write32(&f.memory,address,value));
}
static int ready(void *u) { (void)u; return 1; }
static uint64_t thread(void *u) { (void)u; return 1; }
static int status(void *u,size_t *queued,int *playing) {
    (void)u; *queued=0; *playing=1; return 1;
}
static int queue(void *u,const int16_t *pcm,size_t count) {
    Fixture *p=u;
    size_t i;
    if (p->refuse_queue) return 0;
    assert(pcm && count);
    /* The tested initialization uploads a silent ADPCM block and mutes
     * voices. Inspect actual ordered PCM before consuming it immediately. */
    for (i=0;i<count*2u;i++) assert(pcm[i]==0);
    p->frames+=(unsigned)count;
    return 1;
}
static int advance(void *u,uint32_t cost) {
    Fixture *p=u;
    uint64_t next=p->cycles+cost;
    assert(cost==2 || cost==3);
    if (!musashi_spu_cd_audio_advance(p->audio,next)) return 0;
    p->cycles=next;
    return 1;
}
static uint32_t dma_read(void *u,uint32_t a) {
    Fixture *p=u; uint32_t v;
    assert(musashi_dma_controller_read32(&p->dma,a,&v)); return v;
}
static void dma_write(void *u,uint32_t a,uint32_t v) {
    Fixture *p=u;
    assert(musashi_dma_controller_write32(&p->dma,a,v));
}
static uint16_t irq_read(void *u,uint32_t a) {
    Fixture *p=u; uint16_t v;
    assert(musashi_irq_controller_read16(&p->irq,a,&v)); return v;
}
static void irq_write(void *u,uint32_t a,uint16_t v) {
    Fixture *p=u;
    assert(musashi_irq_controller_write16(&p->irq,a,v));
}
static int spu_read(void *u,uint32_t a,uint16_t *v) {
    Fixture *p=u;
    if (a==0x1f801daeu) {
        ++p->status_reads;
        if (p->refuse_status && p->status_reads==(unsigned)p->refuse_status) return 0;
    }
    return musashi_spu_cd_audio_read16(p->audio,a,v);
}
static int spu_write(void *u,uint32_t a,uint16_t v) {
    Fixture *p=u;
    if (!musashi_spu_cd_audio_write16(p->audio,a,v)) return 0;
    ++p->writes;
    if (a==0x1f801da8u) { assert(v==0x0707); ++p->transfers; }
    return 1;
}
static void init(void) {
    MusashiSpuCdAudioBackend backend={&f,ready,thread,status,queue};
    memset(&f,0,sizeof(f));
    assert(musashi_boot_map_exe(&f.memory,exe,sizeof(exe)));
    musashi_bios_kernel_init(&f.kernel);
    musashi_irq_controller_init(&f.irq);
    musashi_dma_controller_init(&f.dma,&f.irq);
    f.audio=musashi_spu_cd_audio_create(); assert(f.audio);
    assert(musashi_spu_cd_audio_init_bios_muted(f.audio,&backend,0));
    f.enabled=1;
    /* Explicit fixture prerequisite: the prior game IRQ initialization owns
     * this callable slot. This is neither EXE data nor a captured RAM seed. */
    assert(word(0x8006cb84)==0x8006cb64);
    assert(word(0x8006cb68)==0);
    put(0x8006cb68,0x80042f8c);
    /* Prior IRQ setup guard: this fixture does not run/install a scheduler. */
    assert(musashi_boot_write16(&f.memory,0x8006bafc,1));
    f.dma_device.userdata=&f;
    f.dma_device.read32=dma_read;
    f.callbacks.userdata=&f;
    f.callbacks.read16=irq_read;
    f.callbacks.write16=irq_write;
    f.callbacks.write32=dma_write;
    f.callbacks.reset_device=&f.dma_device;
    f.spu.userdata=&f; f.spu.read16=spu_read; f.spu.write16=spu_write;
    f.clock.userdata=&f; f.clock.advance=advance;
    f.cpu.callback_device=&f.callbacks; f.cpu.cd_spu_device=&f.spu;
    f.cpu.clock=&f.clock;
    f.cpu.pc=0x8002c8f4; f.cpu.npc=f.cpu.pc+4;
    f.cpu.r[29]=0x801ff000; f.cpu.r[30]=0x801ff000;
    f.cpu.r[28]=0x80074750; f.cpu.r[31]=0x12345678;
}
/* Only BIOS services and the existing source-owned clear leave the formatter. All source prologues, wrappers,
 * loops, calls, delay slots and MMIO instructions execute on this same CPU.
 * The fixture's native critical owner has no scheduled IRQ producer. */
static int step(void) {
    FormatterCpu *c=&f.cpu;
    int32_t result;
    ++f.steps;
    if (c->pc==0x8003a424) ++f.init_calls;
    if (c->pc==0x8002c904) ++f.first_returns;
    if (c->pc==0x8005cf0c || c->pc==0x8005cf1c) {
        int enter=c->pc==0x8005cf0c;
        assert(c->r[4]==(enter?1u:2u) && c->npc==c->pc+4);
        assert(musashi_bios_kernel_before_exception(&f.kernel));
        if (enter) { c->r[2]=(uint32_t)f.enabled; f.enabled=0; ++f.enters; }
        else { f.enabled=1; ++f.exits; }
        c->pc=c->npc; c->npc+=4; return 1;
    }
    if (c->pc==0x8005ce78) {
        assert(!f.enabled && c->r[4]==0xf0000009 && c->r[5]==0x20 &&
               c->r[6]==0x2000 && c->r[7]==0);
        if (!musashi_bios_events_open(&f.kernel.events,c->r[4],c->r[5],
                                      c->r[6],c->r[7],&result)) return 0;
        ++f.opens; c->r[2]=(uint32_t)result; formatter_return(c); return 1;
    }
    if (c->pc==0x8005ceb8) {
        assert(!f.enabled);
        if (!musashi_bios_events_enable(&f.kernel.events,(int32_t)c->r[4],&result)) return 0;
        ++f.enables; c->r[2]=(uint32_t)result; formatter_return(c); return 1;
    }
    if (c->pc==0x80042c64) {
        if (!musashi_boot_call_80042c64(&f.memory,c->r[4],c->r[5])) return 0;
        return formatter_return(c);
    }
    return formatter_step(&f.memory,c);
}
static int run_to(uint32_t target) {
    while(f.steps<200000) {
        if(f.cpu.pc==target) return 1;
        if(!step()) return 0;
    }
    return 0;
}
static void done(void) { assert(musashi_spu_cd_audio_destroy(f.audio)); }
static void complete(void) {
    MusashiSpuCdAudioSnapshot state;
    MusashiSpuVoiceSnapshot voice;
    uint8_t ram[16]; unsigned i;
    init();
    if(!run_to(0x8002c90c)) {
        fprintf(stderr,"complete refused pc=%08x steps=%u cycles=%llu writes=%u transfers=%u\n",
            f.cpu.pc,f.steps,(unsigned long long)f.cycles,f.writes,f.transfers);
        abort();
    }
    assert(f.init_calls==2 && f.first_returns==1);
    assert(f.cpu.r[29]==0x801fefe0 && f.cpu.r[31]==0x8002c90c);
    assert(word(0x801feff8)==0x12345678);
    assert(f.cpu.r[2]==0x3c && f.enabled && f.enters==1 && f.exits==1);
    assert(f.opens==1 && f.enables==1 && word(0x8006b548)==1);
    assert(word(0x8006b0e0)==0xf1000000);
    assert(f.kernel.events.records[0].class_word==0xf0000009 &&
           f.kernel.events.records[0].status==0x2000);
    for(i=1;i<MUSASHI_BIOS_EVENTS_CAPACITY;i++) assert(!f.kernel.events.records[i].status);
    assert(word(0x8006cbd4)==0x8003aa18 && f.dma.interrupt==0x00900000);
    assert(f.dma.control==(0x07654321u|0x000b0000u));
    assert(f.transfers==16 && f.status_reads==8 && f.frames>0);
    assert(f.cpu.hi==0 && f.cpu.lo==0);
    assert(musashi_spu_cd_audio_snapshot(f.audio,&state) && !state.faulted);
    assert(state.cycle==f.cycles && state.manual_halfwords==16);
    assert(musashi_spu_cd_audio_copy_ram(f.audio,0x1000,ram,sizeof(ram)));
    for(i=0;i<sizeof(ram);i++) assert(ram[i]==7);
    for(i=0;i<24;i++) {
        assert(musashi_spu_cd_audio_voice_snapshot(f.audio,i,&voice));
        assert(voice.envelope==0);
    }
    printf("SPU source fixture: two calls returned, steps=%u cycles=%llu frames=%u\n",
           f.steps,(unsigned long long)f.cycles,f.frames);
    done();
}
static void refusals(void) {
    FormatterCpu before;
    MusashiSpuCdAudioSnapshot state;
    unsigned i; int32_t handle;
    init(); put(0x8006b54c,0x1f801c02);
    assert(!run_to(0x8002c90c) && f.cpu.pc==0x8003a5b8 && !f.writes); done();
    init(); put(0x8006cb68,0x8003a424);
    assert(!run_to(0x8002c90c) && f.cpu.pc==0x800425f8);
    assert(!f.opens && !word(0x8006cbd4)); done();
    init(); put(0x8006cbc0,0x1f8010f0);
    assert(!run_to(0x8002c90c) && f.cpu.pc==0x80042fc8);
    assert(word(0x8006cbd4)==0x8003aa18 && !f.dma.interrupt && !f.opens); done();
    init(); f.refuse_status=1;
    assert(!run_to(0x8002c90c) && f.cpu.pc==0x8003a62c);
    assert(f.writes>0 && f.transfers==0 && !f.opens); done();
    init(); f.refuse_status=3;
    assert(!run_to(0x8002c90c) && f.cpu.pc==0x8003a900);
    assert(f.transfers==8 && !f.opens);
    assert(musashi_spu_cd_audio_snapshot(f.audio,&state) && !state.faulted);
    assert(state.manual_halfwords==8); done();
    init(); f.refuse_queue=1;
    assert(!run_to(0x8002c90c));
    assert(musashi_spu_cd_audio_snapshot(f.audio,&state) && state.faulted);
    assert(!f.frames && state.submitted_frames==0); done();
    init();
    for(i=0;i<MUSASHI_BIOS_EVENTS_CAPACITY;i++)
        assert(musashi_bios_events_open(&f.kernel.events,1,i,0x2000,0,&handle));
    assert(!run_to(0x8002c90c) && f.cpu.pc==0x8005ceb8);
    assert(f.opens==1 && !f.enables && !f.enabled && word(0x8006b548)==1);
    assert(word(0x8006b0e0)==UINT32_MAX);
    assert(word(0x8006cbd4)==0x8003aa18); done();
    /* Exact unbound active-voice indirect PCs refuse before instruction,
     * clock, registers, or source memory effects, even for a mapped alias. */
    for(i=0;i<2;i++) {
        init(); f.cpu.pc=i?0x8003fc58:0x8003fc2c; f.cpu.npc=f.cpu.pc+4;
        f.cpu.r[2]=0x8003a424; before=f.cpu;
        assert(!formatter_step(&f.memory,&f.cpu));
        assert(!memcmp(&before,&f.cpu,sizeof(before)) && f.cycles==0); done();
    }
}
int main(int argc,char **argv) {
    FILE *file;
    assert(argc==2); file=fopen(argv[1],"rb"); assert(file);
    assert(fread(exe,1,sizeof(exe),file)==sizeof(exe) && fgetc(file)==EOF);
    assert(!fclose(file));
    complete(); refusals();
    puts("SPU startup source fixture and refusal guards PASS; no lifecycle/menu claim");
    return 0;
}
