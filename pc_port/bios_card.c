#include "musashi_bios_card.h"

#include <string.h>

#define IRQ_STAT_ADDRESS 0x1f801070u
#define IRQ_MASK_ADDRESS 0x1f801074u
#define SIO_CTRL_ADDRESS 0x1f80104au
#define SIO_BAUD_ADDRESS 0x1f80104eu
#define SIO_MODE_ADDRESS 0x1f801048u
#define VBLANK_MASK 0x00000001u

static int read_step(void *userdata);
static int serial_irq(void *userdata, uint16_t pending);

static void prepare_descriptor(MusashiBiosCardDescriptor *descriptor,
                               MusashiBiosCardDescriptorRole role) {
    descriptor->next = NULL;
    descriptor->reserved = 0;
    descriptor->role = role;
    descriptor->handler = NULL;
    descriptor->verifier = NULL;
}

static int zero_storage(const MusashiBiosCard *card) {
    const unsigned char *bytes = (const unsigned char *)card;
    size_t i;
    for (i = 0; i < sizeof(*card); ++i)
        if (bytes[i] != 0) return 0;
    return 1;
}

static int descriptor_coherent(const MusashiBiosCardDescriptor *descriptor,
                               MusashiBiosCardDescriptorRole role) {
    return descriptor->next == NULL && descriptor->role == role &&
           descriptor->handler == NULL && descriptor->verifier == NULL;
}

static int node_in_registry(const MusashiBiosKernel *kernel,
                            const MusashiBiosIrqNode *wanted) {
    const MusashiBiosIrqNode *node;
    if (!kernel || !wanted || wanted->priority >= 4) return 0;
    for (node = kernel->heads[wanted->priority]; node; node = node->next)
        if (node == wanted) return 1;
    return 0;
}

static int hook_coherent(const MusashiBiosCard *card) {
    const MusashiBiosEarlyHook *hook = &card->early_hook;
    const MusashiBiosKernel *kernel = card->kernel;
    return kernel && hook->owner == kernel && kernel->early_hook == hook &&
           hook->execute == musashi_bios_card_early_hook &&
           hook->userdata == card;
}

static int device_coherent(const MusashiBiosCardStartDevice *device) {
    return device && device->write_sio16 && device->read_irq32 &&
           device->write_irq32 && device->enter_critical &&
           device->exit_critical && device->read_pad &&
           device->exchange_pad && device->change_timer;
}

int musashi_bios_card_init(MusashiBiosCard *card, MusashiBiosKernel *kernel) {
    if (!card || !kernel || !zero_storage(card)) return 0;
    memset(card, 0, sizeof(*card));
    card->kernel = kernel;
    prepare_descriptor(&card->sio_descriptor,
                       MUSASHI_BIOS_CARD_DESCRIPTOR_SIO);
    prepare_descriptor(&card->card_descriptor,
                       MUSASHI_BIOS_CARD_DESCRIPTOR_CARD);
    return 1;
}

int musashi_bios_card_early_hook(void *userdata) {
    MusashiBiosCard *card = userdata;
    if (!card || !hook_coherent(card) || card->hook_calls == UINT64_MAX)
        return 0;
    card->hook_calls++;
    return card->fast_track_active == 0;
}

static void reset_source_state(MusashiBiosCard *card) {
    prepare_descriptor(&card->sio_descriptor,
                       MUSASHI_BIOS_CARD_DESCRIPTOR_SIO);
    card->action_in_progress = 0;
    card->port = 0;
    card->flags[0] = 1;
    card->flags[1] = 1;
    prepare_descriptor(&card->card_descriptor,
                       MUSASHI_BIOS_CARD_DESCRIPTOR_CARD);
    card->fast_track_active = 0;
}

static int service_preflight(MusashiBiosCard *card) {
    MusashiBiosKernel *kernel = card->kernel;
    if (!kernel || card->fast_track_active != 0 || kernel->dispatching ||
        card->sio_node.owner != NULL || card->sio_node.next != NULL ||
        node_in_registry(kernel, &card->sio_node) || card->start_faulted ||
        card->card_node.owner || card->card_node.next || card->transfer_faulted ||
        !descriptor_coherent(&card->sio_descriptor,
                             MUSASHI_BIOS_CARD_DESCRIPTOR_SIO) ||
        !descriptor_coherent(&card->card_descriptor,
                             MUSASHI_BIOS_CARD_DESCRIPTOR_CARD))
        return 0;
    if (card->early_hook.owner != NULL) {
        if (!hook_coherent(card)) return 0;
    } else if (card->early_hook.execute != NULL ||
               card->early_hook.userdata != NULL || kernel->early_hook != NULL) {
        return 0;
    }
    return musashi_bios_kernel_can_install_early_hook(kernel,
                                                        &card->early_hook);
}

int musashi_bios_card_init_service(void *userdata, int32_t pad_started,
                                   int32_t *old_initialized) {
    MusashiBiosCard *card = userdata;
    MusashiBiosKernel *kernel;
    if (!card || !old_initialized || !(kernel = card->kernel) ||
        !service_preflight(card))
        return 0;

    reset_source_state(card);
    if (!musashi_bios_kernel_install_early_hook(
            kernel, &card->early_hook, musashi_bios_card_early_hook, card))
        return 0;
    *old_initialized = (int32_t)card->initialized_word;
    card->initialized_word = 1;
    card->pad_started = pad_started;
    return 1;
}

int musashi_bios_card_bind_start_device(
    MusashiBiosCard *card, const MusashiBiosCardStartDevice *device) {
    if (!card || !card->kernel || card->kernel->dispatching || card->start_device ||
        !device_coherent(device) || card->sio_node.owner != NULL)
        return 0;
    card->start_device = device;
    return 1;
}

static int write_sio(const MusashiBiosCardStartDevice *device,
                     uint32_t address, uint16_t value) {
    return device->write_sio16(device->userdata, address, value);
}

static void delay_helper(MusashiBiosCard *card) {
    /* ROM 445C: post-decrement loop, including the terminating store. This
     * models ordinary private stack work, not elapsed hardware time. */
    volatile uint32_t value = 10;
    uint32_t previous;
    do {
        previous = value;
        value = previous - 1u;
    } while (previous != 0);
    card->delay_calls++;
    card->delay_final_word = (int32_t)value;
}

static int start_preflight(MusashiBiosCard *card) {
    MusashiBiosKernel *kernel = card->kernel;
    if (!card->initialized_word || !device_coherent(card->start_device) ||
        !kernel || kernel->dispatching || card->fast_track_active != 0 ||
        card->start_faulted || card->transfer_faulted || card->card_node.owner ||
        card->delay_calls > UINT32_MAX - 3u ||
        !descriptor_coherent(&card->sio_descriptor,
                             MUSASHI_BIOS_CARD_DESCRIPTOR_SIO) ||
        !descriptor_coherent(&card->card_descriptor,
                             MUSASHI_BIOS_CARD_DESCRIPTOR_CARD) ||
        !hook_coherent(card))
        return 0;
    if (!card->sio_node.owner) {
        if (node_in_registry(kernel, &card->sio_node) || card->sio_node.next ||
            (card->sio_node.execute &&
             (card->sio_node.execute != musashi_bios_card_sio_vblank ||
              card->sio_node.userdata != card)))
            return 0;
    } else if (card->sio_node.owner != kernel ||
               !node_in_registry(kernel, &card->sio_node) ||
               card->sio_node.priority != 2 || card->sio_node.mask != 1 ||
               card->sio_node.execute != musashi_bios_card_sio_vblank ||
               card->sio_node.userdata != card)
        return 0;
    return 1;
}

int musashi_bios_card_start_service(void *userdata, int32_t *guest_result) {
    MusashiBiosCard *card = userdata;
    const MusashiBiosCardStartDevice *device;
    MusashiBiosKernel *kernel;
    uint32_t mask;
    int32_t old_pad;
    int node_was_registered;

    if (!card || !guest_result || !start_preflight(card)) return 0;
    device = card->start_device;
    kernel = card->kernel;
    node_was_registered = card->sio_node.owner == kernel;
    if (!write_sio(device, SIO_CTRL_ADDRESS, 0x0040)) goto failed;
    if (!write_sio(device, SIO_BAUD_ADDRESS, 0x0088) ||
        !write_sio(device, SIO_MODE_ADDRESS, 0x000d) ||
        !write_sio(device, SIO_CTRL_ADDRESS, 0x0000)) goto failed;
    delay_helper(card);
    if (!write_sio(device, SIO_CTRL_ADDRESS, 0x0002)) goto failed;
    delay_helper(card);
    if (!write_sio(device, SIO_CTRL_ADDRESS, 0x2002)) goto failed;
    delay_helper(card);
    if (!write_sio(device, SIO_CTRL_ADDRESS, 0x0000)) goto failed;
    card->serial_reset_word = 0;
    if (!device->enter_critical(device->userdata)) goto failed;

    if (node_was_registered && !musashi_bios_kernel_dequeue(kernel,
                                                              &card->sio_node))
        goto failed;
    if (!musashi_bios_kernel_enqueue(kernel, &card->sio_node, 2, 1,
                                     musashi_bios_card_sio_vblank, card))
        goto failed;

    if (!device->read_irq32(device->userdata, IRQ_MASK_ADDRESS, &mask) ||
        !device->write_irq32(device->userdata, IRQ_MASK_ADDRESS, mask | 1u))
        goto failed;
    if (!device->exchange_pad(device->userdata, 1, &old_pad) ||
        !device->change_timer(device->userdata, 3, 0))
        goto failed;
    card->card_started = 1;
    if (!device->exit_critical(device->userdata)) goto failed;
    *guest_result = 1;
    return 1;
failed:
    card->start_faulted = 1;
    return 0;
}

static int undeliver_idle_events(MusashiBiosCard *card) {
    static const uint32_t specs[5] = {4u, 0x8000u, 0x100u, 0x200u, 0x2000u};
    unsigned i;
    for (i = 0; i < 5; ++i)
        if (!musashi_bios_events_undeliver(&card->kernel->events,
                                           0xf0000011u, specs[i]))
            return 0;
    return 1;
}

static int active_capable(const MusashiBiosCard *card) {
    const MusashiBiosCardStartDevice *d = card->start_device;
    return device_coherent(d) && d->read_sio16 && d->read_sio8 &&
        d->write_sio8 && d->cards_disconnected &&
        d->cards_disconnected(d->userdata) == 1 && card->completion &&
        card->completion->error2;
}

static int card_node_coherent(const MusashiBiosCard *card) {
    const MusashiBiosIrqNode *n = &card->card_node;
    if (!n->owner)
        return !n->next && !node_in_registry(card->kernel, n) &&
            (!n->execute || (n->execute == serial_irq && n->userdata == card));
    return n->owner == card->kernel && n->priority == 1 && n->mask == 0x80 &&
        n->execute == serial_irq && n->userdata == card &&
        node_in_registry(card->kernel, n);
}

int musashi_bios_card_bind_completion(MusashiBiosCard *card,
    const MusashiBiosCardCompletion *completion) {
    if (!card || !card->kernel || card->kernel->dispatching ||
        card->completion || !completion || !completion->error2 ||
        card->card_node.owner || card->action_in_progress)
        return 0;
    card->completion = completion;
    return 1;
}

int musashi_bios_card_unbind_completion(MusashiBiosCard *card,
    const MusashiBiosCardCompletion *completion) {
    if (!card || card->completion != completion || !completion ||
        card->card_node.owner || card->card_node.next ||
        (card->kernel && (card->kernel->dispatching || card->card_node.owner ||
                          card->action_in_progress)))
        return 0;
    card->completion = NULL;
    return 1;
}

int musashi_bios_card_allow_new(MusashiBiosCard *card) {
    if (!card || !card->kernel || card->kernel->dispatching ||
        !hook_coherent(card) || card->start_faulted || card->transfer_faulted)
        return 0;
    card->serial_reset_word = 1; /* Selected B0:50 does not write v0. */
    return 1;
}

int musashi_bios_card_queue_read(MusashiBiosCard *card, uint32_t device_id,
    uint32_t sector, uint8_t *buffer, size_t bytes, int32_t *accepted) {
    unsigned port = device_id >> 4;
    MusashiBiosCardRead *request;
    if (!card || !card->kernel || card->kernel->dispatching || !accepted ||
        (device_id != 0 && device_id != 0x10) || sector != 0 ||
        !buffer || bytes != 128 || !active_capable(card) ||
        !hook_coherent(card) || !card->initialized_word || !card->card_started ||
        card->start_faulted || card->transfer_faulted ||
        card->fast_track_active || !card_node_coherent(card))
        return 0;
    if (!(card->flags[port] & 1u)) { *accepted = 0; return 1; }
    if (card->action_in_progress || card->card_node.owner) return 0;
    request = &card->reads[port];
    card->operation_step = 0;
    card->source_word_74a4 = 0;
    request->device_id = device_id;
    request->buffer = buffer;
    request->bytes = bytes;
    request->execute = read_step;
    request->sector = sector;
    card->flags[port] = 2;
    *accepted = 1;
    return 1;
}

/* Selected RAM5688, operation1 only. No ready status, ACK, payload, or
 * completion is synthesized: the explicit disconnected SIO owns its RX. */
static int read_step(void *userdata) {
    MusashiBiosCard *card = userdata;
    const MusashiBiosCardStartDevice *d = card->start_device;
    uint16_t control;
    uint8_t discarded;
    if (card->operation_step != 1 || card->port > 1 ||
        card->reads[card->port].device_id != (uint32_t)card->port * 16u)
        return 0;
    if (!write_sio(d, SIO_CTRL_ADDRESS, (uint16_t)(card->port_mask | 0x1003)) ||
        !d->read_sio8(d->userdata, 0x1f801040, &discarded) ||
        !d->write_sio8(d->userdata, 0x1f801040, 0x81) ||
        !d->read_sio16(d->userdata, SIO_CTRL_ADDRESS, &control) ||
        !write_sio(d, SIO_CTRL_ADDRESS, control | 0x10) ||
        !d->write_irq32(d->userdata, IRQ_STAT_ADDRESS, 0xffffff7f))
        return 0;
    card->action_in_progress = 1;
    card->port_mask = card->port ? 0x2000 : 0;
    return 1; /* Host acceptance; the selected step's guest result is0. */
}

static int first_card_step(MusashiBiosCard *card) {
    const MusashiBiosCardStartDevice *d = card->start_device;
    uint16_t control;
    uint32_t mask;
    uint32_t port_mask = card->port ? 0x2000 : 0;
    if (!d->read_sio16(d->userdata, SIO_CTRL_ADDRESS, &control) ||
        !write_sio(d, SIO_CTRL_ADDRESS, (uint16_t)(control | port_mask | 0x12)))
        return 0;
    card->operation_step++;
    card->port_mask = port_mask;
    if (!card->reads[card->port].execute(card) ||
        !d->write_irq32(d->userdata, IRQ_STAT_ADDRESS, 0xffffff7f) ||
        !d->read_irq32(d->userdata, IRQ_MASK_ADDRESS, &mask) ||
        !d->write_irq32(d->userdata, IRQ_MASK_ADDRESS, mask | 0x80))
        return 0;
    return 1;
}

static int serial_irq(void *userdata, uint16_t pending) {
    MusashiBiosCard *card = userdata;
    const MusashiBiosCardStartDevice *d;
    uint32_t mask, status;
    (void)pending;
    if (!card || !card->kernel || card->transfer_faulted ||
        !card_node_coherent(card) || !active_capable(card)) return 0;
    d = card->start_device;
    if (!d->read_irq32(d->userdata, IRQ_MASK_ADDRESS, &mask)) goto refused;
    if (!(mask & 0x80)) return 1;
    if (!d->read_irq32(d->userdata, IRQ_STAT_ADDRESS, &status)) goto refused;
    if (!(status & 0x80) || (card->fast_track_active & 1u)) return 1;
    /* A disconnected card never ACKs. A live IRQ here would require the
     * unsupported next serial operation, so preserve the pending cause. */
refused:
    card->transfer_faulted = 1;
    return 0;
}

static int launch_read(MusashiBiosCard *card) {
    MusashiBiosCardRead *request = &card->reads[card->port];
    if (!active_capable(card) || !card_node_coherent(card) ||
        request->execute != read_step || request->device_id != card->port * 16u ||
        request->sector != 0 || !request->buffer || request->bytes != 128)
        return 0;
    card->fast_track_counter = 0;
    card->fast_track_active = 0;
    card->fast_track_buffer = request->buffer;
    card->fast_track_flags = &card->flags[card->port];
    card->fast_track_checksum = &card->checksum[card->port];
    if (card->card_node.owner && !musashi_bios_kernel_dequeue_from_handler(
            card->kernel, &card->sio_node, &card->card_node)) goto refused;
    if (!musashi_bios_kernel_enqueue_from_handler(card->kernel, &card->sio_node,
            &card->card_node, 1, 0x80, serial_irq, card)) goto refused;
    card->operation_step = 0;
    card->got_error = 0;
    if (!first_card_step(card)) goto refused;
    return 1;
refused:
    card->transfer_faulted = 1;
    return 0;
}

static int timeout_read(MusashiBiosCard *card) {
    const MusashiBiosCardStartDevice *d = card->start_device;
    uint32_t mask;
    if (!active_capable(card) || card->port > 1 ||
        !card_node_coherent(card) || !card->card_node.owner ||
        card->reads[card->port].execute != read_step ||
        card->operation_step != 1)
        return 0;
    card->action_in_progress = 0;
    card->source_word_74a4 = 0;
    card->operation_step = 0;
    if (!d->write_irq32(d->userdata, IRQ_STAT_ADDRESS, 0xffffff7f) ||
        !d->read_irq32(d->userdata, IRQ_MASK_ADDRESS, &mask) ||
        !d->write_irq32(d->userdata, IRQ_MASK_ADDRESS, mask & 0xffffff7f) ||
        !write_sio(d, SIO_CTRL_ADDRESS, 0)) goto refused;
    card->serial_reset_word = 0;
    card->flags[card->port] = 0x11;
    card->last_port = card->port;
    if (!card->completion->error2(card->completion->userdata,
                                  card->reads[card->last_port].device_id) ||
        !musashi_bios_events_deliver(&card->kernel->events, 0xf0000011, 0x100,
                                      d->execute_event, d->userdata) ||
        !musashi_bios_kernel_dequeue_from_handler(card->kernel, &card->sio_node,
                                                  &card->card_node) ||
        !write_sio(d, SIO_CTRL_ADDRESS, 0x40) ||
        !write_sio(d, SIO_BAUD_ADDRESS, 0x88) ||
        !write_sio(d, SIO_MODE_ADDRESS, 0xd) ||
        !write_sio(d, SIO_CTRL_ADDRESS, 0)) goto refused;
    return 1;
refused:
    card->transfer_faulted = 1;
    return 0;
}

int musashi_bios_card_sio_vblank(void *userdata, uint16_t pending) {
    MusashiBiosCard *card = userdata;
    const MusashiBiosCardStartDevice *device;
    uint32_t status, mask;
    int32_t pad_auto_ack;
    (void)pending;
    if (!card || !card->kernel || card->start_faulted || card->transfer_faulted ||
        !hook_coherent(card) ||
        card->sio_node.owner != card->kernel ||
        card->sio_node.priority != 2 || card->sio_node.mask != 1 ||
        card->sio_node.execute != musashi_bios_card_sio_vblank ||
        card->sio_node.userdata != card ||
        !node_in_registry(card->kernel, &card->sio_node) ||
        !device_coherent(card->start_device))
        return 0;
    device = card->start_device;
    if (!device->read_irq32(device->userdata, IRQ_MASK_ADDRESS, &mask)) return 0;
    if (!(mask & VBLANK_MASK)) return 1;
    if (!device->read_irq32(device->userdata, IRQ_STAT_ADDRESS, &status)) return 0;
    if (!(status & VBLANK_MASK)) return 1;
    if (card->vblank_calls == UINT64_MAX) return 0;
    card->vblank_calls++;
    if (card->pad_started != 0 ||
        !device->read_pad(device->userdata, &pad_auto_ack))
        return 0;
    if (pad_auto_ack != 0 &&
        !device->write_irq32(device->userdata, IRQ_STAT_ADDRESS, 0xfffffffeu))
        return 0;
    if (!card->card_started) return 1;
    if (card->maintenance_calls == UINT64_MAX) return 0;
    card->maintenance_calls++;
    if (!undeliver_idle_events(card)) return 0;
    if (card->action_in_progress != 0) return timeout_read(card);
    card->port = (uint8_t)(1u - card->port);
    if (card->port > 1) return 0;
    if ((card->flags[card->port] & 1u) == 0) return launch_read(card);
    return 1;
}

int musashi_bios_card_shutdown(MusashiBiosCard *card) {
    MusashiBiosKernel *kernel;
    if (!card || !(kernel = card->kernel) || kernel->dispatching) return 0;
    /* Preflight all leases before releasing any. Host shutdown does not
     * complete, cancel, or reset the guest's partially executed request. */
    if (!card_node_coherent(card)) return 0;
    if (card->early_hook.owner ? !hook_coherent(card) :
        (card->early_hook.execute || card->early_hook.userdata || kernel->early_hook))
        return 0;
    if (!card->sio_node.owner &&
        (card->sio_node.next || node_in_registry(kernel, &card->sio_node)))
        return 0;
    if (card->sio_node.owner != NULL) {
        if (card->sio_node.owner != kernel ||
            card->sio_node.priority != 2 || card->sio_node.mask != 1 ||
            card->sio_node.execute != musashi_bios_card_sio_vblank ||
            card->sio_node.userdata != card ||
            !node_in_registry(kernel, &card->sio_node))
            return 0;
    }
    if (card->card_node.owner &&
        !musashi_bios_kernel_dequeue(kernel, &card->card_node)) return 0;
    if (card->sio_node.owner &&
        !musashi_bios_kernel_dequeue(kernel, &card->sio_node)) return 0;
    if (card->early_hook.owner == NULL) {
        if (card->early_hook.execute != NULL ||
            card->early_hook.userdata != NULL || kernel->early_hook != NULL)
            return 0;
        card->kernel = NULL;
        return 1;
    }
    if (!hook_coherent(card) ||
        !musashi_bios_kernel_remove_early_hook(kernel, &card->early_hook))
        return 0;
    card->kernel = NULL;
    return 1;
}
