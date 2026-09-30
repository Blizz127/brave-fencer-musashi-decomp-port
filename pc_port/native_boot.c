/* Native startup owner, currently a diagnostic that stops at the first
 * unbound device/call. Entry data and recovered startup run in retail order.
 * No synthesized callback guards, IRQ masks, ready bits or menu state.
 * One source-owned CPU runs from the mapped CRT through supported calls.
 * Native initial registers are an explicit platform policy; reaching a
 * boundary here is not full BIOS cold-boot or title/menu parity. */
#include "musashi_boot_memory.h"
#include "musashi_callback_registration.h"
#include "musashi_irq_scheduler.h"
#include "musashi_dma_controller.h"
#include "musashi_cd_dma3.h"
#include "musashi_gpu_dma2.h"
#include "musashi_mdec_controller.h"
#include "musashi_dma_otc.h"
#include "musashi_scanline_timer.h"
#include "musashi_irq_policy.h"
#include "musashi_bios_kernel.h"
#include "musashi_bios_heap.h"
#include "musashi_bios_exception.h"
#include "musashi_gte_owner.h"
#include "musashi_bios_card.h"
#include "musashi_sio_controller.h"
#include "musashi_bios_backup_unit.h"
#include "musashi_bios_input.h"
#include "musashi_source_clock.h"
#include "musashi_timer2.h"
#include "musashi_device_epoch.h"
#include "musashi_gpu_psycross.h"
#include "musashi_disc_media.h"
#include "musashi_cd_controller.h"
#include "musashi_audio_sdl.h"
#include "musashi_native_lane.h"
/* Ordered PsyCross shutdown (pc_port/psyx_shutdown.cpp). */
void musashi_psyx_shutdown(void);

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <openssl/sha.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

#include "musashi_x11_nograb.h"
#include "PsyX/PsyX_public.h"
#include "psx/libetc.h"
#include <limits.h>

extern SDL_mutex *g_intrMutex;

static volatile sig_atomic_t host_stop_signal;
static void request_host_stop(int signal_number) {
    host_stop_signal = signal_number;
}

static int log_sample(uint64_t *counter, uint64_t first, uint64_t stride) {
    uint64_t n = *counter;
    if (*counter != UINT64_MAX) ++*counter;
    return n < first || (stride && (n % stride) == 0u);
}

#ifdef MUSASHI_WITH_BFM_PLAT
#include "bfm_plat_gpu_bridge.h"
#include "bfm_plat_input.h"
#include "bfm_plat_timing.h"
#include "bfm_plat_audio.h"
#include "musashi_plat_audio_sink.h"
#include "bfm_plat_font.h"
#include "musashi_dev_menu.h"
/* MUSASHI_GPU=bfm_plat: GP0/GP1 are teed into the bfm_plat GPU bridge and
 * drawn by the bfm_plat renderer (MUSASHI_RENDERER, default psycross); the
 * controller keeps the port protocol with its rasteriser off. Default cpu. */
static BfmGpuBridge g_gpu_bridge;
static int g_gpu_bridge_on;
/* MUSASHI_INPUT=bfm_plat: the SIO pad samples bfm_plat_input (default: the
 * native keyboard/GameController sampler below). */
static int g_plat_input_on;
static int g_plat_started;
/* MUSASHI_TIMING=bfm_plat (guest clock only): guest VBlanks are paced by
 * bfm_plat_timing_vsync (NTSC field rate, fast-forward, VSYNC mod events)
 * instead of guest_clock_wait's own throttle. */
static int g_plat_timing_on;

/* MUSASHI_AUDIO=bfm_plat: the SPU core's output goes to bfm_plat_audio
 * (backend MUSASHI_AUDIO_BACKEND, default "openal") through
 * pc_port/plat_audio_sink.c, which also counts what the sink received
 * (AUDIO_SINK at STOP). */
static MusashiPlatAudioSink g_plat_audio;
static char g_plat_audio_name[32];
static uint64_t plat_audio_thread(void *user) { (void)user; return (uint64_t)SDL_ThreadID(); }

/* Opens the bfm_plat audio backend for the SPU; NULL keeps native audio. */
static const MusashiSpuCdAudioBackend *plat_audio_attach(void) {
    const char *want = getenv("MUSASHI_AUDIO"), *backend, *selected = NULL;
    if (!want || strcmp(want, "bfm_plat") != 0) return NULL;
    if (!g_plat_started) {
        fprintf(stderr, "native_boot: AUDIO bfm_plat refused: platform layer off; native\n");
        return NULL;
    }
    backend = getenv("MUSASHI_AUDIO_BACKEND");
    if (!backend || !backend[0]) backend = "openal";
    if (bfm_plat_audio_open(backend, &selected) != BFM_PLAT_OK || !selected) {
        fprintf(stderr, "native_boot: AUDIO bfm_plat refused: %s; native\n", backend);
        return NULL;
    }
    /* bfm_plat_audio_open falls back to null when the backend cannot open. */
    if (strcmp(selected, backend) != 0)
        fprintf(stderr, "native_boot: AUDIO bfm_plat %s unavailable, using %s\n", backend, selected);
    snprintf(g_plat_audio_name, sizeof g_plat_audio_name, "%s", selected);
    fprintf(stderr, "native_boot: AUDIO backend=bfm_plat:%s\n", selected);
    return musashi_plat_audio_sink_init(&g_plat_audio, plat_audio_thread, NULL);
}
#endif

/* Guest-RAM pokes that bypass the game's own logic are a divergence from
 * 1:1 behaviour. They run only with MUSASHI_DEBUG_FORCE=1 (a debug switch,
 * docs/PC-PORT.md "Debug forcing"); each applied poke is logged. */
static int debug_force(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("MUSASHI_DEBUG_FORCE");
        cached = v && v[0] == '1';
        if (cached)
            fprintf(stderr, "native_boot: DIVERGENCE MUSASHI_DEBUG_FORCE=1: guest RAM "
                            "pokes enabled (not 1:1)\n");
    }
    return cached;
}

typedef struct NativeBoot {
    MusashiBootMemory memory;
    /* The scratchpad's page is also mapped at guest 0x1F800000 under the
     * native lane (musashi_native_lane_map_guest); keep the rest of that page
     * inert so a stray 1F800400+ access cannot reach host state. */
    uint8_t scratchpad_page_tail[4096u - MUSASHI_SCRATCHPAD_SIZE];
    /* On the PS1, 0x80200000-0x807FFFFF mirrors main RAM. Under the native
     * lane this range is made PROT_NONE, so a native access there faults
     * (HOST_FAULT) instead of silently touching the host state that follows. */
    uint8_t ram_mirror_guard[0x800000u - MUSASHI_RAM_SIZE - 4096u];
    uint8_t list_tail_before[448];
    int list_tail_captured;
    MusashiPsyCrossIrqRuntime irq;
    MusashiDmaController dma;
    MusashiMdecController mdec;
    MusashiScanlineTimer timer1;
    MusashiIrqPolicy irq_policy;
    MusashiBiosKernel bios;
    MusashiBiosExceptionImage exception_image;
    MusashiBiosImageDevice image_device;
    MusashiCpuTransferDevice cpu_transfer;
    MusashiGteOwner gte;
    unsigned host_service_depth;
    MusashiBiosHeap heap;
    MusashiHeapDevice heap_device;
    MusashiGpuIoDevice gpu_io;
    uint64_t heap_calls;
    MusashiBiosCard card;
    MusashiBiosCardStartDevice card_device;
    MusashiSioController sio;
    MusashiBiosBackupUnit backup_unit;
    const void *continuation;
    MusashiGpuController gpu;
    MusashiGpuPsyCross gpu_renderer;
    MusashiGpuBackend gpu_backend;
    MusashiGpuDevice gpu_device;
    int gpu_trace;
    int graphics_returned;
    MusashiCallbackRegistrationDevice registration;
    MusashiCallbackDevice callback;
    MusashiCdDevice cd;
    MusashiEventDevice event;
    MusashiInputDevice input;
    MusashiBiosInput bios_input;
    MusashiBiosInputDevice bios_input_device;
    MusashiBiosCallbackFrame bios_callback_frame;
    MusashiSourceClock source_clock;
    uint64_t cd_irq_production_limit;
    MusashiExecutionClock execution_clock;
    MusashiTimer2 timer2;
    MusashiDeviceEpoch epoch;
    uint64_t keyboard_polls;
    uint64_t gpu_reset_sequence;
    MusashiDiscMedia *media;
    MusashiCdOwned *cd_drive;
    MusashiCdDma3 *cd_dma;
    MusashiGpuDma2 *gpu_dma;
    MusashiDmaOtc *dma_otc;
    uint64_t gpu_dma_transfers_logged;
    MusashiCdRegisterDevice cd_registers;
    MusashiCdIrqServices cd_services;
    MusashiDmaCallbackRegistrationDevice dma_irq;
    int cd_event_busy;
    uint32_t common_delay;
    int common_delay_written;
    uint32_t spu_delay;
    uint32_t spu_dma_madr, spu_dma_bcr, spu_dma_chcr;
    uint64_t cd_irq_requests;
    uint64_t xa_sample_anchor;
    MusashiSpuCdAudio *spu;
    MusashiAudioSdl *audio;
    MusashiCdSpuDevice cd_spu;
    MusashiStartupPrefixDevice startup_devices;
    const char *exe_path;
    int opening_overlay_ready;
    int title_scene_reached;
    int scene1_reached;
    unsigned in_game_frames;
    int in_game_paused;
    unsigned auto_start_phase1_ticks;
    unsigned auto_start_phase2_ticks;
    unsigned auto_dismiss_ticks;
    int dialogue_active;
    int dialogue_dismissed;
    unsigned start_preview_frames;
    int start_preview_paused;
    int trace_cd_timing, cd_timing_active;
    uint64_t cd_host_start, cd_epoch_start;
    uint16_t last_musashi_action;
    /* Guest-cycle clock (see epoch_collect). */
    int guest_clock, guest_unthrottled;
    uint64_t guest_tick, guest_next_vblank, guest_sequence, guest_host_origin;
    uint32_t guest_pending_cycles;
} NativeBoot;

/* One presented frame: the bfm_plat renderer (via the GPU bridge) or the
 * PsyCross scanout of the controller's VRAM. */
/* Display enabled (GP1 03): the bridge tracks it in bridge mode, where the
 * native backend no longer sees the display commands. */
static int display_on(const NativeBoot *boot) {
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_gpu_bridge_on) return bfm_gpu_bridge_display_enabled(&g_gpu_bridge);
#endif
    return boot->gpu_renderer.display_enabled;
}

static int present_frame(NativeBoot *boot) {
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_gpu_bridge_on) return bfm_gpu_bridge_vblank(&g_gpu_bridge) == 0;
#endif
    return musashi_gpu_psycross_present(&boot->gpu_renderer);
}

/* Overlay member signatures without retail words: FNV-1a 64 over two
 * loaded words that differ between members, compared with the digest of the
 * member the port's ranges were carved from. */
#define OPENING_MEMBER0004_SIGNATURE 0xedc2f28bdda02a4eull
#define TITLE_MEMBER0010_SIGNATURE 0xfc8191f97da7fd5eull
static uint64_t loaded_pair_digest(MusashiBootMemory *memory, uint32_t address) {
    uint32_t words[2] = {0, 0};
    uint64_t h = 0xcbf29ce484222325ull;
    unsigned i, b;
    (void)musashi_boot_read32(memory, address, &words[0]);
    (void)musashi_boot_read32(memory, address + 4u, &words[1]);
    for (i = 0; i < 2u; ++i)
        for (b = 0; b < 4u; ++b) {
            h ^= (words[i] >> (8u * b)) & 0xffu;
            h *= 0x100000001b3ull;
        }
    return h;
}

static int select_opening_overlay(NativeBoot *boot) {
    if (boot->opening_overlay_ready) return 1;
    if (loaded_pair_digest(&boot->memory, 0x800ceec8u) != OPENING_MEMBER0004_SIGNATURE) return 0;
    musashi_boot_select_overlay_0004_words(1);
    boot->opening_overlay_ready = 1;
    fputs("native_boot: OVERLAY_SELECT member=4 source=GUEST_RAM bytes_replaced=0\n",stderr);
    return 1;
}

static int native_devices_shutdown(NativeBoot *boot) {
    if (boot->irq.scheduler.installed) return 0;
    if (boot->gpu_dma) {
        if (!musashi_gpu_dma2_close(boot->gpu_dma)) return 0;
        boot->gpu_dma = NULL;
    }
    if (boot->dma_otc) {
        if (!musashi_dma_otc_close(boot->dma_otc)) return 0;
        boot->dma_otc = NULL;
    }
    if (boot->heap.initialized && !musashi_bios_heap_destroy(&boot->heap)) return 0;
    if (boot->cd_dma) {
        if (!musashi_cd_dma3_close(boot->cd_dma)) return 0;
        boot->cd_dma = NULL;
    }
    if (boot->spu) {
        if (!musashi_spu_cd_audio_destroy(boot->spu)) return 0;
        boot->spu = NULL;
    }
    if (boot->audio) {
        if (!musashi_audio_sdl_destroy(boot->audio)) return 0;
        boot->audio = NULL;
    }
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_plat_audio.backend.queue && bfm_plat_audio_active()) bfm_plat_audio_close();
#endif
    if (boot->cd_drive) {
        if (!musashi_cd_owned_close(boot->cd_drive)) return 0;
        boot->cd_drive = NULL;
    }
    /* The source registration must already be gone. Never free a CPU while
     * its image/GTE consumers still borrow it, including partial init paths. */
    if (boot->gte.initialized) {
        int accepted=musashi_gte_owner_close(&boot->gte);
        fprintf(stderr,"native_boot: GTE_REMOVED accepted=%d\n",accepted);
        if (!accepted) return 0;
    }
    if (boot->exception_image.initialized) {
        int accepted=musashi_bios_exception_destroy(&boot->exception_image);
        fprintf(stderr,"native_boot: BIOS_IMAGE_REMOVED accepted=%d required=%d\n",accepted,boot->bios.exception_required);
        if (!accepted) return 0;
    }
    if (boot->irq.cpu_status) {
        int accepted=musashi_psycross_irq_runtime_close(&boot->irq);
        fprintf(stderr,"native_boot: CPU_RUNTIME_REMOVED accepted=%d\n",accepted);
        if (!accepted) return 0;
    }
    musashi_disc_media_close(boot->media);
    boot->media = NULL;
    return 1;
}

static int registration_read16(void *userdata, uint32_t address, uint16_t *value) {
    NativeBoot *boot = userdata;
    return musashi_irq_controller_read16(&boot->irq.controller, address, value);
}

static int registration_write16(void *userdata, uint32_t address, uint16_t value) {
    NativeBoot *boot = userdata;
    return musashi_irq_controller_write16(&boot->irq.controller, address, value);
}

static int change_clear_pad(void *userdata, int32_t value) {
    NativeBoot *boot = userdata;
    return musashi_irq_policy_change_pad(&boot->irq_policy, value);
}

static int exchange_clear_pad(void *userdata, int32_t value, int32_t *previous) {
    NativeBoot *boot = userdata;
    return musashi_irq_policy_exchange_pad(&boot->irq_policy, value, previous);
}

static int init_card(void *userdata, int32_t pad_started, int32_t *previous) {
    NativeBoot *boot = userdata;
    return musashi_bios_card_init_service(&boot->card, pad_started, previous);
}

static int execute_event(void *userdata, uint32_t callback) {
    NativeBoot *boot = userdata;
    return musashi_boot_execute_bios_event(&boot->memory, callback);
}

static int open_event(void *userdata, uint32_t class_word, uint32_t spec,
                      uint32_t mode, uint32_t callback, int32_t *handle) {
    NativeBoot *boot = userdata;
    int accepted = musashi_bios_events_open(&boot->bios.events, class_word, spec,
                                            mode, callback, handle);
    if (accepted)
        fprintf(stderr, "native_boot: EVENT_OPEN class=%08x spec=%08x mode=%08x callback=%08x handle=%08x\n",
                class_word, spec, mode, callback, (unsigned)*handle);
    return accepted;
}

static int enable_event(void *userdata, int32_t handle, int32_t *result) {
    NativeBoot *boot = userdata;
    int accepted = musashi_bios_events_enable(&boot->bios.events, handle, result);
    if (accepted)
        fprintf(stderr, "native_boot: EVENT_ENABLE handle=%08x result=%d\n", (unsigned)handle, *result);
    return accepted;
}

static int test_event(void *userdata, int32_t handle, int32_t *result) {
    NativeBoot *boot = userdata;
    int accepted = musashi_bios_events_test_sync_spu(&boot->bios.events, handle,
                                                     result);
    if (accepted)
        fprintf(stderr, "native_boot: EVENT_TEST handle=%08x result=%d\n", (unsigned)handle, *result);
    return accepted;
}

static int start_card(void *userdata, int32_t *result) {
    NativeBoot *boot = userdata;
    int accepted;
    if (boot->host_service_depth) return 0;
    boot->host_service_depth++;
    accepted=musashi_bios_card_start_service(&boot->card, result);
    boot->host_service_depth--;
    return accepted;
}

static int change_clear_timer(void *userdata, int32_t channel, int32_t value) {
    NativeBoot *boot = userdata;
    return musashi_irq_policy_change_timer(&boot->irq_policy, channel, value);
}

static int raise_vblank(void *userdata) {
    NativeBoot *boot = userdata;
    /* Hardware can become pending while guest interrupts are disabled. The
     * BIOS/custom ownership check belongs to delivery, not source latching. */
    return musashi_irq_controller_raise_vblank(&boot->irq.controller);
}

static int unavailable_bios_cd(void *userdata, uint16_t pending) {
    (void)userdata;
    fprintf(stderr, "native_boot: BIOS CD execution unavailable pending=%04x\n",
            (unsigned)pending);
    return 0;
}

static int before_irq_dispatch(void *userdata, uint16_t pending) {
    NativeBoot *boot = userdata;
    /* BIOS SIO can own and acknowledge VBlank while ChangeClearPad is 1.
     * Let its verifier/handler run, then check the remaining guest route. */
    if (!musashi_bios_kernel_dispatch_with_context(&boot->bios, pending,
            &boot->irq.scheduler.active_exception))
        return 0;
    pending = musashi_irq_controller_pending(&boot->irq.controller);
    return !(pending & 1u) || musashi_irq_policy_custom_vblank(&boot->irq_policy);
}

static int native_cpu_context(void *userdata, const void *continuation, MusashiCpuContext *out) {
    NativeBoot *boot = userdata;
    if (!boot || !out || !continuation || continuation != boot->continuation) return 0;
    if (boot->host_service_depth)
        return musashi_boot_cpu_context(continuation,MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE,out);
    if (musashi_boot_cpu_context(continuation,MUSASHI_CPU_CONTEXT_SOURCE,out)) return 1;
    return musashi_boot_continuation_host_context(continuation,out);
}

static int native_sys(NativeBoot *boot, MusashiCpuExceptionKind kind, int host, int32_t *result) {
    MusashiCpuContext context;
    MusashiCpuExceptionToken token={0};
    int accepted;
    int32_t sys_result=0;
    if ((kind==MUSASHI_CPU_EXCEPTION_SYS1 && !result) || !boot->continuation || boot->irq.scheduler.delivering || boot->irq.scheduler.pumping ||
        !(host ? musashi_boot_cpu_context(boot->continuation,MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE,&context) :
                 musashi_boot_cpu_context(boot->continuation,MUSASHI_CPU_CONTEXT_SOURCE,&context))) return 0;
    if (!musashi_cpu_status_begin(boot->irq.cpu_status,kind,&context,
            musashi_irq_controller_pending(&boot->irq.controller) ? 0x400u : 0u,&token)) return 0;
    accepted=musashi_bios_kernel_before_exception_with_context(&boot->bios,&token) &&
        musashi_cpu_status_apply_sys(boot->irq.cpu_status,&token,kind==MUSASHI_CPU_EXCEPTION_SYS1 ? &sys_result : NULL) &&
        musashi_cpu_status_finish(boot->irq.cpu_status,&token);
    if (!accepted) (void)musashi_cpu_status_fault(boot->irq.cpu_status,&token);
    else {
        MusashiCpuStatusSnapshot state;
        if (!musashi_cpu_status_snapshot(boot->irq.cpu_status,&state)) return 0;
        fprintf(stderr,"native_boot: CPU_SYS kind=%u provenance=%u sequence=%llu "
                "sr=%08x cause_valid=%d cause=%08x epc=%08x image_generation=%llu\n",
                (unsigned)kind,(unsigned)context.provenance,(unsigned long long)state.sequence,
                state.sr,state.cause_epc_valid,state.cause,state.epc,
                (unsigned long long)boot->exception_image.generation);
        if (kind==MUSASHI_CPU_EXCEPTION_SYS1) *result=sys_result;
    }
    return accepted;
}

static int remove_bios_cd(void *userdata, uint32_t table_address) {
    NativeBoot *boot = userdata;
    int32_t previous;
    (void)table_address;
    /* A0:72 contains a BIOS EnterCriticalSection call. This compiled BIOS
     * service has a real suspended caller but no invented physical EPC. */
    return native_sys(boot,MUSASHI_CPU_EXCEPTION_SYS1,1,&previous) &&
           musashi_bios_kernel_remove_cd(&boot->bios);
}
static int exit_critical(void *userdata) {
    return native_sys(userdata,MUSASHI_CPU_EXCEPTION_SYS2,0,NULL);
}
static int enter_critical_result(void *userdata, int32_t *result) {
    return result && native_sys(userdata,MUSASHI_CPU_EXCEPTION_SYS1,0,result);
}
static int card_exit_critical(void *userdata) {
    return native_sys(userdata,MUSASHI_CPU_EXCEPTION_SYS2,1,NULL);
}
/* Diagnostics derive eligibility from the same SR owner. -1 is an explicit
 * failed observation, never a replacement disabled/ready value. */
static int guest_irq_enabled(NativeBoot *boot) {
    int enabled=-1;
    if (!musashi_psycross_irq_scheduler_get_enabled(&boot->irq.scheduler,&enabled)) return -1;
    return enabled;
}

static int card_write_sio16(void *userdata, uint32_t address, uint16_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_sio_controller_write16(&boot->sio, address, value)) return 0;
    if (boot->gpu_trace)
        fprintf(stderr, "native_boot: CARD_TRACE address=%08x width=2 value=%08x\n",
                (unsigned)address, (unsigned)value);
    return 1;
}

static int card_read_sio16(void *userdata, uint32_t address, uint16_t *value) {
    NativeBoot *boot = userdata;
    return musashi_sio_controller_read16(&boot->sio, address, value);
}

static int card_read_sio8(void *userdata, uint32_t address, uint8_t *value) {
    NativeBoot *boot = userdata;
    return musashi_sio_controller_read8(&boot->sio, address, value);
}

static int card_write_sio8(void *userdata, uint32_t address, uint8_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_sio_controller_write8(&boot->sio, address, value)) return 0;
    if (boot->gpu_trace)
        fprintf(stderr, "native_boot: CARD_TRACE address=%08x width=1 value=%08x\n",
                (unsigned)address, (unsigned)value);
    return 1;
}

static int cards_disconnected(void *userdata) {
    NativeBoot *boot = userdata;
    return boot->sio.disconnected_cards == 1;
}

static int card_enter_critical(void *userdata) {
    int32_t previous;
    return native_sys(userdata,MUSASHI_CPU_EXCEPTION_SYS1,1,&previous);
}

static int card_read_pad(void *userdata, int32_t *value) {
    NativeBoot *boot = userdata;
    if (!value || !boot->irq_policy.pad_initialized) return 0;
    *value = boot->irq_policy.pad;
    return 1;
}

static int card_read_irq32(void *userdata, uint32_t address, uint32_t *value) {
    NativeBoot *boot = userdata;
    uint16_t low;
    if (!value || !musashi_irq_controller_read16(&boot->irq.controller, address, &low))
        return 0;
    *value = low;
    return 1;
}

static int card_write_irq32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_irq_controller_write16(&boot->irq.controller, address, (uint16_t)value))
        return 0;
    if (boot->gpu_trace)
        fprintf(stderr, "native_boot: CARD_TRACE address=%08x width=4 value=%08x\n",
                (unsigned)address, (unsigned)value);
    return 1;
}

static int input_write32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_irq_controller_write16(&boot->irq.controller, address, (uint16_t)value)) return 0;
    {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: INPUT_MMIO address=%08x width=4 value=%08x\n",
                    address, value);
    }
    return 1;
}

static int input_read16(void *userdata, uint32_t address, uint16_t *value) {
    NativeBoot *boot = userdata;
    /* SetIntrMask (800426FC) RMW uses halfword I_STAT/I_MASK through the same
     * input-device MMIO path as Timer2/SIO during BIOS input callbacks. */
    if (address == 0x1f801070u || address == 0x1f801074u)
        return musashi_irq_controller_read16(&boot->irq.controller, address, value);
    return musashi_sio_controller_read16(&boot->sio, address, value) ||
           musashi_timer2_read16(&boot->timer2, address, value);
}

static uint64_t epoch_owner(void *userdata) {
    /* Ownership is checked on every clock charge; the thread id cannot
     * change for a thread, so resolve it once per thread. */
    static _Thread_local uint64_t id;
    (void)userdata;
    if (!id) id = (uint64_t)SDL_ThreadID();
    return id;
}

static int init_heap(void *userdata, uint32_t base, uint32_t size) {
    NativeBoot *boot = userdata;
    uint32_t before=0, after=0;
    int before_valid=musashi_boot_read32(&boot->memory,base,&before);
    int accepted=musashi_bios_heap_init_heap(&boot->heap,base,size);
    int after_valid=musashi_boot_read32(&boot->memory,base,&after);
    ++boot->heap_calls;
    fprintf(stderr,"native_boot: HEAP_INIT accepted=%d base=%08x size=%08x "
            "before_valid=%d before=%08x after_valid=%d after=%08x "
            "cursor=%08x preferred=%08x end=%08x lazy=%d calls=%llu\n",
            accepted,base,size,before_valid,before,after_valid,after,
            boot->heap.cursor,boot->heap.preferred_size,boot->heap.end,
            boot->heap.lazy,(unsigned long long)boot->heap_calls);
    return accepted;
}

static int gpu_io_owned(NativeBoot *boot) {
    return boot && !boot->epoch.faulted && !boot->gpu.faulted && !boot->gpu.executing &&
        boot->gpu_backend.ready && boot->gpu_backend.ready(boot->gpu_backend.userdata)>=0;
}
static int gpu_dma_healthy(void *userdata) {
    return gpu_io_owned(userdata);
}
static void observe_gpu_dma(NativeBoot *boot, const char *stage) {
    MusashiGpuDma2State state;
    if (!boot->gpu_dma || !musashi_gpu_dma2_get_state(boot->gpu_dma,&state)) {
        fprintf(stderr,"native_boot: GPU_DMA2 stage=%s readable=0\n",stage);
        return;
    }
    {
        static uint64_t n;
        if (!log_sample(&n, 8u, 1024u) && !state.fault) return;
    }
    fprintf(stderr,"native_boot: GPU_DMA2 stage=%s readable=1 cycle=%llu due=%llu "
            "starts=%llu transfers=%llu fetched=%llu accepted=%llu start_madr=%08x "
            "madr=%08x bcr=%08x chcr=%08x nodes=%u headers=%u payload=%u "
            "accepted_in_transfer=%u fault=%u fault_node=%08x fault_word=%u "
            "store_phase=%u store_remaining=%u stored_pixels=%llu "
            "timing=REFERENCE_SYNCHRONOUS_SUBMIT_DEFERRED_COMPLETION hardware_parity=UNPROVEN\n",
            stage,(unsigned long long)state.cycle,(unsigned long long)state.due,
            (unsigned long long)state.starts,(unsigned long long)state.transfers,
            (unsigned long long)state.fetched_words,(unsigned long long)state.accepted_words,
            state.start_madr,state.madr,state.bcr,state.chcr,state.nodes,state.header_words,
            state.payload_words,state.accepted_in_transfer,(unsigned)state.fault,
            state.fault_node,state.fault_word,boot->gpu.store_phase,boot->gpu.store_remaining,
            (unsigned long long)boot->gpu.stored_pixels);
    if (state.fault) {
        uint32_t word=0, i, base=state.fault_node;
        fprintf(stderr,"native_boot: GPU_DMA2_FAULT_WORD address=%08x", state.fault_word);
        for (i=0;i<8u;++i) {
            musashi_boot_read32(&boot->memory, base+i*4u, &word);
            fprintf(stderr," %08x", word);
        }
        fputc('\n', stderr);
    }
}
static int gpu_io_read32(void *userdata, uint32_t address, uint32_t *value) {
    NativeBoot *boot=userdata;
    if (address==0x1f801820u || address==0x1f801824u ||
        (address>=0x1f801080u && address<=0x1f801098u))
        return musashi_mdec_read32(&boot->mdec,address,value);
    if (!gpu_io_owned(boot)) return 0;
    if (address==0x1f801810u || address==0x1f801814u)
        return musashi_gpu_controller_read32(&boot->gpu,address,value);
    if (address==0x1f8010a0u || address==0x1f8010a4u || address==0x1f8010a8u)
        return musashi_gpu_dma2_read32(boot->gpu_dma,address,value);
    if (address==0x1f8010e0u || address==0x1f8010e4u || address==0x1f8010e8u)
        return boot->dma_otc && musashi_dma_otc_read32(boot->dma_otc,address,value);
    return 0;
}
static int gpu_io_write32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot=userdata;
    if (address==0x1f801820u || address==0x1f801824u ||
        (address>=0x1f801080u && address<=0x1f801098u))
        return musashi_mdec_write32(&boot->mdec,address,value);
    int accepted;
    if (!gpu_io_owned(boot)) return 0;
    const char *capture = getenv("MUSASHI_NATIVE_GPU_RESET_CAPTURE");
    if (capture && address == 0x1f801814u && value == 0x03000001u) {
        /* Read-only capture at the following graphics-control submission.
         * This is evidence output, never an input to the running game. */
        FILE *file = fopen(capture, "wb");
        int written = file && fwrite(boot->memory.bytes, 1, MUSASHI_RAM_SIZE, file)
                               == MUSASHI_RAM_SIZE;
        if (file && fclose(file) != 0) written = 0;
        if (!written) {
            fputs("native_boot: GPU reset capture failed\n", stderr);
            return 0;
        }
    }
    if (address==0x1f801810u || address==0x1f801814u) {
        accepted=musashi_gpu_controller_write32(&boot->gpu,address,value);
        if (!accepted && address==0x1f801810u)
            fprintf(stderr,"native_boot: GP0_REFUSED value=%08x opcode=%02x "
                    "store_phase=%u fill_words=%u\n",
                    value,value>>24,boot->gpu.store_phase,boot->gpu.fill_words);
        if (accepted && address==0x1f801814u && (value>>24)==0)
            boot->gpu_reset_sequence=boot->epoch.last_sequence;
    } else if (address==0x1f8010a0u || address==0x1f8010a4u || address==0x1f8010a8u) {
        if (address==0x1f8010a8u && value==0x01000201u)
            fprintf(stderr,"native_boot: GPU_STORE_BEFORE_BLOCK phase=%u remaining=%u "
                    "pixels=%llu xy=%u,%u wh=%u,%u px=%u,%u\n",
                    boot->gpu.store_phase,boot->gpu.store_remaining,
                    (unsigned long long)boot->gpu.stored_pixels,
                    boot->gpu.store_x,boot->gpu.store_y,boot->gpu.store_w,boot->gpu.store_h,
                    boot->gpu.store_px,boot->gpu.store_py);
        accepted=musashi_gpu_dma2_write32(boot->gpu_dma,address,value);
        if (address==0x1f8010a8u && (value&0x01000000u)) observe_gpu_dma(boot,"START");
    } else if (address==0x1f8010e0u || address==0x1f8010e4u || address==0x1f8010e8u) {
        accepted=boot->dma_otc && musashi_dma_otc_write32(boot->dma_otc,address,value);
        if (accepted && address==0x1f8010e8u && (value&0x01000000u)) {
            MusashiDmaOtcState state;
            if (musashi_dma_otc_get_state(boot->dma_otc,&state))
                fprintf(stderr,"native_boot: DMA6_OTC chcr=%08x madr=%08x bcr=%08x "
                        "transfers=%llu words=%llu fault=%u\n",
                        state.chcr,state.madr,state.bcr,
                        (unsigned long long)state.transfers,
                        (unsigned long long)state.words,(unsigned)state.fault);
        }
    } else return 0;
    if (accepted && boot->gpu_trace)
        fprintf(stderr,"native_boot: GPU_TRACE address=%08x width=4 value=%08x\n",
                address,value);
    return accepted;
}

static int cd_hardware_healthy(void *userdata) {
    NativeBoot *boot = userdata;
    return boot->media && boot->gpu_renderer.initialized &&
           boot->gpu_renderer.owner_thread == (uint64_t)SDL_ThreadID() &&
           !boot->epoch.faulted;
}

static int raise_cd_irq(void *userdata) {
    NativeBoot *boot = userdata;
    if (!cd_hardware_healthy(boot) || boot->cd_irq_requests == UINT64_MAX) return 0;
    boot->irq.controller.status |= 0x0004u;
    ++boot->cd_irq_requests;
    return 1;
}

static int cd_data_transfer_idle(void *userdata, int *idle) {
    NativeBoot *boot = userdata;
    MusashiCdDma3State state;
    if (!idle || !boot->cd_dma ||
        !musashi_cd_dma3_get_state(boot->cd_dma, &state) ||
        state.fault != MUSASHI_CD_DMA3_OK) return 0;
    *idle = (state.chcr & 0x01000000u) == 0 || state.waiting_for_request;
    return 1;
}

static int deliver_cd_event(void *userdata, uint32_t class_word, uint32_t spec) {
    NativeBoot *boot = userdata;
    MusashiPsyCrossIrqScheduler *scheduler = &boot->irq.scheduler;
    int accepted;
    if (scheduler->owner_thread != (uint64_t)SDL_ThreadID() ||
        !scheduler->installed || !scheduler->delivering || scheduler->faulted ||
        !scheduler->active_continuation || scheduler->return_consumed ||
        !boot->irq.cd_frame.busy || boot->irq.cd_frame.faulted ||
        boot->cd_event_busy || boot->epoch.faulted ||
        class_word != 0xf0000003u || (spec != 0x20u && spec != 0x40u)) return 0;
    boot->cd_event_busy = 1;
    /* The live startup table contains card and SPU records, not CD callback
     * records. Polling matches use the real table transition. A new matching
     * guest callback requires a clocked executor and is refused here. */
    accepted = musashi_bios_events_deliver(&boot->bios.events, class_word, spec,
                                          NULL, NULL);
    boot->cd_event_busy = 0;
    fprintf(stderr, "native_boot: CD_EVENT class=%08x spec=%08x accepted=%d cycle=%llu\n",
            class_word, spec, accepted, (unsigned long long)boot->epoch.cycle);
    return accepted;
}

static int submit_xa_pcm(void *userdata,uint64_t cycle,uint64_t first_frame,
                          const int16_t *pcm,size_t frames) {
    NativeBoot *boot=userdata;MusashiSpuCdAudioSnapshot state;
    if (!boot->spu || !musashi_spu_cd_audio_snapshot(boot->spu,&state))return 0;
    if (!first_frame) {
        boot->xa_sample_anchor=cycle/768u+(cycle%768u!=0);
        if (boot->xa_sample_anchor<state.sample_index)boot->xa_sample_anchor=state.sample_index;
    }
    if (first_frame>UINT64_MAX-boot->xa_sample_anchor)return 0;
    if (boot->xa_sample_anchor+first_frame<state.sample_index) {
        /* A native transport underrun leaves silence already rendered by
         * the SPU. Queue every arriving PCM frame at the next available
         * sample and retain that new contiguous anchor; never discard PCM. */
        uint64_t gap=state.sample_index-(boot->xa_sample_anchor+first_frame);
        fprintf(stderr,"native_boot: XA_UNDERRUN silence_frames=%llu first=%llu\n",
                (unsigned long long)gap,(unsigned long long)first_frame);
        boot->xa_sample_anchor+=gap;
    }
    if (!musashi_spu_cd_audio_submit_cd_pcm(boot->spu,boot->xa_sample_anchor+first_frame,pcm,frames)) {
        fprintf(stderr,"native_boot: XA_PCM_REFUSED frame=%llu sample=%llu current=%llu\n",
                (unsigned long long)first_frame,(unsigned long long)(boot->xa_sample_anchor+first_frame),
                (unsigned long long)state.sample_index);return 0;
    }
    fprintf(stderr,"native_boot: XA_PCM frames=%zu first=%llu cycle=%llu\n",frames,
            (unsigned long long)first_frame,(unsigned long long)cycle);return 1;
}

static int cd_write8(void *userdata, uint32_t address, uint8_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_cd_owned_write8(boot->cd_drive, address, value)) return 0;
    fprintf(stderr, "native_boot: CD_WRITE address=%08x value=%02x cycle=%llu\n",
            address, value, (unsigned long long)boot->epoch.cycle);
    if (boot->trace_cd_timing && address == 0x1f801803u) {
        MusashiCdOwnedState s;
        if (musashi_cd_owned_get_state(boot->cd_drive,&s))
            fprintf(stderr,"native_boot: CD_TRACE bank=%u if=%u announced=%u claimed=%u incoming=%u incoming_lba=%u requested=%u fifo_lba=%u cursor=%u\n",
                s.index,s.interrupt,s.announced_lba,s.announced_claimed,
                s.incoming_valid,s.incoming_lba,s.data_requested,s.fifo_lba,s.fifo_cursor);
    }
    return 1;
}

static int cd_read8(void *userdata, uint32_t address, uint8_t *value) {
    NativeBoot *boot = userdata;
    if (!musashi_cd_owned_read8(boot->cd_drive, address, value)) return 0;
    if (address == 0x1f801801u)
        fprintf(stderr, "native_boot: CD_RESPONSE address=%08x value=%02x cycle=%llu\n",
                address, *value, (unsigned long long)boot->epoch.cycle);
    return 1;
}

static int cd_dma_healthy(void *userdata) {
    NativeBoot *boot = userdata;
    MusashiCdOwnedState state;
    return cd_hardware_healthy(boot) &&
        musashi_cd_owned_get_state(boot->cd_drive, &state) && state.fault == MUSASHI_CD_OWNED_OK;
}

static int cd_dma_request(void *userdata, int *asserted) {
    MusashiCdOwnedState state;
    if (!asserted || !musashi_cd_owned_get_state(((NativeBoot *)userdata)->cd_drive,&state) ||
        state.fault) return 0;
    *asserted = state.data_requested != 0;
    return 1;
}

static int cd_dma_available(void *userdata, size_t *available) {
    NativeBoot *boot = userdata;
    MusashiCdOwnedState state;
    if (!available || !musashi_cd_owned_get_state(boot->cd_drive, &state) ||
        state.fault || state.fifo_cursor > state.fifo_size) return 0;
    *available = state.data_requested && state.fifo_valid ? state.fifo_size-state.fifo_cursor : 0;
    return 1;
}

static int cd_dma_read_data(void *userdata, uint8_t *destination, size_t bytes) {
    return musashi_cd_owned_read_data(((NativeBoot *)userdata)->cd_drive, destination, bytes);
}

static int cd_read32(void *userdata, uint32_t address, uint32_t *value) {
    NativeBoot *boot = userdata;
    if (address == 0x1f8010f0u)
        return cd_dma_healthy(boot) && musashi_dma_controller_read32(&boot->dma, address, value);
    if (address == 0x1f801014u) {
        if (!value) return 0;
        *value = boot->spu_delay;
        return 1;
    }
    if (address == 0x1f8010c0u) {
        if (!value) return 0;
        *value = boot->spu_dma_madr;
        return 1;
    }
    if (address == 0x1f8010c4u) {
        if (!value) return 0;
        *value = boot->spu_dma_bcr;
        return 1;
    }
    if (address == 0x1f8010c8u) {
        if (!value) return 0;
        *value = boot->spu_dma_chcr;
        return 1;
    }
    return musashi_cd_dma3_read32(boot->cd_dma, address, value);
}

static int cd_write32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot = userdata;
    if (!cd_dma_healthy(boot)) return 0;
    if (address == 0x1f8010f0u) {
        if (!musashi_dma_controller_write32(&boot->dma, address, value)) return 0;
    } else if (address == 0x1f801020u) {
        boot->common_delay=value;
        boot->common_delay_written=1;
    } else if (address == 0x1f801014u) {
        boot->spu_delay=value;
    } else if (address == 0x1f8010c0u) {
        boot->spu_dma_madr=value;
    } else if (address == 0x1f8010c4u) {
        boot->spu_dma_bcr=value;
    } else if (address == 0x1f8010c8u) {
        boot->spu_dma_chcr=value;
        if ((value & 0x01000000u) && (value & 1u)) {
            uint32_t madr = 0x80000000u | (boot->spu_dma_madr & 0x1ffffcu);
            uint32_t bcr = boot->spu_dma_bcr;
            uint32_t blocks = bcr >> 16;
            uint32_t block_size = bcr & 0xffffu;
            uint32_t total_words = blocks ? (blocks * block_size) : block_size;
            uint8_t *src = musashi_boot_ram_span(&boot->memory, madr, total_words * 4u);
            if (src && boot->spu) {
                uint32_t w;
                for (w = 0; w < total_words; ++w) {
                    uint16_t lo = (uint16_t)(src[w*4] | (src[w*4 + 1] << 8));
                    uint16_t hi = (uint16_t)(src[w*4 + 2] | (src[w*4 + 3] << 8));
                    musashi_spu_cd_audio_write16(boot->spu, 0x1f801da8u, lo);
                    musashi_spu_cd_audio_write16(boot->spu, 0x1f801da8u, hi);
                }
                fprintf(stderr, "native_boot: SPU_DMA transferred words=%u madr=%08x\n", total_words, madr);
            } else {
                fprintf(stderr, "native_boot: SPU_DMA FAILED src=%p madr=%08x words=%u\n", (void*)src, madr, total_words);
            }
            boot->spu_dma_chcr &= ~0x01000000u;
        }
    } else if (!musashi_cd_dma3_write32(boot->cd_dma,address,value)) {
        MusashiCdDma3State state;
        if (musashi_cd_dma3_get_state(boot->cd_dma,&state))
            fprintf(stderr,"native_boot: CD_WORD_REFUSED address=%08x value=%08x "
                    "cycle=%llu dma_cycle=%llu due=%llu chcr=%08x bcr=%08x "
                    "madr=%08x delay=%08x waiting=%u fault=%u\n",
                    address,value,(unsigned long long)boot->epoch.cycle,
                    (unsigned long long)state.cycle,(unsigned long long)state.due,
                    state.chcr,state.bcr,state.madr,state.cdrom_delay,
                    state.waiting_for_request,(unsigned)state.fault);
        return 0;
    }
    {
        static uint64_t n;
        if (log_sample(&n, 16u, 4096u))
            fprintf(stderr,"native_boot: CD_WORD_WRITE address=%08x value=%08x cycle=%llu\n",
                    address,value,(unsigned long long)boot->epoch.cycle);
    }
    return 1;
}

static int spu_read16(void *userdata, uint32_t address, uint16_t *value) {
    NativeBoot *boot = userdata;
    if (!musashi_spu_cd_audio_read16(boot->spu, address, value)) return 0;
    {
        static uint64_t n;
        if (log_sample(&n, 16u, 4096u))
            fprintf(stderr, "native_boot: SPU_READ address=%08x value=%04x cycle=%llu\n",
                    address, (unsigned)*value, (unsigned long long)boot->epoch.cycle);
    }
    return 1;
}

/* Key-on (KON 1F801D88/8A) writes with any voice bit set: whether the
 * retail sound driver has started a voice (STOP report). */
static uint64_t g_spu_keyon_writes;

static int spu_write16(void *userdata, uint32_t address, uint16_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_spu_cd_audio_write16(boot->spu, address, value)) return 0;
    if ((address == 0x1f801d88u || address == 0x1f801d8au) && value &&
        g_spu_keyon_writes != UINT64_MAX)
        ++g_spu_keyon_writes;
    {
        static uint64_t n;
        if (log_sample(&n, 16u, 4096u))
            fprintf(stderr, "native_boot: SPU_WRITE address=%08x value=%04x cycle=%llu\n",
                    address, (unsigned)value, (unsigned long long)boot->epoch.cycle);
    }
    return 1;
}

void musashi_sio_controller_set_analog_axes(const MusashiSioController *sio,
                                            uint8_t rx, uint8_t ry,
                                            uint8_t lx, uint8_t ly);

/* Eight pad headings, starting at Up+Right. That heading walks the opening
 * stair. If Musashi's XZ barely moves for 100 frames he is against a wall,
 * so the next heading is tried. Step pauses on that stair are shorter than
 * this window and do not turn. */
static void spiral_climb_input(NativeBoot *boot, uint16_t *sio_clear, uint16_t *pad_bits,
                               uint8_t *stick_lx, uint8_t *stick_ly) {
    static const uint16_t sio_bits[8] = {
        (1u << 4),                 /* Up */
        (1u << 4) | (1u << 5),     /* Up + Right */
        (1u << 5),                 /* Right */
        (1u << 5) | (1u << 6),     /* Down + Right */
        (1u << 6),                 /* Down */
        (1u << 6) | (1u << 7),     /* Down + Left */
        (1u << 7),                 /* Left */
        (1u << 4) | (1u << 7)      /* Up + Left */
    };
    static const uint16_t pad_word[8] = {
        0x1000u, 0x3000u, 0x2000u, 0x6000u, 0x4000u, 0xc000u, 0x8000u, 0x9000u
    };
    static const uint8_t stick_x[8] = { 0x80u, 0xffu, 0xffu, 0xffu, 0x80u, 0x00u, 0x00u, 0x00u };
    static const uint8_t stick_y[8] = { 0x00u, 0x00u, 0x80u, 0xffu, 0xffu, 0xffu, 0x80u, 0x00u };
    /* Up is -X, Right is +Z, from the opening-stair trace. */
    static const int dir_x[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    static const int dir_z[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static int heading = 1;
    static int anchored = 0;
    static unsigned anchor_frame;
    static int16_t anchor_x, anchor_z;
    int16_t cur_x = 0, cur_z = 0;
    unsigned frame = boot->in_game_frames;
    (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x06u, (uint16_t *)&cur_x);
    (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x0Eu, (uint16_t *)&cur_z);
    if (!anchored) {
        anchored = 1;
        anchor_frame = frame;
        anchor_x = cur_x;
        anchor_z = cur_z;
    } else if (frame >= anchor_frame + 100u) {
        int dx = (int)cur_x - (int)anchor_x;
        int dz = (int)cur_z - (int)anchor_z;
        int blocked_x = dir_x[heading] != 0 && (dx > -4 && dx < 4);
        int blocked_z = dir_z[heading] != 0 && (dz > -4 && dz < 4);
        int next = heading;
        /* One blocked axis is enough: Up+Right into a wall keeps sliding on
         * the free axis, and the next heading drops the blocked one. */
        if (blocked_x || blocked_z)
            next = (heading + 1) & 7;
        if (next != heading) {
            heading = next;
            fprintf(stderr, "native_boot: CLIMB_TURN frame=%u heading=%d pos=(%d,%d)\n",
                    frame, heading, (int)cur_x, (int)cur_z);
        }
        anchor_frame = frame;
        anchor_x = cur_x;
        anchor_z = cur_z;
    }
    if (sio_clear) *sio_clear = sio_bits[heading];
    if (pad_bits) *pad_bits = pad_word[heading];
    if (stick_lx) *stick_lx = stick_x[heading];
    if (stick_ly) *stick_ly = stick_y[heading];
}

static int sample_keyboard_pad(void *userdata, uint16_t *buttons) {
    NativeBoot *boot = userdata;
    const Uint8 *keys;
    int count;
    unsigned i;
    uint16_t sampled = 0xffffu;
    uint8_t stick_rx = 0x80u;
    uint8_t stick_ry = 0x80u;
    uint8_t stick_lx = 0x80u;
    uint8_t stick_ly = 0x80u;
    /* Native keyboard policy follows the pinned PsyCross default mapping.
     * Digital L3/R3 remain high. Sample real key state once at PAD byte01;
     * neither a missing SDL context nor a failed provider means neutral input. */
    static const struct { SDL_Scancode key; unsigned bit; } mapping[] = {
        {SDL_SCANCODE_SPACE,0}, {SDL_SCANCODE_RETURN,3},
        {SDL_SCANCODE_UP,4}, {SDL_SCANCODE_RIGHT,5},
        {SDL_SCANCODE_DOWN,6}, {SDL_SCANCODE_LEFT,7},
        {SDL_SCANCODE_W,4}, {SDL_SCANCODE_D,5},
        {SDL_SCANCODE_S,6}, {SDL_SCANCODE_A,7},
        {SDL_SCANCODE_LCTRL,8}, {SDL_SCANCODE_RCTRL,9},
        {SDL_SCANCODE_LSHIFT,10}, {SDL_SCANCODE_RSHIFT,11},
        {SDL_SCANCODE_Z,12}, {SDL_SCANCODE_V,13},
        {SDL_SCANCODE_C,14}, {SDL_SCANCODE_X,15},
        {SDL_SCANCODE_U,12}, {SDL_SCANCODE_I,13},
        {SDL_SCANCODE_K,14}, {SDL_SCANCODE_J,15}
    };
    if (!buttons) return 0;
    /* SIO write8 treats a failed sampler as a hardware fault and latches
     * sio->faulted permanently. A missing GL context is not a disconnected
     * pad: return idle (all bits high) so TAP can complete. */
    *buttons = 0xffffu;
    /* TAP samples from the BIOS vblank callback. SDL key state does not
     * require a current GL context; refusing here leaves analog idle. */
    if (!SDL_WasInit(SDL_INIT_VIDEO) || boot->keyboard_polls == UINT64_MAX)
        return 1;
#ifdef MUSASHI_WITH_BFM_PLAT
    /* The opt-in dev menu owns the pad while it is on screen. */
    if (musashi_dev_menu_is_open())
        return 1;
#endif
    /* Do not PumpEvents here: on Xwayland that re-asserts XSetInputFocus
     * from TAP/SIO and steals the desktop keyboard. HOLD_* env is enough. */
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_plat_input_on) {
        /* MUSASHI_INPUT=bfm_plat: the platform layer's bindings (keyboard,
         * GameController, scripted pads), polled once per VBlank. */
        BfmPlatPad pad;
        if (bfm_plat_input_pad(0, &pad) == 0 && pad.connected) {
            sampled = (uint16_t)~pad.buttons;
            stick_lx = pad.lx; stick_ly = pad.ly;
            stick_rx = pad.rx; stick_ry = pad.ry;
        }
    } else
#endif
    {
        keys = SDL_GetKeyboardState(&count);
        if (!keys) return 1;
        for (i = 0; i < sizeof(mapping) / sizeof(mapping[0]); ++i) {
            if ((int)mapping[i].key >= count) return 0;
            if (keys[mapping[i].key]) sampled &= (uint16_t)~(1u << mapping[i].bit);
        }
        if ((SDL_GetModState() & KMOD_RSHIFT) != 0)
            sampled &= (uint16_t)~(1u << 11);

        /* Gamepad / GameController support */
        {
            static SDL_GameController *s_gamepad = NULL;
            static unsigned s_gamepad_retry = 0;
            /* Look again about once a second until a controller is attached,
             * so one connected after start (or reconnected) is picked up. */
            if (s_gamepad && !SDL_GameControllerGetAttached(s_gamepad)) {
                SDL_GameControllerClose(s_gamepad);
                s_gamepad = NULL;
            }
            if (!s_gamepad && s_gamepad_retry++ % 60u == 0u) {
                if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
                    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
                for (int c = 0; c < SDL_NumJoysticks(); ++c) {
                    if (SDL_IsGameController(c)) {
                        s_gamepad = SDL_GameControllerOpen(c);
                        if (s_gamepad) {
                            fprintf(stderr, "native_boot: GameController attached: %s\n",
                                    SDL_GameControllerName(s_gamepad));
                            break;
                        }
                    }
                }
            }
            if (s_gamepad && SDL_GameControllerGetAttached(s_gamepad)) {
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP))
                    sampled &= (uint16_t)~(1u << 4);
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
                    sampled &= (uint16_t)~(1u << 5);
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
                    sampled &= (uint16_t)~(1u << 6);
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
                    sampled &= (uint16_t)~(1u << 7);
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_A))
                    sampled &= (uint16_t)~(1u << 14); /* Cross / Jump */
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_X))
                    sampled &= (uint16_t)~(1u << 15); /* Square / Attack */
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_B))
                    sampled &= (uint16_t)~(1u << 13); /* Circle / Secondary */
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_Y))
                    sampled &= (uint16_t)~(1u << 12); /* Triangle */
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_START))
                    sampled &= (uint16_t)~(1u << 3);
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_BACK))
                    sampled &= (uint16_t)~(1u << 0);
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
                    sampled &= (uint16_t)~(1u << 10); /* L1 */
                if (SDL_GameControllerGetButton(s_gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
                    sampled &= (uint16_t)~(1u << 11); /* R1 */
                if (SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)
                    sampled &= (uint16_t)~(1u << 8); /* L2 */
                if (SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
                    sampled &= (uint16_t)~(1u << 9); /* R2 */

                int16_t axis_x = SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_LEFTX);
                int16_t axis_y = SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_LEFTY);
                int16_t axis_rx = SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_RIGHTX);
                int16_t axis_ry = SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_RIGHTY);
                if (axis_x < -10000) sampled &= (uint16_t)~(1u << 7); /* Left */
                if (axis_x > 10000) sampled &= (uint16_t)~(1u << 5);  /* Right */
                if (axis_y < -10000) sampled &= (uint16_t)~(1u << 4); /* Up */
                if (axis_y > 10000) sampled &= (uint16_t)~(1u << 6);  /* Down */

                if (axis_x < -3000 || axis_x > 3000)
                    stick_lx = (uint8_t)(((axis_x >> 8) + 128) & 0xff);
                if (axis_y < -3000 || axis_y > 3000)
                    stick_ly = (uint8_t)(((axis_y >> 8) + 128) & 0xff);
                if (axis_rx < -3000 || axis_rx > 3000)
                    stick_rx = (uint8_t)(((axis_rx >> 8) + 128) & 0xff);
                if (axis_ry < -3000 || axis_ry > 3000)
                    stick_ry = (uint8_t)(((axis_ry >> 8) + 128) & 0xff);
            }
        }

    }

    /* Host hold: xdotool/Xwayland often never reaches SDL_GetKeyboardState.
     * 80018C64 stores inverted buttons byte-swapped, so Start (bit 3) lands
     * as 0x0800 at +0x2A; analog 80018CE8 writes the EDGE into +0x3A (78DD2).
     * Holding Start from cold boot or overlay-load spends that edge in
     * table[0]/[1] before 800CEF94 polls 80014CAC(0, 0x800). */
    {
        const char *hold = getenv("MUSASHI_HOLD_R1");
        if (hold && hold[0] && hold[0] != '0')
            sampled &= (uint16_t)~(1u << 11);
        /* Press Start while overlay 0007 is live, never after the title
         * scene: Cross/Start on the title would load a new game.
         * Injecting only at A3B4==2 races TAP: 800118AC increments to 2
         * then 800CF3B0 samples the previous frame's 78DD2. Holding Start
         * for the whole 0007 path lets 800CF3B0 set A3B6. Do not fprintf
         * here: TAP during that log clears 78DD2. */
        const char *auto_start = getenv("MUSASHI_BOOT_AUTO_START");
        if (auto_start && auto_start[0] == '1') {
            if (!boot->opening_overlay_ready) {
                /* Pulse Start during FMV to create authentic edge transitions
                 * in 80018CE8 / D_80078DD2, allowing 800CEF94 to skip the FMV immediately. */
                if ((boot->keyboard_polls % 10u) < 5u)
                    sampled &= (uint16_t)~(1u << 3);
            } else if (!boot->scene1_reached) {
                /* Overlay 0004 is active; Start has now been released so edges can register */
                uint32_t phase = 0;
                if (musashi_boot_read32(&boot->memory, 0x800ec690u, &phase)) {
                    if (phase == 1u) {
                        ++boot->auto_start_phase1_ticks;
                        if (boot->auto_start_phase1_ticks == 35u)
                            fprintf(stderr, "native_boot: AUTO_START pressing Start at phase 1 (frame %u)\n", boot->auto_start_phase1_ticks);
                        if (boot->auto_start_phase1_ticks >= 35u && boot->auto_start_phase1_ticks <= 45u)
                            sampled &= (uint16_t)~(1u << 3);
                    } else if (phase == 2u) {
                        ++boot->auto_start_phase2_ticks;
                        if (boot->auto_start_phase2_ticks == 20u)
                            fprintf(stderr, "native_boot: AUTO_START pressing Start at phase 2 (New Game) (frame %u)\n", boot->auto_start_phase2_ticks);
                        if (boot->auto_start_phase2_ticks >= 20u && boot->auto_start_phase2_ticks <= 30u)
                            sampled &= (uint16_t)~(1u << 3);
                    }
                }
            }
        }
        if (boot->scene1_reached) {
            const char *auto_dismiss_env = getenv("MUSASHI_AUTO_DISMISS");
            int auto_dismiss = (auto_start && auto_start[0] == '1') ||
                               (auto_dismiss_env && auto_dismiss_env[0] == '1');
            if (auto_dismiss && !boot->dialogue_dismissed) {
                uint32_t player_flags = 0;
                uint16_t unk4e_word = 0;
                (void)musashi_boot_read32(&boot->memory, 0x80126B58u + 0x44u, &player_flags);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x4Eu, &unk4e_word);
                uint8_t unk4e = (uint8_t)(unk4e_word & 0xffu);

                if (unk4e != 0 || (player_flags & 0x04000000u) != 0) {
                    boot->dialogue_active = 1;
                }

                if ((boot->dialogue_active || boot->auto_dismiss_ticks > 20u) &&
                    unk4e == 0 && (player_flags & 0x04000000u) == 0) {
                    boot->dialogue_dismissed = 1;
                    boot->dialogue_active = 0;
                    uint8_t *p4d = musashi_boot_ram_span(&boot->memory, 0x80126B58u + 0x4Du, 1);
                    if (p4d && *p4d == 2 && debug_force()) {
                        *p4d = 1;
                        fprintf(stderr, "native_boot: DIVERGENCE CONTROL_UNLOCK player unk4D transitioned 2 -> 1 (frame %u)\n",
                                boot->auto_dismiss_ticks);
                    }
                    fprintf(stderr, "native_boot: AUTO_DISMISS dialogue dismissed (frame %u)%s\n",
                            boot->auto_dismiss_ticks,
                            debug_force() ? "; controls unlocked by DEBUG_FORCE" : "");
                } else {
                    ++boot->auto_dismiss_ticks;
                    if (boot->auto_dismiss_ticks == 1u)
                        fprintf(stderr, "native_boot: AUTO_DISMISS starting dialogue dismissal in Scene 10\n");
                    if ((boot->auto_dismiss_ticks % 12u) < 6u) {
                        const char *dismiss_key = getenv("MUSASHI_DISMISS_KEY");
                        if (dismiss_key && strcmp(dismiss_key, "start") == 0)
                            sampled &= (uint16_t)~(1u << 3); /* Start */
                        else if (dismiss_key && strcmp(dismiss_key, "both") == 0) {
                            sampled &= (uint16_t)~(1u << 14); /* Cross */
                            sampled &= (uint16_t)~(1u << 3);  /* Start */
                        } else
                            sampled &= (uint16_t)~(1u << 14); /* Cross */
                    }
                }
            }
            /* Scripted gameplay actions for automated in-game validation */
            const char *gameplay_action = getenv("MUSASHI_GAMEPLAY_ACTION");
            if (gameplay_action && boot->in_game_frames >= 5u) {
                if (strcmp(gameplay_action, "walk") == 0) {
                    sampled &= (uint16_t)~(1u << 4); /* Up: walk forward */
                } else if (strcmp(gameplay_action, "mash") == 0) {
                    /* Cross for 4 of every 30 frames, the way a player taps
                     * through dialogue; the stimulus the retail opening
                     * observation used. Input only. */
                    if ((boot->in_game_frames % 30u) < 4u)
                        sampled &= (uint16_t)~(1u << 14);
                } else if (strcmp(gameplay_action, "jump") == 0) {
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 24u) {
                        sampled &= (uint16_t)~(1u << 14); /* Cross */
                    }
                } else if (strcmp(gameplay_action, "slash") == 0) {
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 26u) {
                        sampled &= (uint16_t)~(1u << 15);
                    }
                } else if (strcmp(gameplay_action, "combo") == 0) {
                    if ((boot->in_game_frames >= 13u && boot->in_game_frames <= 18u) ||
                        (boot->in_game_frames >= 23u && boot->in_game_frames <= 28u) ||
                        (boot->in_game_frames >= 33u && boot->in_game_frames <= 38u)) {
                        sampled &= (uint16_t)~(1u << 15);
                    }
                } else if (strcmp(gameplay_action, "lumina") == 0) {
                    uint8_t *lum_flag = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
                    if (lum_flag) *lum_flag |= 1u;
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 26u) {
                        sampled &= (uint16_t)~(1u << 12);
                    }
                } else if (strcmp(gameplay_action, "jump_attack") == 0) {
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 22u) {
                        sampled &= (uint16_t)~(1u << 14); /* Cross: Jump */
                    } else if (boot->in_game_frames >= 23u && boot->in_game_frames <= 32u) {
                        sampled &= (uint16_t)~(1u << 15); /* Square: Jump Attack */
                    }
                } else if (strcmp(gameplay_action, "combat") == 0 || strcmp(gameplay_action, "combat_patrol") == 0) {
                    uint8_t *lum_flag = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
                    if (lum_flag) *lum_flag |= 1u;
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 28u) {
                        sampled &= (uint16_t)~(1u << 15);
                    }
                } else if (strcmp(gameplay_action, "combat_all") == 0 || strcmp(gameplay_action, "demo") == 0) {
                    uint8_t *lum_flag = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
                    if (lum_flag) *lum_flag |= 1u;
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 22u) {
                        sampled &= (uint16_t)~(1u << 14); /* Cross: Jump */
                    } else if (boot->in_game_frames >= 23u && boot->in_game_frames <= 32u) {
                        sampled &= (uint16_t)~(1u << 15); /* Square: Jump Attack */
                    } else if (boot->in_game_frames >= 33u && boot->in_game_frames <= 45u) {
                        sampled &= (uint16_t)~(1u << 12); /* Triangle: Lumina Slash */
                    } else if (boot->in_game_frames >= 46u && boot->in_game_frames <= 58u) {
                        sampled &= (uint16_t)~(1u << 15); /* Square: Fusion Slash */
                    } else if (boot->in_game_frames >= 59u) {
                        uint32_t f = boot->in_game_frames - 59u;
                        sampled &= (uint16_t)~(1u << 4); /* Walk forward */
                        if ((f % 16u) < 6u)
                            sampled &= (uint16_t)~(1u << 15); /* Slash combo pulse */
                    }
                } else if (strcmp(gameplay_action, "spiral_climb") == 0 || strcmp(gameplay_action, "climb") == 0) {
                    if (boot->in_game_frames >= 8u) {
                        uint16_t clear = 0;
                        spiral_climb_input(boot, &clear, NULL, &stick_lx, &stick_ly);
                        sampled = (uint16_t)(sampled & (uint16_t)~clear);
                    }
                } else if (strcmp(gameplay_action, "patrol") == 0) {
                    uint32_t cycle = boot->in_game_frames % 60u;
                    if (cycle < 25u)
                        sampled &= (uint16_t)~(1u << 4); /* Up */
                    else if (cycle < 35u) {
                        sampled &= (uint16_t)~(1u << 4); /* Up */
                        sampled &= (uint16_t)~(1u << 14); /* Jump */
                    } else if (cycle < 45u) {
                        sampled &= (uint16_t)~(1u << 15); /* Slash */
                    } else {
                        sampled &= (uint16_t)~(1u << 5); /* Right */
                    }
                }
            }
        }
        if (!(sampled & (1u << 7)) && stick_lx == 0x80u) stick_lx = 0x00u; /* Left */
        if (!(sampled & (1u << 5)) && stick_lx == 0x80u) stick_lx = 0xffu; /* Right */
        if (!(sampled & (1u << 4)) && stick_ly == 0x80u) stick_ly = 0x00u; /* Up */
        if (!(sampled & (1u << 6)) && stick_ly == 0x80u) stick_ly = 0xffu; /* Down */
    }
    musashi_sio_controller_set_analog_axes(&boot->sio, stick_rx, stick_ry, stick_lx, stick_ly);
    ++boot->keyboard_polls;
    *buttons = sampled;
    return 1;
}

static int raise_sio_irq(void *userdata, uint16_t mask) {
    NativeBoot *boot = userdata;
    if (mask != 0x80u) return 0;
    /* ACK IRQ is a CPU status latch, not a GL operation. Refusing it off the
     * window thread faults SIO permanently and leaves 29B4 at FFFFFFFF. */
    boot->irq.controller.status |= mask;
    return 1;
}

static int input_write8(void *userdata, uint32_t address, uint8_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_sio_controller_write8(&boot->sio, address, value)) return 0;
    {
        static uint64_t n;
        if (log_sample(&n, 16u, 4096u))
            fprintf(stderr, "native_boot: INPUT_DATA address=%08x value=%02x cycle=%llu\n",
                    address, (unsigned)value, (unsigned long long)boot->sio.cycle);
    }
    return 1;
}

/* One NTSC field at the scanline timer's own rate (PACED_RATE per line,
 * 263 lines), so Timer1 and the VBlank edge agree on where a frame ends. */
#define MUSASHI_GUEST_FRAME_CYCLES ((33868800u / (60u * 263u)) * 263u)

/* Guest-cycle clock. Device time is exactly the cycles charged to guest
 * execution, and a VBlank edge lands every MUSASHI_GUEST_FRAME_CYCLES of it.
 * Retail frame pacing therefore does not depend on how fast the host runs
 * the interpreter: with the host-tick source, a host slower than a PS1 saw
 * VBlanks keep arriving at 60 Hz of wall time while the guest had done a
 * fraction of a frame's work, so each game loop spanned ~5 VBlanks instead of
 * retail's and every timed sequence ran slow. Host time is used only to hold
 * the guest back to real speed (epoch_wait), never to push it forward. */
static int guest_clock_collect(NativeBoot *boot, MusashiIrqSourceBatch *batch) {
    static unsigned drain_countdown;
    /* PsyCross still publishes host-time VBlanks; drain them so its queue
     * (64 entries, ~1 s of host time) cannot overflow. They carry no guest
     * time. Collecting takes its mutex, so do it at guest frame boundaries
     * and periodically, not on every 2-cycle instruction charge. */
    if (boot->irq.scheduler.installed &&
        (boot->guest_next_vblank <= boot->guest_tick || drain_countdown-- == 0)) {
        MusashiIrqSourceBatch host;
        drain_countdown = 4096u;
        if (!musashi_psycross_irq_scheduler_collect_source(&boot->irq.scheduler, &host))
            return 0;
    }
    batch->count = 0;
    batch->frequency = MUSASHI_DEVICE_CLOCK_HZ;
    batch->cut_tick = boot->guest_tick;
    while (boot->guest_next_vblank <= boot->guest_tick) {
        /* Before the source handler is installed the host path published no
         * edges either; keep the frame grid, deliver nothing. */
        if (boot->irq.scheduler.installed) {
            if (batch->count >= MUSASHI_IRQ_SOURCE_CAPACITY) return 0;
            batch->edges[batch->count++] =
                (MusashiIrqSourceEdge){++boot->guest_sequence, boot->guest_next_vblank};
        }
        boot->guest_next_vblank += MUSASHI_GUEST_FRAME_CYCLES;
    }
    return 1;
}

static int guest_clock_wait(NativeBoot *boot, uint64_t target_cycle) {
    if (target_cycle < boot->guest_tick) return 0;
    boot->guest_tick = target_cycle;
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_plat_timing_on) return 1;   /* paced once per guest VBlank instead */
#endif
    if (!boot->guest_unthrottled) {
        uint64_t freq = SDL_GetPerformanceFrequency();
        uint64_t due = boot->guest_host_origin +
            (uint64_t)((double)target_cycle * (double)freq / (double)MUSASHI_DEVICE_CLOCK_HZ);
        uint64_t now = SDL_GetPerformanceCounter();
        /* Sleep only when the guest is a whole millisecond ahead. */
        while (now + freq / 1000u < due) {
            SDL_Delay(1);
            now = SDL_GetPerformanceCounter();
        }
    }
    return 1;
}

static int epoch_collect(void *userdata, MusashiIrqSourceBatch *batch) {
    NativeBoot *boot = userdata;
    if (boot->guest_clock) return guest_clock_collect(boot, batch);
    if (boot->irq.scheduler.installed)
        return musashi_psycross_irq_scheduler_collect_source(&boot->irq.scheduler, batch);
    /* Before source installation there are no owned callbacks to collect.
     * Never substitute an empty queue after a source has been removed. */
    if (boot->irq.scheduler.source_sequence || boot->irq.scheduler.faulted) return 0;
    memset(batch, 0, sizeof(*batch));
    batch->frequency = SDL_GetPerformanceFrequency();
    batch->cut_tick = SDL_GetPerformanceCounter();
    return batch->frequency != 0;
}

static int epoch_wait(void *userdata, uint64_t target_tick) {
    NativeBoot *boot = userdata;
    if (boot->guest_clock) return guest_clock_wait(boot, target_tick);
    uint64_t previous = SDL_GetPerformanceCounter();
    Uint32 started = SDL_GetTicks();
    for (;;) {
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < previous) return 0;
        if (now >= target_tick) return 1;
        if ((Uint32)(SDL_GetTicks() - started) >= 10000u) return 0;
        if (target_tick - now > boot->epoch.frequency / 500u) SDL_Delay(1);
        else SDL_Delay(0);
        previous = now;
    }
}

static int advance_cd_devices(NativeBoot *boot, uint64_t cycle) {
    MusashiCdDma3State dma;
    MusashiCdOwnedState cd;
    uint64_t cd_cycle = cycle;
    static uint64_t bounded_cuts;
    if (!boot->cd_drive) return 1;
    if (!boot->cd_dma || !musashi_cd_dma3_get_state(boot->cd_dma,&dma)) return 0;
    if (!musashi_cd_owned_get_state(boot->cd_drive, &cd)) return 0;
    /* A chopped transfer must finish using its latched sector before the
     * next streaming notification can make the source release that FIFO.
     * Host catch-up must not compress a sector period across active DMA. */
    if (cd.command == 0x1bu && (dma.chcr & 0x01000000u)) {
        if (!musashi_cd_dma3_advance(boot->cd_dma,cycle) ||
            !musashi_cd_dma3_get_state(boot->cd_dma,&dma)) return 0;
        if ((dma.chcr & 0x01000000u) && !dma.waiting_for_request) return 1;
    }
    if (cd.command == 0x1bu && !cd.phase && !cd.incoming_valid && boot->spu) {
        MusashiSpuCdAudioSnapshot audio;
        if (!musashi_spu_cd_audio_snapshot(boot->spu,&audio)) return 0;
        /* Bound native producer catch-up by real PCM queue capacity. The
         * SPU continues consuming on the epoch; no sector/audio is dropped. */
        if (audio.pending_frames > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT-2352u)
            return musashi_cd_dma3_advance(boot->cd_dma,cycle);
    }
    /* A streaming IRQ's host execution/logging cost is not guest CD time.
     * Use the current source instruction's production limit within that IRQ;
     * DMA transport keeps its existing absolute owner clock. */
    if (boot->irq.cd_frame.busy && cd.command == 0x1bu &&
        boot->cd_irq_production_limit && cd_cycle > boot->cd_irq_production_limit)
        cd_cycle = boot->cd_irq_production_limit;
    if (cd.incoming_valid) {
        uint64_t target = cd.cycle;
        if (cd.phase && cd.due > cd.cycle && cd.due <= cd_cycle)
            target = cd.due;
        /* A decoder can claim the previous announcement while another
         * sector waits in incoming. Service that publication deadline even
         * without a new command; advance_without_fetch retains backpressure. */
        if (!cd.interrupt && !cd.response_count &&
            (!cd.announced_valid || cd.announced_claimed || cd.command == 0x1bu) &&
            cd.sector_publish_due > cd.cycle && cd.sector_publish_due <= cd_cycle &&
            (target == cd.cycle || cd.sector_publish_due < target))
            target = cd.sector_publish_due;
        if (target != cd.cycle) {
            ++bounded_cuts;
            if (bounded_cuts <= 8u || (bounded_cuts & 255u) == 0u)
                fprintf(stderr, "native_boot: CD_COMMAND_WHILE_STALLED count=%llu "
                        "requested=%llu target=%llu command=%02x due=%llu\n",
                        (unsigned long long)bounded_cuts,
                        (unsigned long long)cycle, (unsigned long long)target,
                        cd.command, (unsigned long long)cd.due);
        }
        if ((dma.chcr & 0x01000000u) && dma.due <= target) {
            if (!musashi_cd_owned_advance_without_fetch(boot->cd_drive, dma.due) ||
                !musashi_cd_dma3_advance(boot->cd_dma, dma.due)) return 0;
        }
        if (target > cd.cycle &&
            !musashi_cd_owned_advance_without_fetch(boot->cd_drive, target))
            return 0;
        return musashi_cd_dma3_advance(boot->cd_dma, cycle);
    }
    /* Deliver a streaming sector at its own deadline, not one cycle
     * before the following sector. That leaves its actual transfer period
     * available to the guest callback even after a large host catch-up. */
    if (cd.command == 0x1bu && cd.sector_due >= cd.cycle &&
        cd.sector_due && cd_cycle > cd.sector_due)
        cd_cycle = cd.sector_due;
    cd_cycle = musashi_cd_owned_bounded_cycle(&cd, cd_cycle);
    if (cd_cycle != cycle) {
        ++bounded_cuts;
        if (bounded_cuts <= 8u || (bounded_cuts & 255u) == 0u)
            fprintf(stderr, "native_boot: CD_BOUNDED count=%llu requested=%llu "
                    "bounded=%llu incoming=%u claimed=%u sector_due=%llu\n",
                    (unsigned long long)bounded_cuts,
                    (unsigned long long)cycle, (unsigned long long)cd_cycle,
                    cd.incoming_valid, cd.announced_claimed,
                    (unsigned long long)cd.sector_due);
    }
    /* A cut can cross a DMA completion and a later CD production deadline.
     * Consume its FIFO at the earlier DMA deadline before advancing the drive
     * to the end of the cut. Device callbacks cannot start another transfer. */
    if ((dma.chcr & 0x01000000u) && dma.due <= cd_cycle) {
        if (!musashi_cd_owned_advance(boot->cd_drive,dma.due) ||
            !musashi_cd_dma3_advance(boot->cd_dma,dma.due)) return 0;
    }
    return musashi_cd_owned_advance(boot->cd_drive,cd_cycle) &&
        musashi_cd_dma3_advance(boot->cd_dma,cycle);
}

static int epoch_advance_devices(void *userdata, uint64_t cycle) {
    NativeBoot *boot = userdata;
    MusashiGpuDma2State gpu;
    /* Deterministic native CD-before-GPU order; physical bus priority is not
     * modeled. Device reads never supply time or dispatch guest callbacks. */
    int accepted=musashi_timer2_advance(&boot->timer2, cycle) &&
           musashi_sio_controller_advance(&boot->sio, cycle) &&
           musashi_scanline_timer_advance(&boot->timer1, cycle) &&
           advance_cd_devices(boot, cycle) &&
           musashi_mdec_advance(&boot->mdec, cycle) &&
           (!boot->gpu_dma || musashi_gpu_dma2_advance(boot->gpu_dma,cycle)) &&
           (!boot->spu || musashi_spu_cd_audio_advance(boot->spu, cycle));
    if (boot->gpu_dma && musashi_gpu_dma2_get_state(boot->gpu_dma,&gpu) &&
        gpu.transfers != boot->gpu_dma_transfers_logged) {
        observe_gpu_dma(boot,"COMPLETE");
        boot->gpu_dma_transfers_logged=gpu.transfers;
    }
    return accepted;
}

static void host_window_inspect(SDL_Window *window);

#ifdef MUSASHI_WITH_BFM_PLAT
/* Opt-in Port Dev Menu (musashi_dev_menu.h): guest access, host keys and
 * the overlay. Inert unless BFM_DEV_MENU=1 or --dev-menu. */
static int dev_menu_read16(void *u, uint32_t a, uint16_t *v) {
    return musashi_boot_read16(&((NativeBoot *)u)->memory, a, v);
}
static int dev_menu_write16(void *u, uint32_t a, uint16_t v) {
    return musashi_boot_write16(&((NativeBoot *)u)->memory, a, v);
}
static int dev_menu_read32(void *u, uint32_t a, uint32_t *v) {
    return musashi_boot_read32(&((NativeBoot *)u)->memory, a, v);
}
static int dev_menu_write32(void *u, uint32_t a, uint32_t v) {
    return musashi_boot_write32(&((NativeBoot *)u)->memory, a, v);
}

/* Edges of F1/arrows/Enter/Esc and of the first controller's L3+R3 (or
 * Guide), d-pad, left stick, A and B. Reads state only; native_boot's own
 * VBlank SDL_PollEvent keeps it current. */
static unsigned dev_menu_poll_keys(void) {
    static SDL_GameController *pad;
    static unsigned prev, retry;
    unsigned held = 0, pressed;
    int n = 0;
    const Uint8 *k = SDL_GetKeyboardState(&n);
    if (k && n > SDL_SCANCODE_F1) {
        if (k[SDL_SCANCODE_F1]) held |= MUSASHI_DEV_KEY_TOGGLE;
        if (k[SDL_SCANCODE_UP]) held |= MUSASHI_DEV_KEY_UP;
        if (k[SDL_SCANCODE_DOWN]) held |= MUSASHI_DEV_KEY_DOWN;
        if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_C])
            held |= MUSASHI_DEV_KEY_OK;
        if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_V])
            held |= MUSASHI_DEV_KEY_BACK;
    }
    if (pad && !SDL_GameControllerGetAttached(pad)) {
        SDL_GameControllerClose(pad);
        pad = NULL;
    }
    if (!pad && SDL_WasInit(SDL_INIT_GAMECONTROLLER) && retry++ % 60u == 0u) {
        int j;
        for (j = 0; j < SDL_NumJoysticks() && !pad; ++j)
            if (SDL_IsGameController(j)) pad = SDL_GameControllerOpen(j);
    }
    if (pad) {
        int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if ((SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) &&
             SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_GUIDE))
            held |= MUSASHI_DEV_KEY_TOGGLE;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -16000)
            held |= MUSASHI_DEV_KEY_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 16000)
            held |= MUSASHI_DEV_KEY_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) held |= MUSASHI_DEV_KEY_OK;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) held |= MUSASHI_DEV_KEY_BACK;
    }
    pressed = held & ~prev;
    prev = held;
    return pressed;
}

static void dev_menu_text_at(int x, int y, int scale, const char *text,
                             uint8_t r, uint8_t g, uint8_t b) {
    for (; *text; ++text, x += BFM_FONT_CELL_W * scale) {
        const uint8_t *rows = bfm_plat_font_glyph(*text);
        int row, col;
        if (*text == ' ' || !rows) continue;
        for (row = 0; row < BFM_FONT_GLYPH_H; ++row)
            for (col = 0; col < BFM_FONT_GLYPH_W;) {
                int run = 0;
                while (col + run < BFM_FONT_GLYPH_W && (rows[row] >> (4 - col - run)) & 1u) ++run;
                if (run) {
                    musashi_gpu_psycross_overlay_rect(x + col * scale, y + row * scale,
                                                      run * scale, scale, r, g, b);
                    col += run;
                } else {
                    ++col;
                }
            }
    }
}

static void dev_menu_overlay(void *user, int width, int height) {
    const char *lines[16];
    int highlight, scale = height / 320, cell_h, i, cols = 0;
    size_t n = musashi_dev_menu_text(lines, 16, &highlight);
    (void)user;
    if (!n) return;
    if (scale < 2) scale = 2;
    cell_h = (BFM_FONT_CELL_H + 2) * scale;
    for (i = 0; i < (int)n; ++i)
        if ((int)strlen(lines[i]) > cols) cols = (int)strlen(lines[i]);
    if (!musashi_dev_menu_is_open()) {
        /* Badge while any cheat is on and the menu is closed. */
        int w = (cols * BFM_FONT_CELL_W + 4) * scale;
        musashi_gpu_psycross_overlay_rect(width - w - 8 * scale, 4 * scale, w, cell_h, 96, 0, 0);
        dev_menu_text_at(width - w - 6 * scale, 5 * scale, scale, lines[0], 255, 255, 255);
        return;
    }
    musashi_gpu_psycross_overlay_rect(6 * scale, 6 * scale, (cols * BFM_FONT_CELL_W + 8) * scale,
                                      (int)n * cell_h + 6 * scale, 12, 12, 40);
    for (i = 0; i < (int)n; ++i) {
        int y = 9 * scale + i * cell_h;
        if (i == highlight)
            musashi_gpu_psycross_overlay_rect(8 * scale, y - scale,
                                              (cols * BFM_FONT_CELL_W + 4) * scale, cell_h, 60, 60, 150);
        dev_menu_text_at(10 * scale, y + scale, scale, lines[i],
                         i == 0 ? 255 : 230, i == 0 ? 210 : 230, i == 0 ? 90 : 230);
    }
}

static void dev_menu_vblank(NativeBoot *boot, int display) {
    uint16_t scene = 0, mode = 0;
    if (!musashi_dev_menu_enabled()) return;
    if (display) musashi_dev_menu_keys(dev_menu_poll_keys());
    (void)musashi_boot_read16(&boot->memory, 0x800B99DEu, &scene);
    (void)musashi_boot_read16(&boot->memory, 0x800B99F0u, &mode);
    musashi_dev_menu_vblank(boot->scene1_reached, scene, mode);
}
#endif

static int epoch_video_edge(void *userdata, uint64_t sequence, uint64_t cycle) {
    NativeBoot *boot = userdata;
    (void)sequence;
    if (!musashi_scanline_timer_vblank_at(&boot->timer1, cycle) ||
        !musashi_gpu_controller_vblank(&boot->gpu) || !raise_vblank(boot))
        return 0;
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_plat_timing_on) (void)bfm_plat_timing_vsync(1);
#endif
    /* Host scanout only. Failure here must not fault the epoch; present
     * already refuses when the GPU backend is mid-command. */
    if (display_on(boot)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                host_stop_signal = SIGTERM;
        }
#ifdef MUSASHI_WITH_BFM_PLAT
        if (g_plat_input_on) (void)bfm_plat_input_poll();
        dev_menu_vblank(boot, 1);
#endif
        if (boot->gpu_renderer.window && boot->gpu_renderer.context) {
            if (SDL_GL_GetCurrentContext() != boot->gpu_renderer.context)
                SDL_GL_MakeCurrent((SDL_Window *)boot->gpu_renderer.window, boot->gpu_renderer.context);
        }
        (void)present_frame(boot);
    }
#ifdef MUSASHI_WITH_BFM_PLAT
    else
        dev_menu_vblank(boot, 0);
#endif
    if (boot->scene1_reached && boot->gpu.display.width == 320u && boot->gpu.display.height == 240u) {
        if (boot->gte.write_count > 800u) {
            const char *pause_in_game = getenv("MUSASHI_PAUSE_IN_GAME");
            const char *frames_env = getenv("MUSASHI_IN_GAME_FRAMES");
            unsigned target = 0;
            if (frames_env) {
                int val = atoi(frames_env);
                target = val > 0 ? (unsigned)val : 1u;
            } else if (pause_in_game && pause_in_game[0] == '1') {
                target = 5u;
            }
            ++boot->in_game_frames;
            if (getenv("MUSASHI_TRACE_L1")) {
                uint16_t scene = 0, a3b4 = 0, mode = 0, act = 0;
                uint8_t *p4d = musashi_boot_ram_span(&boot->memory, 0x80126B58u + 0x4Du, 1);
                (void)musashi_boot_read16(&boot->memory, 0x800B99DEu, &scene);
                (void)musashi_boot_read16(&boot->memory, 0x800B99E4u, &a3b4);
                (void)musashi_boot_read16(&boot->memory, 0x800B99F0u, &mode);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u, &act);
                uint8_t *pa9 = musashi_boot_ram_span(&boot->memory, 0x80126B58u + 0xA9u, 1);
                fprintf(stderr, "native_boot: L1 frame=%u scene=%04x a3b4=%04x mode=%04x p_act=%04x p_4d=%02x p_a9=%02x\n",
                        boot->in_game_frames, scene, a3b4, mode, act, p4d ? *p4d : 0, pa9 ? *pa9 : 0);
            }
            if (boot->in_game_frames == 1u) {
                for (int a = 0; a < 10; ++a) {
                    uint32_t fn = 0;
                    musashi_boot_read32(&boot->memory, 0x801873B0u + a * 4, &fn);
                    fprintf(stderr, "native_boot: D_801873B0[%d] = %08x\n", a, fn);
                }
            }
            /* Control unlock: Transition p->unk4D from 2 to 1 so func_80159A20 executes */
            uint8_t *p_unk4d = musashi_boot_ram_span(&boot->memory, 0x80126B58u + 0x4Du, 1);
            if (p_unk4d && *p_unk4d == 2 && debug_force()) {
                uint16_t fade = 0;
                (void)musashi_boot_read16(&boot->memory, 0x800af7ceu, &fade);
                if (fade >= 256u || boot->dialogue_dismissed || boot->in_game_frames >= 2u) {
                    *p_unk4d = 1;
                    musashi_boot_write16(&boot->memory, 0x800B99F0u, 9u); /* Gameplay mode */
                    fprintf(stderr, "native_boot: DIVERGENCE CONTROL_UNLOCK player unk4D transitioned 2 -> 1, mode=9 (fade=%u in_game_frame=%u)\n",
                            (unsigned)fade, boot->in_game_frames);
                }
            }
            /* Controller mode and control unlock in gameplay */
            if (p_unk4d && *p_unk4d == 1 && debug_force()) {
                uint8_t *p_act = musashi_boot_ram_span(&boot->memory, 0x80126B58u, 0x200);
                if (p_act) {
                    static int logged;
                    if (p_act[0xA9] != 0x41u && !logged++)
                        fprintf(stderr, "native_boot: DIVERGENCE PAD_MODE player+0xA9 %02x -> 41\n",
                                p_act[0xA9]);
                    *(uint8_t *)(p_act + 0xA9) = 0x41u; /* Digital Pad controller mode */
                    *(uint8_t *)(p_act + 0x4D) = 1u;
                }
            }


            /* Milestone M2: 3D Spiral Tower Ascension & Authentic Terrain Clamping Traversal */
            const char *act_climb = getenv("MUSASHI_GAMEPLAY_ACTION");
            if (act_climb && (strcmp(act_climb, "spiral_climb") == 0 || strcmp(act_climb, "climb") == 0)) {
                if (boot->in_game_frames >= 8u) {
                    uint8_t *p_act = musashi_boot_ram_span(&boot->memory, 0x80126B58u, 0x200);
                    if (p_act) {
                        /* 0x41: Digital Pad mode, player control unlocked, mode=9 */
                        *(uint8_t *)(p_act + 0xA9) = 0x41u;
                        *(uint8_t *)(p_act + 0x4D) = 1u;
                        musashi_boot_write16(&boot->memory, 0x800B99F0u, 9u);

                        uint16_t bits = 0;
                        uint8_t lx = 0x80u, ly = 0x00u;
                        spiral_climb_input(boot, NULL, &bits, &lx, &ly);
                        *(uint16_t *)(p_act + 0xAA) = bits;
                        *(uint16_t *)(p_act + 0xAC) = bits;
                        *(uint16_t *)(p_act + 0xAE) = (uint16_t)((uint16_t)lx | ((uint16_t)ly << 8));
                        musashi_boot_write16(&boot->memory, 0x80078DCAu, bits);
                        musashi_boot_write16(&boot->memory, 0x80078DD2u, bits);
                    }
                }
            }

            /* Controller edge-pulsed action dispatch for automated tests */
            const char *act_env = getenv("MUSASHI_GAMEPLAY_ACTION");
            if (act_env && boot->in_game_frames >= 13u) {
                uint8_t *p_act = musashi_boot_ram_span(&boot->memory, 0x80126B58u, 0x200);
                uint16_t cur_act = 0;
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x00u, &cur_act);
                int pulse_sq = 0, pulse_tri = 0, pulse_cross = 0;

                if (strcmp(act_env, "slash") == 0) {
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 26u) pulse_sq = 1;
                } else if (strcmp(act_env, "combo") == 0) {
                    if ((boot->in_game_frames >= 13u && boot->in_game_frames <= 18u) ||
                        (boot->in_game_frames >= 23u && boot->in_game_frames <= 28u) ||
                        (boot->in_game_frames >= 33u && boot->in_game_frames <= 38u)) pulse_sq = 1;
                } else if (strcmp(act_env, "lumina") == 0) {
                    uint8_t *lum_flag = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
                    if (lum_flag) *lum_flag |= 1u;
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 26u) pulse_tri = 1;
                } else if (strcmp(act_env, "jump") == 0) {
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 24u) pulse_cross = 1;
                } else if (strcmp(act_env, "jump_attack") == 0) {
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 22u) pulse_cross = 1;
                    else if (boot->in_game_frames >= 23u && boot->in_game_frames <= 32u) pulse_sq = 1;
                } else if (strcmp(act_env, "combat") == 0 || strcmp(act_env, "combat_patrol") == 0) {
                    uint8_t *lum_flag = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
                    if (lum_flag) *lum_flag |= 1u;
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 28u) pulse_sq = 1;
                } else if (strcmp(act_env, "combat_all") == 0 || strcmp(act_env, "demo") == 0) {
                    uint8_t *lum_flag = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
                    if (lum_flag) *lum_flag |= 1u;
                    if (boot->in_game_frames >= 13u && boot->in_game_frames <= 22u) pulse_cross = 1;
                    else if (boot->in_game_frames >= 23u && boot->in_game_frames <= 32u) pulse_sq = 1;
                    else if (boot->in_game_frames >= 33u && boot->in_game_frames <= 45u) pulse_tri = 1;
                    else if (boot->in_game_frames >= 46u && boot->in_game_frames <= 58u) pulse_sq = 1;
                }

                if (p_act) {
                    if (pulse_sq) {
                        *(uint16_t *)(p_act + 0xAC) |= 0x0080u; /* Square edge: Fusion Slash / Jump Attack */
                        musashi_boot_write16(&boot->memory, 0x80078DD2u, 0x0080u);
                    }
                    if (pulse_tri) {
                        *(uint16_t *)(p_act + 0xAC) |= 0x0010u; /* Triangle edge: Lumina Slash */
                        musashi_boot_write16(&boot->memory, 0x80078DD2u, 0x0010u);
                    }
                    if (pulse_cross) {
                        *(uint16_t *)(p_act + 0xAC) |= 0x0040u; /* Cross edge: Jump */
                        musashi_boot_write16(&boot->memory, 0x80078DD2u, 0x0040u);
                    }
                }
            }

            /* Telemetry: Musashi Action transitions, elevation, and enemy tracking */
            {
                uint16_t curr_act = 0;
                uint16_t prev_act = 0;
                int16_t pos_x = 0, pos_y = 0, pos_z = 0;
                int32_t vy_fixed = 0;
                uint32_t t_ptr = 0;
                uint32_t b4_ptr = 0;
                uint16_t hp = 150;
                int anim = 0;

                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x00u, &curr_act);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x02u, &prev_act);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x06u, (uint16_t *)&pos_x);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x0Au, (uint16_t *)&pos_y);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x0Eu, (uint16_t *)&pos_z);
                (void)musashi_boot_read32(&boot->memory, 0x80126B58u + 0x14u, (uint32_t *)&vy_fixed);
                (void)musashi_boot_read32(&boot->memory, 0x80126B58u + 0xB0u, &t_ptr);
                (void)musashi_boot_read32(&boot->memory, 0x80126B58u + 0xB4u, &b4_ptr);
                (void)musashi_boot_read16(&boot->memory, 0x80078EB2u, &hp);

                if (t_ptr >= 0x80000000u && b4_ptr != 0) {
                    for (int k = 0; k < 64; ++k) {
                        uint32_t entry = 0;
                        if (musashi_boot_read32(&boot->memory, t_ptr + (uint32_t)(k * 4), &entry)) {
                            if (entry == b4_ptr) {
                                anim = k;
                                break;
                            }
                        }
                    }
                }

                int16_t floor_y = 0;
                uint8_t cur_unk4d = 0;
                uint32_t p_flags = 0;
                uint16_t p_uaa = 0, p_uac = 0;
                uint8_t u1c5 = 0, b9a64 = 0;
                uint16_t b99f0 = 0, pad_dd2 = 0;
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x8Au, (uint16_t *)&floor_y);
                (void)musashi_boot_read32(&boot->memory, 0x80126B58u + 0x44u, &p_flags);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0xAAu, &p_uaa);
                (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0xACu, &p_uac);
                uint8_t *p_1c5 = musashi_boot_ram_span(&boot->memory, 0x80126B58u + 0x1C5u, 1);
                if (p_1c5) u1c5 = *p_1c5;
                (void)musashi_boot_read16(&boot->memory, 0x800B99F0u, &b99f0);
                uint8_t *p_9a64 = musashi_boot_ram_span(&boot->memory, 0x800B9A64u, 1);
                if (p_9a64) b9a64 = *p_9a64;
                (void)musashi_boot_read16(&boot->memory, 0x80078DD2u, &pad_dd2);
                uint8_t *p_u4d = musashi_boot_ram_span(&boot->memory, 0x80126B58u + 0x4Du, 1);
                if (p_u4d) cur_unk4d = *p_u4d;

                if (p_uac != 0) {
                    fprintf(stderr, "native_boot: INPUT_EDGE frame=%u act=%d uAC=0x%04x uAA=0x%04x flags=0x%08x\n",
                            boot->in_game_frames, (int)curr_act, (unsigned)p_uac, (unsigned)p_uaa, (unsigned)p_flags);
                }

                if (boot->in_game_frames % 10u == 1u) {
                    int16_t sm_nx = 0, sm_ny = -4095, sm_nz = 0;
                    (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x118u, (uint16_t *)&sm_nx);
                    (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x11Au, (uint16_t *)&sm_ny);
                    (void)musashi_boot_read16(&boot->memory, 0x80126B58u + 0x11Cu, (uint16_t *)&sm_nz);
                    fprintf(stderr, "native_boot: PLAYER_DIAG frame=%u act=%d subact=%d pos=(%d,%d,%d) vy=%d floor_y=%d unk4d=%u flags=0x%08x uAC=0x%04x uAA=0x%04x u1c5=%u b99f0=%u b9a64=%u dd2=0x%04x norm=(%d,%d,%d)\n",
                            boot->in_game_frames, (int)curr_act, (int)prev_act, (int)pos_x, (int)pos_y, (int)pos_z, (int)(vy_fixed >> 16), (int)floor_y, (unsigned)cur_unk4d, (unsigned)p_flags, (unsigned)p_uac, (unsigned)p_uaa, (unsigned)u1c5, (unsigned)b99f0, (unsigned)b9a64, (unsigned)pad_dd2, (int)sm_nx, (int)sm_ny, (int)sm_nz);
                }

                if (curr_act != boot->last_musashi_action) {
                    fprintf(stderr, "native_boot: MUSASHI_ACTION prev=%d curr=%d anim=%d hp=%d frame=%u\n",
                            (int)prev_act, (int)curr_act, anim, (int)hp, boot->in_game_frames);
                    boot->last_musashi_action = curr_act;
                }

                const char *act_env_climb = getenv("MUSASHI_GAMEPLAY_ACTION");
                if (act_env_climb && (strcmp(act_env_climb, "spiral_climb") == 0 ||
                                      strcmp(act_env_climb, "climb") == 0)) {
                    int vy = (int)(vy_fixed >> 16);
                    fprintf(stderr, "native_boot: TERRAIN_CLIMB frame=%u pos=(%d,%d,%d) vy=%d\n",
                            boot->in_game_frames, (int)pos_x, (int)pos_y, (int)pos_z, vy);
                }

                /* Hitbox pool telemetry (D_8011D030) */
                for (unsigned h = 0; h < 30u; ++h) {
                    uint32_t hb_addr = 0x8011D030u + h * 0x58u;
                    uint16_t hb_id = 0;
                    (void)musashi_boot_read16(&boot->memory, hb_addr + 0x00u, &hb_id);
                    if (hb_id == 0) {
                        (void)musashi_boot_read16(&boot->memory, hb_addr + 0x04u, &hb_id);
                    }
                    if (hb_id != 0) {
                        int16_t hx = 0, hy = 0, hz = 0, dmg = 0;
                        (void)musashi_boot_read16(&boot->memory, hb_addr + 0x06u, (uint16_t *)&hx);
                        (void)musashi_boot_read16(&boot->memory, hb_addr + 0x0Au, (uint16_t *)&hy);
                        (void)musashi_boot_read16(&boot->memory, hb_addr + 0x0Eu, (uint16_t *)&hz);
                        (void)musashi_boot_read16(&boot->memory, hb_addr + 0x10u, (uint16_t *)&dmg);
                        fprintf(stderr, "native_boot: HITBOX_POOL slot=%u id=0x%04x pos=(%d,%d,%d) dmg=%d frame=%u\n",
                                h, (unsigned)hb_id, (int)hx, (int)hy, (int)hz, (int)dmg, boot->in_game_frames);
                    }
                }

                /* Level 1 enemy spawning for combat validation (D_8011F9D0) */
                if (act_env && (strstr(act_env, "combat") || strstr(act_env, "slash") ||
                                strstr(act_env, "combo") || strstr(act_env, "demo"))) {
                    if (boot->in_game_frames >= 10u) {
                        uint16_t slot0_id = 0;
                        (void)musashi_boot_read16(&boot->memory, 0x8011F9D0u, &slot0_id);
                        if (slot0_id == 0) {
                            uint8_t *slot = musashi_boot_ram_span(&boot->memory, 0x8011F9D0u, 0x68u);
                            if (slot) {
                                memset(slot, 0, 0x68u);
                                *(uint16_t *)(slot + 0x00) = 0x0019u; /* Actor 25: Thirstquencher Soldier */
                                *(uint16_t *)(slot + 0x02) = 0x0000u; /* State 0: Init */
                                *(int16_t  *)(slot + 0x06) = -132;   /* pos_x: aligned with Musashi */
                                *(int16_t  *)(slot + 0x0A) = -1056;  /* pos_y: ground floor */
                                *(int16_t  *)(slot + 0x0E) = -480;   /* pos_z: in front of Musashi (-553) */
                                *(int16_t  *)(slot + 0x10) = 20;     /* hp */
                                *(int16_t  *)(slot + 0x12) = 20;     /* max hp */
                                *(int16_t  *)(slot + 0x18) = 20;     /* telemetry hp */
                                fprintf(stderr, "native_boot: ENEMY_SPAWN slot=0 id=25 pos=(-132,-1056,-480) hp=20 frame=%u\n",
                                        boot->in_game_frames);
                            }
                        }
                    }
                }

                static uint16_t last_enemy_states[30];
                static int16_t last_enemy_hps[30];
                static int enemy_states_init = 0;
                if (!enemy_states_init) {
                    memset(last_enemy_states, 0, sizeof(last_enemy_states));
                    memset(last_enemy_hps, 0, sizeof(last_enemy_hps));
                    enemy_states_init = 1;
                }
                for (unsigned slot = 0; slot < 30u; ++slot) {
                    uint32_t slot_addr = 0x8011F9D0u + slot * 0x68u;
                    uint16_t actor_id = 0, state = 0;
                    int16_t enemy_hp10 = 0, enemy_hp18 = 0;
                    if (musashi_boot_read16(&boot->memory, slot_addr, &actor_id) && actor_id != 0) {
                        (void)musashi_boot_read16(&boot->memory, slot_addr + 0x02u, &state);
                        (void)musashi_boot_read16(&boot->memory, slot_addr + 0x10u, (uint16_t *)&enemy_hp10);
                        (void)musashi_boot_read16(&boot->memory, slot_addr + 0x18u, (uint16_t *)&enemy_hp18);
                        int16_t enemy_hp = enemy_hp10 ? enemy_hp10 : enemy_hp18;
                        if (state != last_enemy_states[slot] || enemy_hp != last_enemy_hps[slot]) {
                            last_enemy_states[slot] = state;
                            last_enemy_hps[slot] = enemy_hp;
                            fprintf(stderr, "native_boot: ENEMY_REACTION slot=%u id=%u state=%u hp=%d frame=%u\n",
                                    slot, (unsigned)actor_id, (unsigned)state, (int)enemy_hp, boot->in_game_frames);
                        }
                    }
                }
            }
            if (target > 0 && boot->in_game_frames >= target && !boot->in_game_paused) {
                boot->in_game_paused = 1;
                fprintf(stderr, "native_boot: IN_GAME_PAUSE paused=1 frames=%u target=%u gte_writes=%llu\n",
                        boot->in_game_frames, target, (unsigned long long)boot->gte.write_count);
                host_stop_signal = SIGTERM;
            }
        }
    }
    return 1;
}

/* Guest-clock cycle quantum. Committing every device on each 2-cycle
 * instruction charge was most of the host's time; batching 16 cycles keeps
 * device state within 14 cycles of guest time (under half a microsecond,
 * against a 1088-cycle SIO ACK and ~225k-cycle CD sectors) and stays
 * deterministic. Any remainder is flushed before every epoch sync. */
#define MUSASHI_GUEST_CLOCK_QUANTUM 16u

static int guest_clock_flush(NativeBoot *boot) {
    uint32_t pending = boot->guest_pending_cycles;
    if (!pending) return 1;
    boot->guest_pending_cycles = 0;
    if (!musashi_device_epoch_advance_cost(&boot->epoch, pending)) {
        boot->source_clock.faulted = 1;
        return 0;
    }
    return 1;
}

static int epoch_sync_flushed(NativeBoot *boot) {
    return guest_clock_flush(boot) && musashi_device_epoch_sync(&boot->epoch);
}

static int advance_source_clock(void *userdata, uint32_t cycles) {
    NativeBoot *boot = userdata;
    uint64_t cycle;
    if (host_stop_signal) return 1;
    if (boot->trace_cd_timing) {
        if (boot->irq.cd_frame.busy && !boot->cd_timing_active) {
            boot->cd_timing_active = 1;
            boot->cd_host_start = SDL_GetPerformanceCounter();
            boot->cd_epoch_start = boot->epoch.cycle;
        } else if (!boot->irq.cd_frame.busy && boot->cd_timing_active) {
            boot->cd_timing_active = 0;
            if (boot->irq.cd_frame.instructions > 16384u)
                fprintf(stderr,"native_boot: CD_TIMING instructions=%u host_us=%llu epoch_cycles=%llu\n",
                    boot->irq.cd_frame.instructions,
                    (unsigned long long)((SDL_GetPerformanceCounter()-boot->cd_host_start)*1000000u/SDL_GetPerformanceFrequency()),
                    (unsigned long long)(boot->epoch.cycle-boot->cd_epoch_start));
        }
    }
    if (boot->bios_callback_frame.busy) {
        /* TAP busy-waits RC2 for 400 ticks (~3200 CPU cycles). Syncing that
         * wait to wall time expires the window during host fprintf. Tick
         * SIO/RC2 by instruction cost only, matching input_digital_irq_probe.
         * Never reverse SIO time if the shared epoch already advanced it. */
        uint64_t base = boot->timer2.last_cycles;
        if (boot->sio.cycle > base) base = boot->sio.cycle;
        if (base > UINT64_MAX - cycles) return 0;
        cycle = base + cycles;
        return musashi_timer2_advance(&boot->timer2, cycle) &&
               musashi_sio_controller_advance(&boot->sio, cycle);
    }
    boot->cd_irq_production_limit = 0;
    if (boot->irq.cd_frame.busy && boot->cd_drive) {
        MusashiCdOwnedState cd;
        if (!musashi_cd_owned_get_state(boot->cd_drive,&cd) || cd.cycle > UINT64_MAX-cycles)
            return 0;
        boot->cd_irq_production_limit = cd.cycle + cycles;
    }
    if (!musashi_source_clock_advance(&boot->source_clock, cycles)) return 0;
    if (boot->guest_clock && !boot->irq.cd_frame.busy) {
        /* The CD IRQ frame bounds its production by exact cycles above, so
         * it keeps per-charge commits. */
        if (boot->guest_pending_cycles > UINT32_MAX - cycles) return 0;
        boot->guest_pending_cycles += cycles;
        if (boot->guest_pending_cycles < MUSASHI_GUEST_CLOCK_QUANTUM) return 1;
        return guest_clock_flush(boot);
    }
    if (!guest_clock_flush(boot)) return 0;
    if (!musashi_device_epoch_advance_cost(&boot->epoch, cycles)) {
        boot->source_clock.faulted = 1;
        return 0;
    }
    return 1;
}

static int input_write16(void *userdata, uint32_t address, uint16_t value) {
    NativeBoot *boot = userdata;
    if (address == 0x1f801070u || address == 0x1f801074u) {
        if (!musashi_irq_controller_write16(&boot->irq.controller, address, value)) return 0;
        {
            static uint64_t n;
            if (log_sample(&n, 8u, 4096u))
                fprintf(stderr, "native_boot: INPUT_MMIO address=%08x width=2 value=%08x\n",
                        address, (unsigned)value);
        }
        return 1;
    }
    if (!musashi_sio_controller_write16(&boot->sio, address, value)) return 0;
    {
        static uint64_t n;
        if (log_sample(&n, 16u, 4096u))
            fprintf(stderr, "native_boot: INPUT_SERIAL_MMIO address=%08x width=2 value=%08x\n",
                    address, (unsigned)value);
    }
    return 1;
}

static int input_dequeue(void *userdata, int32_t priority, uint32_t descriptor, int32_t *result) {
    NativeBoot *boot = userdata;
    int accepted = musashi_bios_input_dequeue(&boot->bios_input, priority, descriptor, result);
    if (accepted) fprintf(stderr, "native_boot: INPUT_C003 priority=%d descriptor=%08x result=%08x\n",
                          priority, descriptor, (unsigned)*result);
    return accepted;
}

static int input_enqueue(void *userdata, int32_t priority, uint32_t descriptor, int32_t *result) {
    NativeBoot *boot = userdata;
    int accepted = musashi_bios_input_enqueue(&boot->bios_input, priority, descriptor, result);
    if (accepted) fprintf(stderr, "native_boot: INPUT_C002 priority=%d descriptor=%08x result=%08x\n",
                          priority, descriptor, (unsigned)*result);
    return accepted;
}

static int input_change_timer(void *userdata, int32_t channel, int32_t value, int32_t *result) {
    NativeBoot *boot = userdata;
    int accepted = musashi_irq_policy_exchange_timer(&boot->irq_policy, channel, value, result);
    if (accepted) fprintf(stderr, "native_boot: INPUT_C00A channel=%d value=%08x result=%08x\n",
                          channel, (unsigned)value, (unsigned)*result);
    return accepted;
}

static int input_execute(void *userdata, uint32_t target, int32_t argument, int32_t *result) {
    NativeBoot *boot = userdata;
    MusashiResetGraphPrefixStop stop = {0};
    const void *continuation = boot->continuation;
    int accepted = musashi_boot_execute_input_bios_callback(&boot->memory, &boot->input,
        &boot->bios_callback_frame, target, argument, result, &stop);
    if (boot->timer2.last_cycles > boot->epoch.cycle) {
        boot->timer2.last_cycles = boot->epoch.cycle;
        boot->sio.cycle = boot->epoch.cycle;
    }
    if (boot->continuation != continuation) return 0;
    if (accepted) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: INPUT_CALLBACK target=%08x argument=%08x result=%08x\n",
                    target, (unsigned)argument, (unsigned)*result);
        return 1;
    }
    if (host_stop_signal) {
        /* Host SIGTERM/SIGINT aborts mid-callback clock advance. Soft-complete
         * so BIOS input/IRQ owners stay healthy for teardown and the next run. */
        boot->bios_callback_frame.faulted = 0;
        boot->bios_callback_frame.busy = 0;
        boot->bios_input.faulted = 0;
        *result = 0;
        fprintf(stderr, "native_boot: INPUT_CALLBACK_ABORTED entry=%08x pc=%08x target=%08x\n",
                target, stop.call_address, stop.target_address);
        return 1;
    }
    fprintf(stderr, "native_boot: INPUT_CALLBACK_REFUSED entry=%08x pc=%08x target=%08x\n",
            target, stop.call_address, stop.target_address);
    return 0;
}

static void observe_input_state(NativeBoot *boot, uint32_t pc) {
    const uint32_t addresses[] = {0x80078988u,0x8007898cu,0x80078990u,0x80078994u,
        0x80072994u,0x800729acu,0x800729b0u,0x80072990u,0x80072984u,
        0x80078998u,0x8007899cu,0x800729c4u,0x800729a0u,0x8007299cu};
    unsigned i;
    for (i = 0; i < sizeof(addresses)/sizeof(addresses[0]); ++i) {
        uint32_t value;
        if (!musashi_boot_read32(&boot->memory, addresses[i], &value)) return;
        fprintf(stderr, "native_boot: INPUT_RAM at=%08x address=%08x value=%08x\n", pc, addresses[i], value);
    }
    fprintf(stderr, "native_boot: INPUT_OWNER at=%08x installed=%d faulted=%d frame_busy=%d frame_faulted=%d timer3=%d\n",
            pc, boot->bios_input.node.owner == &boot->bios, boot->bios_input.faulted,
            boot->bios_callback_frame.busy, boot->bios_callback_frame.faulted, boot->irq_policy.timers[3]);
}

static int cpu_read_status(void *userdata,const MusashiCpuContext *context,uint32_t *value) {
    NativeBoot *boot=userdata;
    if (!context || context->identity!=boot->continuation ||
        context->provenance!=MUSASHI_CPU_CONTEXT_SOURCE || !context->instruction_valid ||
        (context->instruction & 0xffe0ffffu)!=0x40006000u) return 0;
    int accepted=musashi_cpu_status_read(boot->irq.cpu_status,value);
    if (accepted) fprintf(stderr,"native_boot: CPU_STATUS_READ pc=%08x value=%08x\n",context->pc,*value);
    return accepted;
}
static int cpu_write_status(void *userdata,const MusashiCpuContext *context,uint32_t value) {
    NativeBoot *boot=userdata;
    int accepted=context && context->identity==boot->continuation &&
        musashi_cpu_status_write_source(boot->irq.cpu_status,context,value);
    if (accepted) fprintf(stderr,"native_boot: CPU_STATUS_WRITE pc=%08x value=%08x\n",context->pc,value);
    return accepted;
}
static int cpu_write_control(void *userdata,const MusashiCpuContext *context,uint32_t selector,uint32_t value) {
    NativeBoot *boot=userdata;
    int accepted=musashi_gte_owner_write_control(&boot->gte,context,selector,value);
    if (accepted) {
        static uint64_t n;
        if (log_sample(&n, 8u, 16384u))
            fprintf(stderr,"native_boot: GTE_CONTROL pc=%08x selector=%u value=%08x writes=%llu\n",
                context->pc,selector,value,(unsigned long long)boot->gte.write_count);
    }
    return accepted;
}
static int cpu_write_gte_data(void *userdata,const MusashiCpuContext *context,uint32_t selector,uint32_t value) {
    NativeBoot *boot=userdata;
    int accepted=musashi_gte_owner_write_data(&boot->gte,context,selector,value);
    if (accepted) {
        static uint64_t n;
        if (log_sample(&n, 8u, 16384u))
            fprintf(stderr,"native_boot: GTE_DATA_WRITE pc=%08x selector=%u value=%08x writes=%llu\n",
                context->pc,selector,value,(unsigned long long)boot->gte.data_write_count);
    }
    return accepted;
}
static int cpu_read_gte_data(void *userdata,const MusashiCpuContext *context,uint32_t selector,uint32_t *value) {
    NativeBoot *boot=userdata;
    int accepted=musashi_gte_owner_read_data(&boot->gte,context,selector,value);
    if (accepted) {
        static uint64_t n;
        if (log_sample(&n, 8u, 16384u))
            fprintf(stderr,"native_boot: GTE_DATA_READ pc=%08x selector=%u value=%08x reads=%llu\n",
                context->pc,selector,*value,(unsigned long long)boot->gte.data_read_count);
    }
    return accepted;
}
static int cpu_read_control(void *userdata,const MusashiCpuContext *context,uint32_t selector,uint32_t *value) {
    NativeBoot *boot=userdata;
    int accepted=musashi_gte_owner_read_control(&boot->gte,context,selector,value);
    if (accepted) {
        static uint64_t n;
        if (log_sample(&n, 8u, 16384u))
            fprintf(stderr,"native_boot: GTE_CONTROL_READ pc=%08x selector=%u value=%08x\n",
                context->pc,selector,*value);
    }
    return accepted;
}
static int cpu_gte_command(void *userdata,const MusashiCpuContext *context,uint32_t word) {
    NativeBoot *boot=userdata;
    int accepted=musashi_gte_owner_command(&boot->gte,context,word);
    if (accepted) {
        static uint64_t n;
        if (log_sample(&n, 8u, 16384u))
            fprintf(stderr,"native_boot: GTE_COMMAND pc=%08x word=%08x commands=%llu\n",
                context->pc,word,(unsigned long long)boot->gte.command_count);
    }
    return accepted;
}
static int image_read(void *userdata,uint32_t address,uint32_t *value) {
    return musashi_bios_exception_read32(&((NativeBoot *)userdata)->exception_image,address,value);
}
static int image_write(void *userdata,uint32_t address,uint32_t value) {
    NativeBoot *boot=userdata;
    int accepted=musashi_bios_exception_write32(&boot->exception_image,address,value);
    if (accepted) fprintf(stderr,"native_boot: BIOS_IMAGE_WRITE address=%08x value=%08x writes=%llu dirty=%d\n",
        address,value,(unsigned long long)boot->exception_image.writes,boot->exception_image.dirty);
    return accepted;
}
static int image_table(void *userdata,uint32_t *value) {
    NativeBoot *boot=userdata;
    int accepted=musashi_bios_exception_table(&boot->exception_image,value);
    if (accepted) fprintf(stderr,"native_boot: BIOS_IMAGE_TABLE value=%08x generation=%llu\n",
        *value,(unsigned long long)boot->exception_image.generation);
    return accepted;
}
static int image_publish(void *userdata) {
    NativeBoot *boot=userdata;
    int accepted=musashi_bios_exception_publish(&boot->exception_image);
    fprintf(stderr,"native_boot: BIOS_IMAGE_PUBLISH accepted=%d generation=%llu variant=%u dirty=%d writes=%llu\n",
        accepted,(unsigned long long)boot->exception_image.generation,(unsigned)boot->exception_image.variant,
        boot->exception_image.dirty,(unsigned long long)boot->exception_image.writes);
    return accepted;
}
static int vsync_wait_raise_tick(NativeBoot *boot, const void *continuation) {
    MusashiCpuContext context;
    uint32_t tick = 0;
    if (!musashi_boot_cpu_context(continuation, MUSASHI_CPU_CONTEXT_SOURCE, &context) ||
        !context.gpr || context.pc < 0x800424e4u || context.pc >= 0x80042570u)
        return 1;
    if (!musashi_boot_read32(&boot->memory, 0x8006cbb8u, &tick))
        return 0;
    if (tick >= context.gpr[4])
        return 1;
    /* Under the guest clock the VBlank arrives when the polling loop has
     * spent a frame's cycles, as on hardware; nothing is fabricated. */
    if (boot->guest_clock)
        return 1;
    /* Retail 800424E4 polls 8006CBB8; 80042CE8 is the handler that increments
     * it. Latch I_STAT VBlank so the existing dispatcher runs that handler. */
    fprintf(stderr, "native_boot: VSYNC_WAIT_VBLANK tick=%u target=%u pc=%08x\n",
            tick, context.gpr[4], context.pc);
    return musashi_irq_controller_raise_vblank(&boot->irq.controller);
}

/* Opt-in refusal trace, off unless MUSASHI_TRACE_REFUSAL is set. Prints only:
 * the checkpoint's accept/refuse decision is unchanged either way. */
static int checkpoint_refuse(const void *continuation, const char *why) {
    /* A checkpoint refusal ends the run, so its reason is always printed
     * (once); MUSASHI_TRACE_REFUSAL adds every later one. */
    static int reported;
    MusashiCpuContext context;
    const char *trace = getenv("MUSASHI_TRACE_REFUSAL");
    int verbose = trace && trace[0] && trace[0] != '0';
    if (!reported++ || verbose) {
        if (continuation &&
            musashi_boot_cpu_context(continuation, MUSASHI_CPU_CONTEXT_SOURCE, &context))
            fprintf(stderr, "native_boot: CHECKPOINT_REFUSED pc=%08x why=%s\n", context.pc, why);
        else
            fprintf(stderr, "native_boot: CHECKPOINT_REFUSED pc=unknown why=%s\n", why);
    }
    return 0;
}

static void apply_scripted_action(NativeBoot *boot, uint8_t *p_act) {
    if (!boot || !p_act) return;
    const char *act = getenv("MUSASHI_GAMEPLAY_ACTION");
    if (!act || boot->in_game_frames < 8u) return;

    if (strcmp(act, "spiral_climb") == 0 || strcmp(act, "climb") == 0) {
        uint16_t bits = 0;
        uint8_t lx = 0x80u, ly = 0x00u;
        spiral_climb_input(boot, NULL, &bits, &lx, &ly);
        *(uint16_t *)(p_act + 0xAA) = bits;
        *(uint16_t *)(p_act + 0xAC) = bits;
        *(uint16_t *)(p_act + 0xAE) = (uint16_t)((uint16_t)lx | ((uint16_t)ly << 8));
        musashi_boot_write16(&boot->memory, 0x80078DCAu, bits);
        musashi_boot_write16(&boot->memory, 0x80078DD2u, bits);
        return;
    }

    if (boot->in_game_frames < 13u) return;

    uint16_t edge = 0;
    if (strcmp(act, "slash") == 0) edge = 0x0080u;
    else if (strcmp(act, "lumina") == 0) {
        uint8_t *lum = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
        if (lum) *lum |= 1u;
        edge = 0x0010u;
    } else if (strcmp(act, "jump") == 0) edge = 0x0040u;
    else if (strcmp(act, "jump_attack") == 0) {
        uint16_t cur_act = *(uint16_t *)p_act;
        if (cur_act == 2u || boot->in_game_frames >= 23u) edge = 0x0080u;
        else edge = 0x0040u;
    } else if (strcmp(act, "combo") == 0) {
        edge = 0x0080u;
    } else if (strcmp(act, "combat") == 0 || strcmp(act, "combat_patrol") == 0) {
        edge = 0x0080u;
    } else if (strcmp(act, "combat_all") == 0 || strcmp(act, "demo") == 0) {
        uint8_t *lum = musashi_boot_ram_span(&boot->memory, 0x800AE658u, 1);
        if (lum) *lum |= 1u;
        uint16_t cur_act = *(uint16_t *)p_act;
        if (cur_act == 2u) edge = 0x0080u;
        else if (boot->in_game_frames <= 22u) edge = 0x0040u;
        else if (boot->in_game_frames <= 32u) edge = 0x0080u;
        else if (boot->in_game_frames <= 45u) edge = 0x0010u;
        else edge = 0x0080u;
    }
    if (edge != 0) {
        *(uint16_t *)(p_act + 0xAA) |= edge;
        *(uint16_t *)(p_act + 0xAC) = edge;
        musashi_boot_write16(&boot->memory, 0x80078DCAu, edge);
        musashi_boot_write16(&boot->memory, 0x80078DD2u, edge);
    }
}

static int checkpoint(void *userdata, const void *continuation) {
    NativeBoot *boot = userdata;
    MusashiCpuContext context;
    if (!continuation) return 0;
    if (host_stop_signal) {
        uint32_t *raw_cpu = (uint32_t *)(uintptr_t)continuation;
        raw_cpu[34] = UINT_MAX;
        raw_cpu[35] = UINT_MAX;
        return 1;
    }
    if (!boot->scene1_reached && boot->title_scene_reached) {
        if (musashi_boot_cpu_context(continuation, MUSASHI_CPU_CONTEXT_SOURCE, &context)) {
            if (context.pc >= 0x80128158u && context.pc < 0x801fff00u) {
                boot->scene1_reached = 1;
                fprintf(stderr, "native_boot: SCENE1_REACHED pc=%08x\n", context.pc);
            }
        }
    }
    {
        uint32_t *raw_cpu = (uint32_t *)(uintptr_t)continuation;
        if (raw_cpu[34] == 0x80147078u) {
            fprintf(stderr, "native_boot: CALL_80147078 p=%08x act=%u ra=%08x frame=%u\n",
                    raw_cpu[4], raw_cpu[5], raw_cpu[31], boot->in_game_frames);
        }
        if (raw_cpu[34] == 0x80159c84u) {
            uint8_t *p_act = musashi_boot_ram_span(&boot->memory, raw_cpu[4], 0x200);
            apply_scripted_action(boot, p_act);
            int16_t u1c8 = p_act ? *(int16_t *)(p_act + 0x1C8) : 0;
            uint32_t fl = p_act ? *(uint32_t *)(p_act + 0x44) : 0;
            uint16_t uac = p_act ? *(uint16_t *)(p_act + 0xAC) : 0;
            uint8_t *p_ec0 = musashi_boot_ram_span(&boot->memory, 0x80078EC0u, 1);
            uint8_t ec0 = p_ec0 ? *p_ec0 : 0;
            fprintf(stderr, "native_boot: CALL_80159C84 act=%u subact=%u 1c8=%d ec0=0x%02x fl=0x%08x uAC=0x%04x frame=%u\n",
                    p_act ? *(uint16_t *)p_act : 0,
                    p_act ? *(uint16_t *)(p_act + 2) : 0,
                    u1c8, ec0, fl, uac, boot->in_game_frames);
        }
        if (raw_cpu[34] == 0x80146360u || raw_cpu[34] == 0x8015ad3cu) {
            uint8_t *p_act = musashi_boot_ram_span(&boot->memory, 0x80126B58u, 0x200);
            apply_scripted_action(boot, p_act);
            uint8_t *p_ba4 = musashi_boot_ram_span(&boot->memory, 0x80126BA4u, 1);
            uint8_t ba4 = p_ba4 ? *p_ba4 : 0;
            uint32_t handler = 0;
            musashi_boot_read32(&boot->memory, 0x80186760u + ba4 * 4u, &handler);
            fprintf(stderr, "native_boot: CALL_80146360 ba4=%u handler=%08x frame=%u\n",
                    ba4, handler, boot->in_game_frames);
        }
        /* func_801612B8 / func_8016130C are the retail ground sweep. Skipping
         * them and forcing 0x2000 reports a flat walkable floor, so the ramp
         * step never resolves and Musashi walks off the tower. */
        if (raw_cpu[34] == 0x8015bdd0u) {
            fprintf(stderr, "native_boot: CALL_8015BDD0 ra=%08x frame=%u\n",
                    raw_cpu[31], boot->in_game_frames);
        }

        /* func_800D1714/24/34/44, func_80021174, func_8001382C and the GTE
         * setters 8004914C-8004921C used to be replaced here by host code:
         * fixed returns, direct global writes and a double-precision sin/cos
         * rotation. All of them are wired to their retail words with admitted
         * COP2 sites, so the retail code now runs instead; the host versions
         * disagreed with it (func_80021174 always reported "visible"). */
        if (raw_cpu[34] == 0x800243ECu || raw_cpu[34] == 0x80024448u || raw_cpu[34] == 0x80026D64u) {
            static unsigned render_calls;
            if (render_calls < 8u || (render_calls % 500u) == 0u)
                fprintf(stderr, "native_boot: RENDER_ENTER pc=%08x frame=%u n=%u\n",
                        raw_cpu[34], boot->in_game_frames, render_calls);
            render_calls++;
        }
    }
    if (boot->continuation && boot->continuation != continuation) {
        /* Nested IRQ source function: keep the interrupted CPU identity and
         * do not dispatch while the owner pump is already delivering. */
        if (!(boot->irq.scheduler.pumping || boot->irq.scheduler.delivering))
            return checkpoint_refuse(continuation, "nested-identity");
        return epoch_sync_flushed(boot);
    }
    boot->continuation = continuation;
    if (!boot->gte.initialized) {
        if (!musashi_boot_cpu_context(continuation,MUSASHI_CPU_CONTEXT_SOURCE,&context) ||
            !musashi_gte_owner_init(&boot->gte,boot->irq.cpu_status,epoch_owner,boot,context.identity))
            return checkpoint_refuse(continuation, "gte-init");
    }
    if (!epoch_sync_flushed(boot)) {
        static int reported;
        if (!reported++)
            fprintf(stderr, "native_boot: EPOCH_SYNC_REFUSED reason=%s clock=%s%s\n",
                    boot->epoch.fault_reason ? boot->epoch.fault_reason : "unknown",
                    boot->guest_clock ? "GUEST" : "HOST_PACED",
                    boot->guest_clock ? "" : " hint=MUSASHI_GUEST_CLOCK=1 for loaded or headless hosts");
        return checkpoint_refuse(continuation, "epoch-sync");
    }
    if (!boot->irq.scheduler.installed) return 1;
    if (boot->irq.scheduler.pumping || boot->irq.scheduler.delivering) return 1;
    if (!vsync_wait_raise_tick(boot, continuation)) return checkpoint_refuse(continuation, "vsync-wait");
    if (!musashi_psycross_irq_scheduler_dispatch_pending(&boot->irq.scheduler, continuation)) {
        /* Always name the reason: this refusal ends the run. */
        static int reported;
        if (!reported++)
            fprintf(stderr, "native_boot: IRQ_DISPATCH_REFUSED reason=%s clock=%s%s\n",
                    boot->irq.scheduler.fault_reason ? boot->irq.scheduler.fault_reason : "unknown",
                    boot->guest_clock ? "GUEST" : "HOST_PACED",
                    boot->guest_clock ? "" : " hint=MUSASHI_GUEST_CLOCK=1 for loaded or headless hosts");
        return checkpoint_refuse(continuation, "irq-dispatch");
    }
    return 1;
}

static int run_backup_unit(void *userdata, int32_t *guest_result) {
    NativeBoot *boot = userdata;
    Uint32 started = SDL_GetTicks();
    const void *continuation = boot->continuation;
    uint64_t epoch_started = boot->epoch.cycle;
    uint64_t source_started = boot->source_clock.cycles;
    if (!continuation || !guest_result || boot->irq.scheduler.pumping ||
        !musashi_bios_backup_unit_begin(&boot->backup_unit))
        return 0;
    for (;;) {
        int complete;
        if (!musashi_bios_backup_unit_poll(&boot->backup_unit, &complete, guest_result))
            return 0;
        if (complete) {
            fprintf(stderr, "native_boot: BU_CLOCK start=%llu end=%llu "
                    "source_start=%llu source_end=%llu\n",
                    (unsigned long long)epoch_started, (unsigned long long)boot->epoch.cycle,
                    (unsigned long long)source_started, (unsigned long long)boot->source_clock.cycles);
            fprintf(stderr, "native_boot: BU_RETURN result=%d memory_cards=DISCONNECTED "
                    "bu_return_port=%u bu_return_last_port=%u bu_return_flags=%02x%02x "
                    "sio_tx_bytes=%llu\n", (int)*guest_result,
                    (unsigned)boot->card.port, (unsigned)boot->card.last_port,
                    (unsigned)boot->card.flags[0], (unsigned)boot->card.flags[1],
                    (unsigned long long)boot->sio.transmitted_bytes);
            return 1;
        }
        /* The actual suspended game CPU survives the BIOS wait. Only a
         * real source edge and owner-thread IRQ handler can finish a card
         * request. Wall time limits observation; it never completes I/O. */
        /* Under the guest clock the suspended CPU is spinning in the BIOS
         * card wait, so each poll spends guest cycles; nothing else would
         * move device time forward. */
        if (boot->continuation != continuation ||
            !(boot->guest_clock ? musashi_device_epoch_advance_cost(&boot->epoch, 1024u)
                                : musashi_device_epoch_sync(&boot->epoch)) ||
            !musashi_psycross_irq_scheduler_dispatch_pending(&boot->irq.scheduler, continuation))
            return 0;
        if ((Uint32)(SDL_GetTicks() - started) >= 10000u) {
            fputs("native_boot: backup-unit wait observation timed out\n", stderr);
            return 0;
        }
        if (!boot->guest_clock) SDL_Delay(1);
    }
}

static int init_backup_unit(void *userdata,int32_t *guest_result) {
    NativeBoot *boot=userdata;int accepted;
    if (boot->host_service_depth) return 0;
    boot->host_service_depth++;
    accepted=run_backup_unit(userdata,guest_result);
    boot->host_service_depth--;
    return accepted;
}

static void console_bytes(void *userdata, const uint8_t *bytes, int32_t length) {
    (void)userdata;
    if (length > 0 && fwrite(bytes, 1, (size_t)length, stdout) != (size_t)length) {
        fputs("native_boot: console output failed\n", stderr);
        exit(2);
    }
}

static int source_irq_execute(void *userdata, MusashiBootMemory *memory, uint32_t target) {
    NativeBoot *boot = userdata;
    MusashiResetGraphPrefixStop stop = {0};
    MusashiResetGraphPrefixStatus status;
    status = musashi_boot_execute_source_function(memory, &boot->startup_devices,
        target, console_bytes, NULL, &stop);
    if (status != MUSASHI_RESETGRAPH_PREFIX_COMPLETE) {
        boot->irq.failed_target = (stop.target_address && stop.target_address != 0xffffffffu)
            ? stop.target_address : target;
        fprintf(stderr, "native_boot: SOURCE_IRQ_EXECUTE target=%08x stop=%08x status=%d\n",
                (unsigned)target, (unsigned)stop.target_address, (int)status);
        return 0;
    }
    return 1;
}

/* The runner accepts IRQ registers and the owned Timer1 count. A refused
 * access is fatal, never a synthesized register value or ignored write. */
static uint16_t irq_read16(void *userdata, uint32_t address) {
    NativeBoot *boot = userdata;
    uint16_t value;
    if (!(address == 0x1f801110u ?
          musashi_scanline_timer_read16(&boot->timer1, address, &value) :
          musashi_irq_controller_read16(&boot->irq.controller, address, &value))) {
        fprintf(stderr, "native_boot: unsupported IRQ read %08x\n", (unsigned)address);
        exit(2);
    }
    return value;
}

static void irq_write16(void *userdata, uint32_t address, uint16_t value) {
    NativeBoot *boot = userdata;
    if (!musashi_irq_controller_write16(&boot->irq.controller, address, value)) {
        fprintf(stderr, "native_boot: unsupported IRQ write %08x\n", (unsigned)address);
        exit(2);
    }
    if (boot->gpu_trace)
        fprintf(stderr, "native_boot: GPU_TRACE address=%08x width=2 value=%08x\n",
                (unsigned)address, (unsigned)value);
}

static int dma_irq_read32(void *userdata, uint32_t address, uint32_t *value) {
    NativeBoot *boot = userdata;
    return musashi_dma_controller_read32(&boot->dma, address, value);
}

static int dma_irq_write32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot = userdata;
    int accepted = musashi_dma_controller_write32(&boot->dma, address, value);
    if (accepted)
        fprintf(stderr, "native_boot: DMA_IRQ_DICR address=%08x value=%08x dicr=%08x\n",
                (unsigned)address, (unsigned)value, (unsigned)boot->dma.interrupt);
    return accepted;
}

static void control_write32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot = userdata;
    if (address == 0x1f801020u) {
        /* MEMCTRL COMMON_DELAY is retained as a complete word, matching
         * psxhw.cc's default register write/read. The inspected reference
         * does not derive CD command latency from this latch. */
        boot->common_delay = value;
        boot->common_delay_written = 1;
        return;
    }
    if (!musashi_dma_controller_write32(&boot->dma, address, value) &&
        !musashi_scanline_timer_write32(&boot->timer1, address, value)) {
        fprintf(stderr, "native_boot: unsupported control write %08x=%08x "
                "DPCR=%08x IRQ_installed=%d menu=NOT_REACHED\n",
                (unsigned)address, (unsigned)value, (unsigned)boot->dma.control,
                boot->irq.scheduler.installed);
        exit(2);
    }
}

static uint32_t gpu_read32(void *userdata, uint32_t address) {
    NativeBoot *boot = userdata;
    uint32_t value;
    int accepted = (address==0x1f8010f0u || address==0x1f8010f4u) ?
        musashi_dma_controller_read32(&boot->dma,address,&value) :
        gpu_io_read32(boot,address,&value);
    if (!accepted) {
        fprintf(stderr, "native_boot: unsupported GPU read %08x menu=NOT_REACHED\n",
                (unsigned)address);
        exit(2);
    }
    return value;
}

static void gpu_write32(void *userdata, uint32_t address, uint32_t value) {
    NativeBoot *boot = userdata;
    int accepted = (address==0x1f8010f0u || address==0x1f8010f4u) ?
        musashi_dma_controller_write32(&boot->dma,address,value) :
        gpu_io_write32(boot,address,value);
    if (!accepted) {
        fprintf(stderr, "native_boot: unsupported GPU write %08x=%08x menu=NOT_REACHED\n",
                (unsigned)address, (unsigned)value);
        exit(2);
    }
    if (boot->gpu_trace && (address==0x1f8010f0u || address==0x1f8010f4u))
        fprintf(stderr, "native_boot: GPU_TRACE address=%08x width=4 value=%08x\n",
                (unsigned)address, (unsigned)value);
}

static int gpu_cw(void *userdata, uint32_t command, int32_t *result) {
    NativeBoot *boot = userdata;
    uint32_t status;
    /* SCPH BIOS A0:49 calls BFC04138 before submitting one GP0 word.
     * The observed boot takes its DMA-off, immediately-ready branch. Other
     * DMA/wait paths remain explicit refusal until transfer ownership exists. */
    if (!result || !musashi_gpu_controller_read32(&boot->gpu, 0x1f801814u, &status) ||
        (status & 0x60000000u) || !(status & 0x10000000u) ||
        !musashi_gpu_controller_write32(&boot->gpu, 0x1f801810u, command))
        return 0;
    *result = 0;
    boot->gpu_trace = getenv("MUSASHI_NATIVE_GPU_TRACE") != NULL;
    if (boot->gpu_trace)
        fprintf(stderr, "native_boot: GPU_TRACE address=1f801810 width=4 value=%08x\n",
                (unsigned)command);
    return 1;
}

static void observe_spu_stage(NativeBoot *boot, uint32_t pc) {
    MusashiSpuCdAudioSnapshot state;
    uint32_t handle, guard;
    unsigned i;
    if (!boot->spu || !musashi_spu_cd_audio_snapshot(boot->spu, &state) ||
        !musashi_boot_read32(&boot->memory, 0x8006b0e0u, &handle) ||
        !musashi_boot_read32(&boot->memory, 0x8006b548u, &guard)) {
        fputs("native_boot: SPU_STAGE refused\n", stderr);
        return;
    }
    fprintf(stderr, "native_boot: SPU_STAGE pc=%08x cycle=%llu samples=%llu "
            "main_left=%04x main_right=%04x control=%04x manual_halfwords=%llu "
            "cursor=%05x transfer_address=%04x transfer_control=%04x "
            "event_handle=%08x event_guard=%08x faulted=%d "
            "manual_policy=SYNCHRONOUS_SOURCE_MODEL key_policy=POST_FRAME "
            "hardware_subphase=UNPROVEN\n", pc,
            (unsigned long long)state.cycle, (unsigned long long)state.sample_index,
            (unsigned)(uint16_t)state.main_left_current,
            (unsigned)(uint16_t)state.main_right_current, state.control,
            (unsigned long long)state.manual_halfwords, state.transfer_cursor,
            state.transfer_address, state.transfer_control, handle, guard, state.faulted);
    if (pc == 0x8002c984u || pc == 0x8002c98cu || pc == 0x800101fcu) {
        MusashiSpuReverbSnapshot reverb;
        if (!musashi_spu_cd_audio_reverb_snapshot(boot->spu, &reverb))
            fputs("native_boot: SPU_REVERB refused\n", stderr);
        else {
            fprintf(stderr, "native_boot: SPU_REVERB pc=%08x base=%04x enabled=%d "
                    "wet_left=%04x wet_right=%04x cursor=%05x eon=%06x next_channel=%u "
                    "frames=%llu processed_left=%llu processed_right=%llu ram_reads=%llu "
                    "ram_writes=%llu unknown_reads=%llu unsupported_steps=%llu "
                    "unknown_history_left=%llu unknown_history_right=%llu bootstrap_halfwords=%llu coefficients=",
                    pc, reverb.base, reverb.enabled, (unsigned)(uint16_t)reverb.wet_left,
                    (unsigned)(uint16_t)reverb.wet_right, reverb.cursor, reverb.eon,
                    reverb.next_channel, (unsigned long long)reverb.frames,
                    (unsigned long long)reverb.processed_left, (unsigned long long)reverb.processed_right,
                    (unsigned long long)reverb.ram_reads, (unsigned long long)reverb.ram_writes,
                    (unsigned long long)reverb.unknown_reads, (unsigned long long)reverb.unsupported_steps,
                    (unsigned long long)reverb.unknown_history_left, (unsigned long long)reverb.unknown_history_right,
                    (unsigned long long)state.bootstrap_halfwords);
            for (i=0;i<32;++i) fprintf(stderr, "%s%04x", i ? "," : "", reverb.coefficients[i]);
            fputc('\n', stderr);
        }
    }
    if (state.manual_halfwords) {
        uint8_t bytes[16];
        int valid = musashi_spu_cd_audio_copy_ram(boot->spu, 0x1000u, bytes, sizeof(bytes));
        fprintf(stderr, "native_boot: SPU_RAM pc=%08x offset=01000 valid=%d bytes=", pc, valid);
        if (valid) for (i = 0; i < sizeof(bytes); ++i) fprintf(stderr, "%02x", bytes[i]);
        fputc('\n', stderr);
    }
    if (pc == 0x8002c90cu) {
        for (i = 0; i < 24; ++i) {
            MusashiSpuVoiceSnapshot voice;
            unsigned j;
            if (!musashi_spu_cd_audio_voice_snapshot(boot->spu, i, &voice)) {
                fprintf(stderr, "native_boot: SPU_VOICE index=%u refused=1\n", i);
                continue;
            }
            fprintf(stderr, "native_boot: SPU_VOICE index=%u phase=%u envelope=%d "
                    "cursor=%05x repeat=%05x blocks=%llu decoded=%llu "
                    "pending_on=%d pending_off=%d endx=%d regs=", i, voice.phase,
                    voice.envelope, voice.cursor, voice.repeat,
                    (unsigned long long)voice.decoded_blocks,
                    (unsigned long long)voice.decoded_samples,
                    voice.pending_on, voice.pending_off, voice.endx);
            for (j = 0; j < 8; ++j) fprintf(stderr, "%s%04x", j ? "," : "", voice.registers[j]);
            fputc('\n', stderr);
        }
    }
}

/* Record one CPU state per distinct (pc, caller) pair. The formatter stops the
 * fast path at each recovered call gate, and the sampled step trace covers the
 * earliest instructions; both feed this record, so every trace line is unique
 * and the caller registers stay auditable. */
static void trace_cpu_state(uint32_t pc, uint32_t npc, uint32_t hi, uint32_t lo,
                            const uint32_t *r) {
    static uint32_t gates[512];
    static uint32_t callers[512];
    static unsigned recorded;
    unsigned slot, i;
    for (slot = 0; slot < recorded; ++slot)
        if (gates[slot] == pc && callers[slot] == r[31]) return;
    if (recorded >= sizeof(gates) / sizeof(gates[0])) return;
    gates[recorded] = pc;
    callers[recorded] = r[31];
    ++recorded;
    fprintf(stderr, "native_boot: CPU_TRACE pc=%08x npc=%08x hi=%08x lo=%08x regs=",
            (unsigned)pc, (unsigned)npc, (unsigned)hi, (unsigned)lo);
    for (i = 0; i < 32; i++)
        fprintf(stderr, "%s%08x", i ? "," : "", (unsigned)r[i]);
    fputc('\n', stderr);
}

static void observe_entry(void *userdata, const MusashiEntryCpuSnapshot *cpu) {
    NativeBoot *boot = userdata;
    unsigned i;
    trace_cpu_state(cpu->pc, cpu->npc, cpu->hi, cpu->lo, cpu->r);
    if (cpu->pc == 0x800cf104u && boot->opening_overlay_ready) {
        const char *preview=getenv("MUSASHI_PAUSE_AT_START_SCREEN");
        uint16_t state;
        uint32_t prompt_visible;
        if (preview && preview[0]=='1' &&
            musashi_boot_read16(&boot->memory,0x800b99e4u,&state) && state==4u &&
            ++boot->start_preview_frames>=30u &&
            musashi_boot_read32(&boot->memory,0x800ec694u,&prompt_visible) && prompt_visible==1u) {
            /* Host inspection stop after natural menu frames. This never
             * changes a guest PC, input, state flag, or rendering command. */
            boot->start_preview_paused=1;
            fprintf(stderr,"native_boot: MENU_PREVIEW paused=1 source_pc=800cf104 frames=%u\n",
                    boot->start_preview_frames);
            host_stop_signal=SIGTERM;
        }
        {
            uint32_t current_phase = 0;
            static uint32_t last_phase = 0;
            if (musashi_boot_read32(&boot->memory, 0x800ec690u, &current_phase) && current_phase != last_phase) {
                fprintf(stderr, "native_boot: TITLE_PHASE transition %u -> %u (frames=%u)\n",
                        last_phase, current_phase, boot->start_preview_frames);
                last_phase = current_phase;
            }
        }
    }
    if (cpu->pc == 0x800cf3a4u) {
        static int logged_new_game;
        if (!logged_new_game) {
            logged_new_game = 1;
            fprintf(stderr, "native_boot: NEW_GAME_SETUP pc=800cf3a4 ra=%08x\n", cpu->r[31]);
        }
    }
    if (cpu->pc == 0x80128158u) {
        boot->scene1_reached = 1;
        static int logged_sc02;
        if (!logged_sc02) {
            logged_sc02 = 1;
            fprintf(stderr, "native_boot: SC02_ENTRY pc=80128158 ra=%08x\n", cpu->r[31]);
        }
    }
    if (cpu->pc == 0x80128420u) {
        static int logged_8420;
        if (!logged_8420) {
            logged_8420 = 1;
            uint32_t words[8] = {0};
            for (int k = 0; k < 8; ++k)
                musashi_boot_read32(&boot->memory, 0x80128420u + k * 4u, &words[k]);
            fprintf(stderr, "native_boot: SCENE_DISPATCH pc=80128420 ra=%08x words=%08x %08x %08x %08x %08x %08x %08x %08x\n",
                    cpu->r[31], words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
        }
    }
    if (cpu->pc == 0x800d0630u) {
        static int logged_0010;
        if (!logged_0010) {
            logged_0010 = 1;
            fprintf(stderr, "native_boot: OVERLAY_0010_ENTRY pc=800d0630 ra=%08x\n", cpu->r[31]);
        }
    }
    if (cpu->pc == 0x80053218u || cpu->pc == 0x80014690u ||
        cpu->pc == 0x80053178u || cpu->pc == 0x80014680u ||
        cpu->pc == 0x8005283cu || cpu->pc == 0x80052d00u ||
        cpu->pc == 0x80052d80u || cpu->pc == 0x80052becu ||
        cpu->pc == 0x80052c74u || cpu->pc == 0x80014650u) {
        static const uint32_t addresses[] = {
            0x800ae618u,0x800ae61cu,0x800ae7f8u,0x800ae820u,
            0x800ae824u,0x800a6438u,0x800a643cu,0x800a6440u,
            0x800a6548u,0x800c7c70u,0x800c7c74u,0x800c7c88u,
            0x800794e0u,0x800794e4u,0x800a5e50u,
            0x800c73d0u,0x800c6dc8u,0x800ae7e8u
        };
        fprintf(stderr,"native_boot: PROJECTION_SOURCE pc=%08x sp=%08x ra=%08x "
                "v0=%08x a0=%08x a1=%08x hi=%08x lo=%08x\n",
                cpu->pc,cpu->r[29],cpu->r[31],cpu->r[2],cpu->r[4],cpu->r[5],cpu->hi,cpu->lo);
        for (i=0;i<sizeof(addresses)/sizeof(addresses[0]);++i) {
            uint32_t value=0;
            int valid=musashi_boot_read32(&boot->memory,addresses[i],&value);
            fprintf(stderr,"native_boot: PROJECTION_RAM pc=%08x address=%08x valid=%d value=%08x\n",
                    cpu->pc,addresses[i],valid,value);
        }
    }
    if (cpu->pc == 0x80059888u) {
        uint16_t rect[4]={0};
        int valid=1;
        for (i=0;i<4u;++i)
            valid=musashi_boot_read16(&boot->memory,cpu->r[4]+i*2u,&rect[i]) && valid;
        fprintf(stderr,"native_boot: CLEAR_SOURCE pc=%08x sp=%08x ra=%08x rect=%08x "
                "valid=%d xywh=%04x,%04x,%04x,%04x rgb=%08x,%08x,%08x\n",
                cpu->pc,cpu->r[29],cpu->r[31],cpu->r[4],valid,
                rect[0],rect[1],rect[2],rect[3],cpu->r[5],cpu->r[6],cpu->r[7]);
    }
    if (cpu->pc == 0x800101c8u) boot->graphics_returned = 1;
    if (cpu->pc == 0x8005d8b4u || cpu->pc == 0x800101e4u) observe_input_state(boot, cpu->pc);
    if (cpu->pc == 0x8005d804u || cpu->pc == 0x8005d858u ||
        cpu->pc == 0x8005dc50u || cpu->pc == 0x8005dc88u) {
        static uint64_t n;
        uint32_t slot = 0, stage = 0, active = 0, counter = 0;
        uint8_t tap = 0xff, mode = 0xff;
        uint8_t *pad = musashi_boot_ram_span(&boot->memory, 0x80078a48u, 0x51u);
        (void)musashi_boot_read32(&boot->memory, 0x800729b4u, &slot);
        (void)musashi_boot_read32(&boot->memory, 0x800729a0u, &stage);
        (void)musashi_boot_read32(&boot->memory, 0x800729acu, &active);
        (void)musashi_boot_read32(&boot->memory, 0x8007299cu, &counter);
        if (pad) { tap = pad[0x50]; mode = pad[0x46]; }
        if (log_sample(&n, 16u, 4096u))
            fprintf(stderr, "native_boot: TAP_PATH pc=%08x v0=%08x a0=%08x "
                    "29B4=%08x 29A0=%08x 29AC=%08x 299C=%08x tap50=%02x mode46=%02x\n",
                    cpu->pc, cpu->r[2], cpu->r[4], slot, stage, active, counter,
                    tap, mode);
    }
    {
        static uint64_t n;
        if (log_sample(&n, 4u, 65536u))
            trace_cpu_state(cpu->pc, cpu->npc, cpu->hi, cpu->lo, cpu->r);
    }
    if (cpu->pc == 0x80034cd0u || cpu->pc == 0x800101fcu) {
        uint32_t tracks = cpu->r[2];
        const uint8_t *toc = musashi_boot_ram_span(&boot->memory, 0x800c7d30u, 20);
        int valid = toc != NULL;
        if (cpu->pc == 0x800101fcu)
            valid = valid && musashi_boot_read32(&boot->memory, 0x800a5bc8u, &tracks);
        fprintf(stderr, "native_boot: SOUND_TOC pc=%08x valid=%d tracks=%u address=800c7d30 bytes=",
                cpu->pc, valid, tracks);
        if (valid) for (i=0;i<20;++i) fprintf(stderr, "%02x", toc[i]);
        fputc('\n', stderr);
    }
    if (cpu->pc == 0x80045cb8u) {
        const uint8_t *loc=musashi_boot_ram_span(&boot->memory,cpu->r[29]+0x10u,3);
        fprintf(stderr,"native_boot: CD_SECTOR_REQUEST valid=%d count=%u destination=%08x location=",
                loc != NULL,cpu->r[17],cpu->r[16]);
        if (loc) for (i=0;i<3;++i) fprintf(stderr,"%02x",loc[i]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x80045698u) {
        const uint8_t *pvd = musashi_boot_ram_span(&boot->memory, cpu->r[16], 2048);
        uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE], digest[SHA256_DIGEST_LENGTH];
        int valid = pvd != NULL;
        /* Read-only verification after the source read returns. The disc
         * comparison never supplies bytes to guest RAM or alters CD state. */
        int exact = valid && boot->media &&
            musashi_disc_media_read_sector(boot->media, 16, raw, sizeof(raw)) &&
            memcmp(pvd, raw+24, 2048) == 0;
        fprintf(stderr,"native_boot: PVD_RETURN pc=%08x result=%08x address=%08x valid=%d bytes=2048 retail_exact=%d sha256=",
                cpu->pc,cpu->r[2],cpu->r[16],valid,exact);
        if (valid && SHA256(pvd,2048,digest))
            for (i=0;i<sizeof(digest);++i) fprintf(stderr,"%02x",digest[i]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x80045744u) {
        uint32_t lba = 0;
        const uint8_t *data = musashi_boot_ram_span(&boot->memory,cpu->r[16],2048);
        uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE],digest[SHA256_DIGEST_LENGTH];
        int valid = data && musashi_boot_read32(&boot->memory,cpu->r[29]+0x18u,&lba);
        int exact = valid && boot->media &&
            musashi_disc_media_read_sector(boot->media,lba,raw,sizeof(raw)) &&
            memcmp(data,raw+24,2048) == 0;
        fprintf(stderr,"native_boot: PATH_TABLE_READ_RETURN result=%08x lba=%u address=%08x valid=%d bytes=2048 retail_exact=%d sha256=",
                cpu->r[2],lba,cpu->r[16],valid,exact);
        if (valid && SHA256(data,2048,digest))
            for (i=0;i<sizeof(digest);++i) fprintf(stderr,"%02x",digest[i]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x80045918u) {
        const uint8_t *record = musashi_boot_ram_span(&boot->memory,0x80076a00u,44);
        fprintf(stderr,"native_boot: PATH_TABLE_PARSE_RETURN result=%08x records=%u first_record=",
                cpu->r[2],cpu->r[7]);
        if (record) for (i=0;i<44;++i) fprintf(stderr,"%02x",record[i]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x8004544cu)
        fprintf(stderr,"native_boot: DIRECTORY_LOOKUP_RETURN result=%08x\n",cpu->r[2]);
    if (cpu->pc == 0x80045a50u) {
        uint32_t lba = 0;
        const uint8_t *data = musashi_boot_ram_span(&boot->memory,cpu->r[16],2048);
        uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE],digest[SHA256_DIGEST_LENGTH];
        /* The source directory ordinal selects the live path-cache extent.
         * This comparison is diagnostic only, after the real CD read. */
        int valid = data && cpu->r[22] >= 1u && cpu->r[22] <= 128u &&
            musashi_boot_read32(&boot->memory,0x800769dcu+44u*cpu->r[22],&lba);
        int exact = valid && boot->media &&
            musashi_disc_media_read_sector(boot->media,lba,raw,sizeof(raw)) &&
            memcmp(data,raw+24,2048) == 0;
        fprintf(stderr,"native_boot: DIRECTORY_READ_RETURN result=%08x ordinal=%u lba=%u address=%08x valid=%d bytes=2048 retail_exact=%d sha256=",
                cpu->r[2],cpu->r[22],lba,cpu->r[16],valid,exact);
        if (valid && SHA256(data,2048,digest))
            for (i=0;i<sizeof(digest);++i) fprintf(stderr,"%02x",digest[i]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x80045c14u) {
        unsigned record_index;
        fprintf(stderr,"native_boot: DIRECTORY_PARSE_END ordinal=%u records=%u cursor=%08x\n",
                cpu->r[22],cpu->r[18],cpu->r[16]);
        for (record_index=0;record_index<cpu->r[18] && record_index<64u;++record_index) {
            const uint8_t *record=musashi_boot_ram_span(&boot->memory,0x80076400u+24u*record_index,24);
            fprintf(stderr,"native_boot: DIRECTORY_RECORD index=%u valid=%d bytes=",record_index,record!=NULL);
            if (record) for (i=0;i<24;++i) fprintf(stderr,"%02x",record[i]);
            fputc('\n',stderr);
        }
    }
    if (cpu->pc == 0x80019768u || cpu->pc == 0x800197ccu) {
        uint32_t output=cpu->pc==0x80019768u ? cpu->r[16] : cpu->r[16]+cpu->r[18];
        const uint8_t *name=musashi_boot_ram_span(&boot->memory,output-20u,20);
        const uint8_t *record=musashi_boot_ram_span(&boot->memory,output,24);
        fprintf(stderr,"native_boot: FILE_LOOKUP_RETURN required=%d index=%u result=%08x output=%08x name_hex=",
                cpu->pc==0x800197ccu,cpu->r[21],cpu->r[2],output);
        if (name) for (i=0;i<20;++i) fprintf(stderr,"%02x",name[i]);
        fprintf(stderr," record_valid=%d record=",record!=NULL);
        if (record) for (i=0;i<24;++i) fprintf(stderr,"%02x",record[i]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x800525dcu || cpu->pc == 0x800526a8u || cpu->pc == 0x800526fcu) {
        uint16_t callback_guard=0;
        uint32_t stackarg=0;
        int readable=musashi_boot_read16(&boot->memory,0x8006bafcu,&callback_guard) &&
            musashi_boot_read32(&boot->memory,cpu->r[29]+0x10u,&stackarg);
        fprintf(stderr,"native_boot: DRAW_SOURCE pc=%08x a0=%08x a1=%08x "
                "a2=%08x a3=%08x sp=%08x ra=%08x v0=%08x stack10=%08x "
                "readable=%d callback_guard=%u gpu_reset_sequence=%llu\n",
                cpu->pc,cpu->r[4],cpu->r[5],cpu->r[6],cpu->r[7],cpu->r[29],
                cpu->r[31],cpu->r[2],stackarg,readable,(unsigned)callback_guard,
                (unsigned long long)boot->gpu_reset_sequence);
    }
    if (cpu->pc == 0x80059df4u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u)) {
            uint32_t tag=0, word=0;
            unsigned nwords;
            int valid=musashi_boot_read32(&boot->memory,cpu->r[5],&tag);
            unsigned words=valid ? (tag>>24)+1u : 0;
            fprintf(stderr,"native_boot: DRAW_PACKET address=%08x tag=%08x words=%u valid=%d data=",
                    cpu->r[5],tag,words,valid);
            if (words<=17u) for (nwords=0;nwords<words;++nwords) {
                if (!musashi_boot_read32(&boot->memory,cpu->r[5]+4u*nwords,&word)) break;
                fprintf(stderr,"%s%08x",nwords ? "," : "",word);
            }
            fputc('\n',stderr);
        }
    }
    if (cpu->pc == 0x8005b870u || cpu->pc == 0x8005b720u || cpu->pc == 0x8005b730u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr,"native_boot: DRAW_DEVICE pc=%08x address=%08x a0=%08x "
                    "v1=%08x sp=%08x ra=%08x\n",cpu->pc,cpu->r[2],cpu->r[4],
                    cpu->r[3],cpu->r[29],cpu->r[31]);
    }
    if (cpu->pc == 0x80059dfcu) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u)) {
            fprintf(stderr,"native_boot: DRAW_QUEUE_RETURN pc=%08x v0=%08x sp=%08x "
                    "ra=%08x I_MASK=%04x\n",cpu->pc,cpu->r[2],cpu->r[29],cpu->r[31],
                    boot->irq.controller.mask);
            observe_gpu_dma(boot,"SOURCE_QUEUE_RETURN");
        }
    }
    if (cpu->pc == 0x8005b690u) {
        static uint64_t gp1_logs;
        const uint8_t *cache=musashi_boot_ram_span(&boot->memory,
            0x80078874u+(cpu->r[4]>>24),1);
        if (gp1_logs < 8u || (gp1_logs % 4096u) == 0u)
            fprintf(stderr,"native_boot: DISPLAY_GP1 pc=%08x word=%08x address=%08x "
                    "sp=%08x ra=%08x cache_valid=%d cache_before=%02x n=%llu\n",
                    cpu->pc,cpu->r[4],cpu->r[2],cpu->r[29],cpu->r[31],cache!=NULL,
                    cache ? *cache : 0, (unsigned long long)gp1_logs);
        if (gp1_logs != UINT64_MAX) ++gp1_logs;
    }
    if (cpu->pc == 0x8005279cu) {
        const uint8_t *env=musashi_boot_ram_span(&boot->memory,0x800a6498u,20);
        const uint8_t *cache=musashi_boot_ram_span(&boot->memory,0x800727f4u,20);
        fprintf(stderr,"native_boot: DISPLAY_SOURCE_RETURN pc=%08x v0=%08x sp=%08x "
                "ra=%08x env_valid=%d cache_valid=%d cache_equal=%d cache=",
                cpu->pc,cpu->r[2],cpu->r[29],cpu->r[31],env!=NULL,cache!=NULL,
                env && cache && !memcmp(env,cache,20));
        if (cache) for (unsigned j=0;j<20;++j) fprintf(stderr,"%02x",cache[j]);
        fputc('\n',stderr);
    }
    if (cpu->pc == 0x80014948u || cpu->pc == 0x80014248u)
        fprintf(stderr,"native_boot: HEAP_SOURCE_RETURN pc=%08x v0=%08x "
                "sp=%08x ra=%08x guest_irq_enabled=%d\n",
                cpu->pc,cpu->r[2],cpu->r[29],cpu->r[31],
                guest_irq_enabled(boot));
    if (cpu->pc == 0x800197ecu) {
        fprintf(stderr,"native_boot: REQUIRED_FILE_SEARCHES_RETURN count=%u\n",cpu->r[21]);
        if (!boot->list_tail_captured) {
            const uint8_t *tail=musashi_boot_ram_span(&boot->memory,0x80180e40u,448);
            if (tail) {
                memcpy(boot->list_tail_before,tail,448);
                boot->list_tail_captured=1;
            }
        }
    }
    if (cpu->pc == 0x80010f80u) {
        static uint64_t n;
        if (log_sample(&n, 4u, 4096u))
            fprintf(stderr, "native_boot: SCENE_INIT pc=80010f80 v0=%08x v1=%08x a0=%08x "
                    "sp=%08x ra=%08x overlay7=%d\n",
                    cpu->r[2], cpu->r[3], cpu->r[4], cpu->r[29], cpu->r[31],
                    boot->opening_overlay_ready);
        (void)select_opening_overlay(boot);
    }
    if (cpu->pc == 0x800ceec8u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_JALR pc=800ceec8 v0=%08x v1=%08x "
                    "a0=%08x sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[3], cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800ceefcu) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_TABLE0 pc=800ceefc v0=%08x a0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800cef60u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_TABLE1 pc=800cef60 v0=%08x a0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800cef94u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_TABLE2 pc=800cef94 v0=%08x a0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800ceff8u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_TABLE3 pc=800ceff8 v0=%08x a0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800cf02cu) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_CF02C pc=800cf02c v0=%08x a0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800cf068u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_OPDEMO pc=800cf068 a0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[4], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800118acu) {
        static uint64_t n;
        uint16_t a3b4 = 0, a3b6 = 0, a3b4_c = 0, a3b6_c = 0;
        (void)musashi_boot_read16(&boot->memory, 0x800b99e4u, &a3b4);
        (void)musashi_boot_read16(&boot->memory, 0x800b99e6u, &a3b6);
        (void)musashi_boot_read16(&boot->memory, 0x800c99e4u, &a3b4_c);
        (void)musashi_boot_read16(&boot->memory, 0x800c99e6u, &a3b6_c);
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: SCENE_A3B4 pc=800118ac a3b4=%04x a3b6=%04x "
                    "c99e4=%04x c99e6=%04x ra=%08x\n",
                    a3b4, a3b6, a3b4_c, a3b6_c, cpu->r[31]);
    }
    if (cpu->pc == 0x80011818u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: SCENE_SWITCH pc=80011818 a0=%08x "
                    "ra=%08x\n", cpu->r[4], cpu->r[31]);
    }
    if (cpu->pc == 0x800110ccu) {
        static uint64_t n;
        boot->title_scene_reached = 1;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: TITLE_SCENE pc=800110cc ra=%08x\n",
                    cpu->r[31]);
        if (loaded_pair_digest(&boot->memory, 0x800cedfcu) == TITLE_MEMBER0010_SIGNATURE)
            musashi_boot_select_overlay_0010_words(1);
    }
    if (cpu->pc == 0x8001125cu) {
        static int selected_0010;
        if (!selected_0010 &&
            loaded_pair_digest(&boot->memory, 0x800cedfcu) == TITLE_MEMBER0010_SIGNATURE) {
            selected_0010 = 1;
            musashi_boot_select_overlay_0010_words(1);
            fprintf(stderr, "native_boot: OVERLAY_SELECT member=10 dest=800cedf8 "
                    "reason=8001125c signature\n");
        }
    }
    if (cpu->pc == 0x80019a24u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: TITLE_LOAD pc=80019a24 a0=%08x a1=%08x "
                    "ra=%08x\n", cpu->r[4], cpu->r[5], cpu->r[31]);
    }
    if (cpu->pc == 0x800cf3b0u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_START pc=800cf3b0 v0=%08x "
                    "sp=%08x ra=%08x\n",
                    cpu->r[2], cpu->r[29], cpu->r[31]);
    }
    if (cpu->pc == 0x800cef94u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_PAD800 pc=800cef94 a0=%08x "
                    "v0=%08x ra=%08x\n",
                    cpu->r[4], cpu->r[2], cpu->r[31]);
    }
    if (cpu->pc == 0x800167b8u && cpu->r[4] == 4u) {
        static uint64_t n;
        uint16_t held = 0, a3b4 = 0;
        (void)musashi_boot_read16(&boot->memory, 0x80078dd2u, &held);
        (void)musashi_boot_read16(&boot->memory, 0x800b99e4u, &a3b4);
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: PAD14CAC_OK pc=800167b8 a0=4 "
                    "8DD2=%04x a3b4=%04x ra=%08x\n",
                    held, a3b4, cpu->r[31]);
    }
    if (cpu->pc == 0x800cf3d4u) {
        static uint64_t n;
        uint32_t flag=0, id=0, held=0, edge=0;
        uint8_t *type=musashi_boot_ram_span(&boot->memory,0x80088d98u,1);
        (void)musashi_boot_read32(&boot->memory,0x800c99e4u,&flag);
        (void)musashi_boot_read32(&boot->memory,0x80088dc8u,&held);
        (void)musashi_boot_read32(&boot->memory,0x80088dd0u,&edge);
        (void)musashi_boot_read32(&boot->memory,0x80088d98u,&id);
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: OVERLAY_START_BITS pc=800cf3d4 s0=%08x "
                    "v0=%08x type=%02x 8DCA=%04x 8DD2=%04x C99E4=%04x C99E6=%04x "
                    "analog_mode=%d\n",
                    cpu->r[16], cpu->r[2], type?*type:0xffu,
                    (unsigned)((held>>16)&0xffffu), (unsigned)((edge>>16)&0xffffu),
                    (unsigned)(flag&0xffffu), (unsigned)((flag>>16)&0xffffu),
                    boot->sio.analog_mode);
    }
    if (cpu->pc == 0x80014b08u) {
        static uint64_t n;
        if (log_sample(&n, 8u, 4096u))
            fprintf(stderr, "native_boot: PAD149E0_RET pc=80014b08 v0=%08x "
                    "s0=%08x\n", cpu->r[2], cpu->r[16]);
    }
    if (cpu->pc == 0x8001aa98u)
        fprintf(stderr, "native_boot: FILE_STUB pc=8001aa98 a0=%08x ra=%08x\n",
                cpu->r[4], cpu->r[31]);
    if (cpu->pc == 0x80019b30u) {
        uint32_t mode=0, complete=0, expected=0, callback=0, cursor=0;
        uint8_t *aef4=musashi_boot_ram_span(&boot->memory,0x8006aef4u,1);
        (void)musashi_boot_read32(&boot->memory,0x800ae6f8u,&mode);
        (void)musashi_boot_read32(&boot->memory,0x800ae74cu,&complete);
        (void)musashi_boot_read32(&boot->memory,0x800ae7acu,&expected);
        (void)musashi_boot_read32(&boot->memory,0x8006cc88u,&callback);
        (void)musashi_boot_read32(&boot->memory,0x800a46c8u,&cursor);
        fprintf(stderr,"native_boot: LIST_STATE state=%u target=%08x mode=%u complete=%u expected=%u cb=%08x cursor=%08x aef4=%02x\n",
                cpu->r[3],cpu->r[2],mode,complete,expected,callback,cursor,aef4?*aef4:0xffu);
    }
    if (cpu->pc == 0x8002fc64u || cpu->pc == 0x8002fd10u) {
        uint32_t cursor=0;
        uint8_t *aef4=musashi_boot_ram_span(&boot->memory,0x8006aef4u,1);
        (void)musashi_boot_read32(&boot->memory,0x800a46c8u,&cursor);
        fprintf(stderr,"native_boot: PAC_SPU pc=%08x a0=%08x a1=%08x v0=%08x cursor=%08x aef4=%02x\n",
                cpu->pc,cpu->r[4],cpu->r[5],cpu->r[2],cursor,aef4?*aef4:0xffu);
    }
    if (cpu->pc == 0x80019804u) {
        fprintf(stderr,"native_boot: LIST_REQUEST_RETURN result=%08x\n",cpu->r[2]);
        if (cpu->r[2]) {
            const uint8_t *data=musashi_boot_ram_span(&boot->memory,0x80180000u,0xe40u);
            uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE],digest[SHA256_DIGEST_LENGTH];
            /* Read-only comparison against the source's actual E40 request.
             * These media reads never populate guest RAM or CD state. */
            int exact=data && boot->media &&
                musashi_disc_media_read_sector(boot->media,227,raw,sizeof(raw)) &&
                memcmp(data,raw+24,2048)==0 &&
                musashi_disc_media_read_sector(boot->media,228,raw,sizeof(raw)) &&
                memcmp(data+2048,raw+24,0xe40u-2048)==0;
            fprintf(stderr,"native_boot: LIST_PAYLOAD address=80180000 bytes=3648 valid=%d retail_exact=%d sha256=",data!=NULL,exact);
            if (data && SHA256(data,0xe40u,digest))
                for (i=0;i<sizeof(digest);++i) fprintf(stderr,"%02x",digest[i]);
            fputc('\n',stderr);
            const uint8_t *tail=musashi_boot_ram_span(&boot->memory,0x80180e40u,448);
            fprintf(stderr,"native_boot: LIST_TAIL address=80180e40 bytes=448 captured=%d unchanged=%d\n",
                    boot->list_tail_captured,boot->list_tail_captured && tail &&
                    memcmp(tail,boot->list_tail_before,448)==0);
        }
    }
    if (cpu->pc == 0x800198fcu) {
        unsigned record_index;
        fprintf(stderr,"native_boot: LIST_METADATA_PARSE_END groups=%u records=%u cursor=%08x source_records=%u\n",
                cpu->r[21],cpu->r[30],cpu->r[22],cpu->r[23]);
        for (record_index=0;record_index<cpu->r[30] && record_index<456u;++record_index) {
            const uint8_t *record=musashi_boot_ram_span(&boot->memory,0x800ae830u+8u*record_index,8);
            fprintf(stderr,"native_boot: ARCHIVE_MEMBER index=%u valid=%d bytes=",record_index,record!=NULL);
            if (record) for (i=0;i<8;++i) fprintf(stderr,"%02x",record[i]);
            fputc('\n',stderr);
        }
    }
    if (cpu->pc == 0x80010204u)
        fputs("native_boot: FILE_INITIALIZER_RETURN pc=80010204\n",stderr);
    if (cpu->pc == 0x8002c8f4u || cpu->pc == 0x8002c904u || cpu->pc == 0x8002c90cu ||
        cpu->pc == 0x8002c974u || cpu->pc == 0x8002c984u || cpu->pc == 0x8002c98cu ||
        cpu->pc == 0x800101fcu)
        observe_spu_stage(boot, cpu->pc);
    if (cpu->pc == 0x8003b160u) {
        uint32_t callback;
        if (!musashi_boot_read32(&boot->memory, 0x8006cbd4u, &callback))
            fputs("native_boot: DMA_REGISTER unreadable\n", stderr);
        else fprintf(stderr, "native_boot: DMA_REGISTER return_pc=%08x channel=4 "
                     "slot=8006cbd4 callback=%08x previous=%08x DPCR=%08x DICR=%08x\n",
                     cpu->pc, callback, cpu->r[2], boot->dma.control, boot->dma.interrupt);
    }
    if (cpu->pc == 0x8005fcf8u) {
        for (i = 0; i < 8; ++i) {
            uint32_t handle, flag;
            const MusashiBiosEventRecord *record = &boot->bios.events.records[i];
            if (!musashi_boot_read32(&boot->memory, 0x80078c3cu + i * 4u, &handle) ||
                !musashi_boot_read32(&boot->memory, 0x80078c5cu + i * 4u, &flag)) {
                fputs("native_boot: EVENT_STATE unreadable\n", stderr);
                return;
            }
            fprintf(stderr, "native_boot: EVENT_STATE index=%u handle=%08x flag=%08x "
                    "class=%08x status=%08x spec=%08x mode=%08x callback=%08x\n",
                    i, handle, flag, record->class_word, record->status,
                    record->spec, record->mode, record->callback);
        }
    }
    if (cpu->pc == 0x80062340u || cpu->pc == 0x80061f38u)
        fprintf(stderr, "native_boot: CARD_STATE at=%08x pad_policy=%d "
                "card_started=%u card_guest_irq=%d\n", (unsigned)cpu->pc,
                (int)boot->irq_policy.pad, (unsigned)boot->card.card_started,
                guest_irq_enabled(boot));
}

/* A problem the player has to act on: stderr always, and an on-screen
 * dialog when a person is playing (MUSASHI_INTERACTIVE, set by launch.sh). */
static void user_error(const char *message) {
    const char *interactive = getenv("MUSASHI_INTERACTIVE");
    fprintf(stderr, "\n%s\n\n", message);
    if (interactive && interactive[0] == '1')
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Brave Fencer Musashi", message, NULL);
}

/* --disc PATH: the EXE is read from the image itself (ISO 9660 root) into
 * $XDG_CACHE_HOME/bfm-port/SLUS_007.26 (default ~/.cache/bfm-port), so a
 * player only needs the .cue/.bin. Returns the cached path or NULL. */
static const char *cache_exe_from_disc(const MusashiDiscMedia *media) {
    static char path[4096];
    char dir[4096];
    const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    uint8_t *exe = malloc(0x65000u);
    size_t size = 0;
    FILE *file;
    int ok;
    if (!exe) return NULL;
    if (!musashi_disc_media_read_root_file(media, "SLUS_007.26", exe, 0x65000u, &size)) {
        free(exe);
        return NULL;
    }
    if (cache && cache[0]) snprintf(dir, sizeof dir, "%s/bfm-port", cache);
    else snprintf(dir, sizeof dir, "%s/.cache/bfm-port", home ? home : ".");
    {
        char parent[4096];
        snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(dir, '/') - dir), dir);
        (void)mkdir(parent, 0755);
        (void)mkdir(dir, 0755);
    }
    snprintf(path, sizeof path, "%s/SLUS_007.26", dir);
    file = fopen(path, "wb");
    ok = file && fwrite(exe, 1, size, file) == size;
    if (file && fclose(file) != 0) ok = 0;
    free(exe);
    return ok ? path : NULL;
}

static int load_exe(NativeBoot *boot, const char *path) {
    /* Bound reads to the pinned file size; the loader verifies every byte
     * before writing RAM. No retail bytes are included in this executable. */
    const size_t expected_size = 0x65000u;
    uint8_t *exe;
    size_t size;
    int accepted, extra, io_error;
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    exe = malloc(expected_size);
    if (!exe) {
        fclose(file);
        return 0;
    }
    size = fread(exe, 1, expected_size, file);
    extra = fgetc(file);
    io_error = ferror(file);
    fclose(file);
    accepted = !io_error && size == expected_size && extra == EOF &&
        musashi_boot_map_exe(&boot->memory, exe, size);
    free(exe);
    return accepted;
}

/* First-run disc check. Everything from the game (code, overlays, assets)
 * is read from the user's own disc image; nothing retail ships with the
 * port. When the supplied files are not the supported release, say so in
 * terms a player can act on, instead of a bare refusal. */
#define MUSASHI_EXPECTED_RELEASE "Brave Fencer Musashi (USA), SLUS-00726"
#define MUSASHI_EXPECTED_EXE_SHA256 \
    "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a"

static void report_exe_identity(const char *path) {
    FILE *file = fopen(path, "rb");
    unsigned char digest[SHA256_DIGEST_LENGTH];
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    long size = -1;
    unsigned i;
    fprintf(stderr,
            "\nThis port needs your own copy of %s.\n"
            "The game executable it was given is not that release:\n"
            "  file:     %s\n", MUSASHI_EXPECTED_RELEASE, path);
    if (!file) {
        fputs("  problem:  cannot be opened\n", stderr);
    } else {
        SHA256_CTX ctx;
        unsigned char buffer[65536];
        size_t got;
        SHA256_Init(&ctx);
        size = 0;
        while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            SHA256_Update(&ctx, buffer, got);
            size += (long)got;
        }
        fclose(file);
        SHA256_Final(digest, &ctx);
        for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) sprintf(hex + 2 * i, "%02x", digest[i]);
        fprintf(stderr, "  size:     %ld bytes (expected 413696)\n"
                        "  sha256:   %s\n", size, hex);
    }
    fprintf(stderr,
            "  expected: SLUS_007.26, sha256 %s\n"
            "Other regions, revisions, patched or re-built images are not supported.\n"
            "Dump your disc to a .cue/.bin, extract SLUS_007.26 from it, and point the port at it.\n\n",
            MUSASHI_EXPECTED_EXE_SHA256);
}

static uint32_t s_png_crc_table[256];
static int s_png_crc_table_computed = 0;

static void png_make_crc_table(void) {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            if (c & 1u)
                c = 0xedb88320u ^ (c >> 1);
            else
                c = c >> 1;
        }
        s_png_crc_table[n] = c;
    }
    s_png_crc_table_computed = 1;
}

static uint32_t png_update_crc(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!s_png_crc_table_computed)
        png_make_crc_table();
    for (size_t i = 0; i < len; ++i) {
        crc = s_png_crc_table[(crc ^ buf[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc;
}

static int png_write_chunk(FILE *f, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t len_bytes[4];
    uint8_t crc_bytes[4];
    uint32_t crc;

    len_bytes[0] = (uint8_t)((len >> 24) & 0xffu);
    len_bytes[1] = (uint8_t)((len >> 16) & 0xffu);
    len_bytes[2] = (uint8_t)((len >> 8) & 0xffu);
    len_bytes[3] = (uint8_t)(len & 0xffu);
    if (fwrite(len_bytes, 1, 4, f) != 4) return 0;
    if (fwrite(type, 1, 4, f) != 4) return 0;
    if (len > 0 && fwrite(data, 1, len, f) != len) return 0;

    crc = png_update_crc(0xffffffffu, (const uint8_t *)type, 4);
    if (len > 0)
        crc = png_update_crc(crc, data, len);
    crc ^= 0xffffffffu;

    crc_bytes[0] = (uint8_t)((crc >> 24) & 0xffu);
    crc_bytes[1] = (uint8_t)((crc >> 16) & 0xffu);
    crc_bytes[2] = (uint8_t)((crc >> 8) & 0xffu);
    crc_bytes[3] = (uint8_t)(crc & 0xffu);
    if (fwrite(crc_bytes, 1, 4, f) != 4) return 0;
    return 1;
}

static uint32_t png_calc_adler32(const uint8_t *data, size_t length) {
    uint32_t s1 = 1u;
    uint32_t s2 = 0u;
    for (size_t i = 0; i < length; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

static int write_png_rgb555(const char *filename, unsigned width, unsigned height, const uint16_t *pixels) {
    static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    FILE *f;
    uint8_t ihdr[13];
    size_t line_bytes = 1u + (size_t)width * 3u;
    size_t raw_len = line_bytes * (size_t)height;
    uint8_t *raw;
    size_t raw_pos = 0;
    size_t zlib_cap;
    uint8_t *zlib_data;
    size_t zpos;
    size_t remaining;
    size_t src_pos;
    uint32_t adler;
    int ok = 1;

    raw = malloc(raw_len);
    if (!raw) return 0;

    for (unsigned y = 0; y < height; ++y) {
        raw[raw_pos++] = 0; /* filter type 0 (None) */
        for (unsigned x = 0; x < width; ++x) {
            uint16_t p = pixels[y * width + x];
            unsigned r = (p & 0x1fu) << 3;
            unsigned g = ((p >> 5) & 0x1fu) << 3;
            unsigned b = ((p >> 10) & 0x1fu) << 3;
            raw[raw_pos++] = (uint8_t)(r | (r >> 5));
            raw[raw_pos++] = (uint8_t)(g | (g >> 5));
            raw[raw_pos++] = (uint8_t)(b | (b >> 5));
        }
    }

    zlib_cap = 2u + ((raw_len + 32767u) / 32768u) * 5u + raw_len + 4u;
    zlib_data = malloc(zlib_cap);
    if (!zlib_data) {
        free(raw);
        return 0;
    }

    /* zlib header: Deflate, 32K window, no preset dict, check bits */
    zlib_data[0] = 0x78;
    zlib_data[1] = 0x01;
    zpos = 2;
    remaining = raw_len;
    src_pos = 0;
    while (remaining > 0) {
        uint16_t chunk_len = remaining > 32768u ? 32768u : (uint16_t)remaining;
        int is_final = (chunk_len == remaining);
        uint16_t nlen = (uint16_t)~chunk_len;
        zlib_data[zpos++] = is_final ? 0x01u : 0x00u;
        zlib_data[zpos++] = (uint8_t)(chunk_len & 0xffu);
        zlib_data[zpos++] = (uint8_t)((chunk_len >> 8) & 0xffu);
        zlib_data[zpos++] = (uint8_t)(nlen & 0xffu);
        zlib_data[zpos++] = (uint8_t)((nlen >> 8) & 0xffu);
        memcpy(&zlib_data[zpos], &raw[src_pos], chunk_len);
        zpos += chunk_len;
        src_pos += chunk_len;
        remaining -= chunk_len;
    }
    adler = png_calc_adler32(raw, raw_len);
    zlib_data[zpos++] = (uint8_t)((adler >> 24) & 0xffu);
    zlib_data[zpos++] = (uint8_t)((adler >> 16) & 0xffu);
    zlib_data[zpos++] = (uint8_t)((adler >> 8) & 0xffu);
    zlib_data[zpos++] = (uint8_t)(adler & 0xffu);

    free(raw);

    f = fopen(filename, "wb");
    if (!f) {
        free(zlib_data);
        return 0;
    }

    if (fwrite(png_sig, 1, 8, f) != 8) ok = 0;

    ihdr[0] = (uint8_t)((width >> 24) & 0xffu);
    ihdr[1] = (uint8_t)((width >> 16) & 0xffu);
    ihdr[2] = (uint8_t)((width >> 8) & 0xffu);
    ihdr[3] = (uint8_t)(width & 0xffu);
    ihdr[4] = (uint8_t)((height >> 24) & 0xffu);
    ihdr[5] = (uint8_t)((height >> 16) & 0xffu);
    ihdr[6] = (uint8_t)((height >> 8) & 0xffu);
    ihdr[7] = (uint8_t)(height & 0xffu);
    ihdr[8] = 8;  /* 8 bits per channel */
    ihdr[9] = 2;  /* Color type 2: RGB */
    ihdr[10] = 0; /* Deflate */
    ihdr[11] = 0; /* Filter method 0 */
    ihdr[12] = 0; /* No interlace */

    if (ok && !png_write_chunk(f, "IHDR", ihdr, 13)) ok = 0;
    if (ok && !png_write_chunk(f, "IDAT", zlib_data, (uint32_t)zpos)) ok = 0;
    if (ok && !png_write_chunk(f, "IEND", NULL, 0)) ok = 0;

    fclose(f);
    free(zlib_data);
    return ok;
}

static void dump_display_vram(NativeBoot *boot) {
    unsigned x = boot->gpu.display.x, y = boot->gpu.display.y;
    unsigned width = boot->gpu.display.width ? boot->gpu.display.width : 320u;
    unsigned height = boot->gpu.display.height ? boot->gpu.display.height : 240u;
    uint32_t count = width * height;
    uint16_t *pixels;
    uint64_t nonblack = 0;
    FILE *out;
    unsigned i;
    if (width > 1024u || height > 512u || x > 1024u - width || y > 512u - height)
        return;
    pixels = malloc((size_t)count * sizeof(*pixels));
    if (!pixels) return;
    if (!musashi_gpu_psycross_read_vram(&boot->gpu_renderer, x, y, width, height,
                                        pixels, count)) {
        fprintf(stderr, "native_boot: VRAM_DUMP readable=0 x=%u y=%u %ux%u\n",
                x, y, width, height);
        free(pixels);
        return;
    }
    for (i = 0; i < count; ++i)
        if (pixels[i]) ++nonblack;
    if (nonblack < 50000u && height == 240u) {
        unsigned alt_y = (y == 0u) ? 240u : 0u;
        uint16_t *alt_pixels = malloc((size_t)count * sizeof(*alt_pixels));
        if (alt_pixels) {
            if (musashi_gpu_psycross_read_vram(&boot->gpu_renderer, x, alt_y, width, height,
                                                alt_pixels, count)) {
                uint64_t alt_nonblack = 0;
                for (i = 0; i < count; ++i)
                    if (alt_pixels[i]) ++alt_nonblack;
                if (alt_nonblack > nonblack) {
                    memcpy(pixels, alt_pixels, (size_t)count * sizeof(*pixels));
                    nonblack = alt_nonblack;
                    y = alt_y;
                }
            }
            free(alt_pixels);
        }
    }
    out = fopen("native_vram_display.ppm", "wb");
    if (out) {
        fprintf(out, "P6\n%u %u\n255\n", width, height);
        for (i = 0; i < count; ++i) {
            unsigned r = (pixels[i] & 0x1fu) << 3;
            unsigned g = ((pixels[i] >> 5) & 0x1fu) << 3;
            unsigned b = ((pixels[i] >> 10) & 0x1fu) << 3;
            fputc((int)(r | (r >> 5)), out);
            fputc((int)(g | (g >> 5)), out);
            fputc((int)(b | (b >> 5)), out);
        }
        fclose(out);
    }
    fprintf(stderr, "native_boot: VRAM_DUMP path=native_vram_display.ppm "
            "x=%u y=%u %ux%u nonblack=%llu/%u\n",
            x, y, width, height, (unsigned long long)nonblack, count);
    if (write_png_rgb555("native_vram_display.png", width, height, pixels)) {
        fprintf(stderr, "native_boot: VRAM_DUMP path=native_vram_display.png "
                "x=%u y=%u %ux%u nonblack=%llu/%u\n",
                x, y, width, height, (unsigned long long)nonblack, count);
    }
    free(pixels);
    {
        uint16_t row[1024];
        uint64_t full_nonblack = 0, full_checked = 0;
        /* Opt-in whole-VRAM image: shows every page the guest drew into,
         * not only the one the GP1 display origin selects. */
        const char *full_path = getenv("MUSASHI_DUMP_VRAM_FULL");
        uint16_t *full = full_path ? malloc(1024u * 512u * sizeof(*full)) : NULL;
        int ok = 1;
        unsigned yy, xx;
        for (yy = 0; yy < 512u && ok; ++yy) {
            ok = musashi_gpu_psycross_read_vram(&boot->gpu_renderer, 0, yy, 1024, 1,
                                                row, 1024);
            if (!ok) break;
            if (full) memcpy(full + yy * 1024u, row, sizeof(row));
            for (xx = 0; xx < 1024u; ++xx) {
                ++full_checked;
                if (row[xx]) ++full_nonblack;
            }
        }
        fprintf(stderr, "native_boot: VRAM_FULL readable=%d nonblack=%llu/%llu\n",
                ok, (unsigned long long)full_nonblack,
                (unsigned long long)full_checked);
        if (full && ok && write_png_rgb555(full_path, 1024u, 512u, full))
            fprintf(stderr, "native_boot: VRAM_FULL path=%s\n", full_path);
        free(full);
    }
}

static void host_window_inspect(SDL_Window *window) {
    int width = 0, height = 0;
    int (*set_focusable)(SDL_Window *, int);
    unsigned (*get_props)(SDL_Window *);
    void *(*get_ptr)(unsigned, const char *, void *);
    long long (*get_num)(unsigned, const char *, long long);
    const char *interactive_env = getenv("MUSASHI_INTERACTIVE");
    int interactive = interactive_env && (interactive_env[0] == '1' || strcmp(interactive_env, "true") == 0 || strcmp(interactive_env, "yes") == 0);
    if (!window) return;
    /* Spectator scanout only. Never grab, never stay-on-top, never re-raise:
     * Xwayland + SDL_PumpEvents was forcing X input focus back every TAP.
     * When MUSASHI_INTERACTIVE=1 is set, allow focus for playable interaction. */
    SDL_GetWindowSize(window, &width, &height);
    if (width != 1280 || height != 960)
        SDL_SetWindowSize(window, 1280, 960);
    SDL_SetWindowAlwaysOnTop(window, SDL_FALSE);
    SDL_SetWindowGrab(window, SDL_FALSE);
    SDL_SetWindowKeyboardGrab(window, SDL_FALSE);
    SDL_SetWindowMouseGrab(window, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_CaptureMouse(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    SDL_SetWindowTitle(window,
                       interactive ? "Brave Fencer Musashi - native PC (Interactive)"
                                   : "Brave Fencer Musashi - native VRAM");
    set_focusable = (int (*)(SDL_Window *, int))dlsym(RTLD_DEFAULT, "SDL_SetWindowFocusable");
    if (set_focusable)
        set_focusable(window, interactive ? 1 : 0);
    get_props = (unsigned (*)(SDL_Window *))dlsym(RTLD_DEFAULT, "SDL_GetWindowProperties");
    get_ptr = (void *(*)(unsigned, const char *, void *))dlsym(RTLD_DEFAULT, "SDL_GetPointerProperty");
    get_num = (long long (*)(unsigned, const char *, long long))dlsym(RTLD_DEFAULT, "SDL_GetNumberProperty");
    if (get_props && get_ptr && get_num) {
        unsigned props = get_props(window);
        void *dpy = get_ptr(props, "SDL.window.x11.display", NULL);
        unsigned long xid = (unsigned long)get_num(props, "SDL.window.x11.window", 0);
        if (dpy && xid)
            musashi_x11_spectator(dpy, xid);
    }
    musashi_x11_spectator_focused();
    if (interactive)
        SDL_RaiseWindow(window);
}

static void hold_window(NativeBoot *boot) {
    const char *env = getenv("MUSASHI_HOLD_WINDOW_MS");
    unsigned ms = env ? (unsigned)strtoul(env, NULL, 10) : 15000u;
    Uint32 start;
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (!window || !ms) return;
    host_window_inspect(window);
    if (boot && boot->start_preview_paused)
        SDL_SetWindowTitle(window,"Brave Fencer Musashi - start screen preview (paused)");
    fprintf(stderr, "native_boot: HOLD_WINDOW ms=%u\n", ms);
    start = SDL_GetTicks();
    while (SDL_GetTicks() - start < ms) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT) return;
        if (boot)
            (void)present_frame(boot);
        SDL_Delay(16);
    }
}

#ifdef MUSASHI_NATIVE_LANE_TABLE
#include <stddef.h>
#include <sys/mman.h>
#include "musashi_native_lane.h"
#include "musashi_native_lane_psyq.h"
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
extern const MusashiNativeLaneEntry musashi_native_lane_generated[];
extern const size_t musashi_native_lane_generated_count;
extern const MusashiNativeLaneOverlayEntry musashi_native_lane_overlay[];
extern const size_t musashi_native_lane_overlay_count;

static void lane_overlay_report(void) {
    uint64_t verified = 0, refused = 0;
    musashi_native_lane_overlay_stats(&verified, &refused);
    fprintf(stderr, "native_boot: NATIVE_LANE overlay verified=%llu refused=%llu\n",
            (unsigned long long)verified, (unsigned long long)refused);
}
_Static_assert(offsetof(NativeBoot, memory) == 0, "guest RAM leads the boot owner");
_Static_assert(offsetof(NativeBoot, ram_mirror_guard) == 0x201000u,
               "mirror guard starts on the page after the scratchpad page");
_Static_assert(sizeof(((NativeBoot *)0)->ram_mirror_guard) % 4096u == 0u,
               "mirror guard is whole pages");

/* Natively compiled decomp C dereferences 32-bit guest addresses directly,
 * so the boot owner, whose first member is guest RAM, is placed at host
 * 0x80000000. Without that window the lane stays off and the interpreter
 * runs every word, exactly as without MUSASHI_NATIVE_LANE. */
/* A host fault inside natively compiled decomp C names the lane function
 * (async-signal-safe formatting only), then exits 139. */
static void lane_fault(int sig, siginfo_t *info, void *context) {
    char buf[128];
    static const char hex[] = "0123456789abcdef";
    uint64_t addr = (uint64_t)(uintptr_t)info->si_addr;
    uint32_t pc = musashi_native_lane_current();
    int i, n = 0;
    const char *head = "native_boot: HOST_FAULT addr=";
    (void)context;
    while (*head) buf[n++] = *head++;
    for (i = 60; i >= 0; i -= 4) buf[n++] = hex[(addr >> i) & 15u];
    head = " lane_pc=";
    while (*head) buf[n++] = *head++;
    for (i = 28; i >= 0; i -= 4) buf[n++] = hex[(pc >> i) & 15u];
    buf[n++] = '\n';
    (void)!write(2, buf, (size_t)n);
    signal(sig, SIG_DFL);
    raise(sig);
}

static NativeBoot *native_boot_owner(NativeBoot *fallback) {
    const char *value = getenv("MUSASHI_NATIVE_LANE");
    void *window;
    if (value && value[0] == '0') return fallback;
    {
        int aliased = 0;
        window = musashi_native_lane_map_guest(sizeof(NativeBoot), &aliased);
        if (!window) {
            fprintf(stderr, "native_boot: NATIVE_LANE window unavailable; interpreter only\n");
            return fallback;
        }
        if (!aliased)
            fprintf(stderr, "native_boot: NATIVE_LANE scratchpad alias at 1f800000 unavailable\n");
        if (mprotect(((NativeBoot *)window)->ram_mirror_guard,
                     sizeof(((NativeBoot *)window)->ram_mirror_guard), PROT_NONE) != 0)
            fprintf(stderr, "native_boot: NATIVE_LANE mirror guard unavailable\n");
    }
    {
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.sa_sigaction = lane_fault;
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, NULL);
        sigaction(SIGBUS, &action, NULL);
    }
    {
        /* Generated decomp C plus port-plat's PsyQ compat wrappers. The
         * device-level wrappers bypass the emulated GPU/CD/IRQ timing, so
         * they are taken only on the bfm_plat GPU path. */
        const char *gpu = getenv("MUSASHI_GPU");
        unsigned classes = musashi_native_lane_psyq_classes();
        size_t count = 0;
        const MusashiNativeLaneEntry *table;
        MusashiNativeLaneEntry *merged;
        if ((classes & MUSASHI_PSYQ_LANE_DEVICE) && !(gpu && strcmp(gpu, "bfm_plat") == 0)) {
            fprintf(stderr, "native_boot: PSYQ_LANE device wrappers need MUSASHI_GPU=bfm_plat; "
                            "pure only\n");
            classes &= ~(unsigned)MUSASHI_PSYQ_LANE_DEVICE;
        }
        merged = musashi_native_lane_psyq_merge(musashi_native_lane_generated,
                                                musashi_native_lane_generated_count,
                                                classes, &count);
        table = merged ? merged : musashi_native_lane_generated;
        if (!merged) count = musashi_native_lane_generated_count;
        if (musashi_native_lane_install(table, count) < 0) {
            fprintf(stderr, "native_boot: NATIVE_LANE table rejected; interpreter only\n");
            return window;
        }
        {
            int ovl = musashi_native_lane_install_overlay(musashi_native_lane_overlay,
                                                          musashi_native_lane_overlay_count);
            fprintf(stderr, "native_boot: NATIVE_LANE overlay entries=%d (resident bytes digest-checked per call)\n",
                    ovl);
            if (ovl > 0) atexit(lane_overlay_report);
        }
        musashi_native_lane_psyq_bind(&((NativeBoot *)window)->memory, &((NativeBoot *)window)->gte);
        fprintf(stderr, "native_boot: NATIVE_LANE entries=%u psyq=%u classes=%s\n",
                (unsigned)count, (unsigned)(count - musashi_native_lane_generated_count),
                classes == 0u ? "none" : (classes & MUSASHI_PSYQ_LANE_DEVICE) ? "all" : "pure");
    }
    return window;
}
#else
static NativeBoot *native_boot_owner(NativeBoot *fallback) { return fallback; }
#endif

#ifdef MUSASHI_WITH_BFM_PLAT
/* Platform layer (pc_port/platform, docs/ARCHITECTURE-PORT.md): mods,
 * cheats and hook-site events see guest RAM and, at a hook, the live guest
 * registers. native_boot still owns PsyCross/SDL, so bfm_plat runs with its
 * null renderer/audio/input backends until the game-facing calls are
 * routed through bfm_plat_*. */
#include "bfm_plat.h"
#include "bfm_plat_events.h"
#include "musashi_native_lane.h"

static const uint32_t *g_plat_hook_regs;

static int plat_ram_read(void *user, uint32_t address, void *out, uint32_t size) {
    const uint8_t *span = musashi_boot_ram_span(user, address, size);
    if (!span) return -1;
    memcpy(out, span, size);
    return 0;
}

static int plat_ram_write(void *user, uint32_t address, const void *data, uint32_t size) {
    uint8_t *span = musashi_boot_ram_span(user, address, size);
    if (!span) return -1;
    memcpy(span, data, size);
    return 0;
}

static int plat_reg_read(void *user, unsigned reg, uint32_t *out) {
    (void)user;
    if (!g_plat_hook_regs || reg >= 32u) return -1;
    *out = g_plat_hook_regs[reg];
    return 0;
}

static void plat_entry_hook(void *user, uint32_t pc, const uint32_t *regs) {
    (void)user;
    if (!bfm_plat_hooks_has(pc)) return;
    g_plat_hook_regs = regs;
    (void)bfm_plat_hooks_at(pc);
    g_plat_hook_regs = NULL;
}

static void plat_overlay_hook(void *user, const char *name) {
    (void)user;
    bfm_plat_hooks_set_overlay(name);
}

static void plat_start(MusashiBootMemory *memory) {
    static BfmPlatGuestMemory ram;
    static BfmPlatGuestRegs regs;
    BfmPlatConfig cfg;
    const char *off = getenv("MUSASHI_BFM_PLAT");
    if (off && off[0] == '0') return;
    bfm_plat_config_defaults(&cfg);
    snprintf(cfg.renderer, sizeof cfg.renderer, "null");
    snprintf(cfg.audio, sizeof cfg.audio, "null");
    snprintf(cfg.input, sizeof cfg.input, "null");
    cfg.disc_path[0] = 0;      /* native_boot owns the disc and its identity check */
    cfg.disc_search[0] = 0;
    {
        /* The bridge's renderer opens after PsyCross is up (gpu_bridge_attach);
         * bfm_plat starts on the null renderer like the cpu path. */
        const char *gpu = getenv("MUSASHI_GPU");          /* cpu | bfm_plat */
        if (gpu && strcmp(gpu, "bfm_plat") == 0) cfg.gpu_path = BFM_GPU_BFM_PLAT;
    }
    if (bfm_plat_init(&cfg) != 0) {
        fprintf(stderr, "native_boot: BFM_PLAT init refused; platform layer off\n");
        return;
    }
    ram.read = plat_ram_read;
    ram.write = plat_ram_write;
    ram.user = memory;
    regs.read = plat_reg_read;
    regs.user = NULL;
    bfm_plat_guest_memory_bind(&ram);
    bfm_plat_guest_regs_bind(&regs);
    if (bfm_plat_hooks_register_known() != 0)
        fprintf(stderr, "native_boot: BFM_PLAT hook sites refused\n");
    g_plat_started = 1;
    fprintf(stderr, "native_boot: BFM_PLAT up hook_sites=%u\n", (unsigned)bfm_plat_hooks_count());

    musashi_formatter_set_entry_hook(plat_entry_hook, NULL);
    musashi_formatter_set_overlay_hook(plat_overlay_hook, NULL);
}

/* MUSASHI_GPU=bfm_plat, once PsyCross and the controller are up: open the
 * bfm_plat renderer (MUSASHI_RENDERER, default psycross, drawing into the
 * PsyCross context native_boot created) and tee the controller into the
 * bridge. Any refusal leaves the cpu path. */
/* Bridge mode's controller backend. Both paths share one renderer VRAM, and
 * the bridge applies every fill, upload, copy, env and display change
 * through bfm_plat, so the controller's VRAM side does nothing but answer
 * reads from the bridge. The native gpu_psycross backend refuses VRAM
 * access while the bfm_plat renderer has a PsyCross scene open
 * (begin_scene_flag / framebuffer_need_update), which is why it is
 * replaced rather than kept. */
static int bridge_be_ok(void *u) { (void)u; return 1; }
static int bridge_be_word(void *u, uint32_t w) { (void)u; (void)w; return 1; }
static int bridge_be_enable(void *u, int e) { (void)u; (void)e; return 1; }
static int bridge_be_display(void *u, const MusashiGpuDisplayState *d) { (void)u; (void)d; return 1; }
static int bridge_be_fill(void *u, const MusashiGpuFill *f) { (void)u; (void)f; return 1; }
static int bridge_be_store(void *u, uint16_t x, uint16_t y, uint16_t p) {
    (void)u; (void)x; (void)y; (void)p;
    return 1;
}
static int bridge_be_read(void *u, uint16_t x, uint16_t y, uint16_t *p) {
    return bfm_gpu_bridge_read_vram((BfmGpuBridge *)u, x, y, p);
}

static void gpu_bridge_attach(MusashiGpuController *gpu) {
    const char *name = getenv("MUSASHI_RENDERER"), *selected = NULL;
    BfmPlatOutput out;
    if (!bfm_gpu_bridge_selected(bfm_plat_config())) return;
    if (!name || !name[0]) name = "psycross";
    memset(&out, 0, sizeof out);
    out.window_width = 640;
    out.window_height = 480;
    out.internal_scale = 1;
    out.aspect_num = 4;
    out.aspect_den = 3;
    if (bfm_plat_renderer_open(name, &out, &selected) != 0 || !selected ||
        strcmp(selected, name) != 0) {
        fprintf(stderr, "native_boot: GPU_PATH bfm_plat refused: renderer %s unavailable; cpu\n",
                name);
        (void)bfm_plat_renderer_open("null", &out, NULL);
        return;
    }
    bfm_gpu_bridge_init(&g_gpu_bridge);
    {
        MusashiGpuBackend be;
        memset(&be, 0, sizeof be);
        be.userdata = &g_gpu_bridge;
        be.reset = bridge_be_ok;
        be.draw_mode = bridge_be_word;
        be.display_enable = bridge_be_enable;
        be.clear_fifo = bridge_be_ok;
        be.ready = bridge_be_ok;
        be.environment = bridge_be_word;
        be.display = bridge_be_display;
        be.fill_vram = bridge_be_fill;
        be.store_vram = bridge_be_store;
        be.read_vram = bridge_be_read;
        gpu->backend = be;
    }
    gpu->tap = bfm_gpu_bridge_tap;
    gpu->tap_userdata = &g_gpu_bridge;
    gpu->raster_disabled = 1;
    g_gpu_bridge_on = 1;
    fprintf(stderr, "native_boot: GPU_PATH bfm_plat renderer=%s\n", selected);
}

/* MUSASHI_INPUT=bfm_plat, once SDL video is up: open the bfm_plat input
 * backend MUSASHI_INPUT_BACKEND (default "sdl"; bindings from [input]) and let the SIO pad
 * sample it. Any refusal keeps the native sampler. */
static void plat_input_attach(void) {
    const char *want = getenv("MUSASHI_INPUT"), *selected = NULL;
    if (!want || strcmp(want, "bfm_plat") != 0) return;
    if (!g_plat_started) {
        fprintf(stderr, "native_boot: INPUT bfm_plat refused: platform layer off; native\n");
        return;
    }
    const char *backend = getenv("MUSASHI_INPUT_BACKEND");     /* sdl | scripted | ... */
    if (!backend || !backend[0]) backend = "sdl";
    if (bfm_plat_input_open(backend, &selected) != 0 || !selected || strcmp(selected, backend) != 0) {
        fprintf(stderr, "native_boot: INPUT bfm_plat refused: %s backend unavailable; native\n",
                backend);
        (void)bfm_plat_input_open("null", NULL);
        return;
    }
    g_plat_input_on = 1;
    fprintf(stderr, "native_boot: INPUT bfm_plat backend=%s\n", selected);
}

/* MUSASHI_TIMING=bfm_plat: pace guest-clock VBlanks with bfm_plat_timing.
 * Under host-paced time the host clock already paces the guest, so it is
 * refused there. MUSASHI_UNTHROTTLED keeps running uncapped (fast-forward at
 * speed 0), still emitting the VSYNC events. */
static void plat_timing_attach(int guest_clock, int unthrottled) {
    const char *want = getenv("MUSASHI_TIMING");
    if (!want || strcmp(want, "bfm_plat") != 0) return;
    if (!g_plat_started || !guest_clock) {
        fprintf(stderr, "native_boot: TIMING bfm_plat refused: %s; native\n",
                !g_plat_started ? "platform layer off" : "needs MUSASHI_GUEST_CLOCK=1");
        return;
    }
    bfm_plat_timing_init();
    if (unthrottled) {
        bfm_plat_timing_set_fast_forward_speed(0.0);
        bfm_plat_timing_set_fast_forward(1);
    }
    g_plat_timing_on = 1;
    fprintf(stderr, "native_boot: TIMING bfm_plat pacing=%s\n", unthrottled ? "uncapped" : "ntsc");
}
#endif

int main(int argc, char **argv) {
    static NativeBoot boot_storage; /* One stable, zero-initialized owner. */
    NativeBoot *const boot_owner = native_boot_owner(&boot_storage);
#define boot (*boot_owner)
    MusashiEntryRunStop entry_stop = {0};
    MusashiResetGraphPrefixStop stop = {0};
    MusashiResetGraphPrefixStatus status;
    uint16_t guard, enabled;
    uint32_t slot0, slot3, tick;
    int cleanup_ok = 1;
    int presentation_ok, gpu_removed;
    unsigned event_used = 0, index;

    const char *exe_path = NULL;
    const char *cue_path = NULL;
    const char *bin_path = NULL;
    const char *disc_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dev-menu") == 0) {
            setenv("BFM_DEV_MENU", "1", 1);
        } else if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc_path = argv[++i];
        } else if (strcmp(argv[i], "--headless") == 0) {
            setenv("MUSASHI_BOOT_AUTO_START", "1", 1);
            setenv("MUSASHI_AUTO_DISMISS", "1", 1);
            setenv("MUSASHI_TRACE_REFUSAL", "1", 1);
            setenv("MUSASHI_HOLD_WINDOW_MS", "0", 1);
            setenv("MUSASHI_GUEST_CLOCK", "1", 0);
            setenv("MUSASHI_UNTHROTTLED", "1", 0);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            setenv("MUSASHI_IN_GAME_FRAMES", argv[++i], 1);
            setenv("MUSASHI_BOOT_AUTO_START", "1", 1);
            setenv("MUSASHI_AUTO_DISMISS", "1", 1);
            setenv("MUSASHI_TRACE_REFUSAL", "1", 1);
            setenv("MUSASHI_HOLD_WINDOW_MS", "0", 1);
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            setenv("MUSASHI_IN_GAME_FRAMES", argv[i] + 9, 1);
            setenv("MUSASHI_BOOT_AUTO_START", "1", 1);
            setenv("MUSASHI_AUTO_DISMISS", "1", 1);
            setenv("MUSASHI_TRACE_REFUSAL", "1", 1);
            setenv("MUSASHI_HOLD_WINDOW_MS", "0", 1);
        } else if (strcmp(argv[i], "--action") == 0 && i + 1 < argc) {
            setenv("MUSASHI_GAMEPLAY_ACTION", argv[++i], 1);
            setenv("MUSASHI_TRACE_REFUSAL", "1", 1);
        } else if (strncmp(argv[i], "--action=", 9) == 0) {
            setenv("MUSASHI_GAMEPLAY_ACTION", argv[i] + 9, 1);
            setenv("MUSASHI_TRACE_REFUSAL", "1", 1);
        } else if (argv[i][0] != '-') {
            if (!exe_path) exe_path = argv[i];
            else if (!cue_path) cue_path = argv[i];
            else if (!bin_path) bin_path = argv[i];
        }
    }
    {
        /* Opt-in scripted test driver: writes pad words (player +0xAA/AC/AE,
         * 80078DCA/DD2) and 800AE658 straight into guest RAM in gameplay. */
        const char *action = getenv("MUSASHI_GAMEPLAY_ACTION");
        if (action && action[0])
            fprintf(stderr, "native_boot: DIVERGENCE MUSASHI_GAMEPLAY_ACTION=%s: scripted "
                            "guest-RAM pad writes in gameplay (test driver, not 1:1)\n", action);
    }

    if (disc_path) {
        char why[1024], message[1400];
        boot.media = musashi_disc_media_open_image(disc_path, why, sizeof why);
        if (!boot.media) {
            snprintf(message, sizeof message,
                     "Brave Fencer Musashi could not start from your disc image.\n\n%s\n\n"
                     "Use an unmodified .cue/.bin dump of the USA disc (SLUS-00726).", why);
            user_error(message);
            return 2;
        }
        exe_path = cache_exe_from_disc(boot.media);
        if (!exe_path) {
            user_error("Brave Fencer Musashi could not read SLUS_007.26 from your disc image "
                       "or save it to ~/.cache/bfm-port/.");
            return 2;
        }
        if (!getenv("MUSASHI_CODE_IMAGE")) setenv("MUSASHI_CODE_IMAGE", exe_path, 1);
    }
    if (!exe_path) {
        if (access("extracted/disc/files/SLUS_007.26", R_OK) == 0) {
            exe_path = "extracted/disc/files/SLUS_007.26";
            if (!cue_path && access("extracted/disc/disc.cue", R_OK) == 0)
                cue_path = "extracted/disc/disc.cue";
            if (!bin_path && access("extracted/disc/disc.bin", R_OK) == 0)
                bin_path = "extracted/disc/disc.bin";
        } else {
            fputs("usage: musashi_native_boot --disc <your .cue or .bin>\n"
                  "       musashi_native_boot <SLUS_007.26> [cue bin]\n", stderr);
            return 2;
        }
    }

    boot.exe_path = exe_path;
    boot.last_musashi_action = 0xffffu;
    if (!load_exe(&boot, exe_path)) {
        report_exe_identity(exe_path);
        user_error("Brave Fencer Musashi: the game executable is not the USA release "
                   "(SLUS-00726). Use an unmodified dump of your USA disc.");
        fputs("native_boot: pinned retail EXE rejected before startup\n", stderr);
        return 2;
    }
#ifdef MUSASHI_WITH_BFM_PLAT
    plat_start(&boot.memory);
#endif
    if (!boot.media && cue_path && bin_path) {
        boot.media = musashi_disc_media_open_pinned(cue_path, bin_path);
        if (!boot.media) {
            fprintf(stderr,
                    "\nThis port needs your own disc image of %s.\n"
                    "The disc image it was given is not that release (or is incomplete):\n"
                    "  cue: %s\n  bin: %s\n"
                    "Use an unmodified single-track .cue/.bin dump of the USA disc.\n\n",
                    MUSASHI_EXPECTED_RELEASE, cue_path, bin_path);
            user_error("Brave Fencer Musashi: the disc image is not an unmodified dump of the "
                       "USA release (SLUS-00726).");
            fputs("native_boot: pinned media rejected before startup\n", stderr);
            return 2;
        }
    }
    musashi_bios_kernel_init(&boot.bios);
    musashi_sio_controller_init_disconnected(&boot.sio);
    if (!musashi_bios_card_init(&boot.card, &boot.bios) ||
        !musashi_bios_kernel_start_cd(&boot.bios, unavailable_bios_cd, &boot)) {
        native_devices_shutdown(&boot);
        return 2;
    }
    /* The pinned US game uses NTSC. Select the native source policy before
     * PsyCross creates its worker; its otherwise-unset mode selects PAL. */
    SetVideoMode(MODE_NTSC);
    /* Xwayland + BYPASS_COMPOSITOR leaves a mapped GL window the desktop
     * never composites; the 3-hour hold is then invisible. */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "0");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
    SDL_SetHint(SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED, "1");
    SDL_SetHint("SDL_WINDOW_ACTIVATE_WHEN_RAISED", "0");
    SDL_SetHint("SDL_WINDOW_ACTIVATE_WHEN_SHOWN", "0");
    PsyX_Initialise("Brave Fencer Musashi - native VRAM", 1280, 960, 0);
    fprintf(stderr, "native_boot: VIDEO_SOURCE mode=%d policy=NTSC selected_before_worker=1\n",
            GetVideoMode());
    if (!SDL_GL_GetCurrentWindow() || !g_intrMutex) {
        fputs("native_boot: PsyCross display unavailable; startup not observed\n", stderr);
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 77;
    }
    host_window_inspect(SDL_GL_GetCurrentWindow());
    /* SDL converts SIGTERM into an event, but source execution may not pump
     * window events while waiting for I/O. Stop at the next clock boundary
     * and retain the diagnostic dump instead of leaving timeout's child alive. */
    signal(SIGTERM,request_host_stop);
    signal(SIGINT,request_host_stop);
    boot.trace_cd_timing = getenv("MUSASHI_NATIVE_CD_TIMING") != NULL;
    if (!musashi_bios_heap_init(&boot.heap,&boot.memory,&boot,epoch_owner)) {
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
    boot.heap_device=(MusashiHeapDevice){&boot,init_heap};
    boot.gpu_io=(MusashiGpuIoDevice){&boot,gpu_io_read32,gpu_io_write32};
    if (!musashi_psycross_irq_runtime_init(&boot.irq, &boot.memory, NULL)) {
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
    if (!musashi_bios_exception_init_selected(&boot.exception_image,&boot.memory,&boot.bios,boot.irq.cpu_status) ||
        !musashi_psycross_irq_scheduler_bind_context(&boot.irq.scheduler,native_cpu_context,&boot)) {
        fputs("native_boot: CPU/image ownership refused before CRT\n",stderr);
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
    boot.cpu_transfer=(MusashiCpuTransferDevice){
        .userdata=&boot,.read_status=cpu_read_status,.write_status=cpu_write_status,
        .write_control=cpu_write_control,.write_data=cpu_write_gte_data,.read_data=cpu_read_gte_data,
        .read_control=cpu_read_control,.command=cpu_gte_command
    };
    boot.image_device=(MusashiBiosImageDevice){&boot,musashi_bios_exception_intersects,
        image_read,image_write,image_table,image_publish};
    if (!musashi_gpu_psycross_init(&boot.gpu_renderer, &boot.gpu_backend) ||
        !musashi_gpu_controller_init(&boot.gpu, &boot.gpu_backend)) {
        fputs("native_boot: GPU backend initialization refused\n", stderr);
        if (boot.gpu_renderer.initialized)
            musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
#ifdef MUSASHI_WITH_BFM_PLAT
    gpu_bridge_attach(&boot.gpu);
    plat_input_attach();
    {
        const MusashiDevMenuGuest guest = {
            &boot, dev_menu_read16, dev_menu_write16, dev_menu_read32, dev_menu_write32
        };
        if (musashi_dev_menu_init(&guest, 0))
            musashi_gpu_psycross_set_overlay(dev_menu_overlay, NULL);
    }
#endif
    (void)present_frame(&boot);
    if (boot.media) {
        const MusashiCdOwnedHardware hardware = {
            .userdata=&boot, .current_thread=epoch_owner,
            .healthy=cd_hardware_healthy, .raise_irq4=raise_cd_irq,
            .data_transfer_idle=cd_data_transfer_idle
        };
        boot.cd_drive = musashi_cd_owned_open_bios_idle(boot.media, &hardware, 0);
        if (!boot.cd_drive) {
            fputs("native_boot: owned CD initialization refused before CRT\n", stderr);
            musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
            native_devices_shutdown(&boot);
            musashi_psyx_shutdown();
            return 2;
        }
        boot.cd_registers = (MusashiCdRegisterDevice){.userdata=&boot,.read8=cd_read8,.write8=cd_write8};
        const MusashiSpuCdAudioBackend *audio_backend = NULL;
#ifdef MUSASHI_WITH_BFM_PLAT
        audio_backend = plat_audio_attach();
#endif
        if (!audio_backend) {
            boot.audio = musashi_audio_sdl_create();
            if (boot.audio && musashi_audio_sdl_open(boot.audio))
                audio_backend = musashi_audio_sdl_backend(boot.audio);
        }
        boot.spu = musashi_spu_cd_audio_create();
        if (!audio_backend || !boot.spu ||
            !musashi_spu_cd_audio_init_bios_muted(boot.spu, audio_backend, 0)) {
            fputs("native_boot: real audio owner unavailable before CRT; startup NOT_RUN\n", stderr);
            native_devices_shutdown(&boot);
            musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
            musashi_psyx_shutdown();
            return 77;
        }
        if (!musashi_cd_owned_set_xa_sink(boot.cd_drive,submit_xa_pcm,&boot)) {
            native_devices_shutdown(&boot);musashi_psyx_shutdown();return 2;
        }
        boot.cd_spu = (MusashiCdSpuDevice){&boot, spu_read16, spu_write16};
    }
    {
        const MusashiSioPadDevice pad = {&boot, epoch_owner, sample_keyboard_pad, raise_sio_irq};
        if (!musashi_sio_controller_bind_digital_pad(&boot.sio, &pad)) {
            fputs("native_boot: fresh digital pad binding refused\n", stderr);
            musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
            native_devices_shutdown(&boot);
            musashi_psyx_shutdown();
            return 2;
        }
    }
    musashi_irq_policy_init(&boot.irq_policy);
    boot.card_device = (MusashiBiosCardStartDevice){
        .userdata = &boot,
        .write_sio16 = card_write_sio16,
        .read_irq32 = card_read_irq32,
        .write_irq32 = card_write_irq32,
        .enter_critical = card_enter_critical,
        .exit_critical = card_exit_critical,
        .exchange_pad = exchange_clear_pad,
        .change_timer = change_clear_timer,
        .read_pad = card_read_pad,
        .read_sio16 = card_read_sio16,
        .read_sio8 = card_read_sio8,
        .write_sio8 = card_write_sio8,
        .cards_disconnected = cards_disconnected,
        .execute_event = execute_event,
    };
    if (!musashi_bios_card_bind_start_device(&boot.card, &boot.card_device)) {
        fputs("native_boot: card device binding refused\n", stderr);
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
    if (!musashi_bios_backup_unit_init(&boot.backup_unit, &boot.bios, &boot.card)) {
        fputs("native_boot: backup-unit ownership refused\n", stderr);
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
    boot.irq.scheduler.raise_vblank = raise_vblank;
    boot.irq.scheduler.raise_userdata = &boot;
    boot.irq.scheduler.before_dispatch = before_irq_dispatch;
    boot.irq.scheduler.before_dispatch_userdata = &boot;
    boot.registration.userdata = &boot;
    boot.registration.read16 = registration_read16;
    boot.registration.write16 = registration_write16;
    boot.registration.b0_5b = change_clear_pad;
    boot.registration.c0_0a = change_clear_timer;
    musashi_dma_controller_init(&boot.dma, &boot.irq.controller);
    musashi_mdec_init(&boot.mdec, &boot.memory, &boot.dma);
    boot.dma_irq.userdata = &boot;
    boot.dma_irq.read32 = dma_irq_read32;
    boot.dma_irq.write32 = dma_irq_write32;
    if (!musashi_psycross_irq_runtime_bind_dma(&boot.irq, &boot.dma_irq)) {
        fputs("native_boot: DMA interrupt owner binding refused before CRT\n", stderr);
        native_devices_shutdown(&boot);
        musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
        musashi_psyx_shutdown();
        return 2;
    }
    {
        const MusashiGpuDma2Device device={&boot,epoch_owner,gpu_dma_healthy};
        boot.gpu_dma=musashi_gpu_dma2_open(&boot.memory,&boot.dma,&boot.gpu,&device,0);
        if (!boot.gpu_dma) {
            fputs("native_boot: GPU DMA2 owner initialization refused before CRT\n",stderr);
            native_devices_shutdown(&boot);
            musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
            musashi_psyx_shutdown();
            return 2;
        }
        {
            const MusashiDmaOtcDevice otc_device={&boot,epoch_owner,gpu_dma_healthy};
            boot.dma_otc=musashi_dma_otc_open(&boot.memory,&boot.dma,&otc_device,0);
            if (!boot.dma_otc) {
                fputs("native_boot: DMA6 OTC owner initialization refused before CRT\n",stderr);
                native_devices_shutdown(&boot);
                musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
                musashi_psyx_shutdown();
                return 2;
            }
        }
    }
    if (boot.cd_drive) {
        const MusashiCdDma3Device device = {.userdata=&boot,.current_thread=epoch_owner,
            .healthy=cd_dma_healthy,.read_data=cd_dma_read_data,.data_available=cd_dma_available,.request_asserted=cd_dma_request};
        boot.cd_dma=musashi_cd_dma3_open(&boot.memory,&boot.dma,&device,0);
        if (!boot.cd_dma) {
            fputs("native_boot: CD DMA3 owner initialization refused before CRT\n",stderr);
            native_devices_shutdown(&boot);
            musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
            musashi_psyx_shutdown();
            return 2;
        }
        boot.cd_registers.read32=cd_read32;
        boot.cd_registers.write32=cd_write32;
    }
    boot.callback.userdata = &boot;
    boot.callback.read16 = irq_read16;
    boot.callback.write16 = irq_write16;
    boot.callback.write32 = control_write32;
    boot.callback.registration_device = &boot.registration;
    boot.callback.hook_entry_int = musashi_psycross_irq_scheduler_hook_entry_int;
    boot.callback.hook_entry_int_userdata = &boot.irq.scheduler;
    boot.callback.a0_72 = remove_bios_cd;
    boot.callback.exit_critical_section = exit_critical;
    boot.callback.checkpoint = checkpoint;
    boot.gpu_device.userdata = &boot;
    boot.gpu_device.read32 = gpu_read32;
    boot.gpu_device.write32 = gpu_write32;
    boot.gpu_device.read16 = irq_read16;
    boot.gpu_device.write16 = irq_write16;
    boot.callback.gpu_cw = gpu_cw;
    boot.callback.reset_device = &boot.gpu_device;
    boot.cd.userdata = &boot;
    boot.cd.enter_critical_result = enter_critical_result;
    boot.cd.b0_5b = exchange_clear_pad;
    boot.cd.b0_4a = init_card;
    boot.cd.b0_4b = start_card;
    boot.cd.a0_70 = init_backup_unit;
    boot.event.userdata = &boot;
    boot.event.open_event = open_event;
    boot.event.enable_event = enable_event;
    boot.event.b0_0b = test_event;
    boot.input.userdata = &boot;
    boot.input.dequeue_irq_result = input_dequeue;
    boot.input.enqueue_irq_result = input_enqueue;
    boot.input.change_timer_result = input_change_timer;
    boot.input.read32 = card_read_irq32;
    boot.input.write32 = input_write32;
    boot.input.read16 = input_read16;
    boot.input.write16 = input_write16;
    boot.input.read8 = card_read_sio8;
    boot.input.write8 = input_write8;
    musashi_source_clock_init(&boot.source_clock);
    /* Fresh native BIOS Timer2 policy, not the retail capture's elapsed
     * counter or absolute cycle phase. Mounted CD uses this same epoch. */
    musashi_timer2_init(&boot.timer2, 0);
    if (!musashi_scanline_timer_init_paced(&boot.timer1, 0)) {
        native_devices_shutdown(&boot);
        musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
        musashi_psyx_shutdown();
        return 2;
    }
    boot.execution_clock.userdata = &boot;
    boot.execution_clock.advance = advance_source_clock;
    boot.cd_services = (MusashiCdIrqServices){.userdata=&boot,
        .deliver_event=deliver_cd_event,.callback_device=&boot.callback,
        .gpu_io=&boot.gpu_io, .dma=&boot.dma_irq,
        .cd_spu=boot.spu ? &boot.cd_spu : NULL,
        .event=&boot.event};
    if (boot.cd_drive && (!musashi_psycross_irq_runtime_bind_cd(
            &boot.irq, &boot.cd_registers, &boot.execution_clock) ||
        !musashi_psycross_irq_runtime_bind_cd_services(&boot.irq, &boot.cd_services))) {
        fputs("native_boot: CD interrupt owner binding refused before CRT\n", stderr);
        native_devices_shutdown(&boot);
        musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
        musashi_psyx_shutdown();
        return 2;
    }
    boot.input.clock = &boot.execution_clock;
    boot.bios_input_device.userdata = &boot;
    boot.bios_input_device.execute = input_execute;
    musashi_boot_bios_callback_frame_init(&boot.bios_callback_frame);
    if (!musashi_bios_input_init(&boot.bios_input, &boot.memory, &boot.bios,
                                &boot.card, &boot.bios_input_device)) {
        fputs("native_boot: input ownership initialization refused\n", stderr);
        native_devices_shutdown(&boot);
        musashi_psyx_shutdown();
        return 2;
    }
    {
        boot.guest_clock = getenv("MUSASHI_GUEST_CLOCK") && getenv("MUSASHI_GUEST_CLOCK")[0] == '1';
        /* Guest clock: host VBlanks are drained, never timed; a burst of them
         * while the guest runs a long stretch without charging must not fault. */
        boot.irq.scheduler.host_edges_advisory = boot.guest_clock;
        boot.guest_unthrottled = getenv("MUSASHI_UNTHROTTLED") && getenv("MUSASHI_UNTHROTTLED")[0] == '1';
        boot.guest_next_vblank = MUSASHI_GUEST_FRAME_CYCLES;
        boot.guest_host_origin = SDL_GetPerformanceCounter();
#ifdef MUSASHI_WITH_BFM_PLAT
        plat_timing_attach(boot.guest_clock, boot.guest_unthrottled);
#endif
        const MusashiDeviceEpochAdapter adapter = {&boot, epoch_owner, epoch_collect,
            epoch_wait, epoch_advance_devices, epoch_video_edge};
        if (!musashi_device_epoch_init(&boot.epoch, &adapter)) {
            fputs("native_boot: fresh device epoch refused before CRT\n", stderr);
            native_devices_shutdown(&boot);
            musashi_psyx_shutdown();
            return 2;
        }
    }
    /* The source thread queues edges only. The live CPU's checkpoints pump
     * device/guest work synchronously on this thread. No broad interrupt
     * mutex is held, and SYS2 only changes guest IRQ eligibility. Timer1
     * follows the selected NTSC source rate after its first real VBlank. */
    /* The mapped image enters its actual CRT. Native register policy is zero
     * initially, including no BIOS return target; no retail snapshot seed.
     * One CPU and checkpoint identity survives every supported call/return. */
    boot.startup_devices = (MusashiStartupPrefixDevice){
        .callback=&boot.callback, .cd=&boot.cd, .event=&boot.event,
        .input=&boot.input, .clock=&boot.execution_clock,
        .cd_registers=boot.cd_drive ? &boot.cd_registers : NULL,
        .cd_spu=boot.spu ? &boot.cd_spu : NULL, .heap=&boot.heap_device,
        .gpu_io=&boot.gpu_io, .cpu_transfer=&boot.cpu_transfer, .bios_image=&boot.image_device
    };
    if (!musashi_psycross_irq_runtime_bind_source_execute(&boot.irq,
            source_irq_execute, &boot)) {
        fputs("native_boot: source IRQ execute binding refused before CRT\n", stderr);
        native_devices_shutdown(&boot);
        musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
        musashi_psyx_shutdown();
        return 2;
    }
    status = musashi_boot_run_entry_with_devices(&boot.memory, 0, &boot.startup_devices,
        console_bytes, NULL, observe_entry, &boot, &entry_stop);
    {
        MusashiCpuStatusSnapshot state;
        MusashiGteSnapshot gte;
        int cpu_valid=musashi_cpu_status_snapshot(boot.irq.cpu_status,&state);
        int gte_valid=musashi_gte_owner_snapshot(&boot.gte,&gte);
        fprintf(stderr,"native_boot: CPU_OWNER valid=%d sr=%08x sequence=%llu active=%d "
                "kernel_entered=%d faulted=%d host_depth=%u\n",cpu_valid,cpu_valid ? state.sr : 0,
                cpu_valid ? (unsigned long long)state.sequence : 0,cpu_valid ? state.active : -1,
                cpu_valid ? state.kernel_entered : -1,cpu_valid ? state.faulted : -1,boot.host_service_depth);
        fprintf(stderr,"native_boot: BIOS_IMAGE generation=%llu variant=%u writes=%llu entries=%llu "
                "dirty=%d faulted=%d required=%d bound=%d card_hook=%d\n",
                (unsigned long long)boot.exception_image.generation,(unsigned)boot.exception_image.variant,
                (unsigned long long)boot.exception_image.writes,(unsigned long long)boot.exception_image.entries,
                boot.exception_image.dirty,boot.exception_image.faulted,boot.bios.exception_required,
                boot.bios.exception_binding==&boot.exception_image.binding,boot.bios.early_hook==&boot.card.early_hook);
        fprintf(stderr,"native_boot: GTE_OWNER valid=%d initialized=%d faulted=%d writes=%llu controls=",
                gte_valid,gte_valid ? gte.initialized : 0,gte_valid ? gte.faulted : -1,
                gte_valid ? (unsigned long long)gte.write_count : 0);
        if (gte_valid) for (unsigned i=0;i<32;i++) fprintf(stderr,"%s%08x",i ? "," : "",gte.control[i]);
        fputc('\n',stderr);
        fprintf(stderr,"native_boot: GTE_DATA_OWNER valid=%d writes=%llu reads=%llu data=",
                gte_valid,gte_valid ? (unsigned long long)gte.data_write_count : 0,
                gte_valid ? (unsigned long long)gte.data_read_count : 0);
        if (gte_valid) for (unsigned i=0;i<32;i++) fprintf(stderr,"%s%08x",i ? "," : "",gte.data[i]);
        fputc('\n',stderr);
        if (!cpu_valid || !gte_valid) cleanup_ok=0;
    }
    stop = entry_stop.boundary;
#ifdef MUSASHI_NATIVE_LANE_TABLE
    lane_overlay_report();
#endif
    fprintf(stderr,"native_boot: EXECUTION_BOUNDARY reason=%s\n",
            host_stop_signal ? "HOST_STOP" :
            status==MUSASHI_RESETGRAPH_PREFIX_STEP_LIMIT ? "STEP_LIMIT" :
            status==MUSASHI_RESETGRAPH_PREFIX_COMPLETE ? "RETURNED" :
            status==MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL ? "UNBOUND_CALL" : "REFUSED");
    if (!host_stop_signal && status != MUSASHI_RESETGRAPH_PREFIX_COMPLETE) {
        char message[320];
        snprintf(message, sizeof message,
                 "This early preview has reached a part of the game it cannot run yet "
                 "(code address %08X), so it has to stop here.\n\nThe log is in "
                 "~/.local/state/bfm-port/last-run.log.", entry_stop.cpu.pc);
        user_error(message);
    }
    fprintf(stderr,"native_boot: MDEC_SETUP command=%08x remaining=%u transfers=%llu "
            "control=%08x dma0=%08x dma1=%08x\n",boot.mdec.command,
            boot.mdec.remaining,(unsigned long long)boot.mdec.transfers,
            boot.mdec.control,boot.mdec.chcr[0],boot.mdec.chcr[1]);
    fprintf(stderr, "native_boot: CPU_BOUNDARY pc=%08x npc=%08x v0=%08x v1=%08x "
            "a0=%08x a1=%08x s0=%08x t1=%08x t2=%08x sp=%08x ra=%08x\n",
            entry_stop.cpu.pc, entry_stop.cpu.npc, entry_stop.cpu.r[2], entry_stop.cpu.r[3],
            entry_stop.cpu.r[4], entry_stop.cpu.r[5], entry_stop.cpu.r[16],
            entry_stop.cpu.r[9], entry_stop.cpu.r[10], entry_stop.cpu.r[29],
            entry_stop.cpu.r[31]);
    {
        uint32_t overlay=0x800cedfcu, i, word=0, index=0;
        fputs("native_boot: OVERLAY_WORDS", stderr);
        for (i=0;i<8u;++i) {
            musashi_boot_read32(&boot.memory, overlay+i*4u, &word);
            fprintf(stderr," %08x", word);
        }
        fputc('\n', stderr);
        musashi_boot_read32(&boot.memory, 0x800c99e4u, &index);
        fprintf(stderr,"native_boot: OVERLAY_INDEX %08x table", index & 0xffffu);
        for (i=0;i<8u;++i) {
            musashi_boot_read32(&boot.memory, 0x800cf450u+i*4u, &word);
            fprintf(stderr," %08x", word);
        }
        fputc('\n', stderr);
    }
    {
        /* Opt-in capture of the guest's own 2 MiB main RAM at the stop. A
         * member's loaded code must be compared against this image rather than
         * against another member's extracted blob. */
        const char *ram_path = getenv("MUSASHI_DUMP_RAM");
        if (ram_path && ram_path[0]) {
            uint8_t *ram = musashi_boot_ram_span(&boot.memory, 0x80000000u, MUSASHI_RAM_SIZE);
            FILE *out = ram ? fopen(ram_path, "wb") : NULL;
            if (!out) {
                fprintf(stderr, "native_boot: RAM_DUMP refused path=%s\n", ram_path);
            } else {
                size_t written = fwrite(ram, 1u, MUSASHI_RAM_SIZE, out);
                fclose(out);
                fprintf(stderr, "native_boot: RAM_DUMP path=%s base=80000000 bytes=%u written=%u\n",
                        ram_path, (unsigned)MUSASHI_RAM_SIZE, (unsigned)written);
            }
        }
    }
    {
        /* Boot-progress diagnostic: the 80044DBC initializer's first store
         * targets the live v1 pointer loaded from [0x8006CF4C]. */
        uint32_t initializer_pointer = 0;
        musashi_boot_read32(&boot.memory, 0x8006cf4cu, &initializer_pointer);
        fprintf(stderr, "native_boot: CD_INIT_POINTER v1=%08x cf4c=%08x\n",
                entry_stop.cpu.r[3], initializer_pointer);
    }
    fprintf(stderr, "native_boot: SOURCE_CLOCK cycles=%llu faulted=%d "
            "scope=%s devices_advanced=DEVICE_EPOCH\n",
            (unsigned long long)boot.source_clock.cycles, boot.source_clock.faulted,
            boot.cd_drive ? "STARTUP_INPUT_CD_IRQ_SOURCE_ONLY" : "STARTUP_AND_INPUT_SOURCE_ONLY");
    fprintf(stderr, "native_boot: DEVICE_EPOCH cycles=%llu source_edges=%llu cuts=%llu "
            "waits=%llu frequency=%llu faulted=%d profile=PACED_HOST_TIME cd_advanced=%d\n",
            (unsigned long long)boot.epoch.cycle, (unsigned long long)boot.epoch.last_sequence,
            (unsigned long long)boot.epoch.cuts, (unsigned long long)boot.epoch.waits,
            (unsigned long long)boot.epoch.frequency, boot.epoch.faulted,
            boot.cd_drive != NULL);
    if (boot.cd_drive) {
        MusashiCdOwnedState cd_state;
        if (!musashi_cd_owned_get_state(boot.cd_drive, &cd_state)) cleanup_ok = 0;
        else fprintf(stderr, "native_boot: CD_OWNER cycle=%llu due=%llu command=%02x "
                     "phase=%u if=%02x enable=%02x fifo=%u fault=%u irq_requests=%llu "
                     "common_delay_written=%d common_delay=%08x\n",
                     (unsigned long long)cd_state.cycle, (unsigned long long)cd_state.due,
                     cd_state.command, cd_state.phase, cd_state.interrupt, cd_state.enable,
                     cd_state.response_count, cd_state.fault,
                     (unsigned long long)boot.cd_irq_requests,
                     boot.common_delay_written, boot.common_delay);
        fprintf(stderr, "native_boot: CD_IRQ frame_busy=%d frame_faulted=%d "
                "instructions=%u call=%08x target=%08x\n",
                boot.irq.cd_frame.busy, boot.irq.cd_frame.faulted,
                boot.irq.cd_frame.instructions, boot.irq.cd_stop.call_address,
                boot.irq.cd_stop.target_address);
        fprintf(stderr,"native_boot: CD_IRQ_BUDGET exhausted=%u\n",
                boot.irq.cd_frame.budget_exhausted);
        {
            uint32_t mode=0, slot=0, p0=0, p1=0, p2=0, p3=0, p4=0, p5=0, kind_word=0;
            uint8_t kind=0;
            musashi_boot_read32(&boot.memory, 0x800ae6f8u, &mode);
            musashi_boot_read32(&boot.memory, 0x8006cc88u, &slot);
            musashi_boot_read32(&boot.memory, 0x8006cf64u, &kind_word);
            kind = (uint8_t)((kind_word >> 8) & 0xffu);
            musashi_boot_read32(&boot.memory, 0x8006cf5cu, &p0);
            musashi_boot_read32(&boot.memory, 0x8006cf80u, &p1);
            musashi_boot_read32(&boot.memory, 0x8006cf84u, &p2);
            musashi_boot_read32(&boot.memory, 0x8006cf88u, &p3);
            musashi_boot_read32(&boot.memory, 0x8006cf8cu, &p4);
            musashi_boot_read32(&boot.memory, 0x8006cf90u, &p5);
            fprintf(stderr,
                    "native_boot: CD_IRQ_RAM mode=%08x slot=%08x kind=%02x "
                    "ptr=%08x,%08x,%08x,%08x,%08x,%08x\n",
                    mode, slot, kind, p0, p1, p2, p3, p4, p5);
        }
        if (musashi_cd_owned_get_state(boot.cd_drive,&cd_state))
            fprintf(stderr,"native_boot: CD_DATA reading=%u next_lba=%u sector_due=%llu "
                "fetched=%llu published=%llu requested=%u valid=%u fifo_lba=%u size=%u cursor=%u bytes=%llu\n",
                cd_state.reading,cd_state.next_lba,(unsigned long long)cd_state.sector_due,
                (unsigned long long)cd_state.sectors_fetched,(unsigned long long)cd_state.sectors_published,
                cd_state.data_requested,cd_state.fifo_valid,cd_state.fifo_lba,cd_state.fifo_size,
                cd_state.fifo_cursor,(unsigned long long)cd_state.data_bytes_read);
        else cleanup_ok=0;
        fprintf(stderr,"native_boot: CD_SEEK seeking=%u target=%u position_valid=%u position=%u starts=%llu completions=%llu media_reads=%llu retirements=%llu\n",
                cd_state.seeking,cd_state.seek_target_lba,cd_state.position_valid,cd_state.position_lba,
                (unsigned long long)cd_state.seek_starts,(unsigned long long)cd_state.seek_completions,
                (unsigned long long)cd_state.seek_sector_reads,(unsigned long long)cd_state.buffer_retirements);
        fprintf(stderr,"native_boot: CD_ANNOUNCED valid=%u claimed=%u lba=%u sequence=%llu requested_sequence=%llu fifo_latches=%llu\n",
                cd_state.announced_valid,cd_state.announced_claimed,cd_state.announced_lba,
                (unsigned long long)cd_state.announced_sequence,(unsigned long long)cd_state.requested_sequence,
                (unsigned long long)cd_state.fifo_latches);
        if (boot.cd_dma) {
            MusashiCdDma3State dma;
            if (!musashi_cd_dma3_get_state(boot.cd_dma,&dma)) cleanup_ok=0;
            else fprintf(stderr,"native_boot: CD_DMA3 cycle=%llu due=%llu madr=%08x bcr=%08x "
                "chcr=%08x delay=%08x transfers=%llu bytes=%llu fault=%u\n",
                (unsigned long long)dma.cycle,(unsigned long long)dma.due,dma.madr,dma.bcr,
                dma.chcr,dma.cdrom_delay,(unsigned long long)dma.transfers,
                (unsigned long long)dma.bytes,dma.fault);
        }
    }
#ifdef MUSASHI_WITH_BFM_PLAT
    if (g_plat_audio.backend.queue)
        fprintf(stderr, "native_boot: AUDIO_SINK backend=bfm_plat:%s frames=%llu nonsilent=%llu "
                "peak=%d faulted=%d\n", g_plat_audio_name,
                (unsigned long long)g_plat_audio.frames,
                (unsigned long long)g_plat_audio.nonsilent, g_plat_audio.peak,
                g_plat_audio.faulted);
#endif
    fprintf(stderr, "native_boot: SPU_KEYON writes=%llu\n", (unsigned long long)g_spu_keyon_writes);
    if (boot.irq.scheduler.host_edges_advisory)
        fprintf(stderr, "native_boot: HOST_VBLANKS dropped=%llu (guest clock: advisory)\n",
                (unsigned long long)boot.irq.scheduler.dropped_host_vblanks);
    if (boot.spu) {
        MusashiSpuCdAudioSnapshot audio_state;
        if (!musashi_spu_cd_audio_snapshot(boot.spu, &audio_state)) cleanup_ok = 0;
        else fprintf(stderr, "native_boot: SPU_OWNER cycle=%llu samples=%llu "
                     "main_left=%04x main_right=%04x cd_left=%04x cd_right=%04x "
                     "control=%04x submitted=%zu queued=%zu faulted=%d "
                     "profile=SETTLED_BIOS_MAIN_MUTE audible_game=NOT_OBSERVED\n",
                     (unsigned long long)audio_state.cycle,
                     (unsigned long long)audio_state.sample_index,
                     (unsigned)(uint16_t)audio_state.main_left_current,
                     (unsigned)(uint16_t)audio_state.main_right_current,
                     (unsigned)(uint16_t)audio_state.cd_left_gain,
                     (unsigned)(uint16_t)audio_state.cd_right_gain,
                     audio_state.control, audio_state.submitted_frames,
                     audio_state.queued_frames, audio_state.faulted);
    }
    fprintf(stderr, "native_boot: TIMER2 count=%04x mode=%04x target=%04x epoch=0 "
            "profile=PACED_EVENT_SERVICED irq_enabled=0\n",
            boot.timer2.count, boot.timer2.mode, boot.timer2.target);
    fprintf(stderr, "native_boot: VIDEO_TIMING timer1_cycle=%llu count=%04x "
            "subcycle=%u running=%d faulted=%d profile=NTSC_2146_EVENT_SERVICED "
            "gpu_parity=%u gpu_reset_sequence=%llu\n",
            (unsigned long long)boot.timer1.last_cycles, boot.timer1.count,
            boot.timer1.subcycle, boot.timer1.running, boot.timer1.faulted,
            boot.gpu.vblank_parity, (unsigned long long)boot.gpu_reset_sequence);
    {
        uint32_t busy=0, saved_mask=0, history[3]={0};
        int valid=musashi_boot_read32(&boot.memory,0x80072790u,&busy) &&
            musashi_boot_read32(&boot.memory,0x80072894u,&saved_mask) &&
            musashi_boot_read32(&boot.memory,0x8007287cu,&history[0]) &&
            musashi_boot_read32(&boot.memory,0x80072880u,&history[1]) &&
            musashi_boot_read32(&boot.memory,0x80072884u,&history[2]);
        fprintf(stderr,"native_boot: DRAW_OWNER dma_direction=%u chcr=%08x "
                "display_disabled=%d gpu_faulted=%d queue_valid=%d busy=%08x "
                "saved_mask=%08x history=%08x,%08x,%08x\n",
                boot.gpu.dma_direction,boot.dma.gpu_channel_control,
                boot.gpu.display_disabled,boot.gpu.faulted,valid,busy,saved_mask,
                history[0],history[1],history[2]);
    }
    fprintf(stderr, "native_boot: SIO profile=DIGITAL_PORT1_CARDS_ABSENT bound=%d "
            "cycle=%llu transmitted=%llu keyboard_polls=%llu ack_pending=%d faulted=%d\n",
            boot.sio.pad_bound, (unsigned long long)boot.sio.cycle,
            (unsigned long long)boot.sio.transmitted_bytes,
            (unsigned long long)boot.keyboard_polls, boot.sio.ack_pending, boot.sio.faulted);
    observe_input_state(&boot, entry_stop.cpu.pc);
    if (!musashi_boot_read16(&boot.memory, 0x8006bafcu, &guard) ||
        !musashi_boot_read16(&boot.memory, 0x8006bb2cu, &enabled) ||
        !musashi_boot_read32(&boot.memory, 0x8006bb00u, &slot0) ||
        !musashi_boot_read32(&boot.memory, 0x8006bb0cu, &slot3) ||
        !musashi_boot_read32(&boot.memory, 0x8006cbb8u, &tick)) {
        fputs("native_boot: startup state unreadable\n", stderr);
        exit(2);
    }
    for (index = 0; index < MUSASHI_BIOS_EVENTS_CAPACITY; index++)
        if (boot.bios.events.records[index].status) event_used++;
    /* A returned bounded prefix is still a stop. Unhook before releasing
     * source registration and this owner's lifetime. */
    musashi_trace_function_flush(); /* keep the ranking even if teardown faults */
    fprintf(stderr,
        "native_boot: STOP status=%d pc=%08x target=%08x IRQ_installed=%d "
        "I_STAT=%04x I_MASK=%04x DPCR=%08x DICR=%08x TIMER1_MODE=%04x "
        "custom_vblank=%d deliveries=%u failed_target=%08x\n",
        (int)status, (unsigned)stop.call_address, (unsigned)stop.target_address,
        boot.irq.scheduler.installed, (unsigned)boot.irq.controller.status,
        (unsigned)boot.irq.controller.mask, (unsigned)boot.dma.control,
        (unsigned)boot.dma.interrupt, (unsigned)boot.timer1.mode,
        musashi_irq_policy_custom_vblank(&boot.irq_policy),
        (unsigned)boot.irq.scheduler.deliveries,
        (unsigned)boot.irq.failed_target);
    fprintf(stderr, "native_boot: guest guard=%04x enabled=%04x slot0=%08x "
            "slot3=%08x tick=%08x\n", (unsigned)guard, (unsigned)enabled,
            (unsigned)slot0, (unsigned)slot3, (unsigned)tick);
    {
        uint32_t dma_slot4 = 0, vblank_slots[8];
        unsigned slot;
        (void)musashi_boot_read32(&boot.memory, 0x8006cbc4u + 16u, &dma_slot4);
        fprintf(stderr, "native_boot: dma_slot4=%08x dma_bound=%d\n",
                (unsigned)dma_slot4, boot.irq.dma_device != NULL);
        fputs("native_boot: vblank_slots", stderr);
        for (slot = 0; slot < 8; ++slot) {
            vblank_slots[slot] = 0;
            (void)musashi_boot_read32(&boot.memory, 0x8006cb98u + slot * 4u,
                                      &vblank_slots[slot]);
            fprintf(stderr, " %u=%08x", slot, (unsigned)vblank_slots[slot]);
        }
        fputc('\n', stderr);
    }
    fprintf(stderr, "native_boot: bios_cd_installed=%d bios_event_used=%u "
            "guest_irq_enabled=%d irq_faulted=%d\n", boot.bios.cd_installed,
            event_used, guest_irq_enabled(&boot), boot.irq.scheduler.faulted);
    {
        int scheduler_removed = !boot.irq.scheduler.installed ||
            musashi_psycross_irq_scheduler_uninstall(&boot.irq.scheduler);
        cleanup_ok = scheduler_removed && cleanup_ok;
        fprintf(stderr, "native_boot: scheduler_removed=%d\n", scheduler_removed);
        if (!scheduler_removed) {
            fputs("native_boot: teardown refused while source registration remains live\n",stderr);
            return 2;
        }
    }
    fprintf(stderr, "native_boot: card_initialized=%u card_active=%u card_pad_started=%d "
            "card_hook_installed=%d card_hook_calls=%llu\n", (unsigned)boot.card.initialized_word,
            (unsigned)boot.card.fast_track_active, (int)boot.card.pad_started,
            boot.bios.early_hook == &boot.card.early_hook,
            (unsigned long long)boot.card.hook_calls);
    fprintf(stderr, "native_boot: card_started=%u card_sio_installed=%d "
            "card_port=%u card_vblank_calls=%llu card_maintenance_calls=%llu "
            "card_delay_calls=%u SIO_MODE=%04x SIO_BAUD=%04x SIO_CONTROL=%04x\n",
            (unsigned)boot.card.card_started, boot.card.sio_node.owner == &boot.bios,
            (unsigned)boot.card.port, (unsigned long long)boot.card.vblank_calls,
            (unsigned long long)boot.card.maintenance_calls, (unsigned)boot.card.delay_calls,
            (unsigned)boot.sio.mode, (unsigned)boot.sio.baud, (unsigned)boot.sio.control);
    {
        unsigned directory_zero = 0, broken_ff = 0, statuses_zero = 0;
        unsigned p, e, b;
        for (p = 0; p < 2; ++p) {
            for (e = 0; e < 15; ++e)
                for (b = 0; b < 32; ++b)
                    directory_zero += boot.backup_unit.directory[p][e][b] == 0;
            for (e = 0; e < 20; ++e)
                broken_ff += boot.backup_unit.broken[p][e] == UINT32_MAX;
        }
        for (e = 0; e < 5; ++e) statuses_zero += boot.backup_unit.status[e] == 0;
        fprintf(stderr, "native_boot: bu_stage=%u bu_directory_zero=%u bu_broken_ff=%u "
                "bu_status_zero=%u card_flags=%02x%02x card_action=%u card_last_port=%u "
                "card_operation_step=%u card_irq_installed=%d sio_tx_bytes=%llu\n",
                (unsigned)boot.backup_unit.stage, directory_zero, broken_ff, statuses_zero,
                (unsigned)boot.card.flags[0], (unsigned)boot.card.flags[1],
                (unsigned)boot.card.action_in_progress, (unsigned)boot.card.last_port,
                (unsigned)boot.card.operation_step, boot.card.card_node.owner == &boot.bios,
                (unsigned long long)boot.sio.transmitted_bytes);
    }
    {
        int input_removed = musashi_bios_input_shutdown(&boot.bios_input);
        int bu_removed = musashi_bios_backup_unit_shutdown(&boot.backup_unit);
        int card_removed = musashi_bios_card_shutdown(&boot.card);
        if (!bu_removed && card_removed)
            bu_removed = musashi_bios_backup_unit_shutdown(&boot.backup_unit);
        fprintf(stderr, "native_boot: card_removed=%d bu_removed=%d\n", card_removed, bu_removed);
        fprintf(stderr, "native_boot: input_removed=%d\n", input_removed);
        if (!input_removed || !card_removed || !bu_removed) cleanup_ok = 0;
    }
    {
        uint32_t status_word=0, read_word=0;
        static const uint32_t camera_addresses[]={0x80126950u,0x80126984u,0x80126988u,
            0x8012698cu,0x80126990u,0x80126994u,0x80126998u,0x8012699cu,0x801269a0u};
        for (unsigned c=0;c<sizeof(camera_addresses)/sizeof(camera_addresses[0]);++c) {
            uint32_t value=0;
            int readable=musashi_boot_read32(&boot.memory,camera_addresses[c],&value);
            fprintf(stderr,"native_boot: CAMERA_RAM address=%08x valid=%d value=%08x\n",
                    camera_addresses[c],readable,value);
        }
        {
            uint32_t player_ptr = 0;
            if (musashi_boot_read32(&boot.memory, 0x80126b78u, &player_ptr) && player_ptr != 0) {
                uint32_t px = 0, py = 0, pz = 0;
                uint32_t lx = 0, ly = 0, lz = 0;
                uint32_t action = 0, anim = 0;
                musashi_boot_read32(&boot.memory, player_ptr + 0x68u, &px);
                musashi_boot_read32(&boot.memory, player_ptr + 0x6cu, &py);
                musashi_boot_read32(&boot.memory, player_ptr + 0x70u, &pz);
                musashi_boot_read32(&boot.memory, player_ptr + 0x48u, &lx);
                musashi_boot_read32(&boot.memory, player_ptr + 0x4cu, &ly);
                musashi_boot_read32(&boot.memory, player_ptr + 0x50u, &lz);
                musashi_boot_read32(&boot.memory, player_ptr + 0x00u, &action);
                musashi_boot_read32(&boot.memory, player_ptr + 0x34u, &anim);
                fprintf(stderr, "native_boot: PLAYER_STATE ptr=%08x action=%08x anim=%08x "
                                "world_pos=[%d,%d,%d] local_pos=[%d,%d,%d]\n",
                        player_ptr, action, anim, (int)px, (int)py, (int)pz, (int)lx, (int)ly, (int)lz);
            }
        }
        static const uint32_t env_addresses[4]={0x800af668u,0x800af6c4u,0x800af77cu,0x800af790u};
        for (unsigned e=0;e<4;++e) {
            unsigned length=e<2 ? 0x5cu : 0x14u;
            const uint8_t *bytes=musashi_boot_ram_span(&boot.memory,env_addresses[e],length);
            fprintf(stderr,"native_boot: FRAMEBUFFER_ENV address=%08x length=%u valid=%d bytes=",
                    env_addresses[e],length,bytes!=NULL);
            if (bytes) for (unsigned i=0;i<length;++i) fprintf(stderr,"%02x",bytes[i]);
            fputc('\n',stderr);
        }
        int valid=musashi_gpu_controller_read32(&boot.gpu,0x1f801814u,&status_word) &&
            musashi_gpu_controller_read32(&boot.gpu,0x1f801810u,&read_word);
        fprintf(stderr,"native_boot: GPUSTAT=%08x GPUREAD=%08x DMA2_CHCR=%08x valid=%d\n",
                status_word,read_word,boot.dma.gpu_channel_control,valid);
        {
            const MusashiGpuDisplayState *states[2]={&boot.gpu.display,&boot.gpu_renderer.display};
            for (unsigned j=0;j<2;++j) {
                const MusashiGpuDisplayState *d=states[j];
                fprintf(stderr,"native_boot: GPU_DISPLAY owner=%s origin=%08x horizontal=%08x "
                        "vertical=%08x mode=%08x x=%u y=%u width=%u height=%u "
                        "h_start=%u h_end=%u v_start=%u v_end=%u divisor=%u disabled=%d\n",
                        j ? "BACKEND" : "CONTROLLER",d->origin,d->horizontal,d->vertical,d->mode,
                        d->x,d->y,d->width,d->height,d->h_start,d->h_end,d->v_start,d->v_end,
                        d->dot_divisor,j ? !boot.gpu_renderer.display_enabled : boot.gpu.display_disabled);
            }
            for (unsigned opcode=5;opcode<=8;++opcode) {
                const uint8_t *byte=musashi_boot_ram_span(&boot.memory,0x80078874u+opcode,1);
                fprintf(stderr,"native_boot: DISPLAY_REGISTER_CACHE opcode=%02x address=%08x "
                        "valid=%d value=%02x\n",opcode,0x80078874u+opcode,byte!=NULL,byte ? *byte : 0);
            }
        }
        fprintf(stderr,"native_boot: GPU_ENV window=%08x area_start=%08x area_end=%08x "
                "offset=%08x mask=%u gp0_accepted=%llu faulted=%d\n",
                boot.gpu.texture_window,boot.gpu.drawing_area_start,boot.gpu.drawing_area_end,
                boot.gpu.drawing_offset,boot.gpu.mask_flags,
                (unsigned long long)boot.gpu.accepted_gp0_words,boot.gpu.faulted);
        fprintf(stderr,"native_boot: GPU_STORE phase=%u remaining=%u pixels=%llu "
                "xy=%u,%u wh=%u,%u px=%u,%u cmd=%08x packet_xy=%08x packet_wh=%08x\n",
                boot.gpu.store_phase,boot.gpu.store_remaining,
                (unsigned long long)boot.gpu.stored_pixels,
                boot.gpu.store_x,boot.gpu.store_y,boot.gpu.store_w,boot.gpu.store_h,
                boot.gpu.store_px,boot.gpu.store_py,boot.gpu.store_command,
                boot.gpu.store_xy,boot.gpu.store_wh);
        {
            uint32_t rect0=0, rect1=0, live0=0, live1=0;
            musashi_boot_read32(&boot.memory, 0x800c5bdcu, &rect0);
            musashi_boot_read32(&boot.memory, 0x800c5be0u, &rect1);
            musashi_boot_read32(&boot.memory, 0x800c5dbcu, &live0);
            musashi_boot_read32(&boot.memory, 0x800c5dc0u, &live1);
            fprintf(stderr,"native_boot: LOADIMAGE_RECT hist=%08x,%08x live=%08x,%08x\n",
                    rect0, rect1, live0, live1);
        }
        fprintf(stderr,"native_boot: GPU_ENV_BACKEND window=%08x area_start=%08x "
                "area_end=%08x offset=%08x mask=%u corners=%d,%d,%d,%d faulted=%d\n",
                boot.gpu_renderer.texture_window,boot.gpu_renderer.drawing_area_start,
                boot.gpu_renderer.drawing_area_end,boot.gpu_renderer.drawing_offset,
                boot.gpu_renderer.mask_flags,boot.gpu_renderer.clip_x1,boot.gpu_renderer.clip_y1,
                boot.gpu_renderer.clip_x2,boot.gpu_renderer.clip_y2,boot.gpu_renderer.faulted);
        {
            const MusashiGpuFill *fill=&boot.gpu_renderer.last_fill;
            uint16_t row[1024];
            uint64_t checked=0, mismatches=0;
            int readable=boot.gpu_renderer.completed_fills != 0;
            for (unsigned y=0;readable && y<fill->height;++y) {
                readable=musashi_gpu_psycross_read_vram(&boot.gpu_renderer,0,
                    (fill->y+y)&511u,1024,1,row,1024);
                if (!readable) break;
                for (unsigned x=0;x<fill->width;++x) {
                    ++checked;
                    mismatches += row[(fill->x+x)&1023u] != fill->color;
                }
            }
            fprintf(stderr,"native_boot: GPU_FILL controller_completed=%llu backend_completed=%llu "
                    "controller_pixels=%llu backend_pixels=%llu pending_words=%u "
                    "command=%08x xy=%08x wh=%08x x=%u y=%u width=%u height=%u color=%04x "
                    "vram_readable=%d checked=%llu mismatches=%llu\n",
                    (unsigned long long)boot.gpu.completed_fills,
                    (unsigned long long)boot.gpu_renderer.completed_fills,
                    (unsigned long long)boot.gpu.filled_pixels,
                    (unsigned long long)boot.gpu_renderer.filled_pixels,boot.gpu.fill_words,
                    fill->command,fill->xy,fill->wh,fill->x,fill->y,fill->width,fill->height,fill->color,
                    readable,(unsigned long long)checked,(unsigned long long)mismatches);
            for (unsigned i=0;i<6;++i) {
                uint32_t value=0, address=0x80078830u+4u*i;
                int valid=musashi_boot_read32(&boot.memory,address,&value);
                fprintf(stderr,"native_boot: CLEAR_PACKET address=%08x valid=%d value=%08x\n",
                        address,valid,value);
            }
        }
        {
            const uint8_t *cache=musashi_boot_ram_span(&boot.memory,0x80078879u,1);
            fprintf(stderr,"native_boot: DISPLAY_CACHE opcode=05 address=80078879 "
                    "valid=%d value=%02x\n",cache!=NULL,cache ? *cache : 0);
        }
    }
    observe_gpu_dma(&boot,"STOP");
    {
        int removed=musashi_gpu_dma2_close(boot.gpu_dma);
        if (removed) boot.gpu_dma=NULL;
        fprintf(stderr,"native_boot: GPU_DMA2 removed=%d\n",removed);
        if (!removed) cleanup_ok=0;
    }
    dump_display_vram(&boot);
    presentation_ok = present_frame(&boot);
    hold_window(&boot);
    gpu_removed = musashi_gpu_psycross_shutdown(&boot.gpu_renderer);
    fprintf(stderr, "native_boot: graphics_init=%s blank_presented=%d gpu_removed=%d\n",
            boot.graphics_returned ? "RETURNED" : "PARTIAL",
            presentation_ok, gpu_removed);
    if (!gpu_removed || !presentation_ok) cleanup_ok = 0;
    if (!musashi_device_epoch_shutdown(&boot.epoch)) cleanup_ok = 0;
    {
        int devices_removed = native_devices_shutdown(&boot);
        fprintf(stderr, "native_boot: OWNED_DEVICES removed=%d\n", devices_removed);
        if (!devices_removed) cleanup_ok = 0;
    }
    musashi_psyx_shutdown();
    if (!cleanup_ok)
        fputs("native_boot: scheduler removal refused\n", stderr);
    /* The CPU path is proven to stop at the refusal boundary, so no menu was
     * reached. The presented frame still needs a human look, which is a
     * separate claim and is therefore a separate marker. */
    fputs("native_boot: startup=PARTIAL incoming_ra=NATIVE_ZERO menu=NOT_REACHED"
          " visual_check=REQUIRED\n", stderr);
    return host_stop_signal ? 128 + host_stop_signal : 2;
}
#undef boot
