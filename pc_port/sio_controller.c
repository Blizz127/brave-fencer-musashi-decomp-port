#include "musashi_sio_controller.h"
#include <string.h>

#define SIO_DATA 0x1f801040u
#define SIO_STATUS 0x1f801044u
#define SIO_MODE 0x1f801048u
#define SIO_CONTROL 0x1f80104au
#define SIO_BAUD 0x1f80104eu
#define STATUS_RX_READY 0x0002u
#define STATUS_TX_BITS 0x0005u
#define STATUS_PARITY_ERROR 0x0008u
#define STATUS_LOCAL_IRQ 0x0200u
#define CONTROL_TX_ENABLE 0x0001u
#define CONTROL_SELECT 0x0002u
#define CONTROL_ACK_IRQ_ENABLE 0x1000u
#define CONTROL_PORT2 0x2000u
#define CONTROL_RESET 0x0040u
#define CONTROL_RESET_ERROR 0x0010u
#define ACK_DELAY_CYCLES (0x88u * 8u)

typedef struct MusashiSioAnalogState {
    const MusashiSioController *sio;
    uint8_t axes[4]; /* 0: Right X, 1: Right Y, 2: Left X, 3: Left Y */
} MusashiSioAnalogState;

static MusashiSioAnalogState s_sio_analog_table[4];

void musashi_sio_controller_set_analog_axes(const MusashiSioController *sio,
                                            uint8_t rx, uint8_t ry,
                                            uint8_t lx, uint8_t ly) {
    int empty_slot = -1;
    for (int i = 0; i < 4; ++i) {
        if (s_sio_analog_table[i].sio == sio) {
            s_sio_analog_table[i].axes[0] = rx;
            s_sio_analog_table[i].axes[1] = ry;
            s_sio_analog_table[i].axes[2] = lx;
            s_sio_analog_table[i].axes[3] = ly;
            return;
        }
        if (empty_slot < 0 && s_sio_analog_table[i].sio == NULL) {
            empty_slot = i;
        }
    }
    if (empty_slot >= 0) {
        s_sio_analog_table[empty_slot].sio = sio;
        s_sio_analog_table[empty_slot].axes[0] = rx;
        s_sio_analog_table[empty_slot].axes[1] = ry;
        s_sio_analog_table[empty_slot].axes[2] = lx;
        s_sio_analog_table[empty_slot].axes[3] = ly;
    }
}

static uint8_t get_analog_axis(const MusashiSioController *sio, unsigned axis_index) {
    if (axis_index >= 4u) return 0x80u;
    for (int i = 0; i < 4; ++i) {
        if (s_sio_analog_table[i].sio == sio) {
            return s_sio_analog_table[i].axes[axis_index];
        }
    }
    return 0x80u;
}

static int owner_ok(const MusashiSioController *sio) {
    uint64_t token;
    if (!sio || sio->faulted) return 0;
    if (!sio->pad_bound) return 1;
    if (!sio->pad_device.owner_thread) return 0;
    token = sio->pad_device.owner_thread(sio->pad_device.userdata);
    return token != 0 && token == sio->owner_token;
}

static void set_pad_state(MusashiSioController *sio, uint8_t state) {
    sio->pad_state = state;
    sio->protocol_ignore = state == MUSASHI_SIO_PAD_BAD_COMMAND;
}

static void clear_fifo(MusashiSioController *sio) {
    sio->rx_count = 0;
    sio->rx_head = 0;
    sio->rx_data = 0;
    sio->status &= (uint16_t)~STATUS_RX_READY;
}

static void push_fifo(MusashiSioController *sio, uint8_t value) {
    unsigned index;
    if (sio->rx_count < 8u) {
        index = (sio->rx_head + sio->rx_count) & 7u;
        sio->rx_count++;
    } else {
        /* Selected source behavior: retain the first seven queued bytes and
         * replace only the newest eighth response. */
        index = (sio->rx_head + 7u) & 7u;
    }
    sio->rx_fifo[index] = value;
    sio->rx_data = value;
    sio->status |= STATUS_RX_READY;
}

static uint8_t pop_fifo(MusashiSioController *sio) {
    uint8_t value = 0xffu;
    if (sio->rx_count) {
        value = sio->rx_fifo[sio->rx_head];
        sio->rx_head = (sio->rx_head + 1u) & 7u;
        sio->rx_count--;
        sio->rx_data = value;
        if (!sio->rx_count) sio->status &= (uint16_t)~STATUS_RX_READY;
    }
    return value;
}

static void reset_protocol(MusashiSioController *sio) {
    sio->selected_device = MUSASHI_SIO_DEVICE_NONE;
    set_pad_state(sio, MUSASHI_SIO_PAD_IDLE);
    sio->pad_cursor = 0;
}

static void ignore_device(MusashiSioController *sio) {
    sio->selected_device = MUSASHI_SIO_DEVICE_IGNORE;
    sio->pad_state = MUSASHI_SIO_PAD_IDLE;
    sio->pad_cursor = 0;
    sio->protocol_ignore = 1;
}

static int control_supported(const MusashiSioController *sio, uint16_t value) {
    switch (value) {
    case 0:
    case 2:
    case 0x2002:
    case CONTROL_RESET:
        return 1;
    case 0x12:
    case 0x2012:
    case 0x1003:
    case 0x3003:
    case 0x1013:
    case 0x3013:
        return sio->disconnected_cards;
    default:
        return 0;
    }
}

static int schedule_ack_checked(MusashiSioController *sio,
                                uint64_t *new_due) {
    if (!(sio->control & CONTROL_TX_ENABLE) ||
        !(sio->control & CONTROL_ACK_IRQ_ENABLE)) {
        *new_due = 0;
        return 1;
    }
    if (sio->cycle > UINT64_MAX - ACK_DELAY_CYCLES) return 0;
    *new_due = sio->cycle + ACK_DELAY_CYCLES;
    return 1;
}

static void finish_transmit(MusashiSioController *sio) {
    sio->status &= (uint16_t)~STATUS_TX_BITS;
    sio->status |= STATUS_TX_BITS;
}

static int pad_state_consistent(const MusashiSioController *sio) {
    if (sio->selected_device == MUSASHI_SIO_DEVICE_NONE)
        return sio->pad_state == MUSASHI_SIO_PAD_IDLE &&
               !sio->protocol_ignore && !sio->pad_cursor;
    if (sio->selected_device == MUSASHI_SIO_DEVICE_IGNORE)
        return sio->pad_state == MUSASHI_SIO_PAD_IDLE &&
               sio->protocol_ignore && !sio->pad_cursor;
    if (sio->selected_device != MUSASHI_SIO_DEVICE_PAD) return 0;
    switch (sio->pad_state) {
    case MUSASHI_SIO_PAD_IDLE:
    case MUSASHI_SIO_PAD_COMMAND:
        return !sio->protocol_ignore && sio->pad_cursor == 0;
    case MUSASHI_SIO_PAD_DATA:
        return !sio->protocol_ignore && sio->pad_cursor <= 8;
    case MUSASHI_SIO_PAD_BAD_COMMAND:
        return sio->protocol_ignore && !sio->pad_cursor;
    default:
        return 0;
    }
}

/* Selected PCSX-Redux reference: src/core/sio.cc reset, writeMode16,
 * writeCtrl16, writeBaud16 and transmitData's disconnected MemoryCard branch.
 * Reset retains control and mode/baud. The optional absence profile accepts
 * ACK-IRQ enable, but disconnected hardware never generates an ACK. Other
 * IRQ modes and subsequent protocol commands remain unsupported. */
void musashi_sio_controller_init(MusashiSioController *sio) {
    if (!sio) return;
    memset(sio, 0, sizeof(*sio));
    sio->selected_port = -1;
    sio->status = 5; /* TX data empty and TX finished; empty receive FIFO. */
    sio->pad_state = MUSASHI_SIO_PAD_IDLE;
    for (int i = 0; i < 4; ++i) {
        if (s_sio_analog_table[i].sio == sio) {
            s_sio_analog_table[i].sio = NULL;
            s_sio_analog_table[i].axes[0] = 0x80u;
            s_sio_analog_table[i].axes[1] = 0x80u;
            s_sio_analog_table[i].axes[2] = 0x80u;
            s_sio_analog_table[i].axes[3] = 0x80u;
        }
    }
}

void musashi_sio_controller_init_disconnected(MusashiSioController *sio) {
    musashi_sio_controller_init(sio);
    if (sio) sio->disconnected_cards = 1;
}

int musashi_sio_controller_read16(void *userdata, uint32_t address,
                                   uint16_t *value) {
    MusashiSioController *sio = userdata;
    if (!sio || !value || !owner_ok(sio)) return 0;
    switch (address) {
    case SIO_STATUS:
        if (!sio->disconnected_cards) return 0;
        *value = sio->status; return 1;
    case SIO_MODE: *value = sio->mode; return 1;
    case SIO_CONTROL: *value = sio->control; return 1;
    case SIO_BAUD: *value = sio->baud; return 1;
    default: return 0;
    }
}

int musashi_sio_controller_write16(void *userdata, uint32_t address,
                                    uint16_t value) {
    MusashiSioController *sio = userdata;
    uint16_t old_control;
    if (!sio || !owner_ok(sio)) return 0;
    switch (address) {
    case SIO_MODE: sio->mode = value; return 1;
    case SIO_BAUD: sio->baud = value; return 1;
    case SIO_CONTROL:
        if (!control_supported(sio, value) ||
            (value & (0x0400u | 0x0800u))) return 0;
        old_control = sio->control;
        /* Changing away from an active connected pad midway through its
         * response has no owned host profile here. Do not fake a reset. */
        if (sio->pad_bound && sio->selected_device == MUSASHI_SIO_DEVICE_PAD &&
            !(old_control & CONTROL_PORT2) && (value & CONTROL_PORT2) &&
            (value & CONTROL_SELECT) && !(value & CONTROL_RESET)) return 0;
        /* Reference deselection and port1 -> port0 reset protocol state.
         * Port0 -> port1 alone does not. Neither transition clears RX FIFO. */
        if (((old_control & CONTROL_SELECT) && !(value & CONTROL_SELECT)) ||
            ((old_control & CONTROL_PORT2) && !(value & CONTROL_PORT2)))
            reset_protocol(sio);
        sio->control = value;
        if (value & CONTROL_RESET_ERROR) {
            sio->status &= (uint16_t)~(STATUS_LOCAL_IRQ | STATUS_PARITY_ERROR);
            sio->control &= (uint16_t)~CONTROL_RESET_ERROR;
        }
        if (value & CONTROL_RESET) {
            clear_fifo(sio);
            sio->ack_pending = 0;
            reset_protocol(sio);
            sio->status = 5;
        }
        sio->selected_port = (value & CONTROL_SELECT) ?
                             (int)((value >> 13) & 1u) : -1;
        return 1;
    default: return 0;
    }
}

int musashi_sio_controller_read8(void *userdata, uint32_t address,
                                  uint8_t *value) {
    MusashiSioController *sio = userdata;
    if (!sio || !value || !owner_ok(sio) || !sio->disconnected_cards ||
        address != SIO_DATA)
        return 0;
    *value = pop_fifo(sio); /* Selected reference's empty DATA read is FF. */
    return 1;
}

int musashi_sio_controller_write8(void *userdata, uint32_t address,
                                   uint8_t value) {
    MusashiSioController *sio = userdata;
    uint8_t response = 0xffu;
    uint64_t new_due = 0;
    int schedule = 0;
    uint16_t sampled = 0;
    if (!sio || !owner_ok(sio)) return 0;
    if (!sio->disconnected_cards || address != SIO_DATA ||
        sio->selected_port < 0 || !(sio->control & CONTROL_TX_ENABLE) ||
        sio->mode != 0x0du || sio->baud != 0x88u ||
        sio->transmitted_bytes == UINT64_MAX) return 0;
    if (!sio->pad_bound) {
        /* Retain the explicitly narrower absence-only API. */
        if (value != 0x81u || sio->protocol_ignore || sio->rx_count) return 0;
        push_fifo(sio, 0xffu);
        ignore_device(sio);
        ++sio->transmitted_bytes;
        finish_transmit(sio);
        return 1;
    }
    if (!pad_state_consistent(sio)) return 0;
    if (sio->selected_device == MUSASHI_SIO_DEVICE_NONE && value == 0x81u) {
        /* Peripheral identity is selected only at the transaction start,
         * independently of unread FIFO data. Both cards are absent. */
        ignore_device(sio);
    } else if (sio->selected_device == MUSASHI_SIO_DEVICE_IGNORE) {
        /* Every subsequent payload byte still belongs to the absent device. */
    } else if (sio->selected_port == 1 &&
               (sio->selected_device == MUSASHI_SIO_DEVICE_PAD || value == 0x01u)) {
        /* An absent pad retains PAD identity and IDLE state for every byte. */
        sio->selected_device = MUSASHI_SIO_DEVICE_PAD;
    } else if (sio->selected_device == MUSASHI_SIO_DEVICE_NONE && value == 0x01u) {
        schedule = (sio->control & CONTROL_ACK_IRQ_ENABLE) != 0;
        if (schedule && !schedule_ack_checked(sio, &new_due)) {
            sio->faulted = 1;
            return 0;
        }
        if (!sio->pad_device.sample_buttons ||
            !sio->pad_device.sample_buttons(sio->pad_device.userdata, &sampled)) {
            sio->faulted = 1;
            return 0;
        }
        sampled |= 0x0006u; /* Digital L3/R3 remain high. */
        sio->sampled_buttons = sampled;
        sio->selected_device = MUSASHI_SIO_DEVICE_PAD;
        set_pad_state(sio, MUSASHI_SIO_PAD_COMMAND);
        sio->pad_cursor = 0;
        response = 0xffu;
    } else if (sio->pad_state == MUSASHI_SIO_PAD_COMMAND) {
        if (value == 0x42u) {
            schedule = (sio->control & CONTROL_ACK_IRQ_ENABLE) != 0;
            if (schedule && !schedule_ack_checked(sio, &new_due)) {
                sio->faulted = 1;
                return 0;
            }
            set_pad_state(sio, MUSASHI_SIO_PAD_DATA);
            sio->pad_cursor = 0;
            if (sio->analog_mode > 0 && sio->analog_mode < 3) {
                /* Config ID until TAP locks +0x46=0xFF / +0x49=2, then analog. */
                response = 0xf3u;
                sio->analog_mode++;
            } else if (sio->analog_mode >= 3)
                response = 0x73u;
            else
                response = 0x41u;
        } else if (value == 0x43u ||
                   (sio->analog_mode > 0 && (value & 0xf0u) == 0x40u)) {
            /* DualShock config (0x43 enter, 0x44 analog set, 0x4D motors).
             * ACK with 0xF3 so TAP can record analog before 0x42 returns 0x73. */
            schedule = (sio->control & CONTROL_ACK_IRQ_ENABLE) != 0;
            if (schedule && !schedule_ack_checked(sio, &new_due)) {
                sio->faulted = 1;
                return 0;
            }
            if (sio->analog_mode < 1) sio->analog_mode = 1;
            set_pad_state(sio, MUSASHI_SIO_PAD_DATA);
            sio->pad_cursor = 0;
            response = 0xf3u;
        } else {
            set_pad_state(sio, MUSASHI_SIO_PAD_BAD_COMMAND);
        }
    } else if (sio->pad_state == MUSASHI_SIO_PAD_DATA) {
        if (sio->pad_cursor == 0) {
            schedule = (sio->control & CONTROL_ACK_IRQ_ENABLE) != 0;
            if (schedule && !schedule_ack_checked(sio, &new_due)) {
                sio->faulted = 1;
                return 0;
            }
            response = 0x5au;
            sio->pad_cursor = 1;
        } else if (sio->pad_cursor == 1) {
            schedule = (sio->control & CONTROL_ACK_IRQ_ENABLE) != 0;
            if (schedule && !schedule_ack_checked(sio, &new_due)) {
                sio->faulted = 1;
                return 0;
            }
            response = (uint8_t)sio->sampled_buttons;
            sio->pad_cursor = 2;
        } else if (sio->analog_mode > 0 && sio->pad_cursor < 8) {
            /* Config/analog frames are 9 bytes; ACK payload until the last. */
            schedule = (sio->control & CONTROL_ACK_IRQ_ENABLE) != 0;
            if (schedule && !schedule_ack_checked(sio, &new_due)) {
                sio->faulted = 1;
                return 0;
            }
            if (sio->pad_cursor == 2)
                response = (uint8_t)(sio->sampled_buttons >> 8);
            else if (sio->analog_mode >= 3 && sio->pad_cursor >= 3 &&
                     sio->pad_cursor <= 6)
                response = get_analog_axis(sio, sio->pad_cursor - 3u);
            else
                response = 0;
            sio->pad_cursor++;
        } else {
            response = (uint8_t)(sio->sampled_buttons >> 8);
            ignore_device(sio);
        }
    }
    /* Ignore and BAD_COMMAND return FF with no new acknowledgement. An
     * invalid first byte is also a synchronous FF without entering a frame. */
    sio->status &= (uint16_t)~STATUS_TX_BITS;
    push_fifo(sio, response);
    sio->transmitted_bytes++;
    finish_transmit(sio);
    if (schedule) {
        sio->ack_pending = 1;
        sio->ack_due = new_due;
    }
    return 1;
}

int musashi_sio_controller_bind_digital_pad(MusashiSioController *sio,
                                             const MusashiSioPadDevice *device) {
    uint64_t token;
    if (!sio || !device || !device->owner_thread ||
        !device->sample_buttons || !device->raise_irq || sio->pad_bound ||
        !sio->disconnected_cards || sio->mode || sio->control || sio->baud ||
        sio->selected_port != -1 || sio->rx_count || sio->ack_pending ||
        sio->rx_head || sio->rx_data || sio->transmitted_bytes ||
        sio->status != 5 || sio->protocol_ignore ||
        sio->pad_state != MUSASHI_SIO_PAD_IDLE || sio->pad_cursor ||
        sio->cycle || sio->cycle_initialized || sio->ack_due ||
        sio->sampled_buttons || sio->owner_token || sio->faulted ||
        sio->selected_device != MUSASHI_SIO_DEVICE_NONE ||
        sio->analog_mode) return 0;
    token = device->owner_thread(device->userdata);
    if (!token) return 0;
    sio->pad_device = *device;
    sio->owner_token = token;
    sio->pad_bound = 1;
    sio->cycle = 0;
    sio->cycle_initialized = 1;
    sio->sampled_buttons = 0;
    sio->pad_cursor = 0;
    return 1;
}

int musashi_sio_controller_advance(void *userdata,
                                   uint64_t absolute_device_cycle) {
    MusashiSioController *sio = userdata;
    if (!sio || !sio->pad_bound || !owner_ok(sio) || sio->faulted ||
        !sio->cycle_initialized)
        return 0;
    if (absolute_device_cycle < sio->cycle) {
        sio->faulted = 1;
        return 0;
    }
    sio->cycle = absolute_device_cycle;
    if (sio->ack_pending && sio->cycle >= sio->ack_due) {
        sio->ack_pending = 0;
        sio->status |= STATUS_LOCAL_IRQ;
        if (!sio->pad_device.raise_irq ||
            !sio->pad_device.raise_irq(sio->pad_device.userdata, 0x0080u)) {
            sio->faulted = 1;
            return 0;
        }
    }
    return 1;
}
