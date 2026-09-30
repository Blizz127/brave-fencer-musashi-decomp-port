#include "musashi_boot_memory.h"

typedef struct MusashiEventRegistration {
    uint32_t destination;
    uint32_t class_word;
    uint32_t spec;
    uint32_t callback;
} MusashiEventRegistration;

int musashi_boot_call_8006189c(MusashiBootMemory *memory,
                               const MusashiEventDevice *device,
                               int32_t *result) {
    static const MusashiEventRegistration registrations[] = {
        {0x80078c3cu, 0xf4000001u, 0x00000004u, 0x800617ccu},
        {0x80078c40u, 0xf4000001u, 0x00008000u, 0x800617e0u},
        {0x80078c44u, 0xf4000001u, 0x00000100u, 0x800617f4u},
        {0x80078c48u, 0xf4000001u, 0x00002000u, 0x80061808u},
        {0x80078c4cu, 0xf0000011u, 0x00000004u, 0x8006181cu},
        {0x80078c50u, 0xf0000011u, 0x00008000u, 0x80061830u},
        {0x80078c54u, 0xf0000011u, 0x00000100u, 0x80061844u},
        {0x80078c58u, 0xf0000011u, 0x00002000u, 0x80061858u},
    };
    int32_t entered, service_result, handle;
    uint32_t handle_bits;
    unsigned index;

    if (!memory || !device || !result || !device->enter_critical_section ||
        !device->open_event || !device->enable_event || !device->b0_0b ||
        !device->exit_critical_section ||
        !musashi_boot_ram_span(memory, 0x80078c3cu, 0x20u) ||
        !musashi_boot_ram_span(memory, 0x80078c5cu, 0x20u))
        return 0;

    if (!device->enter_critical_section(device->userdata, &entered))
        return 0;
    for (index = 0; index < sizeof(registrations) / sizeof(registrations[0]); ++index) {
        const MusashiEventRegistration *registration = &registrations[index];
        if (!device->open_event(device->userdata, registration->class_word,
                                registration->spec, 0x1000u,
                                registration->callback, &handle) ||
            !musashi_boot_write32(memory, registration->destination, (uint32_t)handle))
            return 0;
    }
    for (index = 0; index < sizeof(registrations) / sizeof(registrations[0]); ++index) {
        if (!musashi_boot_read32(memory, registrations[index].destination,
                                 &handle_bits))
            return 0;
        handle = (int32_t)handle_bits;
        if (
            !device->enable_event(device->userdata, handle, &service_result))
            return 0;
    }
    for (index = 0; index < sizeof(registrations) / sizeof(registrations[0]); ++index) {
        if (!musashi_boot_read32(memory, registrations[index].destination,
                                 &handle_bits))
            return 0;
        handle = (int32_t)handle_bits;
        if (
            !device->b0_0b(device->userdata, handle, &service_result))
            return 0;
    }
    if (!musashi_boot_write32(memory, 0x80078c68u, 0) ||
        !musashi_boot_write32(memory, 0x80078c64u, 0) ||
        !musashi_boot_write32(memory, 0x80078c60u, 0) ||
        !musashi_boot_write32(memory, 0x80078c5cu, 0) ||
        !musashi_boot_write32(memory, 0x80078c78u, 0) ||
        !musashi_boot_write32(memory, 0x80078c74u, 0) ||
        !musashi_boot_write32(memory, 0x80078c70u, 0) ||
        !musashi_boot_write32(memory, 0x80078c6cu, 0))
        return 0;
    if (entered == 1 && !device->exit_critical_section(device->userdata, &service_result))
        return 0;
    *result = 0;
    return 1;
}
