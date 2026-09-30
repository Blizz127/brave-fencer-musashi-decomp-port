#include "musashi_irq_policy.h"

/* https://psx-spx.consoledev.net/kernelbios/ (B0:5B and C0:0A).
 * Initial -1 means native ownership is not established; it is not a claimed
 * BIOS power-on value. Only explicit guest writes select the custom route. */
void musashi_irq_policy_init(MusashiIrqPolicy *policy) {
    unsigned i;
    if (!policy) return;
    policy->pad = -1;
    policy->pad_initialized = 0;
    for (i = 0; i < 4; ++i) {
        policy->timers[i] = -1;
        policy->timer_initialized[i] = 0;
    }
}

int musashi_irq_policy_change_pad(void *userdata, int32_t value) {
    MusashiIrqPolicy *policy = userdata;
    if (!policy) return 0;
    policy->pad = value;
    policy->pad_initialized = 1;
    return 1;
}

int musashi_irq_policy_exchange_pad(void *userdata, int32_t value,
                                    int32_t *old_value) {
    MusashiIrqPolicy *policy = userdata;
    if (!policy || !old_value || !policy->pad_initialized) return 0;
    *old_value = policy->pad;
    policy->pad = value;
    return 1;
}

int musashi_irq_policy_change_timer(void *userdata, int32_t channel, int32_t value) {
    MusashiIrqPolicy *policy = userdata;
    if (!policy || channel < 0 || channel >= 4) return 0;
    policy->timers[channel] = value;
    policy->timer_initialized[channel] = 1;
    return 1;
}

int musashi_irq_policy_exchange_timer(void *userdata, int32_t channel,
                                      int32_t value, int32_t *old_value) {
    MusashiIrqPolicy *policy = userdata;
    if (!policy || !old_value || channel < 0 || channel >= 4 ||
        !policy->timer_initialized[channel]) return 0;
    *old_value = policy->timers[channel];
    policy->timers[channel] = value;
    return 1;
}

int musashi_irq_policy_custom_vblank(const MusashiIrqPolicy *policy) {
    return policy && policy->pad_initialized && policy->pad == 0 &&
           policy->timers[3] == 0;
}
