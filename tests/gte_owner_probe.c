#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "musashi_gte_owner.h"
#include "psx/gtereg.h"

struct MusashiCpuStatus { uint32_t sr; };

int musashi_cpu_status_read(MusashiCpuStatus *status, uint32_t *value) {
    if (status == NULL || value == NULL) return 0;
    *value = status->sr;
    return 1;
}

GTERegisters gteRegs;

int GTE_operator(int op) { (void)op; return 0; }
int LIM(int value, int max, int min, unsigned int flag) {
    (void)flag;
    return value > max ? max : (value < min ? min : value);
}
unsigned int gte_leadingzerocount(unsigned int value) {
    unsigned int count = 0;
    while (count < 32u && (value & 0x80000000u) == 0) {
        value <<= 1;
        ++count;
    }
    return count;
}

typedef struct ThreadState {
    uint64_t token;
    MusashiGteOwner *reentry_owner;
    MusashiGteOwner *constructor_owner;
    MusashiCpuStatus *status;
    MusashiCpuContext *context;
    const void *identity;
    int reenter_snapshot;
    int reenter_write;
    int reenter_close;
    int reenter_init;
    int callback_once;
} ThreadState;

static uint64_t thread_token(void *userdata) {
    ThreadState *state = (ThreadState *)userdata;
    if (!state->callback_once) {
        state->callback_once = 1;
        if (state->reenter_snapshot != 0) {
            MusashiGteSnapshot snapshot;
            assert(!musashi_gte_owner_snapshot(state->reentry_owner,
                                               &snapshot));
        }
        if (state->reenter_write != 0) {
            assert(!musashi_gte_owner_write_control(state->reentry_owner,
                                                    state->context, 21, 1));
        }
        if (state->reenter_close != 0)
            assert(!musashi_gte_owner_close(state->reentry_owner));
        if (state->reenter_init != 0) {
            assert(!musashi_gte_owner_init(state->constructor_owner,
                                           state->status, thread_token, state,
                                           state->identity));
        }
    }
    return state->token;
}

static void assert_bank(const uint32_t expected[32]) {
    unsigned i;
    for (i = 0; i < 32u; ++i) assert(gteRegs.CP2C.p[i].d == expected[i]);
}

int main(void) {
    MusashiCpuStatus status = { MUSASHI_CPU_STATUS_CU2 };
    MusashiGteOwner owner = {0}, other = {0}, candidate = {0};
    ThreadState state = {0};
    uint32_t before[32], expected[32], registers[32] = {0};
    MusashiCpuContext context = {0}, foreign_context = {0};
    MusashiGteSnapshot snapshot = {0}, preserved;
    const int identity = 7;
    unsigned i;

    state.token = 11;
    state.status = &status;
    state.context = &context;
    state.identity = &identity;
    context.identity = &identity;
    context.gpr = registers;
    context.provenance = MUSASHI_CPU_CONTEXT_SOURCE;
    context.instruction_valid = 1;
    foreign_context = context;
    foreign_context.identity = &status;

    for (i = 0; i < 32u; ++i) {
        before[i] = 0xa5000000u + i;
        gteRegs.CP2C.p[i].d = before[i];
    }
    memcpy(expected, before, sizeof(expected));
    assert(musashi_gte_owner_init(&owner, &status, thread_token, &state,
                                  &identity));
    assert(!musashi_gte_owner_init(&other, &status, thread_token, &state,
                                   &identity));

    expected[21] = 0x11111111u;
    expected[22] = 0x22222222u;
    expected[23] = 0x33333333u;
    expected[24] = 0x44444444u;
    expected[25] = 0x55555555u;
    expected[26] = 0xffffffffu;
    expected[27] = 0xffff8000u;
    expected[28] = 0x88888888u;
    expected[29] = 0x00007fffu;
    expected[30] = 0xffff8000u;
    assert(musashi_gte_owner_write_control(&owner, &context, 21,
                                           0x11111111u));
    assert(musashi_gte_owner_write_control(&owner, &context, 22,
                                           0x22222222u));
    assert(musashi_gte_owner_write_control(&owner, &context, 23,
                                           0x33333333u));
    assert(musashi_gte_owner_write_control(&owner, &context, 24,
                                           0x44444444u));
    assert(musashi_gte_owner_write_control(&owner, &context, 25,
                                           0x55555555u));
    assert(musashi_gte_owner_write_control(&owner, &context, 26,
                                           0x0000ffffu));
    assert(musashi_gte_owner_write_control(&owner, &context, 27,
                                           0x00008000u));
    assert(musashi_gte_owner_write_control(&owner, &context, 28,
                                           0x88888888u));
    assert(musashi_gte_owner_write_control(&owner, &context, 29,
                                           0x00007fffu));
    assert(musashi_gte_owner_write_control(&owner, &context, 30,
                                           0x00008000u));
    assert_bank(expected);
    assert(owner.write_count == 10u);

    /* Rotation-matrix controls 0..4: CTC2 stores 0..3 raw and sign-extends
     * halfword control 4. CFC2 reads them back exactly. */
    expected[0] = 0x00001234u;
    expected[1] = 0xffff0000u;
    expected[2] = 0x0056789au;
    expected[3] = 0x80000001u;
    expected[4] = 0xffffabcdu;
    assert(musashi_gte_owner_write_control(&owner, &context, 0,
                                           0x00001234u));
    assert(musashi_gte_owner_write_control(&owner, &context, 1,
                                           0xffff0000u));
    assert(musashi_gte_owner_write_control(&owner, &context, 2,
                                           0x0056789au));
    assert(musashi_gte_owner_write_control(&owner, &context, 3,
                                           0x80000001u));
    assert(musashi_gte_owner_write_control(&owner, &context, 4,
                                           0x0000abcdu));
    assert_bank(expected);
    assert(owner.write_count == 15u);
    /* Background-color controls 13..15: CTC2 stores the 32-bit value raw.
     * Retail 800491FC writes these after shifting the SetBackColor args. */
    expected[13] = 0x00000800u;
    expected[14] = 0x00000801u;
    expected[15] = 0x00000802u;
    assert(musashi_gte_owner_write_control(&owner, &context, 13,
                                           0x00000800u));
    assert(musashi_gte_owner_write_control(&owner, &context, 14,
                                           0x00000801u));
    assert(musashi_gte_owner_write_control(&owner, &context, 15,
                                           0x00000802u));
    assert_bank(expected);
    assert(owner.write_count == 18u);
    /* Light-color controls 16..20: CTC2 stores 16..19 raw and sign-extends
     * halfword control 20. Retail 800538BC writes this bank. */
    expected[16] = 0x00011111u;
    expected[17] = 0x00022222u;
    expected[18] = 0x00033333u;
    expected[19] = 0x00044444u;
    expected[20] = 0xffffabcdu;
    assert(musashi_gte_owner_write_control(&owner, &context, 16,
                                           0x00011111u));
    assert(musashi_gte_owner_write_control(&owner, &context, 17,
                                           0x00022222u));
    assert(musashi_gte_owner_write_control(&owner, &context, 18,
                                           0x00033333u));
    assert(musashi_gte_owner_write_control(&owner, &context, 19,
                                           0x00044444u));
    assert(musashi_gte_owner_write_control(&owner, &context, 20,
                                           0x0000abcdu));
    assert_bank(expected);
    assert(owner.write_count == 23u);
    /* SetTransMatrix keeps all 32 translation bits, including negatives. */
    expected[5] = 0x80000001u;
    expected[6] = 0x7fff1234u;
    expected[7] = 0xffff8000u;
    for (i = 5; i <= 7; ++i)
        assert(musashi_gte_owner_write_control(&owner, &context, i, expected[i]));
    assert_bank(expected);
    assert(owner.write_count == 26u);
    /* FLAG is read from the live control bank, never data31/LZCR. */
    {
        uint32_t value = 0xdeadbeefu;
        gteRegs.CP2D.p[31].d = 0x12345678u;
        assert(musashi_gte_owner_read_control(&owner, &context, 31, &value));
        assert(value == expected[31]);
        assert(gteRegs.CP2D.p[31].d == 0x12345678u);
    }
    for (i = 0; i < 8u; ++i) {
        uint32_t value = 0xdeadbeefu;
        assert(musashi_gte_owner_read_control(&owner, &context, i, &value));
        assert(value == expected[i]);
    }
    for (i = 13u; i < 21u; ++i) {
        uint32_t value = 0xdeadbeefu;
        assert(musashi_gte_owner_read_control(&owner, &context, i, &value));
        assert(value == expected[i]);
    }
    for (i = 21u; i < 31u; ++i) {
        uint32_t value = 0xdeadbeefu;
        assert(musashi_gte_owner_read_control(&owner, &context, i, &value));
        assert(value == expected[i]);
    }
    /* Light-matrix control 11 is read-only: the 80048E98 SWC2 source. */
    {
        uint32_t value = 0xdeadbeefu;
        assert(musashi_gte_owner_read_control(&owner, &context, 11, &value));
        assert(value == expected[11]);
    }
    assert(!musashi_gte_owner_write_control(&owner, &context, 11, 1));
    assert_bank(expected);
    /* Reads are observations: no counter advances and the bank is intact. */
    assert(owner.write_count == 26u);
    assert_bank(expected);
    {
        uint32_t sentinel = 0xdeadbeefu;
        assert(!musashi_gte_owner_read_control(&owner, &context, 8,
                                               &sentinel));
        assert(sentinel == 0xdeadbeefu);
        assert(!musashi_gte_owner_read_control(&owner, &context, 9,
                                               &sentinel));
        assert(sentinel == 0xdeadbeefu);
        assert(!musashi_gte_owner_read_control(&owner, &context, 12,
                                               &sentinel));
        assert(sentinel == 0xdeadbeefu);
        assert(!musashi_gte_owner_read_control(&owner, &context, 32,
                                               &sentinel));
        assert(sentinel == 0xdeadbeefu);
        assert(!musashi_gte_owner_read_control(&owner, &context, 0, NULL));
        assert(!musashi_gte_owner_read_control(&owner, &foreign_context, 0,
                                               &sentinel));
        assert(sentinel == 0xdeadbeefu);
    }
    assert(!musashi_gte_owner_write_control(&owner, &context, 8, 1));
    assert(!musashi_gte_owner_write_control(&owner, &context, 12, 1));
    assert(!musashi_gte_owner_write_control(&owner, &context, 31, 1));
    assert(!musashi_gte_owner_write_control(&owner, &foreign_context, 21, 1));
    assert_bank(expected);

    {
        uint32_t value = 0xdeadbeefu;
        assert(!musashi_gte_owner_read_control(&owner, &foreign_context, 31, &value));
        assert(value == 0xdeadbeefu);
        status.sr = 0;
        assert(!musashi_gte_owner_read_control(&owner, &context, 31, &value));
        assert(value == 0xdeadbeefu);
        assert(!musashi_gte_owner_write_control(&owner, &context, 5, 0));
    }
    status.sr = 0;
    assert(!musashi_gte_owner_write_control(&owner, &context, 21, 1));
    assert_bank(expected);
    status.sr = MUSASHI_CPU_STATUS_CU2;

    state.token = 12;
    assert(!musashi_gte_owner_write_control(&owner, &context, 21, 1));
    assert_bank(expected);
    memset(&preserved, 0xa5, sizeof(preserved));
    assert(!musashi_gte_owner_snapshot(&owner, &preserved));
    {
        MusashiGteSnapshot canary;
        memset(&canary, 0xa5, sizeof(canary));
        assert(!memcmp(&preserved, &canary, sizeof(preserved)));
    }
    state.token = 11;

    assert(musashi_gte_owner_snapshot(&owner, &snapshot));
    assert(snapshot.write_count == 26u && snapshot.initialized &&
           !snapshot.faulted);
    assert(!memcmp(snapshot.control, expected, sizeof(expected)));

    state.callback_once = 0;
    state.reentry_owner = &owner;
    state.reenter_snapshot = 1;
    assert(!musashi_gte_owner_write_control(&owner, &context, 21,
                                            0xdeadbeefu));
    assert(owner.faulted && owner.reentry_violation);
    assert_bank(expected);
    state.reenter_snapshot = 0;
    state.callback_once = 0;
    assert(musashi_gte_owner_snapshot(&owner, &snapshot));
    assert(snapshot.faulted && snapshot.write_count == 26u);
    assert_bank(expected);

    assert(musashi_gte_owner_close(&owner));
    assert_bank(expected);
    assert(!musashi_gte_owner_snapshot(&owner, &preserved));

    state.callback_once = 0;
    state.constructor_owner = &candidate;
    state.reentry_owner = &candidate;
    state.reenter_snapshot = 1;
    assert(!musashi_gte_owner_init(&candidate, &status, thread_token, &state,
                                   &identity));
    assert(candidate.faulted && candidate.reentry_violation &&
           !candidate.initialized);
    memset(&candidate, 0, sizeof(candidate));
    state.reenter_snapshot = 0;
    state.callback_once = 0;
    state.reenter_write = 1;
    assert(!musashi_gte_owner_init(&candidate, &status, thread_token, &state,
                                   &identity));
    assert(candidate.faulted && candidate.reentry_violation &&
           !candidate.initialized);
    memset(&candidate, 0, sizeof(candidate));
    state.reenter_write = 0;
    state.callback_once = 0;
    state.reenter_close = 1;
    assert(!musashi_gte_owner_init(&candidate, &status, thread_token, &state,
                                   &identity));
    assert(candidate.faulted && candidate.reentry_violation &&
           !candidate.initialized);
    memset(&candidate, 0, sizeof(candidate));
    state.reenter_close = 0;
    state.callback_once = 0;
    state.reenter_init = 1;
    assert(!musashi_gte_owner_init(&candidate, &status, thread_token, &state,
                                   &identity));
    assert(candidate.faulted && !candidate.initialized);
    memset(&candidate, 0, sizeof(candidate));
    state.reenter_init = 0;
    state.callback_once = 0;
    assert(musashi_gte_owner_init(&candidate, &status, thread_token, &state,
                                  &identity));
    assert(musashi_gte_owner_close(&candidate));
    puts("GTE_OWNER_PASS shared PsyCross control bank and CTC2 lease cases");
    return 0;
}
