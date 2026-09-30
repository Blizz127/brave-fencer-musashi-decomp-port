#ifdef MUSASHI_WITH_PSYCROSS
#include "irq_cpu_fixture.h"
#endif
/* Run the initializer with real decomp-owned registration and a synthetic
 * device/BIOS backend. The pinned EXE supplies initial guest pointer cells.
 * This is a startup integration diagnostic, not interrupt delivery/menu proof. */
#include "musashi_callback_registration.h"
#include "musashi_irq_controller.h"
#ifdef MUSASHI_WITH_PSYCROSS
#include "musashi_irq_scheduler.h"
#include <SDL.h>
#include "PsyX/PsyX_public.h"
extern SDL_mutex *g_intrMutex;
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void musashi_psyx_shutdown(void); /* pc_port/psyx_shutdown.cpp: GL teardown before window */

typedef struct Fixture {
    MusashiBootMemory *memory;
    MusashiCallbackRegistrationDevice registration;
    MusashiIrqController *irq;
    uint32_t dma_control;
    unsigned hooks, b0_calls, c0_calls, exits, removes;
} Fixture;

static uint32_t word(MusashiBootMemory *memory, uint32_t address) {
    uint32_t value;
    assert(musashi_boot_read32(memory, address, &value));
    return value;
}

static uint16_t read16(void *userdata, uint32_t address) {
    Fixture *f = userdata;
    uint16_t value;
    assert(address == 0x1f801074u);
    assert(musashi_irq_controller_read16(f->irq, address, &value));
    return value;
}

static void write16(void *userdata, uint32_t address, uint16_t value) {
    Fixture *f = userdata;
    assert(address == 0x1f801070u || address == 0x1f801074u);
    assert(musashi_irq_controller_write16(f->irq, address, value));
}

static int checked_read16(void *userdata, uint32_t address, uint16_t *out) {
    if (address != 0x1f801074u) return 0;
    *out = read16(userdata, address);
    return 1;
}

static int checked_write16(void *userdata, uint32_t address, uint16_t value) {
    if (address != 0x1f801074u) return 0;
    write16(userdata, address, value);
    return 1;
}

static void write32(void *userdata, uint32_t address, uint32_t value) {
    Fixture *f = userdata;
    assert((address == 0x1f8010f0u && value == 0x33333333u) ||
           (address == 0x1f801114u && value == 0x107u) ||
           (address == 0x1f8010f4u && value == 0));
    if (address == 0x1f8010f4u) f->dma_control = value;
}

static int dma_read32(void *userdata, uint32_t address, uint32_t *value) {
    Fixture *f = userdata;
    if (address != 0x1f8010f4u) return 0;
    *value = f->dma_control;
    return 1;
}

static int dma_write32(void *userdata, uint32_t address, uint32_t value) {
    Fixture *f = userdata;
    if (address != 0x1f8010f4u) return 0;
    f->dma_control = value;
    return 1;
}

static int hook(void *userdata, uint32_t address) {
    Fixture *f = userdata;
    assert(address == 0x8006bb34u);
    f->hooks++;
    return 1;
}

static int b0_5b(void *userdata, int32_t disabled) {
    Fixture *f = userdata;
    assert(disabled == 0 && f->irq->mask == 0);
    /* Slot publication and the shared enabled bit precede BIOS service. */
    assert(word(f->memory, 0x8006bb00u) == 0x80042ce8u);
    assert((word(f->memory, 0x8006bb2cu) & 0xffffu) == 1);
    f->b0_calls++;
    return 1;
}

static int c0_0a(void *userdata, int32_t channel, int32_t disabled) {
    Fixture *f = userdata;
    assert(channel == 3 && disabled == 0 && f->b0_calls == 1);
    f->c0_calls++;
    return 1;
}

static int remove_handler(void *userdata, uint32_t table) {
    Fixture *f = userdata;
    assert(table == word(f->memory, 0x8006cb84u));
    f->removes++;
    return 1;
}

static int exit_critical(void *userdata) {
    Fixture *f = userdata;
    f->exits++;
    return 1;
}

int main(int argc, char **argv) {
    static MusashiBootMemory memory, loaded;
    MusashiEntryState entry;
    MusashiResetGraphPrefixStop stop;
    Fixture f = {0};
    MusashiIrqController standalone_irq = {0};
    MusashiCallbackDevice device = {0};
    FILE *file;
    unsigned char *exe;
    long length;
    int live = argc == 3 && strcmp(argv[2], "--live") == 0;
#ifdef MUSASHI_WITH_PSYCROSS
    MusashiPsyCrossIrqRuntime runtime = {0};
    int observed = 0;
    int attempt;
#endif
    if ((argc != 2 && !live) || !(file = fopen(argv[1], "rb"))) {
        fputs("callback registration: pinned retail EXE required\n", stderr);
        return 1;
    }
    assert(!fseek(file, 0, SEEK_END));
    length = ftell(file);
    assert(length > 0 && !fseek(file, 0, SEEK_SET));
    exe = malloc((size_t)length);
    assert(exe && fread(exe, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    /* Explicitly synthetic incoming RA for the entry-data loader. */
    assert(musashi_boot_load_entry(&loaded, exe, (size_t)length, 0, &entry));
    free(exe);
    memory = loaded;
    f.memory = &memory;
    f.irq = &standalone_irq;
#ifdef MUSASHI_WITH_PSYCROSS
    if (live) {
        PsyX_Initialise("musashi_callback_registration_live", 320, 240, 0);
        if (SDL_GL_GetCurrentWindow() == NULL || SDL_GL_GetCurrentContext() == NULL) {
            fputs("callback registration: PsyCross initialisation failed, skipping\n", stderr);
            return 77;
        }
        assert(musashi_psycross_irq_runtime_init(&runtime, &memory, NULL));
    fixture_irq_cpu_attach(&runtime.scheduler);
        f.irq = &runtime.controller;
        /* This thread owns guest initialization. The source only queues. */
    }
#endif
    f.registration = (MusashiCallbackRegistrationDevice){
        &f, checked_read16, checked_write16, b0_5b, c0_0a
    };
    device.read16 = read16;
    device.write16 = write16;
    device.write32 = write32;
#ifdef MUSASHI_WITH_PSYCROSS
    if (live) {
        device.hook_entry_int = musashi_psycross_irq_scheduler_hook_entry_int;
        device.hook_entry_int_userdata = &runtime.scheduler;
    } else
#endif
    device.hook_entry_int = hook;
    device.registration_device = &f.registration;
    device.a0_72 = remove_handler;
    device.exit_critical_section = exit_critical;
    device.userdata = &f;
    assert(musashi_boot_run_callback_init_prefix(&memory, &device, &stop) ==
           MUSASHI_CALLBACK_INIT_PREFIX_RETURN_BOUNDARY);
#ifdef MUSASHI_WITH_PSYCROSS
    if (live)
        assert(runtime.scheduler.installed && f.hooks == 0);
    else
#endif
        assert(f.hooks == 1);
    assert(f.b0_calls == 1 && f.c0_calls == 1);
    assert(f.removes == 1 && f.exits == 1 && f.irq->mask == 9);
    assert(word(&memory, 0x8006bb00u) == 0x80042ce8u);
    assert(word(&memory, 0x8006bb0cu) == 0x80042e08u);
    assert((word(&memory, 0x8006bb2cu) & 0xffffu) == 9);
    assert(word(&memory, 0x8006cb78u) == 0x80042d60u);
    assert(word(&memory, 0x8006cb68u) == 0x80042f8cu);
    assert(stop.call_address == 0x800427f0u);

#ifdef MUSASHI_WITH_PSYCROSS
    if (live) {
        assert(runtime.scheduler.installed && runtime.scheduler.context_address == 0x8006bb34u);
        assert((word(&memory, 0x8006bafcu) & 0xffffu) == 1u);
        assert(runtime.scheduler.deliveries == 0);
        assert(musashi_psycross_irq_scheduler_set_enabled(&runtime.scheduler, 1));
        for (attempt = 0; attempt < 100; attempt++) {
            SDL_Delay(10);
            /* This probe's suspended owner frame is synthetic; the production
             * boot runner supplies its actual live FormatterCpu instead. */
            assert(musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &stop));
            observed = runtime.scheduler.deliveries > 0 &&
                       f.irq->status == 0 && f.irq->mask == 9 &&
                       runtime.scheduler.last_result == MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED &&
                       word(&memory, 0x8006cbb8u) > 0;
            if (observed)
                break;
        }
        assert(musashi_psycross_irq_scheduler_uninstall(&runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&runtime));
        musashi_psyx_shutdown();
        if (!observed) {
            fputs("callback registration: no live VBlank delivery observed\n", stderr);
            return 1;
        }
        puts("CALLBACK STARTUP LIVE IRQ: initializer guard and I_MASK=0009 -> PsyCross VBlank -> shared IRQ");
        return 0;
    }
#endif

    /* Direct handler diagnostic after genuine table initialization. This is
     * not a delivered VBlank: the newly cleared slots require no executor. */
    assert(word(&memory, 0x8006cbb8u) == 0);
    assert(musashi_boot_call_80042ce8(&memory, NULL, NULL));
    assert(word(&memory, 0x8006cbb8u) == 1);

    /* Exercise the recovered target installed in table +04. The channel-two
     * call is a separate synthetic diagnostic, not continued game startup. */
    {
        MusashiDmaCallbackRegistrationDevice dma = {&f, dma_read32, dma_write32};
        uint32_t previous = 0xdeadbeefu;
        assert(musashi_boot_call_800425e0(&memory, &dma, 2, 0x8005ba90u, &previous));
        assert(previous == 0 && word(&memory, 0x8006cbccu) == 0x8005ba90u);
        assert(f.dma_control == 0x00840000u);
        assert(musashi_boot_call_800425e0(&memory, &dma, 2, 0, &previous));
        assert(previous == 0x8005ba90u && word(&memory, 0x8006cbccu) == 0);
        assert(f.dma_control == 0x00800000u);
        assert(f.irq->mask == 9 && (word(&memory, 0x8006bb2cu) & 0xffffu) == 9);
    }

    /* Removing the first required BIOS service cannot fabricate completion. */
    memory = loaded;
    f.registration.b0_5b = NULL;
    assert(musashi_boot_run_callback_init_prefix(&memory, &device, &stop) ==
           MUSASHI_CALLBACK_INIT_PREFIX_INVALID_INPUT);
    assert(f.irq->mask == 0 && f.removes == 1 && f.exits == 1);
    assert(word(&memory, 0x8006bb00u) == 0x80042ce8u);
    assert(word(&memory, 0x8006bb0cu) == 0);
    puts("CALLBACK REGISTRATION STARTUP: guest slots=0,3 enabled=0009; missing BIOS refused");
    puts("DMA REGISTRATION: channel=2 enabled, then cleared through shared C; synthetic device");
    puts("VBLANK HANDLER: initialized empty slots, guest tick=1; direct diagnostic, no IRQ");
    return 0;
}
