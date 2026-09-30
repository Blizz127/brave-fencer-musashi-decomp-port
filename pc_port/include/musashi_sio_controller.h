#ifndef MUSASHI_SIO_CONTROLLER_H
#define MUSASHI_SIO_CONTROLLER_H

#include <stdint.h>

enum {
    MUSASHI_SIO_PAD_IDLE = 0,
    MUSASHI_SIO_PAD_COMMAND = 1,
    MUSASHI_SIO_PAD_DATA = 2,
    MUSASHI_SIO_PAD_BAD_COMMAND = 4
};
enum {
    MUSASHI_SIO_DEVICE_NONE = 0,
    MUSASHI_SIO_DEVICE_PAD = 1,
    MUSASHI_SIO_DEVICE_IGNORE = 2
};

typedef struct MusashiSioPadDevice {
    void *userdata;
    uint64_t (*owner_thread)(void *userdata);
    int (*sample_buttons)(void *userdata, uint16_t *active_low_buttons);
    int (*raise_irq)(void *userdata, uint16_t mask);
} MusashiSioPadDevice;

/* Owner-thread SIO0. Default construction supports idle configuration only.
 * Explicit disconnected-card construction also supports the initial card
 * selection byte and its unacknowledged FF receive byte. A digital pad is
 * enabled only through the fresh checked binding below. */
typedef struct MusashiSioController {
    uint16_t mode;
    uint16_t control;
    uint16_t baud;
    int selected_port;
    int disconnected_cards;
    uint16_t status;
    uint8_t rx_data;
    unsigned rx_count;
    int protocol_ignore;
    uint64_t transmitted_bytes;
    uint8_t rx_fifo[8];
    unsigned rx_head;
    int pad_bound;
    MusashiSioPadDevice pad_device;
    uint64_t owner_token;
    uint64_t cycle;
    int cycle_initialized;
    int ack_pending;
    uint64_t ack_due;
    uint16_t sampled_buttons;
    uint8_t pad_state;
    uint8_t pad_cursor;
    int faulted;
    uint8_t selected_device;
    /* DualShock: 0 = digital 0x41. After 0x43, 0x42 returns 0xF3 for two
     * polls so TAP can lock analog, then 0x73 ('s'). */
    int analog_mode;
} MusashiSioController;

/* Fresh storage only. Native device reset policy, not BIOS RAM initialization. */
void musashi_sio_controller_init(MusashiSioController *);
void musashi_sio_controller_init_disconnected(MusashiSioController *);
int musashi_sio_controller_read16(void *, uint32_t address, uint16_t *value);
int musashi_sio_controller_write16(void *, uint32_t address, uint16_t value);
int musashi_sio_controller_read8(void *, uint32_t address, uint8_t *value);
int musashi_sio_controller_write8(void *, uint32_t address, uint8_t value);

/* Fresh disconnected-card storage only. The owner/provider is captured and
 * cannot be replaced; this does not seed a guest button state. */
int musashi_sio_controller_bind_digital_pad(MusashiSioController *,
                                            const MusashiSioPadDevice *);
/* Advance the owner-thread device epoch. Reads never advance this clock. */
int musashi_sio_controller_advance(void *, uint64_t absolute_device_cycle);

#endif
