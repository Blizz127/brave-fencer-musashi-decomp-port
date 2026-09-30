#include "irq_cpu_fixture.h"
#include "musashi_boot_memory.h"
#include "musashi_bios_events.h"
#include "musashi_cd_controller.h"
#include "musashi_disc_media.h"
#include "musashi_irq_scheduler.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Explicit PsyCross registration fixture; the actual scheduler and source CD
 * handler run, but no window, source VBlank, retail CPU or native boot is claimed. */
SDL_mutex *g_intrMutex;
void (*vsync_callback)(void);
int VSyncCallback(void (*callback)(void)) { vsync_callback=callback; return 0; }

typedef struct Fixture {
    MusashiBootMemory *memory;
    MusashiCdOwned *cd;
    MusashiPsyCrossIrqRuntime runtime;
    MusashiCdRegisterDevice registers;
    MusashiExecutionClock clock;
    MusashiCdIrqServices services;
    MusashiBiosEvents events;
    uint64_t cycle;
    unsigned costs, irq_raises, event_deliveries, service_accepts, card_callbacks;
    uint32_t event_class, event_spec;
    uint64_t event_cycle;
    int refuse_clock, recurse, changed, refuse_event, recurse_event;
    int nested_event_attempted, nested_event_result;
    uint32_t corrupt_return;
    const void *expected_continuation;
} Fixture;

static int card_event_callback(void *userdata, uint32_t target) {
    Fixture *f = userdata;
    (void)target;
    ++f->card_callbacks;
    return 1;
}

static int deliver_event(void *userdata, uint32_t class_word, uint32_t spec) {
    Fixture *f = userdata;
    if (f->event_deliveries++ != 0) return 0;
    f->event_class = class_word;
    f->event_spec = spec;
    f->event_cycle = f->cycle;
    if (f->refuse_event) return 0;
    if (!musashi_bios_events_deliver(&f->events, class_word, spec,
                                     card_event_callback, f)) return 0;
    ++f->service_accepts;
    if (f->recurse_event && !f->nested_event_attempted) {
        MusashiResetGraphPrefixStop stop = {0};
        f->nested_event_attempted = 1;
        f->nested_event_result = musashi_boot_execute_cd_irq_with_services(
            f->memory, &f->registers, &f->clock, &f->services,
            &f->runtime.cd_frame, 0x8006bb34u, 0x80045290u, &stop);
        if (f->nested_event_result) return 0;
    }
    return 1;
}

/* Negative adapter: a host callback that bypasses the live BIOS event owner
 * must be rejected as an unavailable DeliverEvent service. */
static int bypass_deliver_event(void *userdata, uint32_t class_word,
                                uint32_t spec) {
    Fixture *f = userdata;
    f->event_class = class_word;
    f->event_spec = spec;
    ++f->event_deliveries;
    return 0;
}
static unsigned unstable_sample;
static int unstable_read(void *p,uint32_t address,uint8_t *out) {
    if (!musashi_cd_owned_read8(p,address,out)) return 0;
    if(address==0x1f801803u) *out=(uint8_t)(0xe2u+(++unstable_sample&1u));
    return 1;
}
static int refuse_read(void *p,uint32_t address,uint8_t *out) {
    (void)p; (void)address; (void)out; return 0;
}
static uint64_t tid(void *p) { (void)p; return (uint64_t)SDL_ThreadID(); }
static int health(void *p) { (void)p; return 1; }
static int raise_cd(void *p) {
    Fixture *f=p; f->runtime.controller.status|=4; ++f->irq_raises; return 1;
}
static void put(MusashiBootMemory *m,uint32_t a,uint32_t v) { assert(musashi_boot_write32(m,a,v)); }
static uint32_t get(MusashiBootMemory *m,uint32_t a) { uint32_t v; assert(musashi_boot_read32(m,a,&v)); return v; }
static int tick(void *p,uint32_t cost) {
    Fixture *f=p;
    assert(cost==2 || cost==3);
    if (f->expected_continuation) {
        assert(f->runtime.scheduler.active_continuation==f->expected_continuation);
        assert(f->runtime.scheduler.delivering && !f->runtime.scheduler.return_consumed);
        assert(!(f->runtime.controller.status&4)); /* CPU ACK precedes callback. */
    }
    if (f->refuse_clock) return 0;
    if (f->recurse && !f->changed) {
        MusashiResetGraphPrefixStop stop={0}; f->changed=1;
        assert(!musashi_boot_execute_cd_irq(f->memory,&f->registers,&f->clock,
            &f->runtime.cd_frame,0x8006bb34,0x80045290,&stop));
    }
    if (f->corrupt_return && f->costs>60) put(f->memory,0x8006cabc,f->corrupt_return);
    ++f->costs; f->cycle+=cost;
    return musashi_cd_owned_advance(f->cd,f->cycle);
}
static void init(Fixture *f,const MusashiDiscMedia *media) {
    MusashiCdOwnedHardware hardware={.userdata=f,.current_thread=tid,.healthy=health,.raise_irq4=raise_cd};
    memset(f,0,sizeof(*f)); f->memory=calloc(1,sizeof(*f->memory)); assert(f->memory);
    assert(musashi_psycross_irq_runtime_init(&f->runtime,f->memory,NULL));
    fixture_irq_cpu_attach(&f->runtime.scheduler);
    f->cd=musashi_cd_owned_open_bios_idle(media,&hardware,0); assert(f->cd);
    f->registers=(MusashiCdRegisterDevice){.userdata=f->cd,.read8=musashi_cd_owned_read8,.write8=musashi_cd_owned_write8};
    f->clock=(MusashiExecutionClock){f,tick};
    musashi_bios_events_init(&f->events);
    f->services=(MusashiCdIrqServices){.userdata=f,.deliver_event=deliver_event};
    put(f->memory,0x8006bb34,0x80042780); put(f->memory,0x8006bb38,0x8006cb14);
    for (unsigned i=0;i<4;++i) put(f->memory,0x8006cf4c+4*i,0x1f801800+i);
    put(f->memory,0x800737d0,0x80043f08); put(f->memory,0x800737d4,0x80043e08);
    put(f->memory,0x8006ce50,1); put(f->memory,0x8006ce74,1); put(f->memory,0x8006ce7c,1);
    put(f->memory,0x8006cd74,1);
    /* The source IRQ JR guard admits GetTN/GetTD only when their live
     * command-policy cells match the retail table.  These are source data,
     * not a synthetic completion status. */
    put(f->memory,0x8006ce98,1); put(f->memory,0x8006ce9c,1);
    put(f->memory,0x8006cd98,0); put(f->memory,0x8006cd9c,0);
    for (unsigned i=0;i<4;++i) {
        const unsigned commands[]={2,6,9,14};
        put(f->memory,0x8006ce4c+4*commands[i],1);
        put(f->memory,0x8006cd4c+4*commands[i],commands[i]==9);
    }
    assert(musashi_psycross_irq_runtime_bind_cd(&f->runtime,&f->registers,&f->clock));
    assert(!musashi_psycross_irq_runtime_bind_cd(&f->runtime,&f->registers,&f->clock));
}
static void close_fixture(Fixture *f) {
    if (f->runtime.scheduler.installed) assert(musashi_psycross_irq_scheduler_uninstall(&f->runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&f->runtime));
    assert(musashi_cd_owned_close(f->cd)); free(f->memory);
}
static void write_cd(Fixture *f,unsigned reg,unsigned value) {
    assert(musashi_cd_owned_write8(f->cd,0x1f801800+reg,(uint8_t)value));
}
static void command_with_parameter(Fixture *f,uint8_t cmd,int parameter) {
    *musashi_boot_ram_span(f->memory,0x8006cca5,1)=cmd;
    write_cd(f,0,0);
    if (parameter >= 0) write_cd(f,2,(uint8_t)parameter);
    write_cd(f,0,0); write_cd(f,1,cmd); f->cycle+=0x800;
    assert(musashi_cd_owned_advance(f->cd,f->cycle));
}
static void command(Fixture *f,uint8_t cmd) { command_with_parameter(f,cmd,-1); }
static uint8_t ram_byte(Fixture *f,uint32_t address) {
    uint8_t *p=musashi_boot_ram_span(f->memory,address,1); assert(p); return *p;
}
static void assert_response(Fixture *f,uint8_t a,uint8_t b,uint8_t c) {
    assert(ram_byte(f,0x800763d8)==a && ram_byte(f,0x800763d9)==b &&
           ram_byte(f,0x800763da)==c);
    for (unsigned i=3;i<8;++i) assert(ram_byte(f,0x800763d8+i)==0);
}
static int execute(Fixture *f) {
    return musashi_boot_execute_cd_irq_with_services(f->memory,&f->registers,
        &f->clock,&f->services,
        &f->runtime.cd_frame,0x8006bb34,0x80045290,&f->runtime.cd_stop);
}

static void install_card_events(Fixture *f) {
    static const uint32_t classes[] = {0xf4000001u, 0xf0000011u};
    static const uint32_t specs[] = {4u, 0x8000u, 0x100u, 0x2000u};
    static const uint32_t callbacks[] = {
        0x800617ccu, 0x800617e0u, 0x800617f4u, 0x80061808u,
        0x8006181cu, 0x80061830u, 0x80061844u, 0x80061858u
    };
    unsigned i;
    for (i = 0; i < 8; ++i) {
        f->events.records[i].class_word = classes[i / 4];
        f->events.records[i].status = 0x2000u;
        f->events.records[i].spec = specs[i % 4];
        f->events.records[i].mode = 0x1000u;
        f->events.records[i].callback = callbacks[i];
    }
    f->events.records[8].class_word = 0xf0000009u;
    f->events.records[8].status = 0x2000u;
    f->events.records[8].spec = 20u;
    f->events.records[8].mode = 0x2000u;
}

static void source_callback(const MusashiDiscMedia *media) {
    Fixture f;
    MusashiBiosEventRecord before[9];

    init(&f, media);
    install_card_events(&f);
    memcpy(before, f.events.records, sizeof(before));
    command(&f, 1);
    write_cd(&f, 0, 3);
    put(f.memory, 0x8006cc84u, 0x80043398u);
    put(f.memory, 0x8006cf64u, 2u);
    put(f.memory, 0x8006ca90u, 0xa5a5a5a5u);
    put(f.memory, 0x8006caecu, 0x5a5a5a5au);
    assert(execute(&f));
    assert(f.event_deliveries == 1);
    assert(f.service_accepts == 1 && f.event_cycle > 0 && f.costs > 0);
    assert(f.event_class == 0xf0000003u && f.event_spec == 0x20u);
    assert(get(f.memory, 0x8006cc84u) == 0x80043398u &&
           ram_byte(&f, 0x8006cf64u) == 2u);
    assert(f.card_callbacks == 0);
    assert(memcmp(before, f.events.records, sizeof(before)) == 0);
    assert(get(f.memory, 0x8006ca90u) == 0xa5a5a5a5u);
    assert(get(f.memory, 0x8006caecu) == 0x5a5a5a5au);
    close_fixture(&f);

    init(&f, media);
    f.events.records[0].class_word = 0xf0000003u;
    f.events.records[0].status = 0x2000u;
    f.events.records[0].spec = 0x20u;
    f.events.records[0].mode = 0x2000u;
    command(&f, 1);
    write_cd(&f, 0, 3);
    put(f.memory, 0x8006cc84u, 0x80043398u);
    put(f.memory, 0x8006cf64u, 2u);
    assert(execute(&f));
    assert(f.event_deliveries == 1 && f.service_accepts == 1 && f.card_callbacks == 0);
    assert(f.events.records[0].status == 0x4000u);
    {
        int32_t handle = (int32_t)0xf1000000u;
        int32_t result = -1;
        assert(musashi_bios_events_test(&f.events, handle, &result));
        assert(result == 1 && f.events.records[0].status == 0x2000u);
    }
    close_fixture(&f);

    init(&f, media);
    command(&f, 1);
    write_cd(&f, 0, 3);
    put(f.memory, 0x8006cc84u, 0x80043398u);
    put(f.memory, 0x8006cf64u, 2u);
    put(f.memory, 0x8006ca90u, 0xa5a5a5a5u);
    put(f.memory, 0x8006caecu, 0x5a5a5a5au);
    f.refuse_event = 1;
    assert(!execute(&f));
    assert(f.runtime.cd_frame.faulted && !f.runtime.cd_frame.busy);
    assert(!execute(&f));
    assert(get(f.memory, 0x8006ca90u) == 0xa5a5a5a5u &&
           get(f.memory, 0x8006caecu) == 0x5a5a5a5au);
    close_fixture(&f);

    init(&f, media);
    command(&f, 1);
    write_cd(&f, 0, 3);
    /* Compiled mutation: disable the only admitted 43398 callback target. */
    put(f.memory, 0x8006cc84u, 0x800433c0u);
    put(f.memory, 0x8006cf64u, 2u);
    assert(!execute(&f));
    assert(f.runtime.cd_frame.faulted && f.runtime.cd_stop.call_address == 0x80045334u);
    assert(f.event_deliveries == 0 && f.runtime.cd_frame.instructions > 0);
    close_fixture(&f);

    init(&f, media);
    command(&f, 1);
    write_cd(&f, 0, 3);
    put(f.memory, 0x8006cc84u, 0x80043398u);
    put(f.memory, 0x8006cf64u, 2u);
    /* Compiled mutation: bypass the BIOS event owner instead of scanning it. */
    f.services.deliver_event = bypass_deliver_event;
    assert(!execute(&f));
    assert(f.runtime.cd_frame.faulted);
    assert(f.runtime.cd_stop.call_address == 0x8005ce6cu &&
           f.runtime.cd_frame.instructions > 0 && f.event_deliveries == 1 &&
           f.service_accepts == 0);
    close_fixture(&f);

    init(&f, media);
    command(&f, 1);
    write_cd(&f, 0, 3);
    put(f.memory, 0x8006cc84u, 0x80043398u);
    put(f.memory, 0x8006cf64u, 2u);
    f.recurse_event = 1;
    assert(!execute(&f));
    assert(f.nested_event_attempted && !f.nested_event_result);
    assert(f.runtime.cd_frame.faulted);
    close_fixture(&f);
}
static void direct(const MusashiDiscMedia *media) {
    Fixture f; MusashiCdOwnedState s;
    init(&f,media);
    for(unsigned cmd_index=0;cmd_index<3;++cmd_index) {
        uint8_t cmd=cmd_index==0?1:cmd_index==1?0x0a:0x0c;
        command(&f,cmd); write_cd(&f,0,3);
        put(f.memory,0x8006ca90,0xa5a5a5a5); put(f.memory,0x8006caec,0x5a5a5a5a);
        assert(execute(&f)); assert(!f.runtime.cd_frame.busy && !f.runtime.cd_frame.faulted);
        assert(*musashi_boot_ram_span(f.memory,0x8006cf64,1)==(cmd==0x0a?3:2));
        assert(get(f.memory,0x800763d8)==2 && get(f.memory,0x800763dc)==0);
        assert(get(f.memory,0x8006ca90)==0xa5a5a5a5 && get(f.memory,0x8006caec)==0x5a5a5a5a);
        assert(musashi_cd_owned_get_state(f.cd,&s) && s.index==3 && !s.interrupt && !s.response_count);
        if(cmd==0x0a) {
            f.cycle=s.due; assert(musashi_cd_owned_advance(f.cd,f.cycle));
            assert(execute(&f)); assert(*musashi_boot_ram_span(f.memory,0x8006cf64,1)==2);
        }
    }
    /* These commands consume the actual mounted-owner parameter/response
     * FIFO.  The worker copies the real INT3 packet and pads it to eight
     * bytes before the source return. */
    close_fixture(&f);
    init(&f,media); command(&f,0x13); write_cd(&f,0,3);
    put(f.memory,0x8006ca90,0xa5a5a5a5); put(f.memory,0x8006caec,0x5a5a5a5a);
    assert(execute(&f)); assert_response(&f,2,1,4);
    assert(get(f.memory,0x8006ca90)==0xa5a5a5a5 && get(f.memory,0x8006caec)==0x5a5a5a5a);
    assert(f.irq_raises==1 && f.costs>100);
    close_fixture(&f);
    init(&f,media); command_with_parameter(&f,0x14,0x04); write_cd(&f,0,3);
    put(f.memory,0x8006ca90,0xa5a5a5a5); put(f.memory,0x8006caec,0x5a5a5a5a);
    assert(execute(&f)); assert_response(&f,2,0x36,0x14);
    assert(get(f.memory,0x8006ca90)==0xa5a5a5a5 && get(f.memory,0x8006caec)==0x5a5a5a5a);
    assert(f.irq_raises==1 && f.costs>100);
    close_fixture(&f);
}

static void toc_refusal(const MusashiDiscMedia *media) {
    for (unsigned test=0;test<4;++test) {
        Fixture f; init(&f,media);
        if (test < 2) command(&f,0x13);
        else command_with_parameter(&f,0x14,0x04);
        write_cd(&f,0,3);
        if (test==0) put(f.memory,0x800737d4,0x80043f08); /* mapped, wrong JR */
        if (test==1) put(f.memory,0x8006ce98,0);             /* wrong CE4C policy */
        if (test==2) put(f.memory,0x8006cd9c,1);             /* wrong CD4C policy */
        if (test==3) put(f.memory,0x8006cc84,0x80045290);   /* callback refusal */
        put(f.memory,0x8006ca90,0xa5a5a5a5); put(f.memory,0x8006caec,0x5a5a5a5a);
        assert(!execute(&f));
        assert(f.runtime.cd_frame.faulted && !f.runtime.cd_frame.busy);
        assert(!execute(&f));
        {
            MusashiCdOwnedState state;
            assert(musashi_cd_owned_get_state(f.cd,&state));
            assert(!state.interrupt && !state.response_count);
        }
        assert(get(f.memory,0x8006ca90)==0xa5a5a5a5 && get(f.memory,0x8006caec)==0x5a5a5a5a);
        if (test<3) assert(f.runtime.cd_stop.call_address==0x80043e00);
        else assert(f.runtime.cd_stop.call_address==0x80045334);
        close_fixture(&f);
    }
}
static void sector_command_acks(const MusashiDiscMedia *media) {
    Fixture f; MusashiCdOwnedState s;
    init(&f,media);
    write_cd(&f,0,0); write_cd(&f,2,0); write_cd(&f,2,2); write_cd(&f,2,0x16);
    command(&f,2); assert(execute(&f)); assert_response(&f,2,0,0);
    assert(musashi_cd_owned_get_state(f.cd,&s) && s.pending_lba==16 && s.location_valid);
    command_with_parameter(&f,0x0e,0xa0); assert(execute(&f)); assert_response(&f,2,0,0);
    command(&f,6); assert(execute(&f)); assert_response(&f,0x22,0,0);
    assert(musashi_cd_owned_get_state(f.cd,&s) && s.reading && !s.sectors_fetched);
    assert(!s.interrupt && !s.response_count && f.irq_raises==3);
    close_fixture(&f);
    init(&f,media); command(&f,9); assert(execute(&f));
    assert(ram_byte(&f,0x8006cf64)==3);
    assert(musashi_cd_owned_get_state(f.cd,&s) && s.phase==2);
    f.cycle=s.due; assert(musashi_cd_owned_advance(f.cd,f.cycle));
    assert(execute(&f)); assert_response(&f,2,0,0);
    assert(ram_byte(&f,0x8006cf64)==2 && f.irq_raises==2);
    close_fixture(&f);
    init(&f,media); command_with_parameter(&f,0x0e,0xa0);
    put(f.memory,0x8006cd84,1); /* Mapped command, wrong completion policy. */
    assert(!execute(&f) && f.runtime.cd_frame.faulted);
    assert(f.runtime.cd_stop.call_address==0x80043e00);
    close_fixture(&f);
}

/* Live boot STOP: ReadN still producing INT1 after Setmode, so 8006cca5 is
 * 0x0e while jtbl_800737CC[0] is 80043F54. The jr guard must admit that
 * retail INT1, then the ready callback 800433C0 delivers spec 0x40. */
static void int1_setmode_while_reading(const MusashiDiscMedia *media) {
    Fixture f; MusashiCdOwnedState s;
    MusashiCallbackDevice callback = {0};
    init(&f,media);
    f.services.callback_device = &callback;
    put(f.memory,0x800737cc,0x80043f54);
    put(f.memory,0x8006cc88,0x800433c0);
    write_cd(&f,0,0); write_cd(&f,2,0); write_cd(&f,2,2); write_cd(&f,2,0x16);
    command(&f,2); assert(execute(&f));
    command_with_parameter(&f,0x0e,0xa0); assert(execute(&f));
    command(&f,6); assert(execute(&f));
    assert(musashi_cd_owned_get_state(f.cd,&s) && s.reading && !s.interrupt);
    command_with_parameter(&f,0x0e,0xa0); assert(execute(&f));
    assert(musashi_cd_owned_get_state(f.cd,&s) && s.reading && !s.interrupt);
    assert(s.command==0x0e && ram_byte(&f,0x8006cca5)==0x0e && s.sector_due);
    f.cycle=s.sector_due; assert(musashi_cd_owned_advance(f.cd,f.cycle));
    assert(musashi_cd_owned_get_state(f.cd,&s) && s.interrupt==1 && s.reading);
    assert(execute(&f));
    assert(!f.runtime.cd_frame.busy && !f.runtime.cd_frame.faulted);
    assert(f.event_deliveries==1 && f.service_accepts==1);
    assert(f.event_class==0xf0000003u && f.event_spec==0x40u);
    close_fixture(&f);
}
static void refusal(const MusashiDiscMedia *media) {
    for(unsigned test=0;test<11;++test) {
        Fixture f; init(&f,media); command(&f,1);
        switch(test) {
        case 0: put(f.memory,0x800737d4,0x80043f08); break;
        case 1: put(f.memory,0x8006cc84,0x80045290); break;
        case 2: put(f.memory,0x8006bb38,0x801fff80); break;
        case 3: put(f.memory,0x8006bb34,0x80042788); break;
        case 4: f.refuse_clock=1; break;
        case 5: f.recurse=1; break;
        case 6: put(f.memory,0x8006cf4c,0x800763d8); break;
        case 7: f.corrupt_return=0x80045290; break;
        case 8: unstable_sample=0; f.registers.read8=unstable_read; break;
        case 9: f.registers.read8=refuse_read; break;
        case 10: {
            MusashiCdOwnedState state;
            assert(execute(&f)); /* Finish cmd01 before issuing real Reset. */
            command(&f,0x0a); assert(execute(&f));
            assert(musashi_cd_owned_get_state(f.cd,&state));
            f.cycle=state.due; assert(musashi_cd_owned_advance(f.cd,f.cycle));
            put(f.memory,0x800737d0,0x80043e08); break;
        }
        }
        assert(!execute(&f)); assert(f.runtime.cd_frame.faulted && !f.runtime.cd_frame.busy);
        assert(!execute(&f));
        if(test==0 || test==10) {
            MusashiCdOwnedState state;
            assert(f.runtime.cd_stop.call_address==0x80043e00);
            assert(musashi_cd_owned_get_state(f.cd,&state));
            assert(!state.interrupt && !state.response_count); /* Retain prior source ACK/drain. */
        }
        if(test==1) assert(f.runtime.cd_stop.call_address==0x80045334);
        if(test==8) assert(f.runtime.cd_frame.instructions==16384);
        close_fixture(&f);
    }
}
static void scheduler(const MusashiDiscMedia *media) {
    Fixture f; int token;
    init(&f,media);
    assert(!f.runtime.device.execute(f.runtime.device.userdata,f.memory,0x80045290));
    put(f.memory,0x8006cb88,0x1f801070); put(f.memory,0x8006cb8c,0x1f801074);
    assert(musashi_boot_write16(f.memory,0x8006bafc,1));
    assert(musashi_boot_write16(f.memory,0x8006bb2c,4)); put(f.memory,0x8006bb08,0x80045290);
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&f.runtime.scheduler,0x8006bb34));
    assert(musashi_psycross_irq_scheduler_set_enabled(&f.runtime.scheduler,1));
    f.runtime.controller.mask=4;
    command(&f,1);
    f.expected_continuation=&token;
    assert(musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&token));
    assert(f.runtime.scheduler.deliveries==1 && !f.runtime.scheduler.active_continuation);
    assert(f.runtime.scheduler.last_result==MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    assert(!f.runtime.controller.status && !f.runtime.cd_frame.faulted);
    f.expected_continuation=NULL;
    put(f.memory,0x8006bb08,0x80045294); command(&f,0x0c);
    assert(!musashi_psycross_irq_scheduler_dispatch_pending(&f.runtime.scheduler,&token));
    assert(f.runtime.scheduler.faulted && f.runtime.failed_target==0x80045294);
    close_fixture(&f);
}
int main(int argc,char **argv) {
    MusashiDiscMedia *media; assert(argc==3);
    assert(SDL_Init(0)==0); g_intrMutex=SDL_CreateMutex(); assert(g_intrMutex);
    media=musashi_disc_media_open_pinned(argv[1],argv[2]); assert(media);
    direct(media); refusal(media); scheduler(media);
    toc_refusal(media);
    sector_command_acks(media);
    int1_setmode_while_reading(media);
    source_callback(media);
    musashi_disc_media_close(media); SDL_DestroyMutex(g_intrMutex); SDL_Quit();
    puts("CD IRQ source/scheduler fixture: PASS"); return 0;
}
