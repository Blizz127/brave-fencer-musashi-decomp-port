#include "musashi_bios_events.h"

#include <string.h>

#define MUSASHI_BIOS_EVENT_STATUS_DISABLED 0x1000u
#define MUSASHI_BIOS_EVENT_STATUS_ENABLED 0x2000u
#define MUSASHI_BIOS_EVENT_STATUS_READY 0x4000u
#define MUSASHI_BIOS_EVENT_MODE_CALLBACK 0x1000u
#define MUSASHI_BIOS_EVENT_MODE_POLL 0x2000u

static MusashiBiosEventRecord *record_for_handle(MusashiBiosEvents *events,
                                                   int32_t handle) {
    uint32_t index;

    if (!events) return 0;
    index = (uint32_t)handle & 0xffffu;
    if (index >= MUSASHI_BIOS_EVENTS_CAPACITY) return 0;
    return &events->records[index];
}

void musashi_bios_events_init(MusashiBiosEvents *events) {
    if (events) memset(events, 0, sizeof(*events));
}

int musashi_bios_events_open(MusashiBiosEvents *events, uint32_t class_word,
                             uint32_t spec, uint32_t mode, uint32_t callback,
                             int32_t *handle) {
    uint32_t index;

    if (!events || !handle) return 0;
    for (index = 0; index < MUSASHI_BIOS_EVENTS_CAPACITY; ++index) {
        MusashiBiosEventRecord *record = &events->records[index];
        uint32_t handle_bits;
        if (record->status != 0) continue;
        if (events->allocation_serial[index] == UINT64_MAX) return 0;
        events->allocation_serial[index]++;
        record->class_word = class_word;
        record->status = MUSASHI_BIOS_EVENT_STATUS_DISABLED;
        record->spec = spec;
        record->mode = mode;
        record->callback = callback;
        handle_bits = 0xf1000000u | index;
        memcpy(handle, &handle_bits, sizeof(handle_bits));
        return 1;
    }
    *handle = -1;
    return 1;
}

int musashi_bios_events_close(MusashiBiosEvents *events, int32_t handle,
                              int32_t *result) {
    MusashiBiosEventRecord *record;

    if (!result) return 0;
    record = record_for_handle(events, handle);
    if (!record) return 0;
    record->status = 0;
    *result = 1;
    return 1;
}

int musashi_bios_events_enable(MusashiBiosEvents *events, int32_t handle,
                               int32_t *result) {
    MusashiBiosEventRecord *record;

    if (!result) return 0;
    record = record_for_handle(events, handle);
    if (!record) return 0;
    if (record->status != 0) record->status = MUSASHI_BIOS_EVENT_STATUS_ENABLED;
    *result = 1;
    return 1;
}

int musashi_bios_events_disable(MusashiBiosEvents *events, int32_t handle,
                                int32_t *result) {
    MusashiBiosEventRecord *record;

    if (!result) return 0;
    record = record_for_handle(events, handle);
    if (!record) return 0;
    if (record->status != 0) record->status = MUSASHI_BIOS_EVENT_STATUS_DISABLED;
    *result = 1;
    return 1;
}

int musashi_bios_events_test(MusashiBiosEvents *events, int32_t handle,
                             int32_t *result) {
    MusashiBiosEventRecord *record;

    if (!result) return 0;
    record = record_for_handle(events, handle);
    if (!record) return 0;
    if (record->status == MUSASHI_BIOS_EVENT_STATUS_READY) {
        record->status = MUSASHI_BIOS_EVENT_STATUS_ENABLED;
        *result = 1;
    } else {
        *result = 0;
    }
    return 1;
}

int musashi_bios_events_test_sync_spu(MusashiBiosEvents *events, int32_t handle,
                                     int32_t *result) {
    MusashiBiosEventRecord *record;

    if (!musashi_bios_events_test(events, handle, result)) return 0;
    if (*result) return 1;
    record = record_for_handle(events, handle);
    if (!record) return 0;
    if (record->class_word != 0xf0000009u || record->spec != 0x20u ||
        record->mode != MUSASHI_BIOS_EVENT_MODE_POLL ||
        record->status != MUSASHI_BIOS_EVENT_STATUS_ENABLED)
        return 1;
    record->status = MUSASHI_BIOS_EVENT_STATUS_READY;
    return musashi_bios_events_test(events, handle, result);
}

int musashi_bios_events_deliver(MusashiBiosEvents *events, uint32_t class_word,
                                uint32_t spec,
                                MusashiBiosEventExecutor execute,
                                void *userdata) {
    uint32_t index;

    if (!events) return 0;
    for (index = 0; index < MUSASHI_BIOS_EVENTS_CAPACITY; ++index) {
        MusashiBiosEventRecord *record = &events->records[index];
        if (record->status != MUSASHI_BIOS_EVENT_STATUS_ENABLED ||
            record->class_word != class_word || record->spec != spec)
            continue;
        if (record->mode == MUSASHI_BIOS_EVENT_MODE_POLL) {
            record->status = MUSASHI_BIOS_EVENT_STATUS_READY;
        } else if (record->mode == MUSASHI_BIOS_EVENT_MODE_CALLBACK &&
                   record->callback != 0) {
            if (!execute || !execute(userdata, record->callback)) return 0;
        }
    }
    return 1;
}

int musashi_bios_events_undeliver(MusashiBiosEvents *events,
                                  uint32_t class_word, uint32_t spec) {
    uint32_t index;

    if (!events) return 0;
    for (index = 0; index < MUSASHI_BIOS_EVENTS_CAPACITY; ++index) {
        MusashiBiosEventRecord *record = &events->records[index];
        if (record->status == MUSASHI_BIOS_EVENT_STATUS_READY &&
            record->mode == MUSASHI_BIOS_EVENT_MODE_POLL &&
            record->class_word == class_word && record->spec == spec)
            record->status = MUSASHI_BIOS_EVENT_STATUS_ENABLED;
    }
    return 1;
}
