#include "runtime_api.h"

void musashi_runtime_reset(MusashiRuntimeState *state) {
    if (state == 0) {
        return;
    }
    state->frame_counter = 0;
    state->pad_state = 0;
}

mus_u32 musashi_read_le32(const mus_u8 bytes[4]) {
    return ((mus_u32)bytes[0]) |
           ((mus_u32)bytes[1] << 8) |
           ((mus_u32)bytes[2] << 16) |
           ((mus_u32)bytes[3] << 24);
}
