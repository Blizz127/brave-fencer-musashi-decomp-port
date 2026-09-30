#include "musashi_gpu_controller.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VRAM_WIDTH 1024u
#define VRAM_HEIGHT 512u
#define GP0 MUSASHI_GPU_GP0
#define GP1 MUSASHI_GPU_GP1
#define READY_COMMAND (1u << 26)
#define READY_DMA (1u << 28)

typedef struct FillFixture {
    MusashiGpuController controller;
    uint16_t vram[VRAM_WIDTH * VRAM_HEIGHT];
    MusashiGpuFill last_fill;
    int ready_value;
    int reset_value;
    int clear_value;
    int fill_value;
    int reenter_fill;
    int nested_result;
    unsigned reset_calls;
    unsigned clear_calls;
    unsigned environment_calls;
    unsigned fill_calls;
    uint64_t callback_pixels;
} FillFixture;

static int check_failed(const char *expression, unsigned line) {
    fprintf(stderr, "GPU_FILL_CONTROLLER_FAIL line=%u condition=%s\n",
            line, expression);
    return 0;
}

#define CHECK(expression) \
    do { if (!(expression)) return check_failed(#expression, __LINE__); } while (0)

static int backend_reset(void *userdata) {
    FillFixture *fixture = userdata;
    fixture->reset_calls++;
    return fixture->reset_value;
}

static int backend_draw_mode(void *userdata, uint32_t word) {
    (void)userdata;
    return (word >> 24) == 0xe1u;
}

static int backend_display_enable(void *userdata, int enabled) {
    (void)userdata;
    return enabled == 0;
}

static int backend_clear_fifo(void *userdata) {
    FillFixture *fixture = userdata;
    fixture->clear_calls++;
    return fixture->clear_value;
}

static int backend_ready(void *userdata) {
    const FillFixture *fixture = userdata;
    return fixture->ready_value;
}

static int backend_environment(void *userdata, uint32_t word) {
    FillFixture *fixture = userdata;
    (void)word;
    fixture->environment_calls++;
    return 1;
}

static int backend_display(void *userdata,
                           const MusashiGpuDisplayState *candidate) {
    (void)userdata;
    (void)candidate;
    return 1;
}

static int backend_fill(void *userdata, const MusashiGpuFill *fill) {
    FillFixture *fixture = userdata;
    unsigned row;
    unsigned column;

    fixture->fill_calls++;
    fixture->last_fill = *fill;
    if (fixture->reenter_fill) {
        fixture->nested_result = musashi_gpu_controller_write32(
            &fixture->controller, GP0, 0xe6000003u);
    }
    if (!fixture->fill_value) return 0;
    for (row = 0; row < fill->height; ++row) {
        for (column = 0; column < fill->width; ++column) {
            const unsigned x = ((unsigned)fill->x + column) & (VRAM_WIDTH - 1u);
            const unsigned y = ((unsigned)fill->y + row) & (VRAM_HEIGHT - 1u);
            fixture->vram[y * VRAM_WIDTH + x] = fill->color;
        }
    }
    fixture->callback_pixels += (uint64_t)fill->width * fill->height;
    return 1;
}

static MusashiGpuBackend backend_for(FillFixture *fixture, int include_fill) {
    MusashiGpuBackend backend = {
        .userdata = fixture,
        .reset = backend_reset,
        .draw_mode = backend_draw_mode,
        .display_enable = backend_display_enable,
        .clear_fifo = backend_clear_fifo,
        .ready = backend_ready,
        .environment = backend_environment,
        .display = backend_display,
        .fill_vram = include_fill ? backend_fill : NULL,
    };
    return backend;
}

static int fixture_init(FillFixture *fixture, int include_fill) {
    MusashiGpuBackend backend;
    memset(fixture, 0, sizeof(*fixture));
    fixture->ready_value = 1;
    fixture->reset_value = 1;
    fixture->clear_value = 1;
    fixture->fill_value = 1;
    fixture->controller.backend.userdata = fixture;
    backend = backend_for(fixture, include_fill);
    return musashi_gpu_controller_init(&fixture->controller, &backend);
}

static int write_gp0(FillFixture *fixture, uint32_t word) {
    return musashi_gpu_controller_write32(&fixture->controller, GP0, word);
}

static int write_gp1(FillFixture *fixture, uint32_t word) {
    return musashi_gpu_controller_write32(&fixture->controller, GP1, word);
}

static uint32_t read_status(FillFixture *fixture) {
    uint32_t status = 0;
    if (!musashi_gpu_controller_read32(&fixture->controller, MUSASHI_GPU_GPUSTAT,
                                       &status)) {
        (void)check_failed("read status", __LINE__);
    }
    return status;
}

static int expect_region(const FillFixture *fixture, unsigned x, unsigned y,
                         unsigned width, unsigned height, uint16_t value) {
    unsigned row;
    unsigned column;
    for (row = 0; row < height; ++row) {
        for (column = 0; column < width; ++column) {
            const unsigned actual_x = (x + column) & (VRAM_WIDTH - 1u);
            const unsigned actual_y = (y + row) & (VRAM_HEIGHT - 1u);
            if (fixture->vram[actual_y * VRAM_WIDTH + actual_x] != value)
                return 0;
        }
    }
    return 1;
}

static int test_full_fill_descriptor_and_wrap(void) {
    FillFixture fixture;
    unsigned index;

    CHECK(fixture_init(&fixture, 1));
    for (index = 0; index < VRAM_WIDTH * VRAM_HEIGHT; ++index)
        fixture.vram[index] = UINT16_C(0x1234);
    /* E3/E4/E5/E6 state must not clip, offset, mask, or block a fill. */
    CHECK(write_gp0(&fixture, 0xe3000000u));
    CHECK(write_gp0(&fixture, 0xe4ffffffu));
    CHECK(write_gp0(&fixture, 0xe5003fffu));
    CHECK(write_gp0(&fixture, 0xe6000003u));
    CHECK(write_gp0(&fixture, 0x02f80000u));
    CHECK(write_gp0(&fixture, (510u << 16) | 1009u));
    CHECK(write_gp0(&fixture, (3u << 16) | 17u));
    CHECK(fixture.fill_calls == 1);
    CHECK(fixture.last_fill.x == 1008 && fixture.last_fill.y == 510);
    CHECK(fixture.last_fill.width == 32 && fixture.last_fill.height == 3);
    CHECK(fixture.last_fill.color == UINT16_C(0x7c00));
    CHECK(fixture.controller.completed_fills == 1);
    CHECK(fixture.controller.filled_pixels == 96);
    CHECK(fixture.callback_pixels == 96);
    CHECK(expect_region(&fixture, 1008, 510, 32, 3, UINT16_C(0x7c00)));
    CHECK(fixture.vram[509u * VRAM_WIDTH + 1008u] == UINT16_C(0x1234));
    CHECK(fixture.vram[1u * VRAM_WIDTH + 16u] == UINT16_C(0x1234));
    CHECK(fixture.controller.mask_flags == 3);
    CHECK(fixture.controller.fill_words == 0);
    return 1;
}

static int test_color_quantization_and_zero_height(void) {
    FillFixture fixture;
    unsigned index;

    CHECK(fixture_init(&fixture, 1));
    for (index = 0; index < VRAM_WIDTH * VRAM_HEIGHT; ++index)
        fixture.vram[index] = UINT16_C(0x4567);
    CHECK(write_gp0(&fixture, 0x02123456u));
    CHECK(write_gp0(&fixture, 0x00000010u));
    CHECK(write_gp0(&fixture, (1u << 16) | 1u));
    CHECK(fixture.last_fill.color == UINT16_C(0x08ca));
    CHECK(fixture.last_fill.width == 16 && fixture.last_fill.height == 1);
    CHECK(fixture.vram[16u] == UINT16_C(0x08ca));
    CHECK(write_gp0(&fixture, 0x02ffffffu));
    CHECK(write_gp0(&fixture, 0x00020020u));
    CHECK(write_gp0(&fixture, 0));
    CHECK(fixture.fill_calls == 2);
    CHECK(fixture.controller.completed_fills == 2);
    CHECK(fixture.controller.filled_pixels == 16);
    CHECK(fixture.callback_pixels == 16);
    CHECK(fixture.vram[2u * VRAM_WIDTH + 32u] == UINT16_C(0x4567));
    CHECK(fixture.controller.fill_words == 0);
    return 1;
}

static int test_partial_status_data_and_resets(void) {
    FillFixture fixture;
    uint64_t accepted;
    uint32_t status;

    CHECK(fixture_init(&fixture, 1));
    CHECK(write_gp0(&fixture, 0x02ffffffu));
    CHECK(fixture.controller.fill_words == 1);
    CHECK(fixture.controller.accepted_gp0_words == 1);
    status = read_status(&fixture);
    CHECK((status & READY_COMMAND) == 0 && (status & READY_DMA) != 0);
    CHECK(write_gp0(&fixture, 0xe60003ffu));
    CHECK(fixture.controller.fill_words == 2);
    CHECK(fixture.controller.mask_flags == 0);
    CHECK(fixture.fill_calls == 0);
    accepted = fixture.controller.accepted_gp0_words;
    CHECK(write_gp1(&fixture, 0x02000000u));
    CHECK(fixture.controller.fill_words == 2);
    CHECK(fixture.controller.accepted_gp0_words == accepted);
    CHECK(write_gp1(&fixture, 0x04000002u));
    CHECK(fixture.controller.fill_words == 2);
    CHECK(fixture.controller.dma_direction == 2);
    CHECK(write_gp1(&fixture, 0x01000000u));
    CHECK(fixture.clear_calls == 1 && fixture.controller.fill_words == 0);
    CHECK(fixture.controller.accepted_gp0_words == accepted);
    CHECK(write_gp0(&fixture, 0xe6000003u));
    CHECK(fixture.controller.mask_flags == 3);
    CHECK(write_gp0(&fixture, 0x020000ffu));
    CHECK(write_gp0(&fixture, 0));
    CHECK(write_gp1(&fixture, 0));
    CHECK(fixture.controller.fill_words == 0);
    CHECK(fixture.controller.accepted_gp0_words == accepted + 3);
    CHECK(fixture.controller.completed_fills == 0);
    CHECK(fixture.fill_calls == 0);
    return 1;
}

static int test_refusals_and_counter_preflight(void) {
    FillFixture fixture;
    uint64_t accepted;

    CHECK(fixture_init(&fixture, 0));
    CHECK(!write_gp0(&fixture, 0x020000ffu));
    CHECK(fixture.controller.fill_words == 0);
    CHECK(fixture.controller.accepted_gp0_words == 0);

    CHECK(fixture_init(&fixture, 1));
    fixture.ready_value = 0;
    CHECK(!write_gp0(&fixture, 0x020000ffu));
    CHECK(fixture.controller.fill_words == 0);
    fixture.ready_value = 1;
    CHECK(write_gp0(&fixture, 0x020000ffu));
    CHECK(write_gp0(&fixture, 0));
    fixture.fill_value = 0;
    CHECK(!write_gp0(&fixture, 0x00010010u));
    CHECK(fixture.fill_calls == 1);
    CHECK(fixture.controller.fill_words == 2);
    CHECK(fixture.controller.accepted_gp0_words == 2);
    CHECK(fixture.controller.faulted);

    CHECK(fixture_init(&fixture, 1));
    fixture.controller.accepted_gp0_words = UINT64_MAX;
    CHECK(!write_gp0(&fixture, 0x020000ffu));
    CHECK(fixture.controller.fill_words == 0 && fixture.fill_calls == 0);

    CHECK(fixture_init(&fixture, 1));
    fixture.reenter_fill = 1;
    fixture.controller.backend.userdata = &fixture;
    CHECK(write_gp0(&fixture, 0x020000ffu));
    CHECK(write_gp0(&fixture, 0));
    CHECK(!write_gp0(&fixture, 0x00010010u));
    CHECK(fixture.nested_result == 0);
    CHECK(fixture.fill_calls == 1);
    CHECK(fixture.controller.completed_fills == 1);
    CHECK(fixture.controller.accepted_gp0_words == 3);
    CHECK(fixture.controller.fill_words == 0);
    CHECK(fixture.controller.faulted);

    CHECK(fixture_init(&fixture, 1));
    CHECK(write_gp0(&fixture, 0x020000ffu));
    fixture.reset_value = 0;
    CHECK(!write_gp1(&fixture, 0));
    CHECK(fixture.controller.fill_words == 1);
    CHECK(fixture.controller.faulted);

    CHECK(fixture_init(&fixture, 1));
    CHECK(write_gp0(&fixture, 0x020000ffu));
    fixture.clear_value = 0;
    CHECK(!write_gp1(&fixture, 0x01000000u));
    CHECK(fixture.controller.fill_words == 1);
    CHECK(fixture.controller.faulted);

    CHECK(fixture_init(&fixture, 1));
    CHECK(write_gp0(&fixture, 0x020000ffu));
    fixture.vram[0] = UINT16_C(0x5a5a);
    accepted = fixture.controller.accepted_gp0_words;
    CHECK(write_gp1(&fixture, 0));
    CHECK(fixture.controller.fill_words == 0);
    CHECK(fixture.controller.accepted_gp0_words == accepted);
    CHECK(fixture.vram[0] == UINT16_C(0x5a5a));
    return 1;
}

int main(void) {
    if (!test_full_fill_descriptor_and_wrap() ||
        !test_color_quantization_and_zero_height() ||
        !test_partial_status_data_and_resets() ||
        !test_refusals_and_counter_preflight())
        return 1;
    puts("GPU_FILL_CONTROLLER_PASS full_wrap=1 quantization=1 partial_reset=1 refusals=1");
    return 0;
}
