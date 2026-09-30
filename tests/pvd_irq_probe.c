#include "irq_cpu_fixture.h"
/* First PVD source-IRQ fixture. Metadata/registration/clock are test-owned;
 * actual pinned bytes arrive only through CD FIFO -> DMA3 -> guest RAM.
 * The GPU frontend has an explicit synthetic renderer backend. No real
 * PsyCross worker, native cold boot, physical timing, or menu proof. */
#include "musashi_irq_scheduler.h"
#include "musashi_cd_controller.h"
#include "musashi_cd_dma3.h"
#include "musashi_dma_controller.h"
#include "musashi_disc_media.h"
#include "musashi_bios_kernel.h"
#include "musashi_scanline_timer.h"
#include "musashi_gpu_controller.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_mutex *g_intrMutex;
void (*vsync_callback)(void);
int VSyncCallback(void (*callback)(void)) { vsync_callback=callback; return 0; }

typedef struct Fixture {
    MusashiBootMemory *ram;
    MusashiPsyCrossIrqRuntime runtime;
    MusashiCdOwned *cd;
    MusashiCdDma3 *dma3;
    MusashiDmaController dma;
    MusashiCdRegisterDevice registers;
    MusashiExecutionClock clock;
    MusashiCdIrqServices services;
    MusashiBiosKernel bios;
    MusashiScanlineTimer timer;
    MusashiGpuController gpu;
    MusashiGpuDevice gpu_device;
    MusashiCallbackDevice callback;
    uint64_t cycle;
    uint32_t common_delay;
    unsigned service20, service40, irq_raises, instructions, gpu_reads, timer_reads;
    unsigned commands[32], command_count, requests[32], request_count;
    unsigned dma_words[8], dma_count;
    uint8_t header[12];
    int initialized_renderer, event_busy, reject_dma, reject_event;
    int fail_clock, corrupt_ra, reenter, injected, stall_clock;
    int32_t event20, event40;
    struct { uint32_t r[32], hi, lo, pc, npc; } continuation;
} Fixture;
static uint64_t owner(void *p) { (void)p; return SDL_ThreadID(); }
static int healthy(void *p) { (void)p; return 1; }
static MusashiCdOwnedState cdstate(Fixture *f) {
    MusashiCdOwnedState s; assert(musashi_cd_owned_get_state(f->cd,&s)); return s;
}
static MusashiCdDma3State dmastate(Fixture *f) {
    MusashiCdDma3State s; assert(musashi_cd_dma3_get_state(f->dma3,&s)); return s;
}
static int transfer_idle(void *p,int *idle) {
    MusashiCdDma3State s=dmastate(p);
    if(s.fault) return 0;
    *idle=!(s.chcr&0x01000000u); return 1;
}
static int raise_cd(void *p) { Fixture *f=p; f->runtime.controller.status|=4; ++f->irq_raises; return 1; }
static int dma_healthy(void *p) { return cdstate(p).fault==0; }
static int request(void *p,int *out) {
    MusashiCdOwnedState s=cdstate(p);
    if(s.fault) return 0;
    *out=s.data_requested != 0; return 1;
}
static int available(void *p,size_t *bytes) {
    MusashiCdOwnedState s=cdstate(p);
    if(s.fault || s.fifo_cursor>s.fifo_size) return 0;
    *bytes=(s.data_requested && s.fifo_valid)?s.fifo_size-s.fifo_cursor:0; return 1;
}
static int data(void *p,uint8_t *out,size_t size) {
    Fixture *f=p;
    if(f->reject_dma) return 0;
    if(!musashi_cd_owned_read_data(f->cd,out,size)) return 0;
    if(size==12) memcpy(f->header,out,12);
    return 1;
}
static void put(Fixture *f,uint32_t address,uint32_t value) { assert(musashi_boot_write32(f->ram,address,value)); }
static uint32_t get(Fixture *f,uint32_t address) { uint32_t v; assert(musashi_boot_read32(f->ram,address,&v)); return v; }
static int advance(Fixture *f,uint64_t cycle) {
    MusashiCdDma3State d=dmastate(f);
    if((d.chcr&0x01000000) && d.due<=cycle) {
        if(!musashi_cd_owned_advance(f->cd,d.due) || !musashi_cd_dma3_advance(f->dma3,d.due)) return 0;
    }
    if(!musashi_cd_owned_advance(f->cd,cycle) || !musashi_cd_dma3_advance(f->dma3,cycle) ||
       !musashi_scanline_timer_advance(&f->timer,cycle)) return 0;
    f->cycle=cycle; return 1;
}
static int clock_step(void *p,uint32_t cost) {
    Fixture *f=p;
    assert(cost==2 || cost==3);
    assert(f->runtime.scheduler.active_continuation==&f->continuation);
    assert(f->runtime.scheduler.delivering && f->runtime.cd_frame.busy);
    assert(!f->runtime.scheduler.return_consumed);
    if(f->fail_clock && f->dma_count) return 0;
    if(f->stall_clock && f->dma_count) return 1; /* deliberately broken clock negative */
    if(f->reenter && f->dma_count && !f->injected) {
        MusashiResetGraphPrefixStop stop; f->injected=1;
        assert(!musashi_boot_execute_cd_irq_with_services(f->ram,&f->registers,&f->clock,
            &f->services,&f->runtime.cd_frame,0x8006bb34,0x80045290,&stop));
    }
    if(f->corrupt_ra && f->dma_count==2 && !f->injected) {
        f->injected=1; put(f,0x8006cac0,0x80043398); /* ready-frame saved RA */
    }
    ++f->instructions;
    return advance(f,f->cycle+cost);
}
static int read_byte(void *p,uint32_t address,uint8_t *value) { return musashi_cd_owned_read8(((Fixture *)p)->cd,address,value); }
static int write_byte(void *p,uint32_t address,uint8_t value) {
    Fixture *f=p; MusashiCdOwnedState before=cdstate(f);
    if(!musashi_cd_owned_write8(f->cd,address,value)) return 0;
    if(address==0x1f801801 && before.index==0) {
        assert(f->command_count<32); f->commands[f->command_count++]=value;
    }
    if(address==0x1f801803 && before.index==0) {
        assert(f->request_count<32); f->requests[f->request_count++]=value;
    }
    return 1;
}
static int read_word(void *p,uint32_t address,uint32_t *value) {
    Fixture *f=p;
    return address==0x1f8010f0?musashi_dma_controller_read32(&f->dma,address,value):
        musashi_cd_dma3_read32(f->dma3,address,value);
}
static int write_word(void *p,uint32_t address,uint32_t value) {
    Fixture *f=p;
    if(address==0x1f801020) { f->common_delay=value; return 1; }
    if(address==0x1f8010f0) return musashi_dma_controller_write32(&f->dma,address,value);
    if(!musashi_cd_dma3_write32(f->dma3,address,value)) return 0;
    if(address==0x1f8010b8 && value==0x11000000) {
        MusashiCdDma3State s=dmastate(f);
        assert(f->dma_count<8 && s.chcr==0x01000000 && s.due==f->cycle+(s.bcr&0xffff));
        f->dma_words[f->dma_count++]=s.bcr&0xffff;
        assert(s.transfers==f->dma_count-1); /* source write did not complete */
    }
    return 1;
}
static int renderer_reset(void *p) { ((Fixture *)p)->initialized_renderer=1; return 1; }
static int renderer_draw(void *p,uint32_t word) { (void)word; return ((Fixture *)p)->initialized_renderer; }
static int renderer_display(void *p,int enabled) { (void)enabled; return ((Fixture *)p)->initialized_renderer; }
static int renderer_ready(void *p) { return ((Fixture *)p)->initialized_renderer?1:-1; }
static uint32_t gpu_read(void *p,uint32_t address) {
    Fixture *f=p; uint32_t v; ++f->gpu_reads;
    assert(musashi_gpu_controller_read32(&f->gpu,address,&v)); return v;
}
static uint16_t timer_read(void *p,uint32_t address) {
    Fixture *f=p; uint16_t v; ++f->timer_reads;
    assert(musashi_scanline_timer_read16(&f->timer,address,&v)); return v;
}
static int deliver(void *p,uint32_t class_word,uint32_t spec) {
    Fixture *f=p; MusashiPsyCrossIrqScheduler *s=&f->runtime.scheduler; int accepted;
    assert(s->active_continuation==&f->continuation && s->delivering && !s->return_consumed);
    assert(f->runtime.cd_frame.busy && !f->runtime.cd_frame.faulted && !f->event_busy);
    assert(class_word==0xf0000003 && (spec==0x20 || spec==0x40));
    f->event_busy=1;
    if(spec==0x20) ++f->service20; else ++f->service40;
    assert(!musashi_psycross_irq_scheduler_dispatch_pending(s,&f->continuation));
    accepted=!(f->reject_event && spec==0x40) &&
        musashi_bios_events_deliver(&f->bios.events,class_word,spec,NULL,NULL);
    f->event_busy=0; return accepted;
}
static int32_t event(Fixture *f,uint32_t spec) {
    int32_t handle,result;
    assert(musashi_bios_events_open(&f->bios.events,0xf0000003,spec,0x2000,0,&handle));
    assert(musashi_bios_events_enable(&f->bios.events,handle,&result) && result==1); return handle;
}
static int consume(Fixture *f,int32_t handle) { int32_t result; assert(musashi_bios_events_test(&f->bios.events,handle,&result)); return result; }
static void init(Fixture *f,const MusashiDiscMedia *media,const uint8_t *exe,size_t exe_size) {
    MusashiCdOwnedHardware hw={.userdata=f,.current_thread=owner,.healthy=healthy,
        .raise_irq4=raise_cd,.data_transfer_idle=transfer_idle};
    MusashiCdDma3Device dev={f,owner,dma_healthy,available,data,request};
    MusashiGpuBackend backend={.userdata=f,.reset=renderer_reset,
        .draw_mode=renderer_draw,.display_enable=renderer_display,
        .clear_fifo=renderer_reset,.ready=renderer_ready};
    memset(f,0,sizeof(*f)); f->ram=calloc(1,sizeof(*f->ram)); assert(f->ram);
    assert(musashi_boot_map_exe(f->ram,exe,exe_size));
    memset(&f->continuation,0x5a,sizeof(f->continuation));
    assert(musashi_psycross_irq_runtime_init(&f->runtime,f->ram,NULL));
    fixture_irq_cpu_attach(&f->runtime.scheduler);
    f->cd=musashi_cd_owned_open_bios_idle(media,&hw,0); assert(f->cd);
    musashi_dma_controller_init(&f->dma,&f->runtime.controller);
    f->dma3=musashi_cd_dma3_open(f->ram,&f->dma,&dev,0); assert(f->dma3);
    assert(musashi_scanline_timer_init_paced(&f->timer,0));
    /* Explicit earlier SDK Timer1 configuration; no fabricated edge/count. */
    assert(musashi_scanline_timer_write32(&f->timer,0x1f801114,0x107));
    assert(musashi_gpu_controller_init(&f->gpu,&backend));
    f->gpu_device=(MusashiGpuDevice){.read32=gpu_read,.userdata=f};
    f->callback=(MusashiCallbackDevice){.read16=timer_read,.reset_device=&f->gpu_device,.userdata=f};
    f->registers=(MusashiCdRegisterDevice){f,read_byte,write_byte,read_word,write_word};
    f->clock=(MusashiExecutionClock){f,clock_step};
    f->services=(MusashiCdIrqServices){.userdata=f,.deliver_event=deliver,.callback_device=&f->callback};
    musashi_bios_kernel_init(&f->bios); f->event20=event(f,0x20); f->event40=event(f,0x40);
    assert(musashi_psycross_irq_runtime_bind_cd(&f->runtime,&f->registers,&f->clock));
    assert(musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
    /* Test-owned IRQ registration/context, using the actual source identities.
     * EXE supplies the command-policy tables/MMIO cells, not a capture. */
    put(f,0x8006bb34,0x80042780); put(f,0x8006bb38,0x8006cb14);
    put(f,0x8006cb88,0x1f801070); put(f,0x8006cb8c,0x1f801074);
    put(f,0x8006bb08,0x80045290); put(f,0x8006cc84,0x80043398); put(f,0x8006cc88,0x800433c0);
    assert(musashi_boot_write16(f->ram,0x8006bafc,1));
    assert(musashi_boot_write16(f->ram,0x8006bb2c,4));
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&f->runtime.scheduler,0x8006bb34));
    assert(musashi_psycross_irq_scheduler_set_enabled(&f->runtime.scheduler,1));
    f->runtime.controller.mask=4;
    put(f,0x8006c9b8,0xa5a5a5a5); put(f,0x8006caec,0x5a5a5a5a);
    memset(f->ram->bytes+0x78000,0xa5,2048); /* poison, never prefilled PVD data */
    put(f,0x80077ffc,0x13572468); put(f,0x80078800,0x24681357);
}
static int dispatch(Fixture *f) {
    unsigned char before[sizeof(f->continuation)]; int accepted;
    memcpy(before,&f->continuation,sizeof(before));
    accepted=musashi_psycross_irq_scheduler_dispatch_pending(&f->runtime.scheduler,&f->continuation);
    assert(!memcmp(before,&f->continuation,sizeof(before)));
    if(!accepted) fprintf(stderr,"PVD_STOP call=%08x target=%08x steps=%u CDfault=%u DMAfault=%u transfers=%llu bytes=%llu\n",
        f->runtime.cd_stop.call_address,f->runtime.cd_stop.target_address,f->runtime.cd_frame.instructions,
        cdstate(f).fault,dmastate(f).fault,(unsigned long long)dmastate(f).transfers,(unsigned long long)dmastate(f).bytes);
    return accepted;
}
static void command(Fixture *f,unsigned cmd,const uint8_t *params,unsigned count) {
    uint8_t *slot=musashi_boot_ram_span(f->ram,0x8006cca5,1); assert(slot); *slot=(uint8_t)cmd;
    assert(write_byte(f,0x1f801800,0));
    for(unsigned i=0;i<count;++i) assert(write_byte(f,0x1f801802,params[i]));
    assert(write_byte(f,0x1f801801,(uint8_t)cmd)); assert(advance(f,cdstate(f).due));
    assert(dispatch(f));
}
static void prepare(Fixture *f) {
    const uint8_t location[]={0,2,0x16}, mode=0xa0;
    command(f,2,location,3); assert(consume(f,f->event20)==1);
    /* Explicit source-condition fixture after4637C/460CC setup: one sector,
     * 512 payload words, source callback save/restore and synchronous mode.
     * No ready/status/count-complete field is forced after the IRQ begins. */
    put(f,0x8006cfa0,1); put(f,0x8006cfa4,0x80078000); put(f,0x8006cfa8,0x80078000);
    put(f,0x8006cfac,0xa0); put(f,0x8006cfb0,0x200); put(f,0x8006cfb4,1);
    put(f,0x8006cfc0,16); put(f,0x8006cfc4,0x80043398); put(f,0x8006cfc8,0x800433c0);
    put(f,0x8006cf9c,0x800433e8); put(f,0x8006cfd0,0);
    put(f,0x8006cc84,0); put(f,0x8006cc88,0x80045d00);
    command(f,0x0e,&mode,1); command(f,6,NULL,0);
    assert(cdstate(f).reading && !cdstate(f).sectors_fetched);
    assert(advance(f,cdstate(f).sector_due));
    assert(cdstate(f).announced_valid && cdstate(f).announced_lba==16 &&
           cdstate(f).announced_sequence==1 && !cdstate(f).fifo_valid &&
           !cdstate(f).data_requested && !cdstate(f).fifo_cursor);
    assert(f->runtime.controller.status&4);
    assert(get(f,0x80078000)==0xa5a5a5a5 && dmastate(f).transfers==0);
}
static void finish(Fixture *f) {
    assert(musashi_psycross_irq_scheduler_uninstall(&f->runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&f->runtime));
    assert(musashi_cd_dma3_close(f->dma3)); assert(musashi_cd_owned_close(f->cd)); free(f->ram);
}
static void success(const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    Fixture f; uint8_t raw[2352]; MusashiCdOwnedState s;
    init(&f,media,exe,size); prepare(&f); assert(dispatch(&f));
    assert(musashi_disc_media_read_sector(media,16,raw,sizeof(raw)));
    assert(!memcmp(f.ram->bytes+0x78000,raw+24,2048));
    assert(!memcmp(f.header,raw+12,12));
    assert(get(&f,0x80077ffc)==0x13572468 && get(&f,0x80078800)==0x24681357);
    assert(get(&f,0x8006c9b8)==0xa5a5a5a5 && get(&f,0x8006caec)==0x5a5a5a5a);
    assert(f.dma_count==2 && f.dma_words[0]==3 && f.dma_words[1]==512);
    assert(dmastate(&f).transfers==2 && dmastate(&f).bytes==2060 && !dmastate(&f).chcr);
    assert(cdstate(&f).fifo_cursor==2060 && cdstate(&f).data_bytes_read==2060);
    assert(get(&f,0x8006cfb4)==0 && get(&f,0x8006cfa8)==0x80078800 && get(&f,0x8006cfc0)==17);
    assert(get(&f,0x8006cc84)==0x80043398 && get(&f,0x8006cc88)==0x800433c0);
    assert(f.command_count==4 && f.commands[3]==9 && f.service40==1 && consume(&f,f.event40)==1);
    assert(f.request_count==3 && f.requests[0]==0 && f.requests[1]==0x80 && f.requests[2]==0x80);
    assert(f.gpu_reads && f.timer_reads && f.gpu_reads==f.timer_reads);
    assert(f.common_delay==0x1325 && f.runtime.scheduler.last_result==MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    /* Later Pause ACK and INT2 use the real owner deadline and restored sync.
     * No manually latched IRQ or service completion is supplied. */
    s=cdstate(&f); assert(s.command==9 && s.phase);
    assert(advance(&f,s.due)); assert(dispatch(&f));
    s=cdstate(&f); assert(s.phase==2 && f.service20==1 && !consume(&f,f.event20));
    assert(advance(&f,s.due)); assert(dispatch(&f));
    assert(f.service20==2 && consume(&f,f.event20)==1 && !consume(&f,f.event20));
    assert(!cdstate(&f).reading && !cdstate(&f).phase && !cdstate(&f).interrupt);
    assert(dmastate(&f).bytes==2060 && get(&f,0x8006cfb4)==0);
    finish(&f);
}

enum Negative {
    BAD_TABLE, BAD_READY_CALLBACK, BAD_DMA_POINTER, BAD_DESTINATION,
    BAD_RESTORE, BAD_FINAL_CALLBACK, MISSING_CALLBACK_DEVICE, REFUSE_DMA,
    REFUSE_EVENT, REFUSE_CLOCK, REENTER_FRAME, CORRUPT_SAVED_RA, STALLED_CLOCK,
    NEGATIVE_COUNT
};
static void negative(const MusashiDiscMedia *media,const uint8_t *exe,size_t size,enum Negative which) {
    Fixture f; MusashiCdDma3State d; MusashiCdOwnedState c;
    init(&f,media,exe,size); prepare(&f);
    switch(which) {
    case BAD_TABLE: put(&f,0x800737cc,0x80043f08); break;
    case BAD_READY_CALLBACK: put(&f,0x8006cc88,0x80043398); break;
    case BAD_DMA_POINTER: put(&f,0x8006cf90,0x1f8010b4); break;
    case BAD_DESTINATION: put(&f,0x8006cfa8,0x801ffffc); break;
    case BAD_RESTORE: put(&f,0x8006cfc4,0x800433c0); break;
    case BAD_FINAL_CALLBACK: put(&f,0x8006cf9c,0x80043398); break;
    case MISSING_CALLBACK_DEVICE: f.services.callback_device=NULL; break;
    case REFUSE_DMA: f.reject_dma=1; break;
    case REFUSE_EVENT: f.reject_event=1; break;
    case REFUSE_CLOCK: f.fail_clock=1; break;
    case REENTER_FRAME: f.reenter=1; break;
    case CORRUPT_SAVED_RA: f.corrupt_ra=1; break;
    case STALLED_CLOCK: f.stall_clock=1; break;
    default: assert(0);
    }
    fprintf(stderr,"PVD_NEGATIVE case=%d\n",which);
    assert(!dispatch(&f));
    assert(f.runtime.cd_frame.faulted && f.runtime.scheduler.faulted);
    assert(!f.runtime.cd_frame.busy && !f.runtime.scheduler.delivering);
    assert(f.runtime.scheduler.active_continuation == &f.continuation);
    assert(f.runtime.scheduler.last_result!=MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    d=dmastate(&f); c=cdstate(&f);
    assert(get(&f,0x8006c9b8)==0xa5a5a5a5 && get(&f,0x8006caec)==0x5a5a5a5a);
    assert(get(&f,0x80077ffc)==0x13572468 && get(&f,0x80078800)==0x24681357);
    if(which==BAD_RESTORE || which==BAD_FINAL_CALLBACK || which==REFUSE_EVENT || which==CORRUPT_SAVED_RA) {
        uint8_t raw[2352]; assert(musashi_disc_media_read_sector(media,16,raw,sizeof(raw)));
        assert(d.bytes==2060 && d.transfers==2 && c.fifo_cursor==2060);
        assert(!memcmp(f.ram->bytes+0x78000,raw+24,2048)); /* no rollback of actual copy */
        assert(get(&f,0x8006cfb4)==0 && get(&f,0x8006cfa8)==0x80078800);
    } else {
        assert(get(&f,0x80078000)==0xa5a5a5a5 && !f.service40);
        assert(d.bytes==(which==BAD_DESTINATION?12u:0u));
        assert(c.fifo_cursor==d.bytes);
        assert(get(&f,0x8006cfb4)==1);
    }
    if(which==BAD_TABLE) assert(f.runtime.cd_stop.call_address==0x80043e00);
    if(which==BAD_READY_CALLBACK || which==BAD_DMA_POINTER)
        assert(f.runtime.cd_stop.call_address==0x80045300 && !d.fault && !f.dma_count);
    if(which==BAD_DESTINATION) assert(f.runtime.cd_stop.call_address==0x80045e2c);
    if(which==BAD_RESTORE) assert(f.runtime.cd_stop.call_address==0x80045f3c && !f.service40);
    if(which==BAD_FINAL_CALLBACK) assert(f.runtime.cd_stop.call_address==0x80045fb4 && !f.service40);
    if(which==REFUSE_EVENT) assert(f.service40==1 && !consume(&f,f.event40));
    if(which==CORRUPT_SAVED_RA) {
        assert(f.injected && f.service40==1 && consume(&f,f.event40)==1);
        assert(f.runtime.cd_stop.call_address==0x80045fc8 && f.runtime.cd_stop.target_address==0x80043398);
    }
    if(which==REENTER_FRAME) assert(f.injected);
    if(which==REFUSE_DMA) assert(d.fault && d.chcr==0x01000000 && !d.transfers && !c.fault);
    if(which==STALLED_CLOCK) assert(f.runtime.cd_frame.instructions==16384 && d.chcr==0x01000000);
    finish(&f);
}

int main(int argc,char **argv) {
    MusashiDiscMedia *media; FILE *file; uint8_t *exe; size_t size=0x65000;
    assert(argc==4); assert(SDL_Init(0)==0); g_intrMutex=SDL_CreateMutex(); assert(g_intrMutex);
    media=musashi_disc_media_open_pinned(argv[1],argv[2]); assert(media);
    exe=malloc(size); assert(exe); file=fopen(argv[3],"rb"); assert(file);
    assert(fread(exe,1,size,file)==size && fgetc(file)==EOF); assert(!fclose(file));
    success(media,exe,size);
    for(int i=0;i<NEGATIVE_COUNT;++i) negative(media,exe,size,(enum Negative)i);
    free(exe); musashi_disc_media_close(media); SDL_DestroyMutex(g_intrMutex); SDL_Quit();
    puts("PVD_IRQ_PASS fixture_only=1"); return 0;
}
