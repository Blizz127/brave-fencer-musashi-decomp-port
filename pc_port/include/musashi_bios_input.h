#ifndef MUSASHI_BIOS_INPUT_H
#define MUSASHI_BIOS_INPUT_H

#include "musashi_bios_card.h"
#include "musashi_boot_memory.h"

typedef struct MusashiBiosInputDevice {
    void *userdata;
    /* Host acceptance and guest v0 are separate. The checked source executor
     * owns its BIOS scratch frame; it must not pump or resume the main CPU. */
    int (*execute)(void *userdata, uint32_t target, int32_t argument,
                   int32_t *guest_result);
} MusashiBiosInputDevice;

typedef struct MusashiBiosInput {
    MusashiBootMemory *memory;
    MusashiBiosKernel *kernel;
    MusashiBiosCard *card;
    const MusashiBiosInputDevice *device;
    MusashiBiosIrqNode node;
    int faulted;
} MusashiBiosInput;

/* Fresh zeroed storage only. Dependencies must outlive this owner. Constructor
 * may precede StartCARD; it neither registers a node nor seeds guest RAM. */
int musashi_bios_input_init(MusashiBiosInput *, MusashiBootMemory *,
    MusashiBiosKernel *, MusashiBiosCard *, const MusashiBiosInputDevice *);
/* Selected C0:02/03 subset: priority2, descriptor80078988, real BIOS SIO tail.
 * Unsupported topology/ownership refuses without assigning the guest result.
 * Reserved is untouched; dequeue preserves the guest descriptor's next word. */
int musashi_bios_input_enqueue(MusashiBiosInput *, int32_t priority,
    uint32_t guest_descriptor, int32_t *result);
int musashi_bios_input_dequeue(MusashiBiosInput *, int32_t priority,
    uint32_t guest_descriptor, int32_t *result);
/* Owner thread only, outside dispatch; call before card/kernel destruction.
 * Releases native ownership after execution faults without guest completion,
 * rollback, or changes to guest descriptor words. */
int musashi_bios_input_shutdown(MusashiBiosInput *);

#endif
