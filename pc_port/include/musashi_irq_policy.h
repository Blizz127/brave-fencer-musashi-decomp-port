#ifndef MUSASHI_IRQ_POLICY_H
#define MUSASHI_IRQ_POLICY_H
#include <stdint.h>

/* Native counterparts of B0:5B/C0:0A policy storage. The legacy change
 * callbacks report acceptance for registration. B0:5B's checked exchange
 * separately returns the prior full 32-bit pad word and refuses until a guest
 * or native owner has explicitly established that word. */
typedef struct MusashiIrqPolicy {
    int32_t pad;
    int32_t timers[4];
    int pad_initialized;
    int timer_initialized[4];
} MusashiIrqPolicy;
void musashi_irq_policy_init(MusashiIrqPolicy *);
int musashi_irq_policy_change_pad(void *, int32_t value);
int musashi_irq_policy_exchange_pad(void *, int32_t value, int32_t *old_value);
int musashi_irq_policy_change_timer(void *, int32_t channel, int32_t value);
int musashi_irq_policy_exchange_timer(void *, int32_t channel, int32_t value, int32_t *old_value);
int musashi_irq_policy_custom_vblank(const MusashiIrqPolicy *);
#endif
