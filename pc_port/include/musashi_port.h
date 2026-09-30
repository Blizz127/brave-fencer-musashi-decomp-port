#ifndef MUSASHI_PORT_H
#define MUSASHI_PORT_H

#include "runtime_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Narrow adapter surface used by the native smoke target. */
const char *musashi_port_platform_name(void);
void musashi_port_init(MusashiRuntimeState *state);
int musashi_port_decode_probe(const mus_u8 bytes[4], mus_u32 *value_out);

#ifdef __cplusplus
}
#endif

#endif
