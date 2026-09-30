#include "musashi_bios_input.h"

#define INPUT_DESCRIPTOR 0x80078988u
#define INPUT_HANDLER 0x8005d734u
#define INPUT_VERIFIER 0x8005d6ccu
#define SIO_IDENTITY 0x000074a8u

static int dispatch_input(void *userdata, uint16_t pending);

static int bound(const MusashiBiosInput *input) {
    return input && input->memory && input->kernel && input->card &&
        input->device && input->device->execute;
}

static int input_node_coherent(const MusashiBiosInput *input) {
    const MusashiBiosIrqNode *node = &input->node;
    return node->owner == input->kernel && node->priority == 2 &&
        node->mask == 1 && node->execute == dispatch_input &&
        node->userdata == input;
}

static int unregistered_node_coherent(const MusashiBiosInput *input) {
    const MusashiBiosIrqNode *node = &input->node;
    if (node->owner || node->next) return 0;
    /* A fresh node or our own previously dequeued node, never adoption. */
    if (!node->execute)
        return !node->userdata && !node->priority && !node->mask;
    return node->execute == dispatch_input && node->userdata == input &&
        node->priority == 2 && node->mask == 1;
}

/* The only guest identities this bridge owns are the game input descriptor
 * and the actual StartCARD SIO node. Never follow an arbitrary guest pointer
 * or silently adopt a foreign native node. Other priorities remain untouched. */
static int topology_coherent(const MusashiBiosInput *input) {
    const MusashiBiosKernel *kernel = input->kernel;
    const MusashiBiosCard *card = input->card;
    const MusashiBiosIrqNode *sio = &card->sio_node;
    unsigned priority;
    if (card->kernel != kernel || sio->owner != kernel || sio->priority != 2 ||
        sio->mask != 1 || sio->execute != musashi_bios_card_sio_vblank ||
        sio->userdata != card || sio->next)
        return 0;
    for (priority = 0; priority < 4; ++priority)
        if (priority != 2 && (kernel->heads[priority] == &input->node ||
                             kernel->heads[priority] == sio))
            return 0;
    if (!input->node.owner)
        return unregistered_node_coherent(input) && kernel->heads[2] == sio;
    return input_node_coherent(input) && kernel->heads[2] == &input->node &&
        input->node.next == sio;
}

static int descriptor_callable(const MusashiBiosInput *input) {
    uint32_t handler, verifier;
    return musashi_boot_read32(input->memory, INPUT_DESCRIPTOR + 4, &handler) &&
        musashi_boot_read32(input->memory, INPUT_DESCRIPTOR + 8, &verifier) &&
        handler == INPUT_HANDLER && verifier == INPUT_VERIFIER;
}

static int next_coherent(const MusashiBiosInput *input) {
    uint32_t next;
    return musashi_boot_read32(input->memory, INPUT_DESCRIPTOR, &next) &&
        next == SIO_IDENTITY;
}

int musashi_bios_input_init(MusashiBiosInput *input, MusashiBootMemory *memory,
    MusashiBiosKernel *kernel, MusashiBiosCard *card,
    const MusashiBiosInputDevice *device) {
    if (!input || !memory || !kernel || !card || card->kernel != kernel ||
        !device || !device->execute || kernel->dispatching || input->memory ||
        input->kernel || input->card || input->device || input->faulted ||
        input->node.owner || input->node.next || input->node.execute ||
        input->node.userdata || input->node.priority || input->node.mask)
        return 0;
    input->memory = memory;
    input->kernel = kernel;
    input->card = card;
    input->device = device;
    return 1;
}

static int service_allowed(const MusashiBiosInput *input, int32_t priority,
                           uint32_t descriptor, const int32_t *result) {
    return bound(input) && result && !input->faulted &&
        !input->kernel->dispatching && priority == 2 &&
        descriptor == INPUT_DESCRIPTOR && topology_coherent(input);
}

int musashi_bios_input_enqueue(MusashiBiosInput *input, int32_t priority,
    uint32_t descriptor, int32_t *result) {
    if (!service_allowed(input, priority, descriptor, result) ||
        input->node.owner || !descriptor_callable(input) ||
        !musashi_boot_ram_span(input->memory, INPUT_DESCRIPTOR, 16))
        return 0;
    /* Selected RAM1420: publish head before writing the old head to next.
     * The checked span and serialized owner make the subsequent write valid. */
    if (!musashi_bios_kernel_enqueue(input->kernel, &input->node, 2, 1,
                                    dispatch_input, input))
        return 0;
    if (!musashi_boot_write32(input->memory, INPUT_DESCRIPTOR, SIO_IDENTITY)) {
        input->faulted = 1;
        return 0; /* Preserve published ownership, never fabricate rollback. */
    }
    *result = 0;
    return 1;
}

int musashi_bios_input_dequeue(MusashiBiosInput *input, int32_t priority,
    uint32_t descriptor, int32_t *result) {
    if (!service_allowed(input, priority, descriptor, result)) return 0;
    if (!input->node.owner) {
        *result = 0;
        return 1;
    }
    /* C0:03 does not inspect handler/verifier/reserved or clear guest next. */
    if (!next_coherent(input) ||
        !musashi_bios_kernel_dequeue(input->kernel, &input->node))
        return 0;
    *result = (int32_t)INPUT_DESCRIPTOR;
    return 1;
}

static int dispatch_input(void *userdata, uint16_t pending) {
    MusashiBiosInput *input = userdata;
    int32_t verifier, ignored;
    if (!input) return 0;
    if (!bound(input)) goto refused;
    if (input->faulted || pending != 1 ||
        input->kernel->executing_node != &input->node ||
        !input->kernel->dispatching || !topology_coherent(input) ||
        !next_coherent(input) || !descriptor_callable(input))
        goto refused;
    /* BIOS loads both descriptor callbacks before invoking the verifier.
     * Guest v0=0 is an accepted skip; handler v0 is deliberately ignored. */
    if (!input->device->execute(input->device->userdata, INPUT_VERIFIER, 0,
                                &verifier)) goto refused;
    if (verifier && !input->device->execute(input->device->userdata,
                                           INPUT_HANDLER, verifier, &ignored))
        goto refused;
    return 1;
refused:
    input->faulted = 1;
    return 0;
}

int musashi_bios_input_shutdown(MusashiBiosInput *input) {
    MusashiBiosKernel *kernel;
    if (!input || !(kernel = input->kernel) || kernel->dispatching) return 0;
    if (input->node.owner) {
        /* Host cleanup can release our lease after guest corruption or loss
         * of the SIO tail; it must never unlink a foreign replacement. */
        if (!input_node_coherent(input) || kernel->heads[2] != &input->node ||
            (input->node.next &&
             (!input->card || input->node.next != &input->card->sio_node)) ||
            !musashi_bios_kernel_dequeue(kernel, &input->node))
            return 0;
    } else if (!unregistered_node_coherent(input) ||
               kernel->heads[2] == &input->node) return 0;
    input->memory = NULL;
    input->kernel = NULL;
    input->card = NULL;
    input->device = NULL;
    return 1;
}
