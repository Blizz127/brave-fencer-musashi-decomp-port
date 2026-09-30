#ifndef MUSASHI_RUNTIME_API_H
#define MUSASHI_RUNTIME_API_H

#include "types.h"

/* Small platform-neutral surface for decompiled code and future ports. */
typedef struct MusashiRuntimeState {
    mus_u32 frame_counter;
    mus_u32 pad_state;
} MusashiRuntimeState;

void musashi_runtime_reset(MusashiRuntimeState *state);
mus_u32 musashi_read_le32(const mus_u8 bytes[4]);

#endif
