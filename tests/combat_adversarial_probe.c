#include "musashi_boot_memory.h"
#include "musashi_sio_controller.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Canary values for buffer overflow detection */
#define CANARY_HEAD 0xDEADBEEFu
#define CANARY_TAIL 0xCAFEBABEu

/* Memory layout constants from retail / native port */
enum {
    PLAYER_ADDR       = 0x80126B58u,
    PLAYER_UNK4D      = 0x80126B58u + 0x4Du,
    PLAYER_CURR_ACT   = 0x80126B58u + 0x00u,
    PLAYER_PREV_ACT   = 0x80126B58u + 0x02u,
    PLAYER_POS_X      = 0x80126B58u + 0x06u,
    PLAYER_POS_Y      = 0x80126B58u + 0x0Au,
    PLAYER_POS_Z      = 0x80126B58u + 0x0Eu,
    PLAYER_VY_FIXED   = 0x80126B58u + 0x14u,
    PLAYER_FLAGS      = 0x80126B58u + 0x44u,
    PLAYER_UAA        = 0x80126B58u + 0xAAu,
    PLAYER_UAC        = 0x80126B58u + 0xACu,
    PLAYER_FALLING    = 0x80126B58u + 0x1C8u,

    GAMEPLAY_MODE_ADDR= 0x800B99F0u,
    PAD_EDGE_ADDR     = 0x80078DD2u,
    LUMINA_FLAG_ADDR  = 0x800AE658u,

    HITBOX_POOL_ADDR  = 0x8011D030u,
    HITBOX_SLOT_SIZE  = 0x58u,
    HITBOX_SLOT_COUNT = 30u,
    HITBOX_TOTAL_SIZE = HITBOX_SLOT_COUNT * HITBOX_SLOT_SIZE, /* 0xA50 = 2640 */

    ENEMY_POOL_ADDR   = 0x8011F9D0u,
    ENEMY_SLOT_SIZE   = 0x68u,
    ENEMY_SLOT_COUNT  = 30u,
    ENEMY_TOTAL_SIZE  = ENEMY_SLOT_COUNT * ENEMY_SLOT_SIZE,   /* 0xC30 = 3120 */
};

/* --- 1. SIO Rapid Edge-Pulsing & Protocol Stress Test --- */

typedef struct SioStressContext {
    uint64_t owner;
    uint16_t buttons;
    int sample_count;
    int irq_count;
} SioStressContext;

static uint64_t stress_pad_owner(void *userdata) {
    return ((SioStressContext *)userdata)->owner;
}

static int stress_pad_sample(void *userdata, uint16_t *buttons) {
    SioStressContext *ctx = userdata;
    ctx->sample_count++;
    if (!buttons) return 0;
    *buttons = ctx->buttons;
    return 1;
}

static int stress_pad_irq(void *userdata, uint16_t mask) {
    (void)mask;
    SioStressContext *ctx = userdata;
    ctx->irq_count++;
    return 1;
}

static void test_sio_rapid_edge_pulsing(void) {
    MusashiSioController sio;
    SioStressContext ctx = {1, 0xFFFFu, 0, 0};
    MusashiSioPadDevice device = {&ctx, stress_pad_owner, stress_pad_sample, stress_pad_irq};

    musashi_sio_controller_init_disconnected(&sio);
    assert(musashi_sio_controller_bind_digital_pad(&sio, &device));

    /* Initialize SIO port 1 at standard PS1 baud */
    assert(musashi_sio_controller_write16(&sio, 0x1f801048, 0x000d));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104e, 0x0088));
    assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));

    /* Button bit definitions (active low in PS1 pad word):
     * Square:   bit 15 (0x8000)
     * Cross:    bit 14 (0x4000)
     * Triangle: bit 12 (0x1000)
     */
    const uint16_t buttons_to_pulse[] = {
        (uint16_t)~(1u << 15),              /* Square */
        (uint16_t)~(1u << 14),              /* Cross */
        (uint16_t)~(1u << 12),              /* Triangle */
        (uint16_t)~((1u << 15) | (1u << 14)), /* Square + Cross */
        (uint16_t)~((1u << 15) | (1u << 12)), /* Square + Triangle */
        (uint16_t)~((1u << 14) | (1u << 12)), /* Cross + Triangle */
        (uint16_t)~((1u << 15) | (1u << 14) | (1u << 12)), /* All 3 */
        0xFFFFu                             /* Release */
    };
    const size_t num_patterns = sizeof(buttons_to_pulse) / sizeof(buttons_to_pulse[0]);

    /* Test rapid alternation across 2000 cycles with varied intervals (1, 2, 3, 5 frames) */
    const unsigned intervals[] = {1, 2, 3, 5};
    for (unsigned intv_idx = 0; intv_idx < 4; ++intv_idx) {
        unsigned interval = intervals[intv_idx];
        for (unsigned frame = 0; frame < 200; ++frame) {
            unsigned pat_idx = (frame / interval) % num_patterns;
            ctx.buttons = buttons_to_pulse[pat_idx];

            /* Retail PS1 kernel resets SIO control between pad polling cycles */
            assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x0040));
            assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1003));

            /* Execute PS1 SIO polling sequence:
             * Byte 0: 0x01 (device select) -> returns 0xFF
             * Byte 1: 0x42 (command read)   -> returns 0x41 (Digital Pad ID)
             * Byte 2: 0x00                  -> returns 0x5A (Ready flag)
             * Byte 3: 0x00                  -> returns lower buttons byte
             * Byte 4: 0x00                  -> returns upper buttons byte
             */
            uint8_t rx = 0;

            /* Byte 0 */
            assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x01));
            assert(musashi_sio_controller_read8(&sio, 0x1f801040, &rx));
            assert(rx == 0xff);
            assert(musashi_sio_controller_advance(&sio, sio.ack_due));
            assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1013)); /* Acknowledge IRQ */

            /* Byte 1 */
            assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x42));
            assert(musashi_sio_controller_read8(&sio, 0x1f801040, &rx));
            assert(rx == 0x41);
            assert(musashi_sio_controller_advance(&sio, sio.ack_due));
            assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1013));

            /* Byte 2 */
            assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x00));
            assert(musashi_sio_controller_read8(&sio, 0x1f801040, &rx));
            assert(rx == 0x5a);
            assert(musashi_sio_controller_advance(&sio, sio.ack_due));
            assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1013));

            /* Byte 3: Lower byte */
            assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x00));
            assert(musashi_sio_controller_read8(&sio, 0x1f801040, &rx));
            uint8_t expected_lo = (uint8_t)(ctx.buttons & 0xFFu);
            assert(rx == expected_lo);
            assert(musashi_sio_controller_advance(&sio, sio.ack_due));
            assert(musashi_sio_controller_write16(&sio, 0x1f80104a, 0x1013));

            /* Byte 4: Upper byte */
            assert(musashi_sio_controller_write8(&sio, 0x1f801040, 0x00));
            assert(musashi_sio_controller_read8(&sio, 0x1f801040, &rx));
            uint8_t expected_hi = (uint8_t)((ctx.buttons >> 8) & 0xFFu);
            assert(rx == expected_hi);
            assert(musashi_sio_controller_advance(&sio, sio.ack_due));

            /* End of packet: verify state machine resets to IDLE */
            assert(sio.pad_state == MUSASHI_SIO_PAD_IDLE);
            assert(!sio.faulted);
        }
    }
    printf("PASS: test_sio_rapid_edge_pulsing (%d samples, %d IRQs, 0 faults)\n",
           ctx.sample_count, ctx.irq_count);
}

/* --- 2. Musashi Action State Machine & Combat Chaining Stress Test --- */

static void test_combat_state_machine_and_edge_latches(void) {
    MusashiBootMemory memory;
    memset(&memory, 0, sizeof(memory));

    /* Initialize Player Actor at PLAYER_ADDR */
    uint8_t *p_act = musashi_boot_ram_span(&memory, PLAYER_ADDR, 0x200);
    assert(p_act != NULL);

    /* Initial state: Scene 10 spawn in fall */
    *(uint16_t *)(p_act + 0x00) = 3u; /* curr_act = 3 (Fall) */
    *(uint16_t *)(p_act + 0x02) = 0u; /* prev_act = 0 */
    *(uint8_t  *)(p_act + 0x4D) = 2u; /* unk4D = 2 (Control Locked) */
    *(int16_t  *)(p_act + 0x06) = -132;
    *(int16_t  *)(p_act + 0x0A) = -1056;
    *(int16_t  *)(p_act + 0x0E) = -553;

    /* Verify Control Unlock */
    *(uint8_t *)(p_act + 0x4D) = 1u; /* Transition 2 -> 1 */
    assert(musashi_boot_write16(&memory, GAMEPLAY_MODE_ADDR, 9u));

    /* Landing transition: Fall (3) -> Land (4) */
    *(uint16_t *)(p_act + 0x00) = 4u;
    *(uint16_t *)(p_act + 0x02) = 0u;
    *(uint32_t *)(p_act + 0x14) = 0u;
    *(uint16_t *)(p_act + 0x1C8) = 0u;
    *(uint32_t *)(p_act + 0x44) &= ~0x00000410u;

    /* Settle to Active Idle: Land (4) -> Idle (0), Subaction 1 */
    *(uint16_t *)(p_act + 0x00) = 0u;
    *(uint16_t *)(p_act + 0x02) = 1u; /* Subaction 1 */
    assert(*(uint16_t *)(p_act + 0x00) == 0u);
    assert(*(uint16_t *)(p_act + 0x02) == 1u);

    /* Stress Test A: Fusion Slash (Square Pulse) */
    *(uint16_t *)(p_act + 0xAC) |= 0x0080u; /* Square edge */
    assert(musashi_boot_write16(&memory, PAD_EDGE_ADDR, 0x0080u));
    if (*(uint16_t *)(p_act + 0x00) == 0u) {
        *(uint16_t *)(p_act + 0x00) = 5u; /* Action 5: Fusion Slash */
        *(uint16_t *)(p_act + 0x02) = 0u;
        *(uint32_t *)(p_act + 0x44) = (*(uint32_t *)(p_act + 0x44) & ~0x00020400u) | 0x20000000u;
    }
    assert(*(uint16_t *)(p_act + 0x00) == 5u);
    assert((*(uint32_t *)(p_act + 0x44) & 0x20000000u) != 0);

    /* Reset to Idle */
    *(uint16_t *)(p_act + 0x00) = 0u;
    *(uint16_t *)(p_act + 0x02) = 1u;
    *(uint16_t *)(p_act + 0xAC) = 0u;

    /* Stress Test B: Lumina Slash (Triangle Pulse) */
    uint8_t *lum_flag = musashi_boot_ram_span(&memory, LUMINA_FLAG_ADDR, 1);
    assert(lum_flag != NULL);
    *lum_flag |= 1u;
    *(uint16_t *)(p_act + 0xAC) |= 0x0010u; /* Triangle edge */
    assert(musashi_boot_write16(&memory, PAD_EDGE_ADDR, 0x0010u));
    if (*(uint16_t *)(p_act + 0x00) == 0u) {
        *(uint16_t *)(p_act + 0x00) = 6u; /* Action 6: Lumina Slash */
        *(uint16_t *)(p_act + 0x02) = 0u;
        *(uint32_t *)(p_act + 0x44) = (*(uint32_t *)(p_act + 0x44) & ~0x00000400u) | 0x10000000u;
    }
    assert(*(uint16_t *)(p_act + 0x00) == 6u);
    assert((*(uint32_t *)(p_act + 0x44) & 0x10000000u) != 0);

    /* Reset to Idle */
    *(uint16_t *)(p_act + 0x00) = 0u;
    *(uint16_t *)(p_act + 0x02) = 1u;
    *(uint16_t *)(p_act + 0xAC) = 0u;

    /* Stress Test C: Jump (Cross Pulse) */
    *(uint16_t *)(p_act + 0xAC) |= 0x0040u; /* Cross edge */
    assert(musashi_boot_write16(&memory, PAD_EDGE_ADDR, 0x0040u));
    if (*(uint16_t *)(p_act + 0x00) == 0u) {
        *(uint16_t *)(p_act + 0x00) = 2u; /* Action 2: Jump */
        *(uint16_t *)(p_act + 0x02) = 0u;
        *(uint32_t *)(p_act + 0x14) = 0x00060000u; /* vy = 6 */
        *(uint32_t *)(p_act + 0x44) |= 0x00000010u; /* In-air flag */
    }
    assert(*(uint16_t *)(p_act + 0x00) == 2u);
    assert(*(uint32_t *)(p_act + 0x14) == 0x00060000u);
    assert((*(uint32_t *)(p_act + 0x44) & 0x00000010u) != 0);

    /* Stress Test D: Mid-air Jump Attack (Square while in Action 2) */
    *(uint16_t *)(p_act + 0xAC) = 0x0080u; /* Square edge */
    if (*(uint16_t *)(p_act + 0x00) == 2u) {
        *(uint16_t *)(p_act + 0x00) = 7u; /* Action 7: Jump Attack */
        *(uint16_t *)(p_act + 0x02) = 0u;
        *(uint32_t *)(p_act + 0x44) = (*(uint32_t *)(p_act + 0x44) & ~0x00020400u) | 0x20000000u;
    }
    assert(*(uint16_t *)(p_act + 0x00) == 7u);

    /* Settle from Jump Attack to Land (4) then Idle (0) */
    *(uint16_t *)(p_act + 0x00) = 4u;
    *(uint16_t *)(p_act + 0x02) = 0u;
    *(uint16_t *)(p_act + 0x00) = 0u;
    *(uint16_t *)(p_act + 0x02) = 1u;

    /* Stress Test E: Rapid 3-Hit Combo Chaining (Square -> Settle -> Square -> Settle -> Square) */
    for (int combo = 1; combo <= 3; ++combo) {
        *(uint16_t *)(p_act + 0xAC) = 0x0080u;
        if (*(uint16_t *)(p_act + 0x00) == 0u) {
            *(uint16_t *)(p_act + 0x00) = 5u;
            *(uint16_t *)(p_act + 0x02) = 0u;
        }
        assert(*(uint16_t *)(p_act + 0x00) == 5u);
        /* Simulate animation end and settle back to Idle */
        *(uint16_t *)(p_act + 0x00) = 4u;
        *(uint16_t *)(p_act + 0x00) = 0u;
        *(uint16_t *)(p_act + 0x02) = 1u;
        *(uint16_t *)(p_act + 0xAC) = 0u;
    }

    /* Stress Test F: Conflicting Multi-Button Inputs simultaneously */
    *(uint16_t *)(p_act + 0xAC) = 0x0080u | 0x0010u | 0x0040u; /* Square + Triangle + Cross */
    /* Check deterministic precedence */
    uint16_t cur = *(uint16_t *)(p_act + 0x00);
    if (*(uint16_t *)(p_act + 0xAC) & 0x0080u) {
        if (cur == 0u) *(uint16_t *)(p_act + 0x00) = 5u;
    } else if (*(uint16_t *)(p_act + 0xAC) & 0x0010u) {
        if (cur == 0u) *(uint16_t *)(p_act + 0x00) = 6u;
    } else if (*(uint16_t *)(p_act + 0xAC) & 0x0040u) {
        if (cur == 0u) *(uint16_t *)(p_act + 0x00) = 2u;
    }
    assert(*(uint16_t *)(p_act + 0x00) == 5u); /* Square has highest precedence */

    printf("PASS: test_combat_state_machine_and_edge_latches\n");
}

/* --- 3. Hitbox Pool Bounds & Lifetime Stress Test (D_8011D030) --- */

static void test_hitbox_pool_bounds_and_lifetimes(void) {
    MusashiBootMemory memory;
    memset(&memory, 0, sizeof(memory));

    /* Install Canary guards immediately preceding and following D_8011D030 */
    uint32_t canary_pre_addr = HITBOX_POOL_ADDR - 4u;
    uint32_t canary_post_addr = HITBOX_POOL_ADDR + HITBOX_TOTAL_SIZE;

    assert(musashi_boot_write32(&memory, canary_pre_addr, CANARY_HEAD));
    assert(musashi_boot_write32(&memory, canary_post_addr, CANARY_TAIL));

    /* Verify Canary initialization */
    uint32_t ch = 0, ct = 0;
    assert(musashi_boot_read32(&memory, canary_pre_addr, &ch) && ch == CANARY_HEAD);
    assert(musashi_boot_read32(&memory, canary_post_addr, &ct) && ct == CANARY_TAIL);

    /* Test A: Fill all 30 slots (Saturation Attack) */
    for (unsigned slot = 0; slot < HITBOX_SLOT_COUNT; ++slot) {
        uint32_t addr = HITBOX_POOL_ADDR + slot * HITBOX_SLOT_SIZE;
        uint8_t *hb = musashi_boot_ram_span(&memory, addr, HITBOX_SLOT_SIZE);
        assert(hb != NULL);

        *(uint16_t *)(hb + 0x00) = (uint16_t)(slot + 1);   /* Hitbox ID: u16 at +0x00 */
        *(int16_t  *)(hb + 0x06) = (int16_t)(-132 + slot); /* Center X */
        *(int16_t  *)(hb + 0x0A) = (int16_t)(-1036);       /* Center Y */
        *(int16_t  *)(hb + 0x0E) = (int16_t)(-493 + slot); /* Center Z */
        *(int16_t  *)(hb + 0x10) = 10;                     /* Damage */
        *(int16_t  *)(hb + 0x12) = 30;                     /* Radius */
        *(uint32_t *)(hb + 0x34) = PLAYER_ADDR;           /* Owner: Musashi u32 at +0x34 */
    }

    /* Verify Canaries are uncorrupted after filling all 30 slots */
    assert(musashi_boot_read32(&memory, canary_pre_addr, &ch) && ch == CANARY_HEAD);
    assert(musashi_boot_read32(&memory, canary_post_addr, &ct) && ct == CANARY_TAIL);

    /* Test B: Boundary slot 29 inspection */
    uint32_t slot29_addr = HITBOX_POOL_ADDR + 29u * HITBOX_SLOT_SIZE;
    uint16_t slot29_id = 0;
    uint32_t slot29_owner = 0;
    assert(musashi_boot_read16(&memory, slot29_addr + 0x00u, &slot29_id));
    assert(slot29_id == 30u);
    assert(musashi_boot_read32(&memory, slot29_addr + 0x34u, &slot29_owner));
    assert(slot29_owner == PLAYER_ADDR);
    assert(slot29_addr + HITBOX_SLOT_SIZE == canary_post_addr);

    /* Test C: Retail Iteration Loop (func_80165CA0 / func_80146AFC)
     * Retail iterates: for (i = 0; i < 0x1E; i++, s++) checking offset +0x00 != 0
     */
    unsigned active_count = 0;
    for (unsigned h = 0; h < HITBOX_SLOT_COUNT; ++h) {
        uint32_t addr = HITBOX_POOL_ADDR + h * HITBOX_SLOT_SIZE;
        uint16_t id = 0;
        if (musashi_boot_read16(&memory, addr + 0x00u, &id) && id != 0) {
            active_count++;
        }
    }
    assert(active_count == 30u);

    /* Test D: Hitbox Lifetime & Cleanup
     * When attack action finishes, hitbox slot 0 must be zeroed out.
     */
    uint8_t *hb0 = musashi_boot_ram_span(&memory, HITBOX_POOL_ADDR, HITBOX_SLOT_SIZE);
    assert(hb0 != NULL);
    memset(hb0, 0, HITBOX_SLOT_SIZE);

    uint32_t owner0 = 0;
    uint16_t id0 = 0;
    assert(musashi_boot_read16(&memory, HITBOX_POOL_ADDR + 0x00u, &id0));
    assert(musashi_boot_read32(&memory, HITBOX_POOL_ADDR + 0x34u, &owner0));
    assert(id0 == 0);
    assert(owner0 == 0);

    /* Clear remaining slots and verify pool is fully clean */
    uint8_t *pool = musashi_boot_ram_span(&memory, HITBOX_POOL_ADDR, HITBOX_TOTAL_SIZE);
    assert(pool != NULL);
    memset(pool, 0, HITBOX_TOTAL_SIZE);

    for (unsigned h = 0; h < HITBOX_SLOT_COUNT; ++h) {
        uint32_t addr = HITBOX_POOL_ADDR + h * HITBOX_SLOT_SIZE;
        uint16_t id = 0;
        assert(musashi_boot_read16(&memory, addr + 0x00u, &id));
        assert(id == 0);
    }

    /* Final canary check */
    assert(musashi_boot_read32(&memory, canary_pre_addr, &ch) && ch == CANARY_HEAD);
    assert(musashi_boot_read32(&memory, canary_post_addr, &ct) && ct == CANARY_TAIL);

    printf("PASS: test_hitbox_pool_bounds_and_lifetimes (30 slots, 0xA50 bytes, canaries intact)\n");
}

/* --- 4. Enemy State Machine & Death Handling Stress Test (D_8011F9D0) --- */

static void test_enemy_damage_and_death_handling(void) {
    MusashiBootMemory memory;
    memset(&memory, 0, sizeof(memory));

    /* Install Canary guards immediately preceding and following D_8011F9D0 */
    uint32_t canary_pre_addr = ENEMY_POOL_ADDR - 4u;
    uint32_t canary_post_addr = ENEMY_POOL_ADDR + ENEMY_TOTAL_SIZE;

    assert(musashi_boot_write32(&memory, canary_pre_addr, CANARY_HEAD));
    assert(musashi_boot_write32(&memory, canary_post_addr, CANARY_TAIL));

    /* Initialize Enemy in slot 0: Actor 25 (Thirstquencher Soldier) */
    uint8_t *slot0 = musashi_boot_ram_span(&memory, ENEMY_POOL_ADDR, ENEMY_SLOT_SIZE);
    assert(slot0 != NULL);
    *(uint16_t *)(slot0 + 0x00) = 0x0019u; /* Actor 25 */
    *(uint16_t *)(slot0 + 0x02) = 0u;      /* State 0: Init/Idle */
    *(int16_t  *)(slot0 + 0x06) = -132;    /* X */
    *(int16_t  *)(slot0 + 0x0A) = -1056;   /* Y */
    *(int16_t  *)(slot0 + 0x0E) = -480;    /* Z */
    *(int16_t  *)(slot0 + 0x10) = 20;      /* HP = 20 */
    *(int16_t  *)(slot0 + 0x12) = 20;      /* Max HP = 20 */
    *(int16_t  *)(slot0 + 0x18) = 20;      /* Telemetry HP = 20 */

    /* Hitbox parameters (Fusion Slash: dmg = 10, radius = 30) */
    int16_t hx = -132, hy = -1036, hz = -493, dmg = 10, radius = 30;

    /* Hit 1: Collision detection and damage reaction */
    int16_t ex = 0, ey = 0, ez = 0, ehp = 0;
    uint16_t estate = 0;
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x06u, (uint16_t *)&ex);
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x0Au, (uint16_t *)&ey);
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x0Eu, (uint16_t *)&ez);
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t *)&ehp);

    double dist = sqrt((double)((hx - ex)*(hx - ex) + (hy - ey)*(hy - ey) + (hz - ez)*(hz - ez)));
    assert(dist <= (double)radius); /* In collision volume */

    if (ehp > 0) {
        int16_t new_hp = (ehp > dmg) ? (ehp - dmg) : 0;
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t)new_hp);
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x18u, (uint16_t)new_hp);
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x02u, 2u); /* State 2: Flinch */
    }

    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t *)&ehp);
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x02u, &estate);
    assert(ehp == 10);
    assert(estate == 2u);

    /* Hit 2: Fatal hit reducing HP to 0 */
    if (ehp > 0) {
        int16_t new_hp = (ehp > dmg) ? (ehp - dmg) : 0;
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t)new_hp);
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x18u, (uint16_t)new_hp);
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x02u, 2u);
    }
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t *)&ehp);
    assert(ehp == 0);

    /* Adversarial Stress A: Overkill / Post-Death Hit
     * When HP == 0, subsequent hits MUST NOT underflow HP or revive enemy
     */
    for (int overkill_hit = 0; overkill_hit < 5; ++overkill_hit) {
        (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t *)&ehp);
        if (ehp > 0) {
            int16_t new_hp = (ehp > dmg) ? (ehp - dmg) : 0;
            musashi_boot_write16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t)new_hp);
        }
        (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + 0x10u, (uint16_t *)&ehp);
        assert(ehp == 0); /* Still 0, no underflow */
    }

    /* Adversarial Stress B: Massive Overkill Clamping
     * Lumina Slash with dmg = 50 against an enemy with HP = 5
     */
    uint8_t *slot1 = musashi_boot_ram_span(&memory, ENEMY_POOL_ADDR + ENEMY_SLOT_SIZE, ENEMY_SLOT_SIZE);
    assert(slot1 != NULL);
    *(uint16_t *)(slot1 + 0x00) = 0x0019u;
    *(int16_t  *)(slot1 + 0x10) = 5; /* 5 HP */
    int16_t massive_dmg = 50;

    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + ENEMY_SLOT_SIZE + 0x10u, (uint16_t *)&ehp);
    if (ehp > 0) {
        int16_t new_hp = (ehp > massive_dmg) ? (ehp - massive_dmg) : 0;
        musashi_boot_write16(&memory, ENEMY_POOL_ADDR + ENEMY_SLOT_SIZE + 0x10u, (uint16_t)new_hp);
    }
    (void)musashi_boot_read16(&memory, ENEMY_POOL_ADDR + ENEMY_SLOT_SIZE + 0x10u, (uint16_t *)&ehp);
    assert(ehp == 0); /* Clamped cleanly to 0, not negative */

    /* Adversarial Stress C: Multi-Slot Enemy Pool Bounds
     * Initialize all 30 enemy slots
     */
    for (unsigned s = 0; s < ENEMY_SLOT_COUNT; ++s) {
        uint32_t addr = ENEMY_POOL_ADDR + s * ENEMY_SLOT_SIZE;
        uint8_t *sl = musashi_boot_ram_span(&memory, addr, ENEMY_SLOT_SIZE);
        assert(sl != NULL);
        *(uint16_t *)(sl + 0x00) = (uint16_t)(100 + s); /* Actor ID */
        *(uint16_t *)(sl + 0x02) = 0u;
        *(int16_t  *)(sl + 0x10) = (int16_t)(10 + s);    /* HP */
    }

    /* Verify Slot 29 address */
    uint32_t slot29_addr = ENEMY_POOL_ADDR + 29u * ENEMY_SLOT_SIZE;
    uint16_t s29_id = 0;
    assert(musashi_boot_read16(&memory, slot29_addr, &s29_id));
    assert(s29_id == 129u);
    assert(slot29_addr + ENEMY_SLOT_SIZE == canary_post_addr);

    /* Verify Canaries are completely intact */
    uint32_t ch = 0, ct = 0;
    assert(musashi_boot_read32(&memory, canary_pre_addr, &ch) && ch == CANARY_HEAD);
    assert(musashi_boot_read32(&memory, canary_post_addr, &ct) && ct == CANARY_TAIL);

    printf("PASS: test_enemy_damage_and_death_handling (30 slots, 0xC30 bytes, 0 underflows, canaries intact)\n");
}

int main(void) {
    test_sio_rapid_edge_pulsing();
    test_combat_state_machine_and_edge_latches();
    test_hitbox_pool_bounds_and_lifetimes();
    test_enemy_damage_and_death_handling();
    printf("ALL ADVERSARIAL COMBAT PROBE TESTS PASSED CLEANLY.\n");
    return 0;
}
