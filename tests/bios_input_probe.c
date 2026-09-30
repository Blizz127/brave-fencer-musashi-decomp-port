#include "musashi_bios_input.h"
#include <assert.h>
#include <string.h>

#define DESCRIPTOR 0x80078988u
#define HANDLER 0x8005d734u
#define VERIFIER 0x8005d6ccu

/* Actual BIOS card/kernel owners with fake hardware and source executor.
 * These tests prove ordering/refusal, not controller or MIPS fidelity. */
typedef struct Fixture {
    MusashiBootMemory memory;
    MusashiBiosKernel kernel;
    MusashiBiosCard card;
    MusashiBiosInput input;
    MusashiBiosInputDevice input_device;
    MusashiBiosCardStartDevice card_device;
    uint32_t mask, status;
    int32_t pad, verifier_result;
    unsigned calls, fail_call, sio_visits;
    char order[16];
    unsigned order_count;
    int recurse;
} Fixture;
static Fixture f;
static MusashiBootMemory before;

static uint32_t read_word(uint32_t a) {
    uint32_t value;
    assert(musashi_boot_read32(&f.memory, a, &value));
    return value;
}
static void write_word(uint32_t a, uint32_t v) {
    assert(musashi_boot_write32(&f.memory, a, v));
}
static int sio_write(void *u, uint32_t a, uint16_t v) {
    (void)u; (void)a; (void)v; return 1;
}
static int irq_read(void *u, uint32_t a, uint32_t *v) {
    Fixture *x = u;
    if (a == 0x1f801074u) {
        if (x->kernel.executing_node == &x->card.sio_node) {
            x->sio_visits++;
            assert(x->order_count < sizeof(x->order));
            x->order[x->order_count++] = 'S';
        }
        *v = x->mask;
    } else if (a == 0x1f801070u) *v = x->status;
    else return 0;
    return 1;
}
static int irq_write(void *u, uint32_t a, uint32_t v) {
    Fixture *x = u;
    if (a == 0x1f801074u) x->mask = v;
    else if (a == 0x1f801070u) x->status &= v;
    else return 0;
    return 1;
}
static int critical(void *u) {
    return musashi_bios_kernel_before_exception(&((Fixture *)u)->kernel);
}
static int pad_read(void *u, int32_t *v) { *v = ((Fixture *)u)->pad; return 1; }
static int pad_exchange(void *u, int32_t v, int32_t *old) {
    Fixture *x = u; *old = x->pad; x->pad = v; return 1;
}
static int timer(void *u, int32_t channel, int32_t v) {
    (void)u; assert(channel == 3 && v == 0); return 1;
}
static int execute(void *u, uint32_t target, int32_t argument, int32_t *v) {
    Fixture *x = u;
    int32_t untouched = 123;
    assert(x->kernel.executing_node == &x->input.node);
    if (x->recurse) {
        assert(!musashi_bios_kernel_dispatch(&x->kernel, 1));
        assert(!musashi_bios_input_dequeue(&x->input, 2, DESCRIPTOR, &untouched));
        assert(untouched == 123 && !musashi_bios_input_shutdown(&x->input));
    }
    assert(x->order_count < sizeof(x->order));
    if (target == VERIFIER) {
        x->order[x->order_count++] = 'V';
        *v = x->verifier_result;
    } else {
        assert(target == HANDLER && argument == x->verifier_result);
        x->order[x->order_count++] = 'H';
        write_word(0x800729c4u, 1); /* Explicit partial executor effect. */
        *v = 0;
    }
    return ++x->calls != x->fail_call;
}
static void init(int start) {
    int32_t result;
    memset(&f, 0, sizeof(f));
    memset(&f.memory, 0xa5, sizeof(f.memory));
    musashi_bios_kernel_init(&f.kernel);
    assert(musashi_bios_card_init(&f.card, &f.kernel));
    f.card_device = (MusashiBiosCardStartDevice){.userdata=&f,
        .write_sio16=sio_write, .read_irq32=irq_read, .write_irq32=irq_write,
        .enter_critical=critical, .exit_critical=critical, .read_pad=pad_read,
        .exchange_pad=pad_exchange, .change_timer=timer};
    f.input_device = (MusashiBiosInputDevice){&f, execute};
    before = f.memory;
    assert(musashi_bios_input_init(&f.input, &f.memory, &f.kernel, &f.card,
                                  &f.input_device));
    assert(!memcmp(&before, &f.memory, sizeof(before)));
    assert(!musashi_bios_input_init(&f.input, &f.memory, &f.kernel, &f.card,
                                   &f.input_device));
    assert(musashi_bios_card_bind_start_device(&f.card, &f.card_device));
    assert(musashi_bios_card_init_service(&f.card, 0, &result));
    if (start) assert(musashi_bios_card_start_service(&f.card, &result));
    f.pad = 0; f.mask = 1; f.status = 1;
    write_word(DESCRIPTOR, 0);
    write_word(DESCRIPTOR+4, HANDLER);
    write_word(DESCRIPTOR+8, VERIFIER);
    write_word(DESCRIPTOR+12, 0x12345678); /* Reserved is not a zero-only ABI. */
}
static void enqueue(void) {
    int32_t result = -123;
    before = f.memory;
    assert(musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result));
    assert(result == 0 && f.kernel.heads[2] == &f.input.node);
    assert(f.input.node.next == &f.card.sio_node && read_word(DESCRIPTOR) == 0x74a8);
    assert(musashi_boot_write32(&before, DESCRIPTOR, 0x74a8));
    assert(!memcmp(&before, &f.memory, sizeof(before)));
}
static void finish(void) {
    before = f.memory;
    assert(musashi_bios_input_shutdown(&f.input));
    assert(!memcmp(&before, &f.memory, sizeof(before)));
    assert(musashi_bios_card_shutdown(&f.card));
}
int main(void) {
    int32_t result;
    MusashiBiosIrqNode foreign = {0};
    init(0); result = 88;
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result) && result == 88);
    finish();
    init(1); before = f.memory; result = 88;
    assert(musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR, &result) && result == 0);
    assert(!memcmp(&before, &f.memory, sizeof(before)));
    enqueue();
    result = 88;
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result) && result == 88);
    f.verifier_result = 0;
    assert(musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.calls == 1 && f.sio_visits == 1 && !memcmp(f.order, "VS", 2));
    f.calls = f.sio_visits = f.order_count = 0; f.verifier_result = 7; f.recurse = 1;
    assert(musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.calls == 2 && f.sio_visits == 1 && !memcmp(f.order, "VHS", 3));
    assert(!f.input.faulted && read_word(0x800729c4u) == 1);
    before = f.memory;
    assert(musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR, &result));
    assert((uint32_t)result == DESCRIPTOR && f.kernel.heads[2] == &f.card.sio_node);
    assert(!memcmp(&before, &f.memory, sizeof(before)) && read_word(DESCRIPTOR) == 0x74a8);
    enqueue(); finish();

    init(1); result = 99;
    assert(!musashi_bios_input_enqueue(&f.input, 1, DESCRIPTOR, &result));
    assert(!musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR+4, &result));
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, NULL));
    write_word(DESCRIPTOR+4, HANDLER+4);
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result) && result == 99);
    assert(musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR, &result) && result == 0);
    write_word(DESCRIPTOR+4, HANDLER);
    f.input.node.execute = musashi_bios_card_sio_vblank;
    result = 99;
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result) && result == 99);
    f.input.node.execute = NULL;
    foreign = f.card.sio_node; foreign.next = &f.card.sio_node;
    f.kernel.heads[2] = &foreign;
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result));
    assert(!musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR, &result));
    f.kernel.heads[2] = &f.card.sio_node; f.card.sio_node.next = &f.card.sio_node;
    assert(!musashi_bios_input_enqueue(&f.input, 2, DESCRIPTOR, &result));
    f.card.sio_node.next = NULL;
    enqueue(); write_word(DESCRIPTOR+8, VERIFIER+4);
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.input.faulted && !f.calls);
    finish();

    init(1); enqueue(); write_word(DESCRIPTOR, 0xdeadbeefu); result = 99;
    assert(!musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR, &result) && result == 99);
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.input.faulted && !f.calls);
    finish();
    init(1); enqueue(); f.input_device.execute = NULL;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.input.faulted);
    finish();

    init(1); enqueue(); f.verifier_result = 1; f.fail_call = 2;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.input.faulted && f.calls == 2 && !f.sio_visits);
    assert(read_word(0x800729c4u) == 1);
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.calls == 2);
    result = 99;
    assert(!musashi_bios_input_dequeue(&f.input, 2, DESCRIPTOR, &result) && result == 99);
    finish();

    init(1); enqueue(); f.fail_call = 1;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.input.faulted && f.calls == 1 && !f.sio_visits);
    finish();
    init(1); enqueue();
    assert(musashi_bios_kernel_dequeue(&f.kernel, &f.card.sio_node));
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.input.faulted && !f.calls);
    finish();
    return 0;
}
