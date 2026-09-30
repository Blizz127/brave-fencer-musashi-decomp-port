/* libetc interrupt service (guest 800427F4), port-owned HLE.
 *
 * Behaviour (PS1 interrupt controller, psx-spx; libetc service tables):
 * I_STAT and I_MASK are reached through guest pointer cells 8006CB88 and
 * 8006CB8C; 8006BB2C holds the lines the game services, 8006BB00 one
 * callback word per line, 8006BAFC the "service installed" guard, 8006BAFE
 * the "in service" flag and 8006CB94 an IRQ-storm counter.
 *
 * Not installed: report I_STAT through the guest diagnostic (format
 * 80073558) and return from the exception. Otherwise, with the in-service
 * flag set, repeatedly take pending = I_STAT & I_MASK & serviced lines; for
 * each pending line 0-10, lowest first, acknowledge it alone in I_STAT
 * (write ~bit, I_STAT is write-zero-to-clear) and run its callback, if any.
 * When nothing is pending, a line still raised in I_STAT & I_MASK (one the
 * game does not service) counts toward the storm counter; after 0x800
 * consecutive such exits the service reports I_STAT/I_MASK (format
 * 80073574), clears the counter and acknowledges everything. A clean exit
 * clears the counter. The in-service flag is cleared and the exception
 * returns through the platform (B0:17).
 *
 * Device/RAM access order matches the retail service, which the IRQ
 * probes observe; pointer cells are re-read where the service re-reads them. */
#include "musashi_irq_dispatch.h"

enum {
    I_STAT_POINTER = 0x8006cb88u,
    I_MASK_POINTER = 0x8006cb8cu,
    SERVICED_LINES = 0x8006bb2cu,
    LINE_CALLBACKS = 0x8006bb00u,
    SERVICE_INSTALLED = 0x8006bafcu,
    IN_SERVICE = 0x8006bafeu,
    STORM_COUNTER = 0x8006cb94u,
    FORMAT_NOT_INSTALLED = 0x80073558u,
    FORMAT_STORM = 0x80073574u,
    STORM_LIMIT = 0x801u,
};
enum { LINES = 11 };

typedef struct Service {
    MusashiBootMemory *memory;
    const MusashiIrqDispatchDevice *device;
} Service;

static int word(const Service *s, uint32_t address, uint32_t *value) {
    return musashi_boot_read32(s->memory, address, value);
}

static int device_read(const Service *s, uint32_t address, uint32_t *value) {
    uint16_t half;
    if (!s->device || !s->device->read16 || (address & 1u) ||
        !s->device->read16(s->device->userdata, address, &half))
        return 0;
    *value = half;
    return 1;
}

static int device_write(const Service *s, uint32_t address, uint32_t value) {
    return s->device && s->device->write16 && !(address & 1u) &&
        s->device->write16(s->device->userdata, address, (uint16_t)value);
}

static int diagnostic(const Service *s, uint32_t format, uint32_t a, uint32_t b, uint32_t n) {
    return s->device && s->device->diagnostic &&
        s->device->diagnostic(s->device->userdata, s->memory, format, a, b, n);
}

static int return_from_exception(const Service *s) {
    if (s->device && s->device->return_from_exception) {
        if (!s->device->return_from_exception(s->device->userdata)) return 0;
        return MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED;
    }
    return MUSASHI_IRQ_CONTEXT_RETURN_PENDING;
}

/* pending = I_STAT & I_MASK & serviced lines, read in retail order. */
static int sample(const Service *s, uint32_t *pending) {
    uint32_t stat_address, mask_address, stat, mask;
    uint16_t serviced;
    if (!word(s, I_STAT_POINTER, &stat_address) ||
        !musashi_boot_read16(s->memory, SERVICED_LINES, &serviced))
        return 0;
    if (!word(s, I_MASK_POINTER, &mask_address) ||
        !device_read(s, stat_address, &stat) || !device_read(s, mask_address, &mask))
        return 0;
    *pending = mask & (serviced & stat);
    return 1;
}

int musashi_boot_call_800427f4(MusashiBootMemory *memory,
    const MusashiIrqDispatchDevice *device) {
    Service s;
    uint16_t installed;
    uint32_t pending, stat_address, mask_address, stat, mask, storms;
    s.memory = memory;
    s.device = device;
    if (!memory || !musashi_boot_read16(memory, SERVICE_INSTALLED, &installed)) return 0;
    if (!installed) {
        if (!word(&s, I_STAT_POINTER, &stat_address) || !device_read(&s, stat_address, &stat) ||
            !diagnostic(&s, FORMAT_NOT_INSTALLED, stat, 0, 1))
            return 0;
        return return_from_exception(&s);
    }
    {
        /* First sample sets the in-service flag between the serviced-lines
         * read and the I_MASK pointer read, as the retail service does. */
        uint16_t serviced;
        if (!word(&s, I_STAT_POINTER, &stat_address) ||
            !musashi_boot_read16(memory, SERVICED_LINES, &serviced) ||
            !musashi_boot_write16(memory, IN_SERVICE, 1) ||
            !word(&s, I_MASK_POINTER, &mask_address) ||
            !device_read(&s, stat_address, &stat) || !device_read(&s, mask_address, &mask))
            return 0;
        pending = mask & (serviced & stat);
    }
    while (pending) {
        unsigned line;
        for (line = 0; line < LINES && (pending & 0xffffu); ++line, pending >>= 1) {
            uint32_t callback;
            if (!(pending & 1u)) continue;
            if (!word(&s, I_STAT_POINTER, &stat_address) ||
                !device_write(&s, stat_address, ~(1u << line)) ||
                !word(&s, LINE_CALLBACKS + 4u * line, &callback))
                return 0;
            if (callback && (!device || !device->execute ||
                             !device->execute(device->userdata, memory, callback)))
                return 0;
        }
        if (!sample(&s, &pending)) return 0;
    }
    if (!word(&s, I_STAT_POINTER, &stat_address) || !word(&s, I_MASK_POINTER, &mask_address) ||
        !device_read(&s, stat_address, &stat) || !device_read(&s, mask_address, &mask))
        return 0;
    if (stat & mask) {
        if (!word(&s, STORM_COUNTER, &storms) ||
            !musashi_boot_write32(memory, STORM_COUNTER, storms + 1u))
            return 0;
        if (storms >= STORM_LIMIT && storms < 0x80000000u) {
            if (!device_read(&s, stat_address, &stat) || !device_read(&s, mask_address, &mask) ||
                !diagnostic(&s, FORMAT_STORM, stat, mask, 2) ||
                !word(&s, I_STAT_POINTER, &stat_address) ||
                !musashi_boot_write32(memory, STORM_COUNTER, 0) ||
                !device_write(&s, stat_address, 0))
                return 0;
        }
    } else if (!musashi_boot_write32(memory, STORM_COUNTER, 0)) {
        return 0;
    }
    if (!musashi_boot_write16(memory, IN_SERVICE, 0)) return 0;
    return return_from_exception(&s);
}
