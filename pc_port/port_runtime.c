#include "musashi_port.h"

#include "platform.h"

const char *musashi_port_platform_name(void) {
    return musashi_platform_name();
}

void musashi_port_init(MusashiRuntimeState *state) {
    musashi_runtime_reset(state);
}

int musashi_port_decode_probe(const mus_u8 bytes[4], mus_u32 *value_out) {
    if (bytes == 0 || value_out == 0) {
        return 0;
    }
    *value_out = musashi_read_le32(bytes);
    return 1;
}
