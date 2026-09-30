#include "musashi_gpu_psycross.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include "../tools/third_party/psycross/src/gpu/PsyX_GPU.h"
#include "PsyX/PsyX_render.h"

/* Pinned PsyCross globals are the actual parser queue/scene, not simulated
 * completion words. Only this adapter's installing GL thread may use them. */
extern int g_vertexIndex;
extern char begin_scene_flag;
extern int framebuffer_need_update;
static MusashiGpuPsyCross *active_gpu;

static int valid(const MusashiGpuPsyCross *gpu) {
    return gpu && active_gpu == gpu && gpu->initialized &&
        gpu->owner_thread == (uint64_t)SDL_ThreadID() && !gpu->faulted &&
        gpu->context == SDL_GL_GetCurrentContext() &&
        gpu->window == SDL_GL_GetCurrentWindow() && !g_GPUDisabledState;
}

static void reset_display_state(MusashiGpuPsyCross *gpu);

static int log_sample_psycross(uint64_t *counter, uint32_t initial, uint32_t stride) {
    uint64_t val = (*counter)++;
    return (val < initial) || (stride && (val % stride == 0));
}

static int ready(void *userdata) {
    MusashiGpuPsyCross *gpu = userdata;
    if (!valid(gpu) || g_vertexIndex < 0 || g_splitIndex < 0) {
        static uint64_t n;
        if (log_sample_psycross(&n, 4u, 4096u))
            fprintf(stderr, "gpu_psycross: ready=-1 valid=%d (ctx=%p curr_ctx=%p win=%p curr_win=%p faulted=%d) vIdx=%d sIdx=%d\n",
                    valid(gpu), gpu ? gpu->context : NULL, SDL_GL_GetCurrentContext(),
                    gpu ? gpu->window : NULL, SDL_GL_GetCurrentWindow(),
                    gpu ? gpu->faulted : -1, g_vertexIndex, g_splitIndex);
        return -1;
    }
    return !gpu->command_active && g_vertexIndex == 0 && g_splitIndex == 0;
}

static int reset(void *userdata) {
    MusashiGpuPsyCross *gpu = userdata;
    if (!valid(gpu) || gpu->command_active) return 0;
    gpu->command_active = 1;
    ClearSplits();
    /* GP1 reset's drawing-register defaults. Do not clear VRAM, end the
     * current scene, or use SetDispMask (which discards GPU commands). */
    gpu->texture_window = 0;
    gpu->drawing_area_start = 0;
    gpu->drawing_area_end = 0;
    gpu->drawing_offset = 0;
    gpu->mask_flags = 0;
    gpu->clip_x1 = gpu->clip_y1 = gpu->clip_x2 = gpu->clip_y2 = 0;
    gpu->texture_mask_x = gpu->texture_mask_y = 0;
    gpu->texture_offset_x = gpu->texture_offset_y = 0;
    activeDrawEnv.clip = (RECT16){0, 0, 1, 1};
    activeDrawEnv.tw = (RECT16){0};
    activeDrawEnv.ofs[0] = activeDrawEnv.ofs[1] = 0;
    activeDrawEnv.tpage = 0;
    activeDrawEnv.dtd = activeDrawEnv.dfe = 0;
    reset_display_state(gpu);
    /* Host background policy and packet storage are not GPU drawing-register
     * resets; retain them rather than memset DRAWENV. The owned display
     * descriptor was reset above. */
    gpu->display_enabled = 0;
    gpu->command_active = 0;
    return 1;
}

static int clear_fifo(void *userdata) {
    MusashiGpuPsyCross *gpu = userdata;
    if (!valid(gpu) || gpu->command_active) return 0;
    gpu->command_active = 1;
    ClearSplits();
    gpu->command_active = 0;
    return 1;
}

static int draw_mode(void *userdata, uint32_t word) {
    MusashiGpuPsyCross *gpu = userdata;
    if ((word & 0xff000000u) != 0xe1000000u || ready(gpu) != 1) return 0;
    gpu->command_active = 1;
    activeDrawEnv.tpage = word & 0x1ffu;
    activeDrawEnv.dtd = (word >> 9) & 1;
    activeDrawEnv.dfe = (word >> 10) & 1;
    /* The frontend retains sprite flip fields. This backend does not accept
     * geometry, so it cannot silently render sprites without those fields. */
    gpu->command_active = 0;
    return 1;
}

static int display_enable(void *userdata, int enabled) {
    MusashiGpuPsyCross *gpu = userdata;
    if (!valid(gpu) || gpu->command_active) return 0;
    gpu->display_enabled = enabled ? 1 : 0;
    return 1;
}

static void reset_display_state(MusashiGpuPsyCross *gpu) {
    gpu->display.origin = 0;
    gpu->display.horizontal = 0x00c00200u;
    gpu->display.vertical = 0x00040010u;
    gpu->display.mode = 0;
    gpu->display.x = 0;
    gpu->display.y = 0;
    gpu->display.width = 256;
    gpu->display.height = 240;
    gpu->display.h_start = 0x200;
    gpu->display.h_end = 0xc00;
    gpu->display.v_start = 0x10;
    gpu->display.v_end = 0x100;
    gpu->display.dot_divisor = 10;
    activeDispEnv.disp.x = 0;
    activeDispEnv.disp.y = 0;
    activeDispEnv.disp.w = 256;
    activeDispEnv.disp.h = 240;
    activeDispEnv.isinter = 0;
    activeDispEnv.isrgb24 = 0;
    currentDispEnv.disp = activeDispEnv.disp;
    currentDispEnv.isinter = 0;
    currentDispEnv.isrgb24 = 0;
}

static int display_state_equal(const MusashiGpuDisplayState *left,
                               const MusashiGpuDisplayState *right) {
    return left->origin == right->origin &&
           left->horizontal == right->horizontal &&
           left->vertical == right->vertical && left->mode == right->mode &&
           left->x == right->x && left->y == right->y &&
           left->width == right->width && left->height == right->height &&
           left->h_start == right->h_start && left->h_end == right->h_end &&
           left->v_start == right->v_start && left->v_end == right->v_end &&
           left->dot_divisor == right->dot_divisor;
}

static int display(void *userdata, const MusashiGpuDisplayState *candidate) {
    MusashiGpuPsyCross *gpu = userdata;
    MusashiGpuDisplayState checked;
    if (!valid(gpu) || gpu->command_active || !candidate) {
        return 0;
    }
    checked = *candidate;
    if (!musashi_gpu_display_derive(&checked) ||
        !display_state_equal(&checked, candidate) || ready(gpu) != 1)
        return 0;
    gpu->command_active = 1;
    gpu->display = *candidate;
    activeDispEnv.disp.x = (short)candidate->x;
    activeDispEnv.disp.y = (short)candidate->y;
    activeDispEnv.disp.w = (short)candidate->width;
    activeDispEnv.disp.h = (short)candidate->height;
    activeDispEnv.isinter = 0;
    activeDispEnv.isrgb24 = 0;
    currentDispEnv.disp = activeDispEnv.disp;
    currentDispEnv.isinter = 0;
    currentDispEnv.isrgb24 = 0;
    gpu->command_active = 0;
    return 1;
}

static int sign11(uint32_t value) {
    value &= 0x7ffu;
    return value < 1024u ? (int)value : (int)value - 2048;
}

static void apply_clip(MusashiGpuPsyCross *gpu) {
    int width = (int)gpu->clip_x2 - (int)gpu->clip_x1 + 1;
    int height = (int)gpu->clip_y2 - (int)gpu->clip_y1 + 1;
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    activeDrawEnv.clip = (RECT16){gpu->clip_x1, gpu->clip_y1,
                                  (short)width, (short)height};
}

static int environment(void *userdata, uint32_t word) {
    MusashiGpuPsyCross *gpu = userdata;
    uint32_t opcode = word >> 24;
    uint32_t raw;

    if (!valid(gpu) || gpu->command_active || opcode < 0xe2u || opcode > 0xe6u)
        return 0;
    if (ready(gpu) != 1) return 0;
    raw = word & (opcode == 0xe5u ? 0x003fffffu :
                  opcode == 0xe6u ? 0x00000003u : 0x000fffffu);
    gpu->command_active = 1;
    switch (opcode) {
    case 0xe2u:
        gpu->texture_window = raw;
        gpu->texture_mask_x = (int16_t)(raw & 0x1fu);
        gpu->texture_mask_y = (int16_t)((raw >> 5) & 0x1fu);
        gpu->texture_offset_x = (int16_t)((raw >> 10) & 0x1fu);
        gpu->texture_offset_y = (int16_t)((raw >> 15) & 0x1fu);
        activeDrawEnv.tw.w = gpu->texture_mask_x;
        activeDrawEnv.tw.h = gpu->texture_mask_y;
        activeDrawEnv.tw.x = gpu->texture_offset_x;
        activeDrawEnv.tw.y = gpu->texture_offset_y;
        break;
    case 0xe3u:
        gpu->drawing_area_start = raw;
        gpu->clip_x1 = (int16_t)(raw & 0x3ffu);
        /* The selected reference uses nine Y bits for drawing bounds. */
        gpu->clip_y1 = (int16_t)((raw >> 10) & 0x1ffu);
        apply_clip(gpu);
        break;
    case 0xe4u:
        gpu->drawing_area_end = raw;
        gpu->clip_x2 = (int16_t)(raw & 0x3ffu);
        gpu->clip_y2 = (int16_t)((raw >> 10) & 0x1ffu);
        apply_clip(gpu);
        break;
    case 0xe5u:
        gpu->drawing_offset = raw;
        activeDrawEnv.ofs[0] = (short)sign11(raw);
        activeDrawEnv.ofs[1] = (short)sign11(raw >> 11);
        break;
    default:
        gpu->mask_flags = raw;
        break;
    }
    gpu->command_active = 0;
    return 1;
}

static int same_fill(const MusashiGpuFill *a, const MusashiGpuFill *b) {
    return a->command == b->command && a->xy == b->xy && a->wh == b->wh &&
        a->x == b->x && a->y == b->y && a->width == b->width &&
        a->height == b->height && a->color == b->color;
}

static int fill_vram(void *userdata, const MusashiGpuFill *candidate) {
    MusashiGpuPsyCross *gpu = userdata;
    MusashiGpuFill fill;
    uint16_t row[1024];
    unsigned i, first, remaining;
    uint64_t pixels;
    if (!candidate || ready(gpu) != 1 || begin_scene_flag ||
        framebuffer_need_update) return 0;
    fill = *candidate;
    if (!musashi_gpu_fill_derive(&fill) || !same_fill(&fill, candidate)) return 0;
    pixels = (uint64_t)fill.width * fill.height;
    if (gpu->completed_fills == UINT64_MAX ||
        gpu->filled_pixels > UINT64_MAX - pixels) return 0;
    for (i = 0; i < fill.width; ++i) row[i] = fill.color;
    first = fill.width;
    if (first > 1024u - fill.x) first = 1024u - fill.x;
    remaining = fill.width - first;
    gpu->command_active = 1;
    /* Reuse the MIT PsyCross canonical-VRAM copy (non-null source). All
     * ranges are proven above; these copies have no fallible callbacks/GL.
     * GP0 fill ignores E6 mask and drawing area/offset, wraps at VRAM edges,
     * and quantizes RGB to15bits. GR_ClearVRAM's color packing differs. */
    for (i = 0; i < fill.height && fill.width; ++i) {
        unsigned y = (fill.y + i) & 511u;
        GR_CopyVRAM(row, 0, 0, (int)first, 1, fill.x, (int)y);
        if (remaining)
            GR_CopyVRAM(row, 0, 0, (int)remaining, 1, 0, (int)y);
    }
    gpu->last_fill = fill;
    gpu->completed_fills++;
    gpu->filled_pixels += pixels;
    gpu->command_active = 0;
    return 1;
}

static int store_vram(void *userdata, uint16_t x, uint16_t y, uint16_t pixel) {
    MusashiGpuPsyCross *gpu = userdata;
    uint16_t texel = pixel;
    if (ready(gpu) != 1 || begin_scene_flag || framebuffer_need_update) return 0;
    gpu->command_active = 1;
    GR_CopyVRAM(&texel, 0, 0, 1, 1, (int)(x & 1023u), (int)(y & 511u));
    gpu->command_active = 0;
    return 1;
}

static int read_vram(void *userdata, uint16_t x, uint16_t y, uint16_t *pixel) {
    MusashiGpuPsyCross *gpu = userdata;
    uint16_t texel = 0;
    if (!pixel || ready(gpu) != 1 || begin_scene_flag || framebuffer_need_update)
        return 0;
    gpu->command_active = 1;
    GR_ReadVRAM(&texel, (int)(x & 1023u), (int)(y & 511u), 1, 1);
    gpu->command_active = 0;
    *pixel = texel;
    return 1;
}

int musashi_gpu_psycross_read_vram(MusashiGpuPsyCross *gpu,
                                  unsigned x, unsigned y,
                                  unsigned width, unsigned height,
                                  uint16_t *out, uint32_t out_halfwords) {
    if (!out || ready(gpu) != 1 || begin_scene_flag || framebuffer_need_update ||
        x >= 1024u || y >= 512u || !width || !height ||
        width > 1024u - x || height > 512u - y ||
        out_halfwords < width * height) return 0;
    gpu->command_active = 1;
    GR_ReadVRAM(out, (int)x, (int)y, (int)width, (int)height);
    gpu->command_active = 0;
    return 1;
}

int musashi_gpu_psycross_init(MusashiGpuPsyCross *gpu, MusashiGpuBackend *backend) {
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (!gpu || !backend || active_gpu || gpu->initialized || !context || !window ||
        g_vertexIndex || g_splitIndex || begin_scene_flag || g_GPUDisabledState)
        return 0;
    memset(gpu, 0, sizeof(*gpu));
    gpu->context = context;
    gpu->window = window;
    gpu->owner_thread = (uint64_t)SDL_ThreadID();
    gpu->initialized = 1;
    active_gpu = gpu;
    *backend = (MusashiGpuBackend){
        .userdata = gpu, .reset = reset, .draw_mode = draw_mode,
        .display_enable = display_enable, .clear_fifo = clear_fifo,
        .ready = ready, .environment = environment, .display = display,
        .fill_vram = fill_vram, .store_vram = store_vram, .read_vram = read_vram
    };
    return 1;
}

int musashi_gpu_psycross_shutdown(MusashiGpuPsyCross *gpu) {
    if (!gpu || active_gpu != gpu || !gpu->initialized || gpu->command_active ||
        gpu->owner_thread != (uint64_t)SDL_ThreadID()) return 0;
    active_gpu = NULL;
    gpu->initialized = 0;
    return 1;
}

static int compile_shader(GLenum kind, const char *source, GLuint *out) {
    GLint ok = 0;
    GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    *out = shader;
    return 1;
}

static int blit_vram(MusashiGpuPsyCross *gpu) {
    static GLuint program, vao, texture;
    static int ready_blit;
    unsigned width = gpu->display.width ? gpu->display.width : 320u;
    unsigned height = gpu->display.height ? gpu->display.height : 240u;
    unsigned x = gpu->display.x, y = gpu->display.y;
    /* The frame is drawn into the clip page. When that page does not overlap
     * the GP1 display origin, the origin is the other buffer (the backdrop
     * left at y=0) and the window never shows the 3D scene. */
    if (gpu->clip_x2 > gpu->clip_x1 && gpu->clip_y2 > gpu->clip_y1) {
        int disp_bottom = (int)y + (int)height - 1;
        int overlaps = gpu->clip_y2 >= (int)y && gpu->clip_y1 <= disp_bottom &&
                       gpu->clip_x2 >= (int)x && gpu->clip_x1 <= (int)x + (int)width - 1;
        unsigned dw = (unsigned)(gpu->clip_x2 - gpu->clip_x1 + 1);
        unsigned dh = (unsigned)(gpu->clip_y2 - gpu->clip_y1 + 1);
        if (!overlaps && gpu->clip_x1 >= 0 && gpu->clip_y1 >= 0 &&
            dw >= 256u && dw <= 640u && dh >= 200u && dh <= 480u &&
            (unsigned)gpu->clip_x1 + dw <= 1024u &&
            (unsigned)gpu->clip_y1 + dh <= 512u) {
            x = (unsigned)gpu->clip_x1;
            y = (unsigned)gpu->clip_y1;
            width = dw;
            height = dh;
        }
    }
    uint16_t *src;
    unsigned char *rgba;
    unsigned i, j;
    GLint prev_program = 0, prev_texture = 0, prev_vao = 0, prev_active = 0;
    if (width > 1024u || height > 512u || x > 1024u - width || y > 512u - height)
        return 0;
    src = malloc((size_t)width * height * 2u);
    rgba = malloc((size_t)width * height * 4u);
    if (!src || !rgba) {
        free(src);
        free(rgba);
        return 0;
    }
    GR_ReadVRAM(src, (int)x, (int)y, (int)width, (int)height);
    for (j = 0; j < height; ++j) {
        unsigned src_row = j * width;
        unsigned dst_row = (height - 1u - j) * width;
        for (i = 0; i < width; ++i) {
            uint16_t p = src[src_row + i];
            unsigned r = (p & 0x1fu) << 3;
            unsigned g = ((p >> 5) & 0x1fu) << 3;
            unsigned b = ((p >> 10) & 0x1fu) << 3;
            unsigned o = (dst_row + i) * 4u;
            rgba[o] = (unsigned char)(r | (r >> 5));
            rgba[o + 1u] = (unsigned char)(g | (g >> 5));
            rgba[o + 2u] = (unsigned char)(b | (b >> 5));
            rgba[o + 3u] = 255;
        }
    }
    free(src);
    if (!ready_blit) {
        GLuint vs = 0, fs = 0;
        static const char *vertex_source =
            "#version 330 core\n"
            "const vec2 p[4]=vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));\n"
            "const vec2 t[4]=vec2[](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1));\n"
            "out vec2 uv; void main(){ uv=t[gl_VertexID]; gl_Position=vec4(p[gl_VertexID],0,1); }\n";
        static const char *fragment_source =
            "#version 330 core\n"
            "in vec2 uv; out vec4 c; uniform sampler2D s;\n"
            "void main(){ c=texture(s,uv); }\n";
        if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
            !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs)) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            free(rgba);
            return 0;
        }
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glGenVertexArrays(1, &vao);
        glGenTextures(1, &texture);
        ready_blit = 1;
    }
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "s"), 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray((GLuint)prev_vao);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_texture);
    glActiveTexture((GLenum)prev_active);
    glUseProgram((GLuint)prev_program);
    return 1;
}

/* Host-only overlay (the opt-in dev menu): drawn into the back buffer after
 * the VRAM blit, never into VRAM, so the guest frame is untouched. */
static MusashiGpuPsyCrossOverlay g_overlay;
static void *g_overlay_user;
static int g_overlay_height;

void musashi_gpu_psycross_set_overlay(MusashiGpuPsyCrossOverlay fn, void *user) {
    g_overlay = fn;
    g_overlay_user = user;
}

/* Valid inside the overlay callback only; (x, y) from the top-left. */
void musashi_gpu_psycross_overlay_rect(int x, int y, int w, int h,
                                       uint8_t r, uint8_t g, uint8_t b) {
    if (w <= 0 || h <= 0) return;
    glScissor(x, g_overlay_height - y - h, w, h);
    glClearColor(r / 255.f, g / 255.f, b / 255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

int musashi_gpu_psycross_present(MusashiGpuPsyCross *gpu) {
    GLint framebuffer, draw_buffer;
    GLfloat clear_color[4];
    GLboolean color_mask[4], scissor;
    int blit_ok = 1;
    if (ready(gpu) != 1 || begin_scene_flag ||
        gpu->presentations == UINT64_MAX || glGetError() != GL_NO_ERROR)
        return 0;
    gpu->command_active = 1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
    glDrawBuffer(GL_BACK);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    /* Stretch the GP1 display rect to the whole drawable. Integer letterbox
     * left a black band through the window (GL y=0 is the bottom). */
    {
        int dw = 1280, dh = 960;
        GLint prev_viewport[4];
        SDL_Window *window = (SDL_Window *)gpu->window;
        glGetIntegerv(GL_VIEWPORT, prev_viewport);
        if (window)
            SDL_GL_GetDrawableSize(window, &dw, &dh);
        if (dw < 1) dw = 1280;
        if (dh < 1) dh = 960;
        glViewport(0, 0, dw, dh);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (gpu->display_enabled)
            blit_ok = blit_vram(gpu);
        if (g_overlay) {
            g_overlay_height = dh;
            glEnable(GL_SCISSOR_TEST);
            g_overlay(g_overlay_user, dw, dh);
            glDisable(GL_SCISSOR_TEST);
        }
        glViewport(prev_viewport[0], prev_viewport[1],
                   prev_viewport[2], prev_viewport[3]);
    }
    /* Do not call PsyX_EndScene/GR_Clear: both alter VRAM bookkeeping. */
    SDL_GL_SwapWindow((SDL_Window *)gpu->window);
    glDrawBuffer((GLenum)draw_buffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)framebuffer);
    glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    glColorMaski(0, color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    gpu->command_active = 0;
    if (!blit_ok || glGetError() != GL_NO_ERROR) {
        GLenum err = glGetError();
        fprintf(stderr, "gpu_psycross: present error blit_ok=%d glError=0x%x\n", blit_ok, err);
        gpu->faulted = 1;
        return 0;
    }
    gpu->presentations++;
    return 1;
}
