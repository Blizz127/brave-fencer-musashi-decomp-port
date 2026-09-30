#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "musashi_gte_owner.h"
#include "psx/gtereg.h"

GTERegisters gteRegs;

int GTE_operator(int op) { (void)op; return 0; }
int LIM(int value, int max, int min, unsigned int flag) {
    (void)flag;
    return value > max ? max : (value < min ? min : value);
}
unsigned int gte_leadingzerocount(unsigned int value) {
    unsigned int count = 0;
    while (count < 32u && (value & 0x80000000u) == 0u) {
        value <<= 1;
        ++count;
    }
    return count;
}

typedef struct ThreadState {
    uint64_t token;
    MusashiGteOwner *owner;
    MusashiCpuContext *context;
    int reenter;
    int callback_once;
} ThreadState;

static uint64_t thread_token(void *userdata) {
    ThreadState *state = (ThreadState *)userdata;
    if (state->reenter && !state->callback_once) {
        uint32_t value = 0xa5a5a5a5u;
        state->callback_once = 1;
        assert(!musashi_gte_owner_read_data(state->owner, state->context,
                                             31, &value));
        assert(value == 0xa5a5a5a5u);
    }
    return state->token;
}

static unsigned expected_lzcr(uint32_t value) {
    uint32_t normalized = (value & 0x80000000u) != 0u ? ~value : value;
    unsigned count = 0;
    int bit;
    for (bit = 31; bit >= 0; --bit) {
        if ((normalized & (UINT32_C(1) << bit)) != 0u) break;
        ++count;
    }
    return count;
}

static uint32_t next_random(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void assert_banks(const uint32_t data[32], const uint32_t control[32]) {
    unsigned i;
    for (i = 0; i < 32u; ++i) {
        assert(gteRegs.CP2D.p[i].d == data[i]);
        assert(gteRegs.CP2C.p[i].d == control[i]);
    }
}

static void assert_snapshot(const MusashiGteSnapshot *snapshot,
                            const uint32_t data[32],
                            const uint32_t control[32],
                            uint64_t writes, uint64_t reads) {
    assert(snapshot->data_write_count == writes);
    assert(snapshot->data_read_count == reads);
    assert(!memcmp(snapshot->data, data, sizeof(snapshot->data)));
    assert(!memcmp(snapshot->control, control, sizeof(snapshot->control)));
}

int main(void) {
    MusashiCpuStatus *status = NULL;
    MusashiGteOwner owner = {0};
    ThreadState thread = {0};
    MusashiCpuContext context = {0}, foreign = {0};
    uint32_t data_before[32], control_before[32];
    uint32_t registers[32] = {0};
    const int identity = 19;
    unsigned i;

    thread.token = 0x55;
    thread.context = &context;
    context.identity = &identity;
    context.gpr = registers;
    context.provenance = MUSASHI_CPU_CONTEXT_SOURCE;
    context.instruction_valid = 1;
    foreign = context;
    foreign.identity = &status;

    for (i = 0; i < 32u; ++i) {
        data_before[i] = UINT32_C(0x51000000) + i;
        control_before[i] = UINT32_C(0xa7000000) + i;
        gteRegs.CP2D.p[i].d = data_before[i];
        gteRegs.CP2C.p[i].d = control_before[i];
    }
    assert(musashi_cpu_status_open_fixture(&status, thread_token, &thread,
                                           MUSASHI_CPU_STATUS_CU2));
    assert(musashi_gte_owner_init(&owner, status, thread_token, &thread,
                                  &identity));
    thread.owner = &owner;

    /* Directed sign runs, then every possible leading-sign count. */
    {
        static const uint32_t directed[] = {
            0x00000000u, 0xffffffffu, 0x00000001u, 0xfffffffeu,
            0x7fffffffu, 0x80000000u, 0x00010000u, 0xfffe0000u,
            0x00015f90u, 0xaaaaaaaa, 0x55555555u
        };
        uint32_t value;
        unsigned n;
        for (n = 0; n < sizeof(directed) / sizeof(directed[0]); ++n) {
            value = directed[n];
            assert(musashi_gte_owner_write_data(&owner, &context, 30, value));
            assert(gteRegs.CP2D.p[30].d == value);
            assert(gteRegs.CP2D.p[31].d == expected_lzcr(value));
            data_before[30] = value;
            data_before[31] = expected_lzcr(value);
            assert_banks(data_before, control_before);
        }
        for (i = 0; i < 32u; ++i) {
            value = UINT32_C(1) << i;
            assert(musashi_gte_owner_write_data(&owner, &context, 30, value));
            assert(gteRegs.CP2D.p[31].d == expected_lzcr(value));
            data_before[30] = value;
            data_before[31] = expected_lzcr(value);
            assert_banks(data_before, control_before);
            value = ~value;
            assert(musashi_gte_owner_write_data(&owner, &context, 30, value));
            assert(gteRegs.CP2D.p[31].d == expected_lzcr(value));
            data_before[30] = value;
            data_before[31] = expected_lzcr(value);
            assert_banks(data_before, control_before);
        }
    }

    /* 100,000 deterministic raw words, including both signs and zero runs. */
    {
        uint32_t random = UINT32_C(0x13579bdf);
        unsigned n;
        for (n = 0; n < 100000u; ++n) {
            uint32_t value = next_random(&random);
            assert(musashi_gte_owner_write_data(&owner, &context, 30, value));
            assert(gteRegs.CP2D.p[30].d == value);
            assert(gteRegs.CP2D.p[31].d == expected_lzcr(value));
        }
        data_before[30] = gteRegs.CP2D.p[30].d;
        data_before[31] = gteRegs.CP2D.p[31].d;
    }

    /* Reads are canonical raw values and only the two data selectors work. */
    {
        uint32_t value = 0;
        assert(musashi_gte_owner_read_data(&owner, &context, 30, &value));
        assert(value == data_before[30]);
        assert(musashi_gte_owner_read_data(&owner, &context, 31, &value));
        assert(value == data_before[31]);
    }

    /* IR1/IR2/IR3 accept plain MTC2 moves (800484EC sign/magnitude loads)
     * and MAC1/MAC2/MAC3 read back raw; neighbors stay refused. */
    {
        static const uint32_t ir[3] = {0x00000002u, 0xfffffffeu, 0x00000010u};
        uint32_t value = 0;
        unsigned k;
        for (k = 0; k < 3u; ++k) {
            assert(musashi_gte_owner_write_data(&owner, &context, 9u + k, ir[k]));
            data_before[9u + k] = ir[k];
            assert(gteRegs.CP2D.p[9u + k].d == ir[k]);
            assert_banks(data_before, control_before);
        }
        for (k = 0; k < 3u; ++k) {
            assert(musashi_gte_owner_read_data(&owner, &context, 9u + k, &value));
            assert(value == ir[k]);
            assert(musashi_gte_owner_read_data(&owner, &context, 25u + k, &value));
            assert(value == data_before[25u + k]);
        }
        /* IR0 accepts the func_80047EC8 interpolation-factor write (the GPF
         * site then scales IR1..IR3 by it) and the SWC2 store sites read it
         * back, so both directions are in the profile. */
        assert(musashi_gte_owner_write_data(&owner, &context, 8u, 7u));
        assert(gteRegs.CP2D.p[8].d == 7u);
        data_before[8] = 7u;
        assert_banks(data_before, control_before);
        /* The vector slots VXY1/VZ1/VXY2/VZ2/RGB (2..6) and IRGB (28) join the
         * write profile for the exported LWC2 sites; 12, 24 and most others
         * stay refused. */
        {
            static const unsigned vectors[7] = {2u, 3u, 4u, 5u, 6u, 28u, 0u};
            unsigned k;
            for (k = 0; k < 5u; ++k) {
                assert(musashi_gte_owner_write_data(&owner, &context, vectors[k], 0x40u + k));
            }
            /* IRGB unpacks into IR1..IR3 through the vendor helper. */
            assert(musashi_gte_owner_write_data(&owner, &context, 28u, 0x7fffu));
        }
        for (unsigned k = 0; k < 32u; ++k) data_before[k] = gteRegs.CP2D.p[k].d;
        assert_banks(data_before, control_before);
        assert(!musashi_gte_owner_write_data(&owner, &context, 12u, 7));
        assert(!musashi_gte_owner_read_data(&owner, &context, 2u, &value));
        assert(!musashi_gte_owner_read_data(&owner, &context, 23u, &value));
        assert(musashi_gte_owner_read_data(&owner, &context, 24u, &value));
        assert(!musashi_gte_owner_read_data(&owner, &context, 28u, &value));
        /* Retail SWC2 sites also store VZ2, RGB, OTZ, IR0, SXY0/1/2, SZ3,
         * the RGB0..2 colour FIFO (libgs GsTMDfastG3GL) and ORGB, so those
         * read back even though IRGB (28) and RES1 (23) do not. */
        {
            static const unsigned extra[15] = {6u, 7u, 8u, 12u, 13u, 14u, 16u, 17u,
                                                18u, 19u, 20u, 21u, 22u, 24u, 29u};
            unsigned k;
            for (k = 0; k < 15u; ++k) {
                /* ORGB's read is computed from IR1..IR3, so only acceptance
                 * is asserted here, not a raw-bank equality. PsyCross's MFC2
                 * stores that computed word back, so the bank is re-read
                 * afterwards rather than assumed untouched. */
                assert(musashi_gte_owner_read_data(&owner, &context, extra[k], &value));
                data_before[extra[k]] = gteRegs.CP2D.p[extra[k]].d;
            }
        }
        assert_banks(data_before, control_before);
    }

    /* Real GTE operations replace IR's signed low half only. Readback
     * must normalize stale high halves through MFC2, including SWC2 use. */
    {
        static const uint32_t raw[] = {0x12348000u, 0xffff7fffu, 0xabcdffffu,
                                       0x87650000u, 0x98760001u};
        static const uint32_t expected[] = {0xffff8000u, 0x00007fffu, 0xffffffffu,
                                            0u, 1u};
        unsigned reg, k;
        for (reg = 9; reg <= 11; ++reg) {
            for (k = 0; k < sizeof(raw)/sizeof(raw[0]); ++k) {
                uint32_t value = 0xdeadbeefu;
                gteRegs.CP2D.p[reg].d = raw[k];
                assert(musashi_gte_owner_read_data(&owner, &context, reg, &value));
                assert(value == expected[k]);
                data_before[reg] = expected[k];
                assert_banks(data_before, control_before);
            }
        }
    }

    /* Every refusal leaves the output sentinel, both banks, and counters. */
    {
        MusashiGteSnapshot snapshot;
        uint64_t writes = owner.data_write_count;
        uint64_t reads = owner.data_read_count;
        uint32_t sentinel;
        assert(!musashi_gte_owner_write_data(&owner, &context, 7, 7));
        assert(!musashi_gte_owner_write_data(&owner, &context, 29, 7));
        assert(!musashi_gte_owner_write_data(&owner, &foreign, 30, 7));
        context.gpr = NULL;
        assert(!musashi_gte_owner_write_data(&owner, &context, 30, 7));
        context.gpr = registers;
        context.instruction_valid = 0;
        assert(!musashi_gte_owner_write_data(&owner, &context, 30, 7));
        context.instruction_valid = 1;
        context.provenance = MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE;
        assert(!musashi_gte_owner_write_data(&owner, &context, 30, 7));
        context.provenance = MUSASHI_CPU_CONTEXT_SOURCE;
        thread.token++;
        assert(!musashi_gte_owner_write_data(&owner, &context, 30, 7));
        thread.token--;
        sentinel = UINT32_C(0xdeadc0de);
        /* IRGB (28) stays outside the read profile, so it still stands in for
         * a refused read that must leave the bank and the counters alone. */
        assert(!musashi_gte_owner_read_data(&owner, &context, 28, &sentinel));
        assert(sentinel == UINT32_C(0xdeadc0de));
        assert(!musashi_gte_owner_read_data(&owner, &foreign, 31, &sentinel));
        assert(sentinel == UINT32_C(0xdeadc0de));
        assert(owner.data_write_count == writes && owner.data_read_count == reads);
        assert_banks(data_before, control_before);
        assert(musashi_gte_owner_snapshot(&owner, &snapshot));
        assert_snapshot(&snapshot, data_before, control_before, writes, reads);
    }

    /* An accepted write survives a later refused read, and reentry sticks. */
    {
        uint32_t value = UINT32_C(0xbadf00d);
        uint32_t sentinel = UINT32_C(0xfacefeed);
        assert(musashi_gte_owner_write_data(&owner, &context, 30, value));
        data_before[30] = value;
        data_before[31] = expected_lzcr(value);
        thread.reenter = 1;
        thread.callback_once = 0;
        assert(!musashi_gte_owner_read_data(&owner, &context, 31, &sentinel));
        assert(sentinel == UINT32_C(0xfacefeed));
        assert(owner.faulted && owner.reentry_violation);
        assert_banks(data_before, control_before);
        assert(musashi_gte_owner_snapshot(&owner, &(MusashiGteSnapshot){0}));
        thread.reenter = 0;
    }

    /* A sticky fault can be torn down, and counters are then tested healthy. */
    assert(musashi_gte_owner_close(&owner));
    assert(musashi_cpu_status_close(status));
    status = NULL;
    assert(musashi_cpu_status_open_fixture(&status, thread_token, &thread,
                                           MUSASHI_CPU_STATUS_CU2));
    memset(&owner, 0, sizeof(owner));
    assert(musashi_gte_owner_init(&owner, status, thread_token, &thread,
                                  &identity));
    thread.owner = &owner;

    /* Diagnostic counters refuse overflow before touching the shared bank. */
    owner.data_write_count = UINT64_MAX;
    assert(!musashi_gte_owner_write_data(&owner, &context, 30, 0x1234u));
    owner.data_write_count = UINT64_MAX - 1u;
    assert(musashi_gte_owner_write_data(&owner, &context, 30, 0x1234u));
    data_before[30] = 0x1234u;
    data_before[31] = expected_lzcr(0x1234u);
    assert(owner.data_write_count == UINT64_MAX);
    assert_banks(data_before, control_before);
    assert(!musashi_gte_owner_write_data(&owner, &context, 30, 0x5678u));
    assert_banks(data_before, control_before);

    owner.data_read_count = UINT64_MAX - 1u;
    {
        uint32_t value = 0;
        assert(musashi_gte_owner_read_data(&owner, &context, 31, &value));
        assert(value == data_before[31]);
        assert(owner.data_read_count == UINT64_MAX);
    }
    {
        uint32_t sentinel = UINT32_C(0x12345678);
        assert(!musashi_gte_owner_read_data(&owner, &context, 31, &sentinel));
        assert(sentinel == UINT32_C(0x12345678));
    }
    assert(musashi_gte_owner_close(&owner));
    assert(musashi_cpu_status_close(status));

    /* Exercise CU2 refusal through the declared real CPU-status fixture seam. */
    status = NULL;
    assert(musashi_cpu_status_open_fixture(&status, thread_token, &thread, 0));
    memset(&owner, 0, sizeof(owner));
    assert(musashi_gte_owner_init(&owner, status, thread_token, &thread,
                                  &identity));
    {
        MusashiGteOwner rejected = {0};
        uint32_t sentinel = UINT32_C(0x76543210);
        assert(!musashi_gte_owner_init(&rejected, status, NULL, &thread,
                                       &identity));
        assert(!musashi_gte_owner_init(&rejected, status, thread_token,
                                       &thread, NULL));
        assert(!musashi_gte_owner_write_data(&owner, &context, 30, 0x1234u));
        assert(!musashi_gte_owner_read_data(&owner, &context, 31, &sentinel));
        assert(sentinel == UINT32_C(0x76543210));
        assert_banks(data_before, control_before);
    }
    assert(musashi_gte_owner_close(&owner));
    assert(musashi_cpu_status_close(status));
    puts("GTE_DATA_OWNER_PASS independent signed LZCS/LZCR and refusal cases");
    return 0;
}
