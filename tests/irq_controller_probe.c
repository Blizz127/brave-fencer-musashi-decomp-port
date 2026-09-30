#include "musashi_irq_controller.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    MusashiIrqController controller;
    uint16_t value = 0xffffu;

    musashi_irq_controller_init(&controller);
    assert(musashi_irq_controller_read16(&controller, 0x1f801070u, &value));
    assert(value == 0);
    assert(musashi_irq_controller_read16(&controller, 0x1f801074u, &value));
    assert(value == 0);

    assert(musashi_irq_controller_write16(&controller, 0x1f801074u, 1u));
    assert(musashi_irq_controller_raise_vblank(&controller));
    assert(musashi_irq_controller_pending(&controller) == 1u);
    assert(musashi_irq_controller_read16(&controller, 0x1f801070u, &value));
    assert(value == 1u);

    /* Retail acknowledgement writes a mask with the serviced bit clear. */
    assert(musashi_irq_controller_write16(&controller, 0x1f801070u, 0xfffeu));
    assert(musashi_irq_controller_pending(&controller) == 0);
    assert(musashi_irq_controller_read16(&controller, 0x1f801070u, &value));
    assert(value == 0);

    assert(!musashi_irq_controller_read16(&controller, 0x1f801072u, &value));
    assert(!musashi_irq_controller_write16(&controller, 0x1f801072u, 1u));
    assert(!musashi_irq_controller_read16(NULL, 0x1f801070u, &value));
    assert(!musashi_irq_controller_raise_vblank(NULL));
    puts("IRQ CONTROLLER: I_STAT/I_MASK VBlank raise and acknowledgement pass");
    return 0;
}
