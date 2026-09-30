#ifndef MUSASHI_BIOS_CARD_H
#define MUSASHI_BIOS_CARD_H

#include <stdint.h>
#include <stddef.h>

#include "musashi_bios_kernel.h"

typedef enum MusashiBiosCardDescriptorRole {
    MUSASHI_BIOS_CARD_DESCRIPTOR_SIO = 1,
    MUSASHI_BIOS_CARD_DESCRIPTOR_CARD = 2
} MusashiBiosCardDescriptorRole;

/* Native-only descriptor preparation. These links and bindings never enter
 * guest RAM; null handler/verifier fields deliberately make registration and
 * callable serial support explicit capabilities for a later slice. */
typedef struct MusashiBiosCardDescriptor {
    struct MusashiBiosCardDescriptor *next;
    uint32_t reserved;
    MusashiBiosCardDescriptorRole role;
    void *handler;
    void *verifier;
} MusashiBiosCardDescriptor;

/* Stable native owners used by the bounded StartCARD prefix.  All callbacks
 * are host operations; no callback address is copied into guest RAM. */
typedef struct MusashiBiosCardStartDevice {
    void *userdata;
    int (*write_sio16)(void *userdata, uint32_t address, uint16_t value);
    int (*read_irq32)(void *userdata, uint32_t address, uint32_t *value);
    int (*write_irq32)(void *userdata, uint32_t address, uint32_t value);
    int (*enter_critical)(void *userdata);
    int (*exit_critical)(void *userdata);
    int (*read_pad)(void *userdata, int32_t *value);
    int (*exchange_pad)(void *userdata, int32_t value, int32_t *old_value);
    int (*change_timer)(void *userdata, int32_t channel, int32_t value);
    /* Optional active no-card capability; idle StartCARD needs none of these. */
    int (*read_sio16)(void *userdata, uint32_t address, uint16_t *value);
    int (*read_sio8)(void *userdata, uint32_t address, uint8_t *value);
    int (*write_sio8)(void *userdata, uint32_t address, uint8_t value);
    int (*cards_disconnected)(void *userdata);
    /* Checked source callback executor, required only for callback delivery. */
    MusashiBiosEventExecutor execute_event;
} MusashiBiosCardStartDevice;

typedef struct MusashiBiosCardCompletion {
    void *userdata;
    int (*error2)(void *userdata, uint32_t device_id);
} MusashiBiosCardCompletion;

typedef struct MusashiBiosCardRead {
    uint32_t device_id;
    uint32_t sector;
    uint8_t *buffer;
    size_t bytes;
    /* Callable native binding for the selected RAM5688 read handler. */
    int (*execute)(void *card);
} MusashiBiosCardRead;

typedef struct MusashiBiosCard {
    MusashiBiosKernel *kernel;
    const MusashiBiosCardStartDevice *start_device;
    MusashiBiosEarlyHook early_hook;
    uint64_t hook_calls;
    uint32_t initialized_word;
    int32_t pad_started;
    uint32_t fast_track_active;
    uint32_t card_started;
    uint32_t serial_reset_word;
    uint8_t action_in_progress;
    uint8_t port;
    uint8_t flags[2];
    uint32_t delay_calls;
    int32_t delay_final_word;
    uint64_t vblank_calls;
    uint64_t maintenance_calls;
    int start_faulted;
    MusashiBiosCardDescriptor sio_descriptor;
    MusashiBiosCardDescriptor card_descriptor;
    MusashiBiosIrqNode sio_node;
    const MusashiBiosCardCompletion *completion;
    MusashiBiosCardRead reads[2];
    MusashiBiosIrqNode card_node;
    uint32_t source_word_74a4;
    uint32_t operation_step; /* RAM7514, distinct from RAM74A4. */
    uint32_t got_error;      /* RAM7520. */
    uint32_t port_mask;
    uint32_t last_port;
    uint32_t checksum[2];
    uint32_t fast_track_counter;
    uint8_t *fast_track_buffer;
    uint8_t *fast_track_flags;
    uint32_t *fast_track_checksum;
    int transfer_faulted;
} MusashiBiosCard;

/* Fresh-storage constructor. The caller must provide zeroed, never-associated
 * storage; a live lease is never memset or adopted. It does not seed guest
 * RAM or register a BIOS priority/event node. */
int musashi_bios_card_init(MusashiBiosCard *card, MusashiBiosKernel *kernel);

/* Checked B0:4A service. old_initialized receives the prior full word only
 * on success; refusal leaves it and all card-owned state unchanged. */
int musashi_bios_card_init_service(void *userdata, int32_t pad_started,
                                   int32_t *old_initialized);

/* Bind a stable SIO/IRQ owner before invoking StartCARD. */
int musashi_bios_card_bind_start_device(
    MusashiBiosCard *card, const MusashiBiosCardStartDevice *device);

/* B0:4B's bounded StartCARD continuation. guest_result is written only after
 * the complete sequence. Failure retains earlier device/state effects and
 * latches start_faulted; host shutdown can still release coherent leases. */
int musashi_bios_card_start_service(void *userdata, int32_t *guest_result);

/* Priority-2 mask-1 SIO VBlank owner. */
int musashi_bios_card_sio_vblank(void *userdata, uint16_t pending);

int musashi_bios_card_bind_completion(MusashiBiosCard *,
                                      const MusashiBiosCardCompletion *);
int musashi_bios_card_unbind_completion(MusashiBiosCard *,
                                        const MusashiBiosCardCompletion *);
int musashi_bios_card_allow_new(MusashiBiosCard *);
/* Internal owned-buffer subset of B0:4F: device0/10 and sector0 only. A
 * checked accepted0 means source request rejection; no I/O has completed. */
int musashi_bios_card_queue_read(MusashiBiosCard *, uint32_t device_id,
    uint32_t sector, uint8_t *buffer, size_t bytes, int32_t *accepted);

/* The installed native early-exception binding. It accepts only the selected
 * inactive fast-track branch and refuses before unsupported SIO effects. */
int musashi_bios_card_early_hook(void *userdata);

/* Release the stable early-hook lease after verifying ownership. A
 * constructed-but-never-published card may also detach from its vacant
 * kernel slot; foreign or incoherent leases are refused. */
int musashi_bios_card_shutdown(MusashiBiosCard *card);

#endif
