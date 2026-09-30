#include "musashi_vblank.h"

int musashi_boot_execute_vblank_callback(void *userdata,
    MusashiBootMemory *memory, uint32_t target) {
    if (target != 0x800616d0u) return 0;
    return musashi_boot_call_800616d0(memory, userdata);
}
