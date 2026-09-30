#ifndef MUSASHI_IRQ_CONTROLLER_H
#define MUSASHI_IRQ_CONTROLLER_H

#include <stdint.h>

/* Native storage for the PS1 interrupt controller registers consumed by the
 * recovered dispatcher. Event producers raise status bits; guest code owns
 * masking and acknowledgement through the checked register callbacks. */
typedef struct MusashiIrqController {
    uint16_t status;
    uint16_t mask;
} MusashiIrqController;

void musashi_irq_controller_init(MusashiIrqController *controller);
int musashi_irq_controller_read16(void *userdata, uint32_t address,
                                  uint16_t *value);
int musashi_irq_controller_write16(void *userdata, uint32_t address,
                                   uint16_t value);
uint16_t musashi_irq_controller_pending(const MusashiIrqController *controller);
int musashi_irq_controller_raise_vblank(void *userdata);

#endif
