#include "musashi_gpu_controller.h"
#include <stdio.h>
#include <stdlib.h>

#define GPU_RESET_READ_LATCH 0x400u
#define GPU_STATUS_BASE 0x00802000u
#define GPU_STATUS_DMA_REQUEST (1u << 25)
#define GPU_STATUS_READY_COMMAND (1u << 26)
#define GPU_STATUS_READY_DMA (1u << 28)
#define GPU_STATUS_VBLANK_PARITY (1u << 31)

/* PSX-SPX Quick Rectangle Fill and fill masking. Pinned PCSX command
 * parser is a state-machine aid; its clipping quirks are not reproduced. */
int musashi_gpu_fill_derive(MusashiGpuFill *fill) {
    uint32_t color;
    if (!fill || (fill->command >> 24) != 2u) return 0;
    color = fill->command;
    fill->x = (uint16_t)(fill->xy & 0x3f0u);
    fill->y = (uint16_t)((fill->xy >> 16) & 0x1ffu);
    fill->width = (uint16_t)(((fill->wh & 0x3ffu) + 15u) & ~15u);
    fill->height = (uint16_t)((fill->wh >> 16) & 0x1ffu);
    fill->color = (uint16_t)(((color >> 3) & 0x1fu) |
        ((color >> 6) & 0x3e0u) | ((color >> 9) & 0x7c00u));
    return 1;
}

static void discard_fill(MusashiGpuController *controller) {
    controller->fill_command = controller->fill_xy = 0;
    controller->fill_words = 0;
}

static void discard_store(MusashiGpuController *controller) {
    controller->store_command = controller->store_xy = controller->store_wh = 0;
    controller->store_x = controller->store_y = 0;
    controller->store_w = controller->store_h = 0;
    controller->store_px = controller->store_py = 0;
    controller->store_remaining = 0;
    controller->store_phase = 0;
}

static void discard_copy(MusashiGpuController *controller) {
    controller->copy_command = controller->copy_src = controller->copy_dst = 0;
    controller->copy_words = 0;
}

static void discard_prim(MusashiGpuController *controller) {
    unsigned i;
    controller->prim_needed = controller->prim_got = 0;
    for (i = 0; i < 12u; ++i) controller->prim_data[i] = 0;
}

static void discard_poly(MusashiGpuController *controller) {
    controller->poly_active = controller->poly_gouraud = 0;
    controller->poly_words = controller->poly_vertices = 0;
    controller->poly_command = controller->poly_color = controller->poly_prev_color = 0;
    controller->poly_x = controller->poly_y = 0;
}

#define MAX_STORE_IMAGE_INSTANCES 16

typedef struct MusashiGpuStoreImage {
    const MusashiGpuController *controller;
    uint32_t command;
    uint32_t xy;
    uint32_t wh;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t total_halfwords;
    uint32_t total_words;
    uint32_t words_read;
    uint8_t phase;
    int active;
    uint32_t *buffer;
    size_t capacity;
} MusashiGpuStoreImage;

static MusashiGpuStoreImage s_store_images[MAX_STORE_IMAGE_INSTANCES];

static MusashiGpuStoreImage *get_store_image(const MusashiGpuController *controller, int create) {
    int empty_idx = -1;
    int i;
    if (!controller) return NULL;
    for (i = 0; i < MAX_STORE_IMAGE_INSTANCES; ++i) {
        if (s_store_images[i].controller == controller) {
            return &s_store_images[i];
        }
        if (empty_idx < 0 && s_store_images[i].controller == NULL) {
            empty_idx = i;
        }
    }
    if (!create) return NULL;
    if (empty_idx < 0) {
        for (i = 0; i < MAX_STORE_IMAGE_INSTANCES; ++i) {
            if (!s_store_images[i].active && s_store_images[i].phase == 0) {
                empty_idx = i;
                break;
            }
        }
        if (empty_idx < 0) empty_idx = 0;
    }
    s_store_images[empty_idx].controller = controller;
    s_store_images[empty_idx].command = 0;
    s_store_images[empty_idx].xy = 0;
    s_store_images[empty_idx].wh = 0;
    s_store_images[empty_idx].x = 0;
    s_store_images[empty_idx].y = 0;
    s_store_images[empty_idx].width = 0;
    s_store_images[empty_idx].height = 0;
    s_store_images[empty_idx].total_halfwords = 0;
    s_store_images[empty_idx].total_words = 0;
    s_store_images[empty_idx].words_read = 0;
    s_store_images[empty_idx].phase = 0;
    s_store_images[empty_idx].active = 0;
    return &s_store_images[empty_idx];
}

static void discard_store_image(const MusashiGpuController *controller) {
    int i;
    for (i = 0; i < MAX_STORE_IMAGE_INSTANCES; ++i) {
        if (s_store_images[i].controller == controller) {
            s_store_images[i].command = 0;
            s_store_images[i].xy = 0;
            s_store_images[i].wh = 0;
            s_store_images[i].x = 0;
            s_store_images[i].y = 0;
            s_store_images[i].width = 0;
            s_store_images[i].height = 0;
            s_store_images[i].total_halfwords = 0;
            s_store_images[i].total_words = 0;
            s_store_images[i].words_read = 0;
            s_store_images[i].phase = 0;
            s_store_images[i].active = 0;
            return;
        }
    }
}

static int store_image_in_header(const MusashiGpuController *controller) {
    MusashiGpuStoreImage *si;
    if (!controller) return 0;
    si = get_store_image(controller, 0);
    return si && (si->phase == 1 || si->phase == 2);
}

static int store_image_has_data(const MusashiGpuController *controller) {
    MusashiGpuStoreImage *si;
    if (!controller) return 0;
    si = get_store_image(controller, 0);
    return si && si->active && si->words_read < si->total_words;
}

int musashi_gpu_controller_get_store_image(const MusashiGpuController *controller,
                                           uint16_t *x, uint16_t *y,
                                           uint16_t *width, uint16_t *height,
                                           uint32_t *total_halfwords);
int musashi_gpu_controller_store_image_phase(const MusashiGpuController *controller);
uint32_t musashi_gpu_controller_store_image_remaining_words(const MusashiGpuController *controller);
int musashi_gpu_controller_read_store_image_buffer(MusashiGpuController *controller,
                                                  uint32_t *out_words, uint32_t max_words,
                                                  uint32_t *actual_words);

static int backend_bound(const MusashiGpuController *controller) {
    return controller && controller->backend.reset &&
           controller->backend.draw_mode &&
           controller->backend.display_enable &&
           controller->backend.clear_fifo && controller->backend.ready;
}

static int enter_callback(MusashiGpuController *controller) {
    if (controller->executing) {
        controller->faulted = 1;
        return 0;
    }
    controller->executing = 1;
    return 1;
}

static int finish_callback(MusashiGpuController *controller, int accepted) {
    controller->executing = 0;
    if (!accepted) controller->faulted = 1;
    return accepted > 0;
}

static void display_reset_state(MusashiGpuController *controller);

static int backend_ready(MusashiGpuController *controller, int *ready) {
    int state;
    if (!backend_bound(controller) || !ready) return 0;
    if (!enter_callback(controller)) return 0;
    state = controller->backend.ready(controller->backend.userdata);
    if (!finish_callback(controller, state >= 0) || controller->faulted)
        return 0;
    *ready = state > 0;
    return 1;
}

static void reset_state(MusashiGpuController *controller) {
    controller->read_latch = GPU_RESET_READ_LATCH;
    controller->draw_mode = 0;
    controller->dma_direction = 0;
    controller->display_disabled = 1;
    controller->executing = 0;
    controller->vblank_parity = 0;
    controller->texture_window = 0;
    controller->drawing_area_start = 0;
    controller->drawing_area_end = 0;
    controller->drawing_offset = 0;
    controller->mask_flags = 0;
    display_reset_state(controller);
    discard_fill(controller);
    discard_store(controller);
    discard_copy(controller);
    discard_prim(controller);
    discard_poly(controller);
    discard_store_image(controller);
}

static int backend_reset(MusashiGpuController *controller) {
    int accepted;
    if (!enter_callback(controller)) return 0;
    accepted = controller->backend.reset(controller->backend.userdata);
    return finish_callback(controller, accepted);
}

static int backend_draw_mode(MusashiGpuController *controller, uint32_t word) {
    int accepted;
    if (!enter_callback(controller)) return 0;
    accepted = controller->backend.draw_mode(controller->backend.userdata, word);
    return finish_callback(controller, accepted);
}

static int backend_display_enable(MusashiGpuController *controller, int enabled) {
    int accepted;
    if (!enter_callback(controller)) return 0;
    accepted = controller->backend.display_enable(
        controller->backend.userdata, enabled);
    return finish_callback(controller, accepted);
}

static int backend_clear_fifo(MusashiGpuController *controller) {
    int accepted;
    if (!enter_callback(controller)) return 0;
    accepted = controller->backend.clear_fifo(controller->backend.userdata);
    return finish_callback(controller, accepted);
}

static int backend_environment(MusashiGpuController *controller,
                               uint32_t word) {
    int accepted;
    if (!controller->backend.environment) return 0;
    if (!enter_callback(controller)) return 0;
    accepted = controller->backend.environment(
        controller->backend.userdata, word);
    return finish_callback(controller, accepted);
}

static int display_mode_divisor(uint32_t mode, uint8_t *divisor) {
    mode &= 0xffu;
    /* Bits 2/5 are 480-line and interlace; they do not change the dot clock.
     * PAL, 24-bit, and reverse remain refused. */
    if ((mode & 0x18u) != 0 || (mode & 0x80u) != 0 || !divisor)
        return 0;
    if ((mode & 0x40u) != 0) {
        if ((mode & 3u) != 0) return 0;
        *divisor = 7;
        return 1;
    }
    switch (mode & 3u) {
    case 0: *divisor = 10; return 1;
    case 1: *divisor = 8; return 1;
    case 2: *divisor = 5; return 1;
    case 3: *divisor = 4; return 1;
    default: return 0;
    }
}

int musashi_gpu_display_derive(MusashiGpuDisplayState *state) {
    uint8_t divisor;
    uint32_t width;
    uint32_t height;

    if (!state || !display_mode_divisor(state->mode, &divisor)) return 0;
    state->origin &= 0x0007ffffu;
    state->horizontal &= 0x00ffffffu;
    state->vertical &= 0x000fffffu;
    state->mode &= 0x000000ffu;
    state->x = (uint16_t)(state->origin & 0x3ffu);
    state->y = (uint16_t)((state->origin >> 10) & 0x1ffu);
    state->h_start = (uint16_t)(state->horizontal & 0xfffu);
    state->h_end = (uint16_t)((state->horizontal >> 12) & 0xfffu);
    state->v_start = (uint16_t)(state->vertical & 0x3ffu);
    state->v_end = (uint16_t)((state->vertical >> 10) & 0x3ffu);
    if (state->h_end <= state->h_start || state->v_end <= state->v_start)
        return 0;
    if (state->v_start != 16 || state->v_end != 256) return 0;
    width = (((uint32_t)state->h_end - state->h_start) / divisor + 2u) & ~3u;
    height = (uint32_t)state->v_end - state->v_start;
    /* GP1(08) bits2+5 select 480-line interlaced scanout. The vertical
     * range counts scanlines per field; the host presents both fields. */
    if ((state->mode & 0x24u) == 0x24u) height *= 2u;
    if (width == 0 || width > 1024 || height == 0 || height > 512)
        return 0;
    state->dot_divisor = divisor;
    state->width = (uint16_t)width;
    state->height = (uint16_t)height;
    return 1;
}

static void display_reset_state(MusashiGpuController *controller) {
    controller->display.origin = 0;
    controller->display.horizontal = 0x00c00200u;
    controller->display.vertical = 0x00040010u;
    controller->display.mode = 0;
    (void)musashi_gpu_display_derive(&controller->display);
}

static int backend_display(MusashiGpuController *controller,
                           const MusashiGpuDisplayState *candidate) {
    int accepted;
    if (!controller->backend.display || !candidate ||
        !enter_callback(controller)) return 0;
    accepted = controller->backend.display(controller->backend.userdata,
                                           candidate);
    return finish_callback(controller, accepted);
}

static int display_commit(MusashiGpuController *controller,
                          MusashiGpuDisplayState *candidate) {
    int ready;
    if (!controller->backend.display || !musashi_gpu_display_derive(candidate)) return 0;
    if (!backend_ready(controller, &ready) || !ready) return 0;
    if (!backend_display(controller, candidate)) return 0;
    controller->display = *candidate;
    return controller->faulted ? 0 : 1;
}

static int display_write(MusashiGpuController *controller, uint32_t opcode,
                         uint32_t value) {
    MusashiGpuDisplayState candidate;
    if (!controller->backend.display) return 0;
    candidate = controller->display;
    switch (opcode) {
    case 0x05u: candidate.origin = value & 0x0007ffffu; break;
    case 0x06u: candidate.horizontal = value & 0x00ffffffu; break;
    case 0x07u: candidate.vertical = value & 0x000fffffu; break;
    case 0x08u: candidate.mode = value & 0x000000ffu; break;
    default: return 0;
    }
    return display_commit(controller, &candidate);
}

int musashi_gpu_controller_init(MusashiGpuController *controller,
                                const MusashiGpuBackend *backend) {
    MusashiGpuBackend selected;
    int accepted;

    if (!controller || !backend || !backend->reset || !backend->draw_mode ||
        !backend->display_enable || !backend->clear_fifo || !backend->ready)
        return 0;
    if (controller->executing) {
        controller->faulted = 1;
        return 0;
    }
    selected = *backend;
    /* Reserve the instance while the external reset callback runs. A nested
     * init or MMIO operation therefore becomes a detected reentry fault and
     * cannot publish a half-initialized backend. */
    controller->executing = 1;
    controller->faulted = 0;
    accepted = selected.reset(selected.userdata);
    if (!accepted || controller->faulted) {
        int reentered = controller->faulted;
        controller->executing = 0;
        controller->faulted = reentered ? 1 : 0;
        return 0;
    }
    controller->backend = selected;
    controller->faulted = 0;
    reset_state(controller);
    controller->accepted_gp0_words = 0;
    controller->stored_pixels = 0;
    discard_store(controller);
    discard_store_image(controller);
    return 1;
}

static uint32_t status_word(const MusashiGpuController *controller, int ready) {
    uint32_t status = GPU_STATUS_BASE;
    uint32_t dma_request = 0;

    status &= ~0x00800000u;
    if (controller->display_disabled) status |= 0x00800000u;
    status = (status & ~0x7ffu) | (controller->draw_mode & 0x7ffu);
    if (controller->draw_mode & 0x800u) status |= 0x8000u;
    status &= ~((1u << 14) | (1u << 16) | (7u << 17) | (0xfu << 19));
    status |= (controller->display.mode & 0x40u) ? (1u << 16) : 0u;
    status |= (controller->display.mode & 3u) << 17;
    status |= ((controller->display.mode >> 2) & 0xfu) << 19;
    status &= ~((1u << 11) | (1u << 12));
    status |= (controller->mask_flags & 3u) << 11;
    status = (status & ~(3u << 29)) |
             ((controller->dma_direction & 3u) << 29);
    if (!controller->executing && ready) {
        status |= GPU_STATUS_READY_DMA;
        if (!controller->fill_words && !controller->store_phase &&
            !controller->prim_needed && !controller->copy_words &&
            !store_image_in_header(controller))
            status |= GPU_STATUS_READY_COMMAND;
    }
    if (controller->dma_direction == 1u)
        dma_request = (uint32_t)ready;
    else if (controller->dma_direction == 2u)
        dma_request = (!controller->executing && ready) ? 1u : 0u;
    else if (controller->dma_direction == 3u)
        dma_request = (store_image_has_data(controller) && ready) ? 1u : 0u;
    if (store_image_has_data(controller))
        status |= (1u << 27);
    if (dma_request) status |= GPU_STATUS_DMA_REQUEST;
    if (controller->vblank_parity) status |= GPU_STATUS_VBLANK_PARITY;
    return status;
}

int musashi_gpu_controller_vblank(MusashiGpuController *controller) {
    int ready;

    if (!controller) return 0;
    if (controller->executing) {
        controller->faulted = 1;
        return 0;
    }
    if (!backend_bound(controller) || controller->faulted) return 0;
    if (!backend_ready(controller, &ready)) return 0;
    (void)ready;
    controller->vblank_parity ^= 1u;
    return 1;
}

int musashi_gpu_controller_read32(MusashiGpuController *controller,
                                   uint32_t address, uint32_t *value) {
    int ready;

    if (!controller || !value) return 0;
    if (controller->executing) {
        controller->faulted = 1;
        return 0;
    }
    if (!backend_bound(controller) || controller->faulted) return 0;
    if (address == MUSASHI_GPU_GPUREAD) {
        MusashiGpuStoreImage *si = get_store_image(controller, 0);
        if (si && si->active && si->words_read < si->total_words) {
            uint32_t word = si->buffer[si->words_read++];
            controller->read_latch = word;
            if (si->words_read >= si->total_words) {
                si->active = 0;
                si->phase = 0;
            }
            *value = word;
            return 1;
        }
        *value = controller->read_latch;
        return 1;
    }
    if (address != MUSASHI_GPU_GPUSTAT) return 0;
    if (!backend_ready(controller, &ready)) return 0;
    *value = status_word(controller, ready);
    return 1;
}

static int fill_word(MusashiGpuController *controller, uint32_t word) {
    MusashiGpuFill fill = {0};
    uint64_t pixels;
    int ready, accepted;
    if (!controller->backend.fill_vram || controller->fill_words > 2u ||
        controller->accepted_gp0_words == UINT64_MAX) return 0;
    if (controller->fill_words == 2u) {
        fill.command = controller->fill_command;
        fill.xy = controller->fill_xy;
        fill.wh = word;
        if (!musashi_gpu_fill_derive(&fill)) return 0;
        pixels = (uint64_t)fill.width * fill.height;
        if (controller->completed_fills == UINT64_MAX ||
            controller->filled_pixels > UINT64_MAX - pixels) return 0;
    } else pixels = 0;
    if (!backend_ready(controller, &ready) || !ready) return 0;
    if (controller->fill_words == 0u) {
        controller->fill_command = word;
        controller->fill_words = 1;
    } else if (controller->fill_words == 1u) {
        controller->fill_xy = word;
        controller->fill_words = 2;
    } else {
        if (!enter_callback(controller)) return 0;
        accepted = controller->backend.fill_vram(controller->backend.userdata, &fill);
        if (!finish_callback(controller, accepted)) return 0;
        /* Backend acceptance is an effect even when a nested operation has
         * faulted the controller. DMA observes the monotonic accepted count. */
        controller->last_fill = fill;
        controller->completed_fills++;
        controller->filled_pixels += pixels;
        discard_fill(controller);
    }
    controller->accepted_gp0_words++;
    return controller->faulted ? 0 : 1;
}

static int store_word(MusashiGpuController *controller, uint32_t word) {
    uint16_t pixels[2];
    unsigned count, i;
    int ready;
    if (!controller->backend.store_vram ||
        controller->accepted_gp0_words == UINT64_MAX) return 0;
    if (controller->store_phase == 0) {
        if ((word >> 24) != 0xa0u) return 0;
        controller->store_command = word;
        controller->store_phase = 1;
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }
    if (controller->store_phase == 1) {
        controller->store_xy = word;
        controller->store_phase = 2;
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }
    if (controller->store_phase == 2) {
        uint32_t width = word & 0xffffu;
        uint32_t height = word >> 16;
        if (!width) width = 1024u;
        if (!height) height = 512u;
        if (width > 1024u || height > 512u) return 0;
        controller->store_wh = word;
        controller->store_x = (uint16_t)(controller->store_xy & 0x3ffu);
        controller->store_y = (uint16_t)((controller->store_xy >> 16) & 0x1ffu);
        controller->store_w = (uint16_t)width;
        controller->store_h = (uint16_t)height;
        controller->store_px = controller->store_py = 0;
        controller->store_remaining = width * height;
        controller->store_phase = 3;
        controller->accepted_gp0_words++;
        if (!controller->store_remaining) discard_store(controller);
        return controller->faulted ? 0 : 1;
    }
    if (controller->store_phase != 3 || !controller->store_remaining) return 0;
    if (!backend_ready(controller, &ready) || !ready) return 0;
    pixels[0] = (uint16_t)word;
    pixels[1] = (uint16_t)(word >> 16);
    count = controller->store_remaining >= 2u ? 2u : 1u;
    for (i = 0; i < count; ++i) {
        uint16_t x = (uint16_t)((controller->store_x + controller->store_px) & 1023u);
        uint16_t y = (uint16_t)((controller->store_y + controller->store_py) & 511u);
        if (!enter_callback(controller)) return 0;
        if (!finish_callback(controller,
                controller->backend.store_vram(controller->backend.userdata,
                                               x, y, pixels[i])))
            return 0;
        if (controller->stored_pixels == UINT64_MAX) return 0;
        controller->stored_pixels++;
        controller->store_remaining--;
        controller->store_px++;
        if (controller->store_px >= controller->store_w) {
            controller->store_px = 0;
            controller->store_py++;
        }
    }
    controller->accepted_gp0_words++;
    if (!controller->store_remaining) discard_store(controller);
    return controller->faulted ? 0 : 1;
}

static int store_image_word(MusashiGpuController *controller, uint32_t word) {
    int ready;
    MusashiGpuStoreImage *si;
    uint32_t opcode;

    if (!controller->backend.read_vram ||
        controller->accepted_gp0_words == UINT64_MAX) return 0;

    si = get_store_image(controller, 1);
    if (!si) return 0;

    if (si->phase == 0 || si->phase == 3) {
        opcode = word >> 24;
        if ((opcode & 0xe0u) != 0xc0u) return 0;
        si->command = word;
        si->phase = 1;
        si->active = 0;
        si->words_read = 0;
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }

    if (si->phase == 1) {
        si->xy = word;
        si->phase = 2;
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }

    if (si->phase == 2) {
        uint32_t width = word & 0xffffu;
        uint32_t height = (word >> 16) & 0xffffu;
        uint32_t halfword_idx = 0;
        uint32_t row, col;

        if (!width) width = 1024u;
        if (!height) height = 512u;
        if (width > 1024u || height > 512u) return 0;

        if (!backend_ready(controller, &ready) || !ready) return 0;

        si->wh = word;
        si->x = (uint16_t)(si->xy & 0xffffu);
        si->y = (uint16_t)((si->xy >> 16) & 0xffffu);
        si->width = (uint16_t)width;
        si->height = (uint16_t)height;
        si->total_halfwords = width * height;
        si->total_words = (si->total_halfwords + 1u) / 2u;
        si->words_read = 0;

        if (si->total_words > si->capacity) {
            size_t new_cap = si->total_words < 1024u ? 1024u : (size_t)si->total_words;
            uint32_t *new_buf = (uint32_t *)realloc(si->buffer, new_cap * sizeof(uint32_t));
            if (!new_buf) return 0;
            si->buffer = new_buf;
            si->capacity = new_cap;
        }

        for (row = 0; row < height; ++row) {
            for (col = 0; col < width; ++col) {
                uint16_t pixel = 0;
                uint32_t vx = (uint32_t)si->x + col;
                uint32_t vy = (uint32_t)si->y + row;
                uint32_t word_idx;
                int accepted;

                if (vx < 1024u && vy < 512u) {
                    if (!enter_callback(controller)) return 0;
                    accepted = controller->backend.read_vram(
                        controller->backend.userdata, (uint16_t)vx, (uint16_t)vy, &pixel);
                    if (!finish_callback(controller, accepted > 0) || controller->faulted)
                        return 0;
                }

                word_idx = halfword_idx / 2u;
                if ((halfword_idx & 1u) == 0) {
                    si->buffer[word_idx] = (uint32_t)pixel;
                } else {
                    si->buffer[word_idx] |= ((uint32_t)pixel << 16);
                }
                halfword_idx++;
            }
        }

        si->phase = 3;
        si->active = (si->total_words > 0) ? 1 : 0;
        if (si->total_words > 0) {
            controller->read_latch = si->buffer[0];
        }
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }

    return 0;
}

int musashi_gpu_controller_get_store_image(const MusashiGpuController *controller,
                                           uint16_t *x, uint16_t *y,
                                           uint16_t *width, uint16_t *height,
                                           uint32_t *total_halfwords) {
    MusashiGpuStoreImage *si;
    if (!controller) return 0;
    si = get_store_image(controller, 0);
    if (!si || si->phase < 3) return 0;
    if (x) *x = si->x;
    if (y) *y = si->y;
    if (width) *width = si->width;
    if (height) *height = si->height;
    if (total_halfwords) *total_halfwords = si->total_halfwords;
    return 1;
}

int musashi_gpu_controller_store_image_phase(const MusashiGpuController *controller) {
    MusashiGpuStoreImage *si;
    if (!controller) return 0;
    si = get_store_image(controller, 0);
    return si ? (int)si->phase : 0;
}

uint32_t musashi_gpu_controller_store_image_remaining_words(const MusashiGpuController *controller) {
    MusashiGpuStoreImage *si;
    if (!controller) return 0;
    si = get_store_image(controller, 0);
    if (!si || !si->active || si->words_read >= si->total_words) return 0;
    return si->total_words - si->words_read;
}

int musashi_gpu_controller_read_store_image_buffer(MusashiGpuController *controller,
                                                  uint32_t *out_words, uint32_t max_words,
                                                  uint32_t *actual_words) {
    MusashiGpuStoreImage *si;
    uint32_t remaining, count, i;
    if (!controller || !out_words) return 0;
    si = get_store_image(controller, 0);
    if (!si || !si->active) return 0;
    remaining = si->total_words - si->words_read;
    count = remaining < max_words ? remaining : max_words;
    for (i = 0; i < count; ++i) {
        out_words[i] = si->buffer[si->words_read++];
    }
    if (count > 0) {
        controller->read_latch = out_words[count - 1];
    }
    if (si->words_read >= si->total_words) {
        si->active = 0;
        si->phase = 0;
    }
    if (actual_words) *actual_words = count;
    return 1;
}

/* Ordering follows DuckStation gpu_sw_rasterizer.inl CopyVRAMImpl:
 * rows advance, columns reverse for rightward copies. Its wrapped rectangle
 * split is an emulator reference, explicitly not console-verified there.
 * https://github.com/stenzek/duckstation/blob/master/src/core/gpu_sw_rasterizer.inl
 * PSX-SPX GPU Memory Transfer Commands supplies coordinate/size and mask rules.
 */
static int copy_rectangle(MusashiGpuController *c, unsigned sx, unsigned sy,
                          unsigned dx, unsigned dy, unsigned w, unsigned h) {
    unsigned row, step;
    for (row = 0; row < h; ++row) {
        for (step = 0; step < w; ++step) {
            unsigned col = sx < dx ? w - 1u - step : step;
            uint16_t pixel, destination;
            int accepted;
            if (!enter_callback(c)) return 0;
            accepted = c->backend.read_vram(c->backend.userdata,
                (uint16_t)(sx + col), (uint16_t)((sy + row) & 511u), &pixel);
            if (!finish_callback(c, accepted > 0) || c->faulted) return 0;
            if (c->mask_flags & 2u) {
                if (!enter_callback(c)) return 0;
                accepted = c->backend.read_vram(c->backend.userdata,
                    (uint16_t)(dx + col), (uint16_t)((dy + row) & 511u), &destination);
                if (!finish_callback(c, accepted > 0) || c->faulted) return 0;
                if (destination & 0x8000u) continue;
            }
            if (c->mask_flags & 1u) pixel |= 0x8000u;
            if (!enter_callback(c)) return 0;
            accepted = c->backend.store_vram(c->backend.userdata,
                (uint16_t)(dx + col), (uint16_t)((dy + row) & 511u), pixel);
            if (!finish_callback(c, accepted > 0)) return 0;
            c->copied_pixels++;
            if (c->faulted) return 0;
        }
    }
    return 1;
}

static int copy_word(MusashiGpuController *c, uint32_t word) {
    unsigned sx, sy, dx, dy, width, height, ry, rx;
    int ready;
    if (!c->backend.read_vram || !c->backend.store_vram ||
        c->accepted_gp0_words == UINT64_MAX) return 0;
    if (c->copy_words < 3u) {
        if (!c->copy_words) c->copy_command = word;
        else if (c->copy_words == 1u) c->copy_src = word;
        else c->copy_dst = word;
        c->copy_words++;
        c->accepted_gp0_words++;
        return 1;
    }
    width = ((word - 1u) & 1023u) + 1u;
    height = (((word >> 16) - 1u) & 511u) + 1u;
    if (c->completed_copies == UINT64_MAX ||
        c->copied_pixels > UINT64_MAX - (uint64_t)width * height) return 0;
    if (!backend_ready(c, &ready) || !ready) return 0;
    sx = c->copy_src & 1023u;
    sy = (c->copy_src >> 16) & 511u;
    dx = c->copy_dst & 1023u;
    dy = (c->copy_dst >> 16) & 511u;
    if (sx + width <= 1024u && dx + width <= 1024u) {
        if (!copy_rectangle(c, sx, sy, dx, dy, width, height)) return 0;
    } else {
        for (ry = 0; ry < height;) {
            unsigned ys = (sy + ry) & 511u, yd = (dy + ry) & 511u;
            unsigned rows = height - ry;
            if (rows > 512u - ys) rows = 512u - ys;
            if (rows > 512u - yd) rows = 512u - yd;
            for (rx = 0; rx < width;) {
                unsigned xs = (sx + rx) & 1023u, xd = (dx + rx) & 1023u;
                unsigned cols = width - rx;
                if (cols > 1024u - xs) cols = 1024u - xs;
                if (cols > 1024u - xd) cols = 1024u - xd;
                if (!copy_rectangle(c, xs, ys, xd, yd, cols, rows)) return 0;
                rx += cols;
            }
            ry += rows;
        }
    }
    c->accepted_gp0_words++;
    c->completed_copies++;
    discard_copy(c);
    return 1;
}

static int16_t offset11(uint32_t raw, unsigned shift) {
    uint32_t field = (raw >> shift) & 0x7ffu;
    return (int16_t)((field ^ 0x400u) - 0x400u);
}

static int clip_box(const MusashiGpuController *controller,
                    int *x0, int *y0, int *x1, int *y1) {
    int left = (int)(controller->drawing_area_start & 0x3ffu);
    int top = (int)((controller->drawing_area_start >> 10) & 0x1ffu);
    int right = (int)(controller->drawing_area_end & 0x3ffu);
    int bottom = (int)((controller->drawing_area_end >> 10) & 0x1ffu);
    if (right < left) { left = 0; right = 1023; }
    if (bottom < top) { top = 0; bottom = 511; }
    if (*x0 < left) *x0 = left;
    if (*y0 < top) *y0 = top;
    if (*x1 > right) *x1 = right;
    if (*y1 > bottom) *y1 = bottom;
    return *x0 <= *x1 && *y0 <= *y1;
}

/* PS1 semi-transparency, per 5-bit channel of back (B) and front (F):
 * 0: B/2+F/2, 1: B+F, 2: B-F, 3: B+F/4, clamped to 0..31. */
static uint16_t semi_blend(uint16_t back, uint16_t front, unsigned mode) {
    uint16_t out = 0;
    unsigned shift;
    for (shift = 0; shift < 15u; shift += 5u) {
        int b = (back >> shift) & 31, f = (front >> shift) & 31, c;
        switch (mode & 3u) {
        case 0: c = (b + f) >> 1; break;
        case 1: c = b + f; break;
        case 2: c = b - f; break;
        default: c = b + (f >> 2); break;
        }
        if (c < 0) c = 0;
        if (c > 31) c = 31;
        out |= (uint16_t)(c << shift);
    }
    return out;
}

/* color bit 15 is the texel's STP bit for textured primitives.
 * E6 (mask_flags): bit 1 skips pixels whose destination has bit 15 set;
 * bit 0 forces bit 15 on stored pixels. Otherwise a textured pixel keeps its
 * texel's STP bit and an untextured one stores bit 15 clear (psx-spx). */
static int plot_drawn(MusashiGpuController *controller, int x, int y,
                      uint16_t color) {
    uint16_t stp = controller->semi_textured ? (uint16_t)(color & 0x8000u) : 0u;
    int blend = controller->semi && (!controller->semi_textured || (color & 0x8000u));
    if (x < 0 || y < 0 || x > 1023 || y > 511) return 1;
    if (!controller->backend.store_vram) return 0;
    if (blend || (controller->mask_flags & 2u)) {
        uint16_t back = 0;
        if (!controller->backend.read_vram ||
            !controller->backend.read_vram(controller->backend.userdata,
                                           (uint16_t)x, (uint16_t)y, &back))
            return 0;
        if ((controller->mask_flags & 2u) && (back & 0x8000u)) return 1;
        if (blend) color = semi_blend(back, color, controller->semi_mode);
    }
    color = (uint16_t)((color & 0x7fffu) | stp | ((controller->mask_flags & 1u) ? 0x8000u : 0u));
    if (!enter_callback(controller)) return 0;
    if (!finish_callback(controller,
            controller->backend.store_vram(controller->backend.userdata,
                                           (uint16_t)x, (uint16_t)y, color)))
        return 0;
    if (controller->drawn_pixels == UINT64_MAX) return 0;
    controller->drawn_pixels++;
    return controller->faulted ? 0 : 1;
}

static int fill_triangle(MusashiGpuController *controller,
                         int x0, int y0, int x1, int y1, int x2, int y2,
                         uint16_t color) {
    int minx = x0, miny = y0, maxx = x0, maxy = y0, x, y;
    long long area;
    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    if (!clip_box(controller, &minx, &miny, &maxx, &maxy)) return 1;
    area = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
    if (!area) return 1;
    for (y = miny; y <= maxy; ++y) {
        for (x = minx; x <= maxx; ++x) {
            long long w0 = (long long)(x1 - x) * (y2 - y) -
                           (long long)(y1 - y) * (x2 - x);
            long long w1 = (long long)(x2 - x) * (y0 - y) -
                           (long long)(y2 - y) * (x0 - x);
            long long w2 = (long long)(x0 - x) * (y1 - y) -
                           (long long)(y0 - y) * (x1 - x);
            if (area > 0) {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            } else if (w0 > 0 || w1 > 0 || w2 > 0) continue;
            if (!plot_drawn(controller, x, y, color)) return 0;
        }
    }
    return 1;
}

static int sample_texel(MusashiGpuController *controller,
                        unsigned fmt, unsigned tx, unsigned ty,
                        unsigned clut_x, unsigned clut_y,
                        unsigned u, unsigned v, uint16_t *out) {
    uint16_t packed = 0;
    unsigned sx, sy, index;
    if (!controller->backend.read_vram) return 0;
    if (fmt == 2u) {
        sx = tx + (u & 255u);
        sy = ty + (v & 255u);
        return controller->backend.read_vram(controller->backend.userdata,
                (uint16_t)(sx & 1023u), (uint16_t)(sy & 511u), out);
    } else if (fmt == 1u) {
        sx = tx + ((u & 255u) >> 1);
        sy = ty + (v & 255u);
        if (!controller->backend.read_vram(controller->backend.userdata,
                (uint16_t)(sx & 1023u), (uint16_t)(sy & 511u), &packed))
            return 0;
        index = (u & 1u) ? (packed >> 8) : (packed & 0xffu);
        return controller->backend.read_vram(controller->backend.userdata,
                (uint16_t)((clut_x + index) & 1023u),
                (uint16_t)(clut_y & 511u), out);
    } else if (fmt == 0u) {
        sx = tx + ((u & 255u) >> 2);
        sy = ty + (v & 255u);
        if (!controller->backend.read_vram(controller->backend.userdata,
                (uint16_t)(sx & 1023u), (uint16_t)(sy & 511u), &packed))
            return 0;
        index = (packed >> ((u & 3u) * 4u)) & 15u;
        return controller->backend.read_vram(controller->backend.userdata,
                (uint16_t)((clut_x + index) & 1023u),
                (uint16_t)(clut_y & 511u), out);
    }
    return 0;
}

/* Texture colour modulation: channel * rgb / 128, clamped; 0x808080 is
 * identity. Keeps the texel's STP bit. */
static uint16_t modulate_texel(uint16_t texel, uint32_t rgb) {
    uint32_t r, g, b;
    if ((rgb & 0xffffffu) == 0x808080u) return texel;
    r = (((texel & 0x1fu) * (rgb & 0xffu)) >> 7);
    g = ((((texel >> 5) & 0x1fu) * ((rgb >> 8) & 0xffu)) >> 7);
    b = ((((texel >> 10) & 0x1fu) * ((rgb >> 16) & 0xffu)) >> 7);
    if (r > 31u) r = 31u;
    if (g > 31u) g = 31u;
    if (b > 31u) b = 31u;
    return (uint16_t)(r | (g << 5) | (b << 10) | (texel & 0x8000u));
}

static int fill_textured_triangle(MusashiGpuController *controller,
                                  int x0, int y0, unsigned u0, unsigned v0,
                                  int x1, int y1, unsigned u1, unsigned v1,
                                  int x2, int y2, unsigned u2, unsigned v2,
                                  unsigned fmt, unsigned tx, unsigned ty,
                                  unsigned clut_x, unsigned clut_y,
                                  uint32_t rgb, int raw_tex) {
    int minx = x0, miny = y0, maxx = x0, maxy = y0, x, y;
    long long area;
    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    if (!clip_box(controller, &minx, &miny, &maxx, &maxy)) return 1;
    area = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
    if (!area) return 1;
    for (y = miny; y <= maxy; ++y) {
        for (x = minx; x <= maxx; ++x) {
            long long w0 = (long long)(x1 - x) * (y2 - y) -
                           (long long)(y1 - y) * (x2 - x);
            long long w1 = (long long)(x2 - x) * (y0 - y) -
                           (long long)(y2 - y) * (x0 - x);
            long long w2 = (long long)(x0 - x) * (y1 - y) -
                           (long long)(y0 - y) * (x1 - x);
            uint16_t texel = 0;
            long long u_num, v_num;
            int u, v;
            if (area > 0) {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            } else if (w0 > 0 || w1 > 0 || w2 > 0) continue;
            u_num = (long long)w0 * (long long)u0 + (long long)w1 * (long long)u1 + (long long)w2 * (long long)u2;
            v_num = (long long)w0 * (long long)v0 + (long long)w1 * (long long)v1 + (long long)w2 * (long long)v2;
            u = (int)(u_num / area);
            v = (int)(v_num / area);
            if (!sample_texel(controller, fmt, tx, ty, clut_x, clut_y,
                              (unsigned)u & 255u, (unsigned)v & 255u, &texel))
                return 0;
            if (!texel) continue;
            if (!raw_tex) texel = modulate_texel(texel, rgb);
            if (!plot_drawn(controller, x, y, texel)) return 0;
        }
    }
    return 1;
}

static int fill_gouraud_triangle(MusashiGpuController *controller,
                                 int x0, int y0, uint32_t rgb0,
                                 int x1, int y1, uint32_t rgb1,
                                 int x2, int y2, uint32_t rgb2) {
    int minx = x0, miny = y0, maxx = x0, maxy = y0, x, y;
    long long area;
    long long r0 = rgb0 & 0xffu, g0 = (rgb0 >> 8) & 0xffu, b0 = (rgb0 >> 16) & 0xffu;
    long long r1 = rgb1 & 0xffu, g1 = (rgb1 >> 8) & 0xffu, b1 = (rgb1 >> 16) & 0xffu;
    long long r2 = rgb2 & 0xffu, g2 = (rgb2 >> 8) & 0xffu, b2 = (rgb2 >> 16) & 0xffu;

    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    if (!clip_box(controller, &minx, &miny, &maxx, &maxy)) return 1;
    area = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
    if (!area) return 1;

    for (y = miny; y <= maxy; ++y) {
        for (x = minx; x <= maxx; ++x) {
            long long w0 = (long long)(x1 - x) * (y2 - y) -
                           (long long)(y1 - y) * (x2 - x);
            long long w1 = (long long)(x2 - x) * (y0 - y) -
                           (long long)(y2 - y) * (x0 - x);
            long long w2 = (long long)(x0 - x) * (y1 - y) -
                           (long long)(y0 - y) * (x1 - x);
            long long r_num, g_num, b_num;
            int r, g, b;
            uint16_t color;

            if (area > 0) {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            } else if (w0 > 0 || w1 > 0 || w2 > 0) continue;

            r_num = w0 * r0 + w1 * r1 + w2 * r2;
            g_num = w0 * g0 + w1 * g1 + w2 * g2;
            b_num = w0 * b0 + w1 * b1 + w2 * b2;
            r = (int)(r_num / area);
            g = (int)(g_num / area);
            b = (int)(b_num / area);
            uint32_t ur = (uint32_t)(r < 0 ? 0 : r > 255 ? 255 : r);
            uint32_t ug = (uint32_t)(g < 0 ? 0 : g > 255 ? 255 : g);
            uint32_t ub = (uint32_t)(b < 0 ? 0 : b > 255 ? 255 : b);

            color = (uint16_t)(((ur >> 3) & 0x1fu) |
                               (((ug >> 3) & 0x1fu) << 5) |
                               (((ub >> 3) & 0x1fu) << 10));
            if (!plot_drawn(controller, x, y, color)) return 0;
        }
    }
    return 1;
}

static int fill_textured_gouraud_triangle(MusashiGpuController *controller,
                                          int x0, int y0, unsigned u0, unsigned v0, uint32_t rgb0,
                                          int x1, int y1, unsigned u1, unsigned v1, uint32_t rgb1,
                                          int x2, int y2, unsigned u2, unsigned v2, uint32_t rgb2,
                                          unsigned fmt, unsigned tx, unsigned ty,
                                          unsigned clut_x, unsigned clut_y,
                                          int raw_tex) {
    int minx = x0, miny = y0, maxx = x0, maxy = y0, x, y;
    long long area;
    long long r0 = rgb0 & 0xffu, g0 = (rgb0 >> 8) & 0xffu, b0 = (rgb0 >> 16) & 0xffu;
    long long r1 = rgb1 & 0xffu, g1 = (rgb1 >> 8) & 0xffu, b1 = (rgb1 >> 16) & 0xffu;
    long long r2 = rgb2 & 0xffu, g2 = (rgb2 >> 8) & 0xffu, b2 = (rgb2 >> 16) & 0xffu;

    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    if (!clip_box(controller, &minx, &miny, &maxx, &maxy)) return 1;
    area = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
    if (!area) return 1;

    for (y = miny; y <= maxy; ++y) {
        for (x = minx; x <= maxx; ++x) {
            long long w0 = (long long)(x1 - x) * (y2 - y) -
                           (long long)(y1 - y) * (x2 - x);
            long long w1 = (long long)(x2 - x) * (y0 - y) -
                           (long long)(y2 - y) * (x0 - x);
            long long w2 = (long long)(x0 - x) * (y1 - y) -
                           (long long)(y0 - y) * (x1 - x);
            uint16_t texel = 0;
            long long u_num, v_num;
            int u, v;

            if (area > 0) {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            } else if (w0 > 0 || w1 > 0 || w2 > 0) continue;

            u_num = (long long)w0 * (long long)u0 + (long long)w1 * (long long)u1 + (long long)w2 * (long long)u2;
            v_num = (long long)w0 * (long long)v0 + (long long)w1 * (long long)v1 + (long long)w2 * (long long)v2;
            u = (int)(u_num / area);
            v = (int)(v_num / area);
            if (!sample_texel(controller, fmt, tx, ty, clut_x, clut_y,
                              (unsigned)u & 255u, (unsigned)v & 255u, &texel))
                return 0;
            if (!texel) continue;
            if (!raw_tex) {
                long long r_num = w0 * r0 + w1 * r1 + w2 * r2;
                long long g_num = w0 * g0 + w1 * g1 + w2 * g2;
                long long b_num = w0 * b0 + w1 * b1 + w2 * b2;
                int cr = (int)(r_num / area);
                int cg = (int)(g_num / area);
                int cb = (int)(b_num / area);
                uint32_t r, g, b;
                if (cr < 0) cr = 0; else if (cr > 255) cr = 255;
                if (cg < 0) cg = 0; else if (cg > 255) cg = 255;
                if (cb < 0) cb = 0; else if (cb > 255) cb = 255;
                r = (((texel & 0x1fu) * (uint32_t)cr) >> 7);
                g = ((((texel >> 5) & 0x1fu) * (uint32_t)cg) >> 7);
                b = ((((texel >> 10) & 0x1fu) * (uint32_t)cb) >> 7);
                if (r > 31u) r = 31u;
                if (g > 31u) g = 31u;
                if (b > 31u) b = 31u;
                texel = (uint16_t)(r | (g << 5) | (b << 10) | (texel & 0x8000u));
            }
            if (!plot_drawn(controller, x, y, texel)) return 0;
        }
    }
    return 1;
}

static int fill_rect(MusashiGpuController *controller,
                     int x, int y, unsigned width, unsigned height,
                     uint16_t color) {
    int minx = x, miny = y, maxx = x + (int)width - 1, maxy = y + (int)height - 1;
    int px, py;
    if (!width || !height) return 1;
    if (!clip_box(controller, &minx, &miny, &maxx, &maxy)) return 1;
    for (py = miny; py <= maxy; ++py) {
        for (px = minx; px <= maxx; ++px) {
            if (!plot_drawn(controller, px, py, color)) return 0;
        }
    }
    return 1;
}

static int draw_line(MusashiGpuController *controller,
                     int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 >= x0 ? (x1 - x0) : (x0 - x1);
    int dy = y1 >= y0 ? (y1 - y0) : (y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, e2;
    int left = 0, top = 0, right = 1023, bottom = 511;
    /* The GPU skips a line whose extent is 1024+ horizontally or 512+
     * vertically (psx-spx), and clips to the drawing area like polygons. */
    if (dx >= 1024 || dy >= 512) return 1;
    if (!clip_box(controller, &left, &top, &right, &bottom)) return 1;
    for (;;) {
        if (x0 >= left && x0 <= right && y0 >= top && y0 <= bottom &&
            !plot_drawn(controller, x0, y0, color)) return 0;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
    return 1;
}

static int prim_commit(MusashiGpuController *controller) {
    uint32_t opcode = controller->prim_data[0] >> 24;
    if (controller->raster_disabled) return 1;   /* drawn by the bfm_plat bridge */
    /* Bit 1 of every polygon/line/rectangle command selects semi-
     * transparency. Textured polygons carry their own texpage (abr in bits
     * 5-6); lines, rectangles and untextured polygons use the draw mode. */
    controller->semi = (opcode >= 0x20u && opcode <= 0x7fu && (opcode & 2u)) ? 1u : 0u;
    controller->semi_textured = (uint8_t)(((opcode >= 0x20u && opcode <= 0x3fu) ||
                                           (opcode >= 0x60u && opcode <= 0x7fu)) && (opcode & 4u));
    if (opcode >= 0x24u && opcode <= 0x3fu && (opcode & 4u))
        controller->semi_mode = (uint8_t)((controller->prim_data[(opcode & 0x10u) ? 5 : 4] >> 21) & 3u);
    else
        controller->semi_mode = (uint8_t)((controller->draw_mode >> 5) & 3u);
    uint16_t color = (uint16_t)(((controller->prim_data[0] >> 3) & 0x1fu) |
        ((controller->prim_data[0] >> 6) & 0x3e0u) |
        ((controller->prim_data[0] >> 9) & 0x7c00u));
    int ox = offset11(controller->drawing_offset, 0);
    int oy = offset11(controller->drawing_offset, 11);
    int x[4], y[4], i;
    {
        static unsigned trace_count;
        if (getenv("MUSASHI_GPU_TRACE_PRIMITIVES") && trace_count++ < 128u)
            fprintf(stderr,"GPU_PRIM op=%02x xy=%08x uv=%08x wh=%08x offset=%d,%d display=%u,%u size=%u,%u\n",
                opcode,controller->prim_data[1],controller->prim_data[2],controller->prim_data[3],
                ox,oy,controller->display.x,controller->display.y,
                controller->display.width,controller->display.height);
    }
    if (opcode >= 0x40u && opcode <= 0x47u) {
        int lx0 = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        int ly0 = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        int lx1 = (int)(int16_t)(controller->prim_data[2] & 0xffffu) + ox;
        int ly1 = (int)(int16_t)(controller->prim_data[2] >> 16) + oy;
        return draw_line(controller, lx0, ly0, lx1, ly1, color);
    }
    if (opcode >= 0x50u && opcode <= 0x57u) {
        int lx0 = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        int ly0 = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        int lx1 = (int)(int16_t)(controller->prim_data[3] & 0xffffu) + ox;
        int ly1 = (int)(int16_t)(controller->prim_data[3] >> 16) + oy;
        return draw_line(controller, lx0, ly0, lx1, ly1, color);
    }

    if (opcode >= 0x60u && opcode <= 0x7fu && !(opcode & 4u)) {
        int dest_x = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        int dest_y = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        unsigned width, height;
        if ((opcode & 0x18u) == 0x00u) {
            width = controller->prim_data[2] & 0xffffu;
            height = controller->prim_data[2] >> 16;
        } else if ((opcode & 0x18u) == 0x08u) {
            width = 1; height = 1;
        } else if ((opcode & 0x18u) == 0x10u) {
            width = 8; height = 8;
        } else {
            width = 16; height = 16;
        }
        return fill_rect(controller, dest_x, dest_y, width, height, color);
    }
    if (opcode >= 0x60u && opcode <= 0x7fu && (opcode & 4u)) {
        unsigned tpage = controller->draw_mode & 0x1ffu;
        unsigned fmt = (tpage >> 7) & 3u;
        unsigned tx = (tpage & 15u) * 64u;
        unsigned ty = ((tpage >> 4) & 1u) * 256u;
        uint16_t clut = (uint16_t)(controller->prim_data[2] >> 16);
        unsigned clut_x = (unsigned)(clut & 0x3fu) * 16u;
        unsigned clut_y = (unsigned)(clut >> 6);
        int dest_x = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        int dest_y = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        unsigned u0 = controller->prim_data[2] & 0xffu;
        unsigned v0 = (controller->prim_data[2] >> 8) & 0xffu;
        unsigned width, height;
        if ((opcode & 0x18u) == 0x00u) {
            width = controller->prim_data[3] & 0xffffu;
            height = controller->prim_data[3] >> 16;
        } else if ((opcode & 0x18u) == 0x08u) {
            width = 1; height = 1;
        } else if ((opcode & 0x18u) == 0x10u) {
            width = 8; height = 8;
        } else {
            width = 16; height = 16;
        }
        int minx, miny, maxx, maxy;
        int px, py;
        if (!width || !height) return 1;
        if (!controller->backend.read_vram || width > 1024u || height > 512u) return 0;
        minx = dest_x;
        miny = dest_y;
        maxx = dest_x + (int)width - 1;
        maxy = dest_y + (int)height - 1;
        if (!clip_box(controller, &minx, &miny, &maxx, &maxy)) return 1;
        for (py = miny; py <= maxy; ++py) {
            unsigned rel_y = (unsigned)(py - dest_y);
            for (px = minx; px <= maxx; ++px) {
                unsigned rel_x = (unsigned)(px - dest_x);
                uint16_t out = 0;
                if (!sample_texel(controller, fmt, tx, ty, clut_x, clut_y,
                                  (u0 + rel_x) & 255u, (v0 + rel_y) & 255u, &out))
                    return 0;
                if (!out)
                    continue;
                if (!(opcode & 1u)) out = modulate_texel(out, controller->prim_data[0]);
                if (!plot_drawn(controller, px, py, out))
                    return 0;
            }
        }
        return 1;
    }
    if (opcode >= 0x20u && opcode <= 0x23u) {
        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[2] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[2] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[3] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[3] >> 16) + oy;
        return fill_triangle(controller, x[0], y[0], x[1], y[1], x[2], y[2], color);
    }
    if (opcode >= 0x24u && opcode <= 0x27u) {
        int raw_tex = (opcode & 1u) != 0;
        uint32_t rgb = controller->prim_data[0] & 0xffffffu;
        unsigned u0 = controller->prim_data[2] & 0xffu;
        unsigned v0 = (controller->prim_data[2] >> 8) & 0xffu;
        uint16_t clut = (uint16_t)(controller->prim_data[2] >> 16);
        unsigned u1 = controller->prim_data[4] & 0xffu;
        unsigned v1 = (controller->prim_data[4] >> 8) & 0xffu;
        unsigned tpage = (controller->prim_data[4] >> 16) & 0x1ffu;
        unsigned u2 = controller->prim_data[6] & 0xffu;
        unsigned v2 = (controller->prim_data[6] >> 8) & 0xffu;
        unsigned fmt = (tpage >> 7) & 3u;
        unsigned tx = (tpage & 15u) * 64u;
        unsigned ty = ((tpage >> 4) & 1u) * 256u;
        unsigned clut_x = (unsigned)(clut & 0x3fu) * 16u;
        unsigned clut_y = (unsigned)(clut >> 6);

        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[3] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[3] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[5] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[5] >> 16) + oy;

        if (!fill_textured_triangle(controller,
                x[0], y[0], u0, v0,
                x[1], y[1], u1, v1,
                x[2], y[2], u2, v2,
                fmt, tx, ty, clut_x, clut_y, rgb, raw_tex))
            return 0;
        return 1;
    }
    if (opcode >= 0x28u && opcode <= 0x2bu) {
        for (i = 0; i < 4; ++i) {
            x[i] = (int)(int16_t)(controller->prim_data[1u + (unsigned)i] & 0xffffu) + ox;
            y[i] = (int)(int16_t)(controller->prim_data[1u + (unsigned)i] >> 16) + oy;
        }
        if (!fill_triangle(controller, x[0], y[0], x[1], y[1], x[2], y[2], color))
            return 0;
        if (!fill_triangle(controller, x[1], y[1], x[3], y[3], x[2], y[2], color))
            return 0;
        return 1;
    }
    if (opcode >= 0x2cu && opcode <= 0x2fu) {
        int raw_tex = (opcode & 1u) != 0;
        uint32_t rgb = controller->prim_data[0] & 0xffffffu;
        unsigned u0 = controller->prim_data[2] & 0xffu;
        unsigned v0 = (controller->prim_data[2] >> 8) & 0xffu;
        uint16_t clut = (uint16_t)(controller->prim_data[2] >> 16);
        unsigned u1 = controller->prim_data[4] & 0xffu;
        unsigned v1 = (controller->prim_data[4] >> 8) & 0xffu;
        unsigned tpage = (controller->prim_data[4] >> 16) & 0x1ffu;
        unsigned u2 = controller->prim_data[6] & 0xffu;
        unsigned v2 = (controller->prim_data[6] >> 8) & 0xffu;
        unsigned u3 = controller->prim_data[8] & 0xffu;
        unsigned v3 = (controller->prim_data[8] >> 8) & 0xffu;
        unsigned fmt = (tpage >> 7) & 3u;
        unsigned tx = (tpage & 15u) * 64u;
        unsigned ty = ((tpage >> 4) & 1u) * 256u;
        unsigned clut_x = (unsigned)(clut & 0x3fu) * 16u;
        unsigned clut_y = (unsigned)(clut >> 6);

        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[3] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[3] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[5] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[5] >> 16) + oy;
        x[3] = (int)(int16_t)(controller->prim_data[7] & 0xffffu) + ox;
        y[3] = (int)(int16_t)(controller->prim_data[7] >> 16) + oy;

        if (!fill_textured_triangle(controller,
                x[0], y[0], u0, v0,
                x[1], y[1], u1, v1,
                x[2], y[2], u2, v2,
                fmt, tx, ty, clut_x, clut_y, rgb, raw_tex))
            return 0;
        if (!fill_textured_triangle(controller,
                x[1], y[1], u1, v1,
                x[3], y[3], u3, v3,
                x[2], y[2], u2, v2,
                fmt, tx, ty, clut_x, clut_y, rgb, raw_tex))
            return 0;
        return 1;
    }
    if (opcode >= 0x30u && opcode <= 0x33u) {
        uint32_t rgb0 = controller->prim_data[0] & 0xffffffu;
        uint32_t rgb1 = controller->prim_data[2] & 0xffffffu;
        uint32_t rgb2 = controller->prim_data[4] & 0xffffffu;
        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[3] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[3] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[5] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[5] >> 16) + oy;
        return fill_gouraud_triangle(controller,
                                     x[0], y[0], rgb0,
                                     x[1], y[1], rgb1,
                                     x[2], y[2], rgb2);
    }
    if (opcode >= 0x34u && opcode <= 0x37u) {
        int raw_tex = (opcode & 1u) != 0;
        uint32_t rgb0 = controller->prim_data[0] & 0xffffffu;
        unsigned u0 = controller->prim_data[2] & 0xffu;
        unsigned v0 = (controller->prim_data[2] >> 8) & 0xffu;
        uint16_t clut = (uint16_t)(controller->prim_data[2] >> 16);
        uint32_t rgb1 = controller->prim_data[3] & 0xffffffu;
        unsigned u1 = controller->prim_data[5] & 0xffu;
        unsigned v1 = (controller->prim_data[5] >> 8) & 0xffu;
        unsigned tpage = (controller->prim_data[5] >> 16) & 0x1ffu;
        uint32_t rgb2 = controller->prim_data[6] & 0xffffffu;
        unsigned u2 = controller->prim_data[8] & 0xffu;
        unsigned v2 = (controller->prim_data[8] >> 8) & 0xffu;
        unsigned fmt = (tpage >> 7) & 3u;
        unsigned tx = (tpage & 15u) * 64u;
        unsigned ty = ((tpage >> 4) & 1u) * 256u;
        unsigned clut_x = (unsigned)(clut & 0x3fu) * 16u;
        unsigned clut_y = (unsigned)(clut >> 6);

        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[4] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[4] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[7] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[7] >> 16) + oy;

        return fill_textured_gouraud_triangle(controller,
                x[0], y[0], u0, v0, rgb0,
                x[1], y[1], u1, v1, rgb1,
                x[2], y[2], u2, v2, rgb2,
                fmt, tx, ty, clut_x, clut_y, raw_tex);
    }
    if (opcode >= 0x38u && opcode <= 0x3bu) {
        uint32_t rgb0 = controller->prim_data[0] & 0xffffffu;
        uint32_t rgb1 = controller->prim_data[2] & 0xffffffu;
        uint32_t rgb2 = controller->prim_data[4] & 0xffffffu;
        uint32_t rgb3 = controller->prim_data[6] & 0xffffffu;
        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[3] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[3] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[5] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[5] >> 16) + oy;
        x[3] = (int)(int16_t)(controller->prim_data[7] & 0xffffu) + ox;
        y[3] = (int)(int16_t)(controller->prim_data[7] >> 16) + oy;

        if (!fill_gouraud_triangle(controller,
                                   x[0], y[0], rgb0,
                                   x[1], y[1], rgb1,
                                   x[2], y[2], rgb2))
            return 0;
        if (!fill_gouraud_triangle(controller,
                                   x[1], y[1], rgb1,
                                   x[3], y[3], rgb3,
                                   x[2], y[2], rgb2))
            return 0;
        return 1;
    }
    if (opcode >= 0x3cu && opcode <= 0x3fu) {
        int raw_tex = (opcode & 1u) != 0;
        uint32_t rgb0 = controller->prim_data[0] & 0xffffffu;
        unsigned u0 = controller->prim_data[2] & 0xffu;
        unsigned v0 = (controller->prim_data[2] >> 8) & 0xffu;
        uint16_t clut = (uint16_t)(controller->prim_data[2] >> 16);
        uint32_t rgb1 = controller->prim_data[3] & 0xffffffu;
        unsigned u1 = controller->prim_data[5] & 0xffu;
        unsigned v1 = (controller->prim_data[5] >> 8) & 0xffu;
        unsigned tpage = (controller->prim_data[5] >> 16) & 0x1ffu;
        uint32_t rgb2 = controller->prim_data[6] & 0xffffffu;
        unsigned u2 = controller->prim_data[8] & 0xffu;
        unsigned v2 = (controller->prim_data[8] >> 8) & 0xffu;
        uint32_t rgb3 = controller->prim_data[9] & 0xffffffu;
        unsigned u3 = controller->prim_data[11] & 0xffu;
        unsigned v3 = (controller->prim_data[11] >> 8) & 0xffu;
        unsigned fmt = (tpage >> 7) & 3u;
        unsigned tx = (tpage & 15u) * 64u;
        unsigned ty = ((tpage >> 4) & 1u) * 256u;
        unsigned clut_x = (unsigned)(clut & 0x3fu) * 16u;
        unsigned clut_y = (unsigned)(clut >> 6);

        x[0] = (int)(int16_t)(controller->prim_data[1] & 0xffffu) + ox;
        y[0] = (int)(int16_t)(controller->prim_data[1] >> 16) + oy;
        x[1] = (int)(int16_t)(controller->prim_data[4] & 0xffffu) + ox;
        y[1] = (int)(int16_t)(controller->prim_data[4] >> 16) + oy;
        x[2] = (int)(int16_t)(controller->prim_data[7] & 0xffffu) + ox;
        y[2] = (int)(int16_t)(controller->prim_data[7] >> 16) + oy;
        x[3] = (int)(int16_t)(controller->prim_data[10] & 0xffffu) + ox;
        y[3] = (int)(int16_t)(controller->prim_data[10] >> 16) + oy;

        if (!fill_textured_gouraud_triangle(controller,
                x[0], y[0], u0, v0, rgb0,
                x[1], y[1], u1, v1, rgb1,
                x[2], y[2], u2, v2, rgb2,
                fmt, tx, ty, clut_x, clut_y, raw_tex))
            return 0;
        if (!fill_textured_gouraud_triangle(controller,
                x[1], y[1], u1, v1, rgb1,
                x[3], y[3], u3, v3, rgb3,
                x[2], y[2], u2, v2, rgb2,
                fmt, tx, ty, clut_x, clut_y, raw_tex))
            return 0;
        return 1;
    }
    return 0;
}

static uint16_t color15(uint32_t word) {
    return (uint16_t)(((word >> 3) & 0x1fu) | ((word >> 6) & 0x3e0u) | ((word >> 9) & 0x7c00u));
}

/* One polyline word. Segments are drawn as each vertex arrives, with the
 * start vertex's colour like the two-point lines (no gouraud shading yet). */
static int poly_word(MusashiGpuController *controller, uint32_t word) {
    const uint32_t index = controller->poly_words + 1u;  /* position in the packet */
    const int is_color = controller->poly_gouraud && (index & 1u) == 0u;
    const int vertex_slot = controller->poly_gouraud ? (index & 1u) : 1;
    if (controller->accepted_gp0_words == UINT64_MAX || controller->poly_words == UINT32_MAX)
        return 0;
    if (controller->poly_vertices >= 2u && (word & 0xf000f000u) == 0x50005000u &&
        (controller->poly_gouraud ? is_color : 1)) {
        discard_poly(controller);
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }
    controller->poly_words++;
    controller->accepted_gp0_words++;
    if (is_color) {
        controller->poly_color = word;
        return controller->faulted ? 0 : 1;
    }
    if (vertex_slot) {
        int x = (int)(int16_t)(word & 0xffffu) + offset11(controller->drawing_offset, 0);
        int y = (int)(int16_t)(word >> 16) + offset11(controller->drawing_offset, 11);
        if (controller->poly_vertices && !controller->raster_disabled) {
            const uint32_t opcode = controller->poly_command >> 24;
            controller->semi = (opcode & 2u) ? 1u : 0u;
            controller->semi_textured = 0;
            controller->semi_mode = (uint8_t)((controller->draw_mode >> 5) & 3u);
            if (!draw_line(controller, controller->poly_x, controller->poly_y, x, y,
                           color15(controller->poly_prev_color)))
                return 0;
        }
        controller->poly_x = x;
        controller->poly_y = y;
        controller->poly_prev_color = controller->poly_color;
        if (controller->poly_vertices != UINT32_MAX) controller->poly_vertices++;
    }
    return controller->faulted ? 0 : 1;
}

static int poly_start(MusashiGpuController *controller, uint32_t word) {
    if (controller->accepted_gp0_words == UINT64_MAX) return 0;
    discard_poly(controller);
    controller->poly_active = 1;
    controller->poly_gouraud = (uint8_t)(((word >> 24) & 0x10u) != 0);
    controller->poly_command = word;
    controller->poly_color = word;
    controller->accepted_gp0_words++;
    return controller->faulted ? 0 : 1;
}

static int prim_word(MusashiGpuController *controller, uint32_t word) {
    uint32_t opcode;
    if (controller->accepted_gp0_words == UINT64_MAX) return 0;
    if (controller->prim_needed) {
        if (controller->prim_got >= 12u) return 0;
        controller->prim_data[controller->prim_got++] = word;
        controller->accepted_gp0_words++;
        if (controller->prim_got == controller->prim_needed) {
            int accepted = prim_commit(controller);
            if (!accepted)
                fprintf(stderr, "gpu_controller: prim_commit FAILED op=%02x needed=%u got=%u w0=%08x\n",
                        controller->prim_data[0] >> 24, controller->prim_needed, controller->prim_got,
                        controller->prim_data[0]);
            discard_prim(controller);
            return accepted && !controller->faulted;
        }
        return controller->faulted ? 0 : 1;
    }
    opcode = word >> 24;
    if (opcode >= 0x20u && opcode <= 0x23u) controller->prim_needed = 4;
    else if (opcode >= 0x24u && opcode <= 0x27u) controller->prim_needed = 7;
    else if (opcode >= 0x28u && opcode <= 0x2bu) controller->prim_needed = 5;
    else if (opcode >= 0x2cu && opcode <= 0x2fu) controller->prim_needed = 9;
    else if (opcode >= 0x30u && opcode <= 0x33u) controller->prim_needed = 6;
    else if (opcode >= 0x34u && opcode <= 0x37u) controller->prim_needed = 9;
    else if (opcode >= 0x38u && opcode <= 0x3bu) controller->prim_needed = 8;
    else if (opcode >= 0x3cu && opcode <= 0x3fu) controller->prim_needed = 12;
    else if (opcode >= 0x40u && opcode <= 0x47u) controller->prim_needed = 3;
    else if (opcode >= 0x50u && opcode <= 0x57u) controller->prim_needed = 4;
    /* Rectangles 0x60-0x7f: bits 3-4 size (variable/1/8/16), bit 2 textured
     * (+1 word: uv/clut), bit 1 semi, bit 0 raw texture. */
    else if (opcode >= 0x60u && opcode <= 0x7fu)
        controller->prim_needed = (uint8_t)(2u + ((opcode & 0x18u) == 0 ? 1u : 0u) +
                                            ((opcode & 4u) ? 1u : 0u));
    else return 0;
    controller->prim_data[0] = word;
    controller->prim_got = 1;
    controller->accepted_gp0_words++;
    return controller->faulted ? 0 : 1;
}

int musashi_gpu_controller_write32(MusashiGpuController *controller,
                                   uint32_t address, uint32_t value) {
    uint32_t opcode;
    uint32_t candidate;
    int ready;

    if (!controller) return 0;
    if (controller->executing) {
        controller->faulted = 1;
        return 0;
    }
    if (!backend_bound(controller) || controller->faulted) return 0;
    if (controller->tap &&
        (address == MUSASHI_GPU_GPUREAD || address == MUSASHI_GPU_GPUSTAT))
        (void)controller->tap(controller->tap_userdata, address, value);
    if (address == MUSASHI_GPU_GPUREAD) {
        opcode = value >> 24;
        /* An open packet owns every following GP0 word. Pixel payloads may
         * look like opcode 02/A0; they are still texels, not new commands. */
        if (controller->copy_words)
            return copy_word(controller, value);
        if (controller->fill_words)
            return fill_word(controller, value);
        if (controller->store_phase)
            return store_word(controller, value);
        if (controller->prim_needed)
            return prim_word(controller, value);
        if (controller->poly_active)
            return poly_word(controller, value);
        if (store_image_in_header(controller))
            return store_image_word(controller, value);
        if ((opcode & 0xe0u) == 0x80u)
            return copy_word(controller, value);
        if (opcode == 2u)
            return fill_word(controller, value);
        if (opcode == 0xa0u)
            return store_word(controller, value);
        if ((opcode & 0xe0u) == 0xc0u)
            return store_image_word(controller, value);
        if ((opcode >= 0x48u && opcode <= 0x4fu) || (opcode >= 0x58u && opcode <= 0x5fu))
            return poly_start(controller, value);
        if ((opcode >= 0x20u && opcode <= 0x3fu) ||
            (opcode >= 0x40u && opcode <= 0x47u) ||
            (opcode >= 0x50u && opcode <= 0x57u) ||
            (opcode >= 0x60u && opcode <= 0x7fu))
            return prim_word(controller, value);

        candidate = opcode == 0 || opcode == 1u || (opcode >= 0xe1u && opcode <= 0xe6u);
        if (candidate && controller->accepted_gp0_words == UINT64_MAX)
            return 0;
        if (opcode == 0 || opcode == 1u) {
            if (!backend_ready(controller, &ready) || !ready) {
                fprintf(stderr, "gpu_controller: opcode %u not ready: backend_ready=%d ready=%d faulted=%d\n",
                        opcode, backend_ready(controller, &ready), ready, controller->faulted);
                return 0;
            }
            controller->accepted_gp0_words++;
            return controller->faulted ? 0 : 1;
        }
        if (opcode == 0xe2u || opcode == 0xe3u || opcode == 0xe4u ||
            opcode == 0xe5u || opcode == 0xe6u) {
            uint32_t raw = value &
                (opcode == 0xe5u ? 0x003fffffu :
                 opcode == 0xe6u ? 0x00000003u : 0x000fffffu);
            if (!controller->backend.environment ||
                !backend_ready(controller, &ready) || !ready)
                return 0;
            if (!backend_environment(controller, value)) return 0;
            if (opcode == 0xe2u) controller->texture_window = raw;
            else if (opcode == 0xe3u) controller->drawing_area_start = raw;
            else if (opcode == 0xe4u) controller->drawing_area_end = raw;
            else if (opcode == 0xe5u) controller->drawing_offset = raw;
            else controller->mask_flags = raw;
            controller->accepted_gp0_words++;
            return controller->faulted ? 0 : 1;
        }
        if (opcode != 0xe1u) {
            fprintf(stderr, "gpu_controller: UNHANDLED GP0 opcode=%02x value=%08x\n", opcode, value);
            return 0;
        }
        if (!backend_ready(controller, &ready) || !ready) return 0;
        if (!backend_draw_mode(controller, value)) return 0;
        controller->draw_mode = value & 0x3fffu;
        controller->accepted_gp0_words++;
        return controller->faulted ? 0 : 1;
    }
    if (address != MUSASHI_GPU_GPUSTAT) return 0;
    opcode = value >> 24;
    switch (opcode) {
    case 0x00u:
        if (!backend_reset(controller)) return 0;
        reset_state(controller);
        return controller->faulted ? 0 : 1;
    case 0x01u:
        if (!backend_clear_fifo(controller)) return 0;
        discard_fill(controller);
        discard_store(controller);
        discard_copy(controller);
        discard_prim(controller);
        discard_poly(controller);
        discard_store_image(controller);
        return controller->faulted ? 0 : 1;
    case 0x02u:
        return 1;
    case 0x03u:
        ready = (value & 1u) == 0;
        if (!backend_display_enable(controller, ready)) return 0;
        controller->display_disabled = !ready;
        return controller->faulted ? 0 : 1;
    case 0x04u:
        controller->dma_direction = value & 3u;
        return 1;
    case 0x05u:
    case 0x06u:
    case 0x07u:
    case 0x08u:
        return display_write(controller, opcode, value);
    /* GP1 opcode 10 latches selected raw environment registers. */
    case 0x10u:
        switch (value & 7u) {
        case 2u: controller->read_latch = controller->texture_window; break;
        case 3u: controller->read_latch = controller->drawing_area_start; break;
        case 4u: controller->read_latch = controller->drawing_area_end; break;
        case 5u: controller->read_latch = controller->drawing_offset; break;
        default: break;
        }
        return 1;
    default:
        return 0;
    }
}
