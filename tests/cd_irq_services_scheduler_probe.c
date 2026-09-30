#include "irq_cpu_fixture.h"
/* Scheduler/service fixture: real CD/source/event owners, explicit test RAM and
 * registered PsyCross seam. No worker VBlank, cold boot, or menu proof. */
#include "musashi_irq_scheduler.h"
#include "musashi_cd_controller.h"
#include "musashi_disc_media.h"
#include "musashi_bios_kernel.h"
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
    MusashiCdRegisterDevice registers;
    MusashiExecutionClock clock;
    MusashiCdIrqServices services;
    MusashiBiosKernel bios;
    uint64_t cycle;
    unsigned service_calls, instructions, irq_raises;
    int active_service, refuse_service, nested_frame, refuse_clock;
    int32_t first_event, second_event, unrelated_event;
    struct { uint32_t r[32], hi, lo, pc, npc; } continuation;
} Fixture;
static uint64_t owner(void *p) { (void)p; return SDL_ThreadID(); }
static int healthy(void *p) { (void)p; return 1; }
static int raise_cd(void *p) {
    Fixture *f=p; f->runtime.controller.status|=4; ++f->irq_raises; return 1;
}
static void put(Fixture *f,uint32_t address,uint32_t value) {
    assert(musashi_boot_write32(f->ram,address,value));
}
static uint32_t get(Fixture *f,uint32_t address) {
    uint32_t value; assert(musashi_boot_read32(f->ram,address,&value)); return value;
}
static int clock_step(void *p,uint32_t cost) {
    Fixture *f=p;
    assert(cost==2 || cost==3);
    assert(f->runtime.scheduler.active_continuation==&f->continuation);
    assert(f->runtime.scheduler.delivering && f->runtime.cd_frame.busy);
    assert(!f->runtime.scheduler.return_consumed);
    assert(!(f->runtime.controller.status&4)); /* actual dispatcher ACK first */
    if(f->refuse_clock) return 0;
    f->cycle+=cost; ++f->instructions;
    return musashi_cd_owned_advance(f->cd,f->cycle);
}
static int deliver(void *p,uint32_t class_word,uint32_t spec) {
    Fixture *f=p;
    MusashiPsyCrossIrqScheduler *s=&f->runtime.scheduler;
    int accepted;
    if(s->owner_thread!=owner(f) || !s->installed || !s->delivering ||
       s->active_continuation!=&f->continuation || s->return_consumed ||
       !f->runtime.cd_frame.busy || f->runtime.cd_frame.faulted || f->active_service) return 0;
    assert(class_word==0xf0000003 && spec==0x20);
    assert(f->instructions>100 && !(f->runtime.controller.status&4));
    f->active_service=1; ++f->service_calls;
    /* These actual ownership operations must refuse without recursively
     * dispatching, replacing the retained service, or consuming B0:17. */
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
    assert(!musashi_psycross_irq_scheduler_dispatch_pending(s,&f->continuation));
    assert(!musashi_psycross_irq_scheduler_uninstall(s));
    if(f->nested_frame) {
        MusashiResetGraphPrefixStop stop={0};
        assert(!musashi_boot_execute_cd_irq_with_services(f->ram,&f->registers,&f->clock,
            &f->services,&f->runtime.cd_frame,0x8006bb34,0x80045290,&stop));
        /* An accepted real event scan after this violation must retain its
         * effects, but may not turn the already-faulted outer frame into success. */
    }
    accepted=!f->refuse_service && musashi_bios_events_deliver(&f->bios.events,class_word,spec,NULL,NULL);
    f->active_service=0;
    return accepted;
}
static int32_t open_event(Fixture *f,uint32_t spec,uint32_t mode,uint32_t callback) {
    int32_t handle,result;
    assert(musashi_bios_events_open(&f->bios.events,0xf0000003,spec,mode,callback,&handle));
    assert(musashi_bios_events_enable(&f->bios.events,handle,&result) && result==1);
    return handle;
}
static int test_event(Fixture *f,int32_t handle) {
    int32_t result; assert(musashi_bios_events_test(&f->bios.events,handle,&result)); return result;
}
static void init(Fixture *f,const MusashiDiscMedia *media,int bind) {
    MusashiCdOwnedHardware hardware={.userdata=f,.current_thread=owner,
        .healthy=healthy,.raise_irq4=raise_cd};
    memset(f,0,sizeof(*f));
    f->ram=calloc(1,sizeof(*f->ram)); assert(f->ram);
    memset(&f->continuation,0x5a,sizeof(f->continuation));
    assert(musashi_psycross_irq_runtime_init(&f->runtime,f->ram,NULL));
    fixture_irq_cpu_attach(&f->runtime.scheduler);
    f->cd=musashi_cd_owned_open_bios_idle(media,&hardware,0); assert(f->cd);
    f->registers=(MusashiCdRegisterDevice){.userdata=f->cd,.read8=musashi_cd_owned_read8,.write8=musashi_cd_owned_write8};
    f->clock=(MusashiExecutionClock){f,clock_step};
    f->services=(MusashiCdIrqServices){.userdata=f,.deliver_event=deliver};
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
    assert(!f->runtime.cd_services);
    assert(musashi_psycross_irq_runtime_bind_cd(&f->runtime,&f->registers,&f->clock));
    if(bind) {
        assert(musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
        assert(!musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
        assert(f->runtime.cd_services==&f->services);
    }
    musashi_bios_kernel_init(&f->bios);
    f->first_event=open_event(f,0x20,0x2000,0);
    f->unrelated_event=open_event(f,0x40,0x1000,0x8006189c);
    f->second_event=open_event(f,0x20,0x2000,0);
    put(f,0x8006bb34,0x80042780); put(f,0x8006bb38,0x8006cb14);
    for(unsigned i=0;i<4;++i) put(f,0x8006cf4c+4*i,0x1f801800+i);
    put(f,0x800737d0,0x80043f08); put(f,0x800737d4,0x80043e08);
    put(f,0x8006ce54,1); put(f,0x8006cd54,0); /* Setloc policies */
    put(f,0x8006cc84,0x80043398); /* explicit installed source sync callback */
    put(f,0x8006cb88,0x1f801070); put(f,0x8006cb8c,0x1f801074);
    assert(musashi_boot_write16(f->ram,0x8006bafc,1));
    assert(musashi_boot_write16(f->ram,0x8006bb2c,4));
    put(f,0x8006bb08,0x80045290);
    put(f,0x8006ca90,0xa5a5a5a5); put(f,0x8006caec,0x5a5a5a5a);
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&f->runtime.scheduler,0x8006bb34));
    assert(musashi_psycross_irq_scheduler_set_enabled(&f->runtime.scheduler,1));
    f->runtime.controller.mask=4;
}
static void close_fixture(Fixture *f) {
    assert(musashi_psycross_irq_scheduler_uninstall(&f->runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&f->runtime));
    assert(!vsync_callback);
    assert(musashi_cd_owned_close(f->cd)); free(f->ram);
}
static void setloc(Fixture *f) {
    uint8_t *command=musashi_boot_ram_span(f->ram,0x8006cca5,1); assert(command); *command=2;
    assert(musashi_cd_owned_write8(f->cd,0x1f801800,0));
    assert(musashi_cd_owned_write8(f->cd,0x1f801802,0));
    assert(musashi_cd_owned_write8(f->cd,0x1f801802,2));
    assert(musashi_cd_owned_write8(f->cd,0x1f801802,0x16));
    assert(musashi_cd_owned_write8(f->cd,0x1f801801,2));
    f->cycle+=0x800; assert(musashi_cd_owned_advance(f->cd,f->cycle));
    assert(f->runtime.controller.status==4 && f->irq_raises==1);
}
static void lifecycle_and_success(const MusashiDiscMedia *media) {
    Fixture f; unsigned char continuation[sizeof(f.continuation)];
    init(&f,media,1); memcpy(continuation,&f.continuation,sizeof(continuation));
    assert(!deliver(&f,0xf0000003,0x20)); assert(!f.service_calls);
    setloc(&f);
    assert(musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&f.continuation));
    assert(f.service_calls==1 && f.runtime.scheduler.deliveries==1);
    assert(f.runtime.scheduler.last_result==MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    assert(!f.runtime.scheduler.active_continuation && !f.runtime.scheduler.return_consumed);
    assert(!f.runtime.scheduler.delivering && !f.runtime.scheduler.pumping && !f.runtime.scheduler.faulted);
    assert(!f.runtime.cd_frame.busy && !f.runtime.cd_frame.faulted && !f.runtime.controller.status);
    assert(get(&f,0x800763d8)==2 && get(&f,0x800763dc)==0);
    assert(test_event(&f,f.first_event)==1 && test_event(&f,f.second_event)==1);
    assert(test_event(&f,f.first_event)==0 && test_event(&f,f.unrelated_event)==0);
    assert(!memcmp(continuation,&f.continuation,sizeof(continuation)));
    assert(get(&f,0x8006ca90)==0xa5a5a5a5 && get(&f,0x8006caec)==0x5a5a5a5a);
    assert(musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&f.continuation));
    assert(f.service_calls==1 && f.runtime.scheduler.deliveries==1); /* idle != repeated delivery */
    close_fixture(&f);
}
static void no_matching_events(const MusashiDiscMedia *media) {
    Fixture f; int32_t result;
    init(&f,media,1);
    assert(musashi_bios_events_disable(&f.bios.events,f.first_event,&result) && result==1);
    assert(musashi_bios_events_disable(&f.bios.events,f.second_event,&result) && result==1);
    setloc(&f);
    assert(musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&f.continuation));
    assert(f.service_calls==1 && !test_event(&f,f.first_event) && !test_event(&f,f.second_event));
    assert(!f.runtime.cd_frame.faulted && !f.runtime.scheduler.faulted);
    assert(f.runtime.scheduler.last_result==MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    close_fixture(&f);
}
static void refusals(const MusashiDiscMedia *media) {
    for(unsigned kind=0;kind<6;++kind) {
        Fixture f; unsigned char continuation[sizeof(f.continuation)];
        init(&f,media,kind!=0); memcpy(continuation,&f.continuation,sizeof(continuation));
        if(kind==1) f.refuse_service=1;
        if(kind==2) f.nested_frame=1;
        if(kind==3) {
            /* Earlier poll record commits before the matching unsupported
             * callback. A later poll must remain unvisited after refusal. */
            int32_t result;
            assert(musashi_bios_events_close(&f.bios.events,f.unrelated_event,&result) && result==1);
            f.unrelated_event=open_event(&f,0x20,0x1000,0x8006189c);
        }
        if(kind==4) put(&f,0x8006cc84,0x800433c0); /* another real mapped leaf */
        if(kind==5) f.refuse_clock=1;
        setloc(&f);
        assert(!musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&f.continuation));
        assert(f.runtime.scheduler.faulted && f.runtime.cd_frame.faulted && !f.runtime.cd_frame.busy);
        assert(f.runtime.scheduler.active_continuation == &f.continuation && !f.runtime.scheduler.return_consumed);
        assert(!f.runtime.scheduler.delivering && !f.runtime.scheduler.pumping);
        assert(test_event(&f,f.first_event)==(kind==2 || kind==3));
        assert(test_event(&f,f.second_event)==(kind==2));
        assert(!memcmp(continuation,&f.continuation,sizeof(continuation)));
        assert(get(&f,0x8006ca90)==0xa5a5a5a5 && get(&f,0x8006caec)==0x5a5a5a5a);
        assert(!musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&f.continuation));
        close_fixture(&f);
    }
}
static int foreign_bind(void *p) {
    Fixture *f=p;
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
    assert(!f->runtime.cd_services); return 0;
}
static int bind_during_priority(void *p,uint16_t pending) {
    Fixture *f=p;
    assert(pending==4 && f->runtime.scheduler.pumping && f->runtime.scheduler.delivering);
    assert(!f->runtime.cd_services);
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f->runtime,&f->services));
    assert(!f->runtime.cd_services);
    assert(fixture_irq_kernel_entry(&f->runtime.scheduler, pending));
    return 1;
}
static void binder(const MusashiDiscMedia *media) {
    Fixture f; SDL_Thread *thread; MusashiCdIrqServices missing={0};
    init(&f,media,0);
    assert(!musashi_psycross_irq_runtime_bind_cd_services(NULL,&f.services));
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f.runtime,NULL));
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f.runtime,&missing));
    thread=SDL_CreateThread(foreign_bind,"foreign-CD-service",&f); assert(thread);
    SDL_WaitThread(thread,NULL);
    assert(!f.runtime.cd_services && !f.runtime.scheduler.faulted);
    /* Exercise the pumping guard while services are genuinely unbound:
     * duplicate-binding refusal alone would not test this condition. */
    put(&f,0x8006cc84,0);
    f.runtime.scheduler.before_dispatch=bind_during_priority;
    f.runtime.scheduler.before_dispatch_userdata=&f;
    setloc(&f);
    assert(musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&f.continuation));
    assert(!f.runtime.cd_services && f.service_calls==0);
    assert(musashi_psycross_irq_runtime_bind_cd_services(&f.runtime,&f.services));
    assert(!musashi_psycross_irq_runtime_bind_cd_services(&f.runtime,&f.services));
    close_fixture(&f);
}
int main(int argc,char **argv) {
    MusashiDiscMedia *media; assert(argc==3);
    assert(SDL_Init(0)==0); g_intrMutex=SDL_CreateMutex(); assert(g_intrMutex);
    media=musashi_disc_media_open_pinned(argv[1],argv[2]); assert(media);
    lifecycle_and_success(media); no_matching_events(media); refusals(media); binder(media);
    musashi_disc_media_close(media); SDL_DestroyMutex(g_intrMutex); SDL_Quit();
    puts("CD_IRQ_SERVICES_SCHEDULER_PASS fixture_only=1"); return 0;
}
