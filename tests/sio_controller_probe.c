#include "musashi_sio_controller.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct PadContext {
    uint64_t owner;
    uint16_t buttons;
    int sample_calls;
    int irq_calls;
    int fail_sample;
    int fail_irq;
    uint16_t irq_masks[4];
} PadContext;

static uint64_t pad_owner(void *userdata) {
    return ((PadContext *)userdata)->owner;
}

static int pad_sample(void *userdata, uint16_t *buttons) {
    PadContext *ctx = userdata;
    ctx->sample_calls++;
    if (ctx->fail_sample || !buttons) return 0;
    *buttons = ctx->buttons;
    return 1;
}

static int pad_irq(void *userdata, uint16_t mask) {
    PadContext *ctx = userdata;
    if (ctx->irq_calls < 4) ctx->irq_masks[ctx->irq_calls] = mask;
    ctx->irq_calls++;
    return !ctx->fail_irq;
}

static void configure_pad(MusashiSioController *sio, PadContext *ctx) {
    MusashiSioPadDevice device = {ctx, pad_owner, pad_sample, pad_irq};
    musashi_sio_controller_init_disconnected(sio);
    ctx->owner = 1;
    ctx->fail_sample = 0;
    ctx->fail_irq = 0;
    assert(musashi_sio_controller_bind_digital_pad(sio, &device));
    assert(!musashi_sio_controller_bind_digital_pad(sio, &device));
    assert(musashi_sio_controller_write16(sio, 0x1f801048, 0x0d));
    assert(musashi_sio_controller_write16(sio, 0x1f80104e, 0x88));
    assert(musashi_sio_controller_write16(sio, 0x1f80104a, 0x1003));
}

static uint8_t read_rx(MusashiSioController *sio) {
    uint8_t value = 0;
    assert(musashi_sio_controller_read8(sio, 0x1f801040, &value));
    return value;
}

static void test_pad_exchange_and_timing(void) {
    MusashiSioController sio;
    PadContext ctx = {0, 0x1230, 0, 0, 0, 0, {0, 0, 0, 0}};
    uint16_t status = 0;
    uint64_t first_due;

    configure_pad(&sio, &ctx);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x01));
    assert(read_rx(&sio) == 0xff && ctx.sample_calls == 1);
    assert(sio.pad_state == MUSASHI_SIO_PAD_COMMAND);
    assert(sio.ack_pending && sio.ack_due == 1088);
    assert(sio.cycle == 0); /* DATA and DATA reads do not advance the epoch. */
    assert(musashi_sio_controller_advance(&sio, 1087));
    assert(ctx.irq_calls == 0 && !(sio.status & 0x0200));
    assert(musashi_sio_controller_advance(&sio, 1088));
    assert(ctx.irq_calls == 1 && ctx.irq_masks[0] == 0x0080);
    assert(musashi_sio_controller_read16(&sio, 0x1f801044, &status));
    assert((status & 0x0200) && !(status & 0x0080));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1013));
    assert(!(sio.status & 0x0200) && sio.control == 0x1003);

    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0x41 && sio.ack_due == 2176);
    assert(musashi_sio_controller_advance(&sio, 2176));
    assert(ctx.irq_calls == 2);
    assert(sio.status & 0x0200);

    ctx.buttons = 0x0a30;
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0x5a);
    assert(sio.status & 0x0200); /* DATA does not clear the local IRQ latch. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1013));
    first_due = sio.ack_due;
    ctx.buttons = 0xffff;
    assert(musashi_sio_controller_advance(&sio, first_due - 264));
    assert(ctx.irq_calls == 2);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0x36);
    assert(musashi_sio_controller_advance(&sio, first_due));
    assert(ctx.irq_calls == 2); /* The later response replaced this deadline. */
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0x12);
    assert(sio.ack_pending && sio.ack_due == first_due + 824);
    assert(musashi_sio_controller_advance(&sio, first_due + 824));
    assert(ctx.irq_calls == 3);
    assert(sio.selected_device == MUSASHI_SIO_DEVICE_IGNORE && sio.pad_state == MUSASHI_SIO_PAD_IDLE);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0xff && !sio.ack_pending);

    /* Port 2 is absent even when the pad owner is bound to port 1. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x3003));
    assert(sio.pad_state == MUSASHI_SIO_PAD_IDLE && !sio.ack_pending);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff && sio.pad_state == MUSASHI_SIO_PAD_IDLE);
    assert(ctx.sample_calls == 1 && !sio.ack_pending);

    /* A subsequent 01 samples the then-current provider state. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    ctx.buttons = 0x0100;
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff && ctx.sample_calls == 2);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0x41);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0x5a);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0x06);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(read_rx(&sio) == 0x01);

    /* DualShock 0x43 enters config; later 0x42 stays 0xF3 for TAP analog lock. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x43));
    assert(read_rx(&sio) == 0xf3 && sio.analog_mode == 1);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0xf3);

    /* Two config 0x42 frames lock analog; the next 0x42 is DualShock 0x73. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0xf3);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0x73 && sio.analog_mode >= 3);
}

static void test_pad_refusals_and_reset(void) {
    MusashiSioController sio, saved;
    PadContext ctx = {1, 0x4560, 0, 0, 0, 0, {0, 0, 0, 0}};
    uint16_t out = 0xbeef;
    uint8_t byte = 0xaa;
    uint64_t due;

    configure_pad(&sio, &ctx);
    saved = sio;
    ctx.owner = 2;
    assert(!musashi_sio_controller_read16(&sio, 0x1f80104a, &out));
    assert(out == 0xbeef && !musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
    ctx.owner = 1;
    assert(!musashi_sio_controller_read8(&sio, 0x1f801042, &byte));
    assert(byte == 0xaa);
    saved = sio;
    assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1403));
    assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1803));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x12));
    saved = sio;
    assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));

    /* Card identity 81 remains the absent-card path alongside the pad. */
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x81));
    assert(read_rx(&sio) == 0xff && sio.selected_device == MUSASHI_SIO_DEVICE_IGNORE && sio.pad_state == MUSASHI_SIO_PAD_IDLE);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x52));
    assert(read_rx(&sio) == 0xff && !sio.ack_pending);

    configure_pad(&sio, &ctx);
    ctx.fail_sample = 1;
    assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(sio.faulted && sio.rx_count == 0 && sio.transmitted_bytes == 0);

    configure_pad(&sio, &ctx);
    ctx.fail_sample = 0;
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    due = sio.ack_due;
    ctx.fail_irq = 1;
    assert(!musashi_sio_controller_advance(&sio, due));
    assert(sio.faulted && !sio.ack_pending && (sio.status & 0x0200));

    configure_pad(&sio, &ctx);
    assert(musashi_sio_controller_advance(&sio, 10));
    saved = sio;
    assert(!musashi_sio_controller_advance(&sio, 9));
    assert(sio.faulted && sio.cycle == saved.cycle &&
           sio.rx_count == saved.rx_count && sio.transmitted_bytes == saved.transmitted_bytes);

    configure_pad(&sio, &ctx);
    sio.pad_state = 99;
    saved = sio;
    assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);

    configure_pad(&sio, &ctx);
    sio.cycle = UINT64_MAX - 1000;
    saved = sio;
    assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(sio.faulted && sio.rx_count == saved.rx_count &&
           sio.transmitted_bytes == saved.transmitted_bytes);

    configure_pad(&sio, &ctx);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    due = sio.ack_due;
    assert(musashi_sio_controller_write16(&sio, 0x1f80104e, 0x44));
    assert(sio.ack_due == due);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0));
    assert(sio.pad_state == MUSASHI_SIO_PAD_IDLE && sio.ack_pending);
    assert(sio.rx_count == 0);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    assert(musashi_sio_controller_advance(&sio, due));
    assert(ctx.irq_calls > 0);

    /* RESET cancels the peripheral deadline and FIFO. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104e, 0x88));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(sio.rx_count == 1 && sio.ack_pending);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(sio.rx_count == 0 && !sio.ack_pending && sio.status == 5);
    assert(sio.control == 0x40);
}

static void test_pad_fifo_and_bad_command(void) {
    MusashiSioController sio;
    PadContext ctx = {1, 0x3400, 0, 0, 0, 0, {0, 0, 0, 0}};
    uint8_t byte;
    configure_pad(&sio, &ctx);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x20));
    assert(sio.pad_state == MUSASHI_SIO_PAD_BAD_COMMAND && sio.ack_pending);
    assert(read_rx(&sio) == 0xff && read_rx(&sio) == 0xff);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0xff && sio.ack_pending);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(sio.rx_count == 5);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(sio.rx_count == 8);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
    assert(sio.rx_count == 8);
    assert(read_rx(&sio) == 0xff && read_rx(&sio) == 0x41 &&
           read_rx(&sio) == 0x5a && read_rx(&sio) == 0x06 &&
           read_rx(&sio) == 0x34);
    while (sio.rx_count) assert(read_rx(&sio) == 0xff);
    byte = 0;
    assert(musashi_sio_controller_read8(&sio, 0x1f801040, &byte) && byte == 0xff);
}

static void test_transaction_identity_is_not_a_data_byte(void) {
    MusashiSioController sio;
    PadContext ctx = {0};
    unsigned position, port;
    ctx.buttons = 0x1230;
    /* Test-owned button sample: 81 is an ignored dummy in every DATA slot,
     * not a new memory-card selector in the middle of a PAD transaction. */
    for (position = 0; position < 3; ++position) {
        unsigned i;
        const uint8_t expected[] = {0x5a, 0x36, 0x12};
        configure_pad(&sio, &ctx);
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
        assert(read_rx(&sio) == 0xff);
        assert(musashi_sio_controller_advance(&sio, 1088));
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
        assert(read_rx(&sio) == 0x41);
        for (i = 0; i < 3; ++i) {
            assert(musashi_sio_controller_advance(&sio, sio.ack_due));
            assert(!sio.ack_pending);
            assert(musashi_sio_controller_write8(&sio, 0x1f801040, i == position ? 0x81 : 0));
            assert(read_rx(&sio) == expected[i]);
            assert(sio.ack_pending == (i < 2));
        }
    }
    configure_pad(&sio, &ctx);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    {
        MusashiSioController before = sio;
        assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 0x3003));
        assert(memcmp(&sio, &before, sizeof(sio)) == 0);
    }
    assert(musashi_sio_controller_advance(&sio, 1088));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x81));
    assert(read_rx(&sio) == 0xff && sio.pad_state == MUSASHI_SIO_PAD_BAD_COMMAND);
    assert(!sio.ack_pending);
    for (port = 0; port < 2; ++port) {
        int before;
        configure_pad(&sio, &ctx);
        assert(musashi_sio_controller_write16(&sio, 0x1f80104a, port ? 0x3003 : 0x1003));
        /* Unread FIFO data cannot change which peripheral owns the wire. */
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0));
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x81));
        before = ctx.sample_calls;
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x52));
        assert(ctx.sample_calls == before && !sio.ack_pending);
        assert(sio.rx_count == 4);
        while (sio.rx_count) assert(read_rx(&sio) == 0xff);
    }
    configure_pad(&sio, &ctx);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x3003));
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 1));
    assert(read_rx(&sio) == 0xff);
    assert(sio.selected_device == MUSASHI_SIO_DEVICE_PAD);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
    assert(read_rx(&sio) == 0xff && sio.pad_state == MUSASHI_SIO_PAD_IDLE);
    assert(sio.selected_device == MUSASHI_SIO_DEVICE_PAD);
    assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x81));
    assert(read_rx(&sio) == 0xff && sio.pad_state == MUSASHI_SIO_PAD_IDLE);
    assert(sio.selected_device == MUSASHI_SIO_DEVICE_PAD);
    assert(!sio.ack_pending);
}

int main(void) {
    test_transaction_identity_is_not_a_data_byte();
    MusashiSioController sio, saved;
    uint16_t value;
    musashi_sio_controller_init(&sio);
    assert(sio.control == 0 && sio.mode == 0 && sio.baud == 0);
    assert(sio.selected_port == -1);
    /* Actual BIOS 43E8 write order, with no DATA access. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104e, 0x88));
    assert(musashi_sio_controller_write16(&sio, 0x1f801048, 0x0d));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 2));
    assert(sio.selected_port == 0);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x2002));
    assert(sio.selected_port == 1);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0));
    assert(sio.selected_port == -1);
    assert(musashi_sio_controller_read16(&sio, 0x1f801048, &value) && value == 0x0d);
    assert(musashi_sio_controller_read16(&sio, 0x1f80104e, &value) && value == 0x88);
    assert(musashi_sio_controller_read16(&sio, 0x1f80104a, &value) && value == 0);
    saved = sio;
    /* TX/IRQ enables and DATA/status never turn into successful idle I/O. */
    assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 3));
    assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1000));
    assert(!musashi_sio_controller_write16(&sio, 0x1f801040, 0x81));
    assert(!musashi_sio_controller_write16(&sio, 0x1f801044, 0));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
    value = 0xbeef;
    assert(!musashi_sio_controller_read16(&sio, 0x1f801044, &value));
    assert(!musashi_sio_controller_read16(&sio, 0x1f801040, &value));
    assert(value == 0xbeef);
    /* Selected emulator retains RESET in control; it does not reset MODE/BAUD. */
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
    assert(sio.control == 0x40 && sio.mode == 0x0d && sio.baud == 0x88);
    assert(sio.selected_port == -1);
    assert(!musashi_sio_controller_read16(NULL, 0x1f80104a, &value));
    assert(!musashi_sio_controller_read16(&sio, 0x1f80104a, NULL));
    assert(!musashi_sio_controller_write16(NULL, 0x1f80104a, 0));
    /* Explicit disconnected-card profile: a real transmit produces an FF RX
     * byte, no ACK/IRQ, and waits for software timeout. No card command or
     * sector payload completion is synthesized. */
    musashi_sio_controller_init_disconnected(&sio);
    assert(sio.disconnected_cards && sio.rx_count == 0);
    assert(musashi_sio_controller_write16(&sio, 0x1f801048, 0x0d));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104e, 0x88));
    for (unsigned port = 0; port < 2; ++port) {
        uint16_t select = (uint16_t)(port << 13);
        uint8_t byte = 0;
        assert(musashi_sio_controller_write16(&sio, 0x1f80104a, select | 0x12));
        assert(sio.control == (select | 2));
        assert(musashi_sio_controller_write16(&sio, 0x1f80104a, select | 0x1003));
        assert(musashi_sio_controller_read8(&sio, 0x1f801040, &byte) && byte == 0xff);
        assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x81));
        assert(sio.rx_count == 1 && sio.protocol_ignore == 1);
        assert(musashi_sio_controller_read16(&sio, 0x1f801044, &value) && value == 7);
        assert(musashi_sio_controller_write16(&sio, 0x1f80104a, select | 0x1013));
        assert(sio.control == (select | 0x1003));
        saved = sio;
        assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 0x52));
        assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1403));
        assert(!musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1803));
        assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
        assert(musashi_sio_controller_read8(&sio, 0x1f801040, &byte) && byte == 0xff);
        assert(sio.rx_count == 0);
        assert(musashi_sio_controller_read16(&sio, 0x1f801044, &value) && value == 5);
        assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0));
        assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x40));
        assert(sio.rx_count == 0 && !sio.protocol_ignore);
    }
    assert(sio.transmitted_bytes == 2);
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));
    saved = sio;
    assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 1)); /* Pad polling. */
    assert(!musashi_sio_controller_write8(&sio, 0x1f801044, 0x81));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
    sio.transmitted_bytes = UINT64_MAX;
    saved = sio;
    assert(!musashi_sio_controller_write8(&sio, 0x1f801040, 0x81));
    assert(memcmp(&sio, &saved, sizeof(sio)) == 0);
    test_pad_exchange_and_timing();
    test_pad_refusals_and_reset();
    test_pad_fifo_and_bad_command();
    return 0;
}
