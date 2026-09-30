#include "musashi_irq_controller.h"

#define MUSASHI_I_STAT 0x1f801070u
#define MUSASHI_I_MASK 0x1f801074u
#define MUSASHI_IRQ_VBLANK 0x0001u

void musashi_irq_controller_init(MusashiIrqController *controller) {
    if (!controller)
        return;
    controller->status = 0;
    controller->mask = 0;
}

int musashi_irq_controller_read16(void *userdata, uint32_t address,
                                  uint16_t *value) {
    MusashiIrqController *controller = userdata;
    if (!controller || !value)
        return 0;
    if (address == MUSASHI_I_STAT)
        *value = controller->status;
    else if (address == MUSASHI_I_MASK)
        *value = controller->mask;
    else
        return 0;
    return 1;
}

int musashi_irq_controller_write16(void *userdata, uint32_t address,
                                   uint16_t value) {
    MusashiIrqController *controller = userdata;
    if (!controller)
        return 0;
    if (address == MUSASHI_I_STAT)
        controller->status &= value;
    else if (address == MUSASHI_I_MASK)
        controller->mask = value;
    else
        return 0;
    return 1;
}

uint16_t musashi_irq_controller_pending(const MusashiIrqController *controller) {
    if (!controller)
        return 0;
    return controller->status & controller->mask;
}

int musashi_irq_controller_raise_vblank(void *userdata) {
    MusashiIrqController *controller = userdata;
    if (!controller)
        return 0;
    controller->status |= MUSASHI_IRQ_VBLANK;
    return 1;
}
