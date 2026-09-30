/* GL 3.3 core executor. See bfm_gl_exec.h. */

#include "bfm_gl_exec.h"

#include <GL/glcorearb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BFM_GL_FUNCS(X)                                                    \
    X(PFNGLGETSTRINGPROC, GetString)                                       \
    X(PFNGLGETERRORPROC, GetError)                                         \
    X(PFNGLGETINTEGERVPROC, GetIntegerv)                                   \
    X(PFNGLVIEWPORTPROC, Viewport)                                         \
    X(PFNGLSCISSORPROC, Scissor)                                           \
    X(PFNGLENABLEPROC, Enable)                                             \
    X(PFNGLDISABLEPROC, Disable)                                           \
    X(PFNGLCLEARCOLORPROC, ClearColor)                                     \
    X(PFNGLCLEARPROC, Clear)                                               \
    X(PFNGLBLENDEQUATIONPROC, BlendEquation)                               \
    X(PFNGLBLENDFUNCPROC, BlendFunc)                                       \
    X(PFNGLBLENDCOLORPROC, BlendColor)                                     \
    X(PFNGLGENTEXTURESPROC, GenTextures)                                   \
    X(PFNGLDELETETEXTURESPROC, DeleteTextures)                             \
    X(PFNGLBINDTEXTUREPROC, BindTexture)                                   \
    X(PFNGLTEXIMAGE2DPROC, TexImage2D)                                     \
    X(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D)                               \
    X(PFNGLTEXPARAMETERIPROC, TexParameteri)                               \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                               \
    X(PFNGLPIXELSTOREIPROC, PixelStorei)                                   \
    X(PFNGLREADPIXELSPROC, ReadPixels)                                     \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)                           \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)                     \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)                           \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)                 \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)             \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)                           \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)                     \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)                           \
    X(PFNGLGENBUFFERSPROC, GenBuffers)                                     \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                               \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                                     \
    X(PFNGLBUFFERDATAPROC, BufferData)                                     \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer)                   \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, VertexAttribIPointer)                 \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray)           \
    X(PFNGLCREATESHADERPROC, CreateShader)                                 \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)                                 \
    X(PFNGLCOMPILESHADERPROC, CompileShader)                               \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)                                   \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)                         \
    X(PFNGLDELETESHADERPROC, DeleteShader)                                 \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)                               \
    X(PFNGLATTACHSHADERPROC, AttachShader)                                 \
    X(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation)                     \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)                                   \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                                 \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)                       \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                               \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                                     \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)                     \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                                       \
    X(PFNGLUNIFORM2FPROC, Uniform2f)                                       \
    X(PFNGLUNIFORM4FPROC, Uniform4f)                                       \
    X(PFNGLUNIFORM4IPROC, Uniform4i)                                       \
    X(PFNGLDRAWARRAYSPROC, DrawArrays)                                     \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate)                       \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate)               \
    X(PFNGLSTENCILFUNCPROC, StencilFunc)                                   \
    X(PFNGLSTENCILOPPROC, StencilOp)                                       \
    X(PFNGLCLEARSTENCILPROC, ClearStencil)                                 \
    X(PFNGLCOLORMASKPROC, ColorMask)                                       \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)                         \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)                   \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)                         \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage)                   \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer)           \
    X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer)

typedef struct Target {
    GLuint tex, fbo;
    int w, h;                    /* 1x size */
    GLuint stencil;              /* depth24/stencil8, once mask-check is used */
} Target;

typedef struct Snapshot {
    GLuint tex;
    int w, h;                    /* device pixels */
} Snapshot;

struct BfmGlExec {
#define X(type, name) type name;
    BFM_GL_FUNCS(X)
#undef X
    GLuint prog, blit, vao, vbo, empty_vao, vram_tex, white_tex, image_tex;
    Target tgt[3];               /* VRAM, scene 0, scene 1 */
    Snapshot snap[BFM_GL_MAX_SNAPSHOTS];
    GLuint snap_fbo;
    int scale;
    GLint u_size, u_flip, u_pass, u_pass_bit, u_dither, u_scale, u_win, u_vram, u_repl;
    GLint b_src, b_tex, b_alpha;
    /* redundant-state filter for run_cmds (-2 = unknown) */
    int cur_blend, cur_pass, cur_dither;
    BfmPlatRect cur_win;
    unsigned err;
};

static const char *vs_src =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec4 a_color;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "layout(location = 3) in uvec4 a_info;\n"
    "layout(location = 4) in vec2 a_st;\n"
    "uniform vec2 u_size;\n"
    "uniform int u_flip;\n"
    "out vec4 v_color;\n"
    "noperspective out vec2 v_uv;\n"
    "flat out uvec4 v_info;\n"
    "noperspective out vec2 v_st;\n"
    "void main() {\n"
    "    vec2 n = a_pos / u_size * 2.0 - 1.0;\n"
    "    if (u_flip != 0) n.y = -n.y;\n"
    "    gl_Position = vec4(n, 0.0, 1.0);\n"
    "    v_color = a_color; v_uv = a_uv; v_info = a_info; v_st = a_st;\n"
    "}\n";

/* Mirrors bfm_gl_sample / bfm_gl_modulate / bfm_gl_dither5. */
static const char *fs_src =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "noperspective in vec2 v_uv;\n"
    "flat in uvec4 v_info;\n"
    "noperspective in vec2 v_st;\n"
    "uniform usampler2D u_vram;\n"
    "uniform sampler2D u_repl;\n"
    "uniform int u_pass;\n"
    "uniform int u_pass_bit;\n"
    "uniform int u_dither;\n"
    "uniform int u_scale;\n"
    "uniform ivec4 u_win;\n"
    "layout(location = 0, index = 0) out vec4 o_color;\n"
    "layout(location = 0, index = 1) out vec4 o_factor;\n"
    "const int dm[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);\n"
    "uint vr(int x, int y) { return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "int wnd(int u, int off, int size) {\n"
    "    if (size <= 0) return u;\n"
    "    return (u & (size - 1)) | (off & ~(size - 1) & 255);\n"
    "}\n"
    "void main() {\n"
    "    uint flags = v_info.z;\n"
    "    vec3 rgb = v_color.rgb;\n"
    "    bool stp = false;\n"
    "    if ((flags & 1u) != 0u) {\n"
    "        vec3 t;\n"
    "        if ((flags & 16u) != 0u) {\n"
    "            vec4 r = texture(u_repl, v_st);\n"
    "            stp = r.a > 0.5;\n"
    "            if (!stp && all(lessThan(r.rgb, vec3(0.5 / 255.0)))) discard;\n"
    "            t = r.rgb;\n"
    "        } else if (v_info.w != 0u) {\n"
    "            vec4 r = texture(u_repl, v_st);\n"
    "            if (r.a < 0.5) discard;\n"
    "            t = r.rgb;\n"
    "        } else {\n"
    "            uint p;\n"
    "            if ((flags & 8u) != 0u) {\n"
    "                p = vr(int(floor(v_uv.x)), int(floor(v_uv.y)));\n"
    "            } else {\n"
    "                int u = int(floor(v_uv.x)) & 255, v = int(floor(v_uv.y)) & 255;\n"
    "                uint tp = v_info.y, cl = v_info.x, d = (tp >> 7) & 3u;\n"
    "                int bx = int(tp & 15u) * 64, by = int((tp >> 4) & 1u) * 256;\n"
    "                int cx = int(cl & 63u) * 16, cy = int((cl >> 6) & 511u);\n"
    "                u = wnd(u, u_win.x, u_win.z);\n"
    "                v = wnd(v, u_win.y, u_win.w);\n"
    "                if (d == 0u) {\n"
    "                    uint w = vr(bx + u / 4, by + v);\n"
    "                    p = vr(cx + int((w >> uint((u & 3) * 4)) & 15u), cy);\n"
    "                } else if (d == 1u) {\n"
    "                    uint w = vr(bx + u / 2, by + v);\n"
    "                    p = vr(cx + int((w >> uint((u & 1) * 8)) & 255u), cy);\n"
    "                } else {\n"
    "                    p = vr(bx + u, by + v);\n"
    "                }\n"
    "                if (p == 0u) discard;\n"
    "            }\n"
    "            stp = (p & 0x8000u) != 0u;\n"
    "            t = vec3(float(p & 31u), float((p >> 5) & 31u), float((p >> 10) & 31u)) / 31.0;\n"
    "        }\n"

    "        rgb = (flags & 2u) != 0u ? t : min(t * v_color.rgb * (255.0 / 128.0), vec3(1.0));\n"
    "    }\n"
    "    if (u_dither != 0 && (flags & 4u) != 0u) {\n"
    "        ivec2 q = ivec2(gl_FragCoord.xy) / u_scale;\n"
    "        float d = float(dm[(q.y & 3) * 4 + (q.x & 3)]);\n"
    "        vec3 c8 = clamp(floor(rgb * 255.0 + 0.5) + d, 0.0, 255.0);\n"
    "        rgb = floor(c8 / 8.0) / 31.0;\n"
    "    }\n"
    "    /* two-pass draws: pass 1 keeps fragments whose bit is 0, pass 2 those\n"
    "     * whose bit is 1; the bit is STP (mode-2 split) or the written bit 15\n"
    "     * (stencil upkeep). Untextured fragments have STP 0. */\n"
    "    {\n"
    "        bool bit = u_pass_bit != 0 ? (stp || (flags & 32u) != 0u) : stp;\n"
    "        if (u_pass == 1 && bit) discard;\n"
    "        if (u_pass == 2 && !bit) discard;\n"
    "    }\n"
    "    /* semi-transparency per fragment (dual-source): untextured pixels,\n"
    "     * STP texels and replacement texels blend; the rest are opaque.\n"
    "     * Factors are bfm_gl_blend_state's; mode 2 draws use a\n"
    "     * reverse-subtract equation and ignore o_factor. */\n"
    "    float sf = 1.0, df = 0.0;\n"
    "    if ((flags & 64u) != 0u && ((flags & 1u) == 0u || stp || v_info.w != 0u)) {\n"
    "        uint mode = (flags >> 8) & 3u;\n"
    "        df = 1.0;\n"
    "        if (mode == 0u) { sf = 0.5; df = 0.5; }\n"
    "        else if (mode == 3u) sf = 0.25;\n"
    "    }\n"
    "    /* alpha carries the PS1 mask bit (bit 15) of the written pixel */\n"
    "    o_color = vec4(rgb * sf, (stp || (flags & 32u) != 0u) ? 1.0 : 0.0);\n"
    "    o_factor = vec4(df, df, df, 0.0);\n"
    "}\n";

static const char *blit_vs =
    "#version 330 core\n"
    "uniform vec4 u_src;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    gl_Position = vec4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = vec2(mix(u_src.x, u_src.z, c.x), mix(u_src.y, u_src.w, c.y));\n"
    "}\n";

static const char *blit_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_alpha_test;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec4 c = texture(u_tex, v_uv);\n"
    "    if (u_alpha_test != 0 && c.a < 0.5) discard;\n"
    "    o_color = vec4(c.rgb, 1.0);\n"
    "}\n";

static void note_err(BfmGlExec *x) {
    GLenum e;
    while ((e = x->GetError()) != GL_NO_ERROR)
        if (!x->err) x->err = e;
}

unsigned bfm_gl_exec_error(BfmGlExec *x) {
    unsigned e;
    note_err(x);
    e = x->err;
    x->err = 0;
    return e;
}

static GLuint compile(BfmGlExec *x, GLenum kind, const char *src, char *err,
                      size_t n) {
    GLuint s = x->CreateShader(kind);
    GLint ok = 0;
    x->ShaderSource(s, 1, &src, NULL);
    x->CompileShader(s);
    x->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (err && n) x->GetShaderInfoLog(s, (GLsizei)n, NULL, err);
        x->DeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(BfmGlExec *x, const char *vs, const char *fs, char *err,
                   size_t n) {
    GLuint v = compile(x, GL_VERTEX_SHADER, vs, err, n), f, p;
    GLint ok = 0;
    if (!v) return 0;
    if (!(f = compile(x, GL_FRAGMENT_SHADER, fs, err, n))) {
        x->DeleteShader(v);
        return 0;
    }
    p = x->CreateProgram();
    x->AttachShader(p, v);
    x->AttachShader(p, f);
    x->LinkProgram(p);
    x->DeleteShader(v);
    x->DeleteShader(f);
    x->GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        if (err && n) x->GetProgramInfoLog(p, (GLsizei)n, NULL, err);
        x->DeleteProgram(p);
        return 0;
    }
    return p;
}

static void target_free(BfmGlExec *x, Target *t) {
    if (t->stencil) x->DeleteRenderbuffers(1, &t->stencil);
    if (t->fbo) x->DeleteFramebuffers(1, &t->fbo);
    if (t->tex) x->DeleteTextures(1, &t->tex);
    memset(t, 0, sizeof *t);
}

static int target_alloc(BfmGlExec *x, Target *t, int w, int h) {
    target_free(x, t);
    x->GenTextures(1, &t->tex);
    x->BindTexture(GL_TEXTURE_2D, t->tex);
    x->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w * x->scale, h * x->scale, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    x->GenFramebuffers(1, &t->fbo);
    x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    x->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            t->tex, 0);
    if (x->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        x->BindFramebuffer(GL_FRAMEBUFFER, 0);
        target_free(x, t);
        return BFM_PLAT_ERROR;
    }
    x->Disable(GL_SCISSOR_TEST);
    x->ClearColor(0, 0, 0, 0);   /* alpha = mask bit 0 */
    x->Clear(GL_COLOR_BUFFER_BIT);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
    /* targets can be (re)allocated in the middle of a command list: give
     * unit 0 back to the VRAM texture the draw shader samples */
    x->ActiveTexture(GL_TEXTURE0);
    x->BindTexture(GL_TEXTURE_2D, x->vram_tex);
    t->w = w;
    t->h = h;
    return BFM_PLAT_OK;
}

BfmGlExec *bfm_gl_exec_create(BfmGlGetProc gp, int scale, char *err, size_t n) {
    BfmGlExec *x;
    static const uint8_t white[4] = {255, 255, 255, 255};
    const char *missing = NULL;
    if (err && n) err[0] = '\0';
    if (!gp) return NULL;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    if (!(x = (BfmGlExec *)calloc(1, sizeof *x))) return NULL;
    /* object -> function pointer via memcpy (POSIX representation;
     * ISO C has no cast for it) */
#define X(type, name)                                                      \
    {                                                                      \
        void *p_ = gp("gl" #name);                                         \
        memcpy(&x->name, &p_, sizeof p_);                                  \
        if (!p_ && !missing) missing = "gl" #name;                         \
    }
    BFM_GL_FUNCS(X)
#undef X
    if (missing) {
        if (err && n) snprintf(err, n, "GL function %s not available", missing);
        free(x);
        return NULL;
    }
    x->scale = scale;
    if (!(x->prog = link(x, vs_src, fs_src, err, n)) ||
        !(x->blit = link(x, blit_vs, blit_fs, err, n))) {
        bfm_gl_exec_destroy(x);
        return NULL;
    }
    x->u_size = x->GetUniformLocation(x->prog, "u_size");
    x->u_flip = x->GetUniformLocation(x->prog, "u_flip");
    x->u_pass = x->GetUniformLocation(x->prog, "u_pass");
    x->u_pass_bit = x->GetUniformLocation(x->prog, "u_pass_bit");
    x->u_dither = x->GetUniformLocation(x->prog, "u_dither");
    x->u_scale = x->GetUniformLocation(x->prog, "u_scale");
    x->u_win = x->GetUniformLocation(x->prog, "u_win");
    x->u_vram = x->GetUniformLocation(x->prog, "u_vram");
    x->u_repl = x->GetUniformLocation(x->prog, "u_repl");
    x->b_src = x->GetUniformLocation(x->blit, "u_src");
    x->b_tex = x->GetUniformLocation(x->blit, "u_tex");
    x->b_alpha = x->GetUniformLocation(x->blit, "u_alpha_test");
    x->GenFramebuffers(1, &x->snap_fbo);

    x->GenVertexArrays(1, &x->vao);
    x->GenVertexArrays(1, &x->empty_vao);
    x->GenBuffers(1, &x->vbo);
    x->BindVertexArray(x->vao);
    x->BindBuffer(GL_ARRAY_BUFFER, x->vbo);
    x->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BfmGlVertex),
                           (const void *)offsetof(BfmGlVertex, x));
    x->VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BfmGlVertex),
                           (const void *)offsetof(BfmGlVertex, r));
    x->VertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BfmGlVertex),
                           (const void *)offsetof(BfmGlVertex, u));
    x->VertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, sizeof(BfmGlVertex),
                            (const void *)offsetof(BfmGlVertex, clut));
    x->VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(BfmGlVertex),
                           (const void *)offsetof(BfmGlVertex, s));
    x->EnableVertexAttribArray(0);
    x->EnableVertexAttribArray(1);
    x->EnableVertexAttribArray(2);
    x->EnableVertexAttribArray(3);
    x->EnableVertexAttribArray(4);
    x->BindVertexArray(0);

    x->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    x->PixelStorei(GL_PACK_ALIGNMENT, 1);
    x->GenTextures(1, &x->vram_tex);
    x->BindTexture(GL_TEXTURE_2D, x->vram_tex);
    x->TexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, BFM_GL_VRAM_W, BFM_GL_VRAM_H, 0,
                  GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    x->white_tex = bfm_gl_exec_texture(x, 1, 1, white);

    if (target_alloc(x, &x->tgt[0], BFM_GL_VRAM_W, BFM_GL_VRAM_H) != BFM_PLAT_OK) {
        if (err && n) snprintf(err, n, "VRAM render target incomplete at %dx", scale);
        bfm_gl_exec_destroy(x);
        return NULL;
    }
    note_err(x);
    if (x->err) {
        if (err && n) snprintf(err, n, "GL error 0x%x during setup", x->err);
        bfm_gl_exec_destroy(x);
        return NULL;
    }
    return x;
}

void bfm_gl_exec_destroy(BfmGlExec *x) {
    int i;
    if (!x) return;
    for (i = 0; i < 3; i++) target_free(x, &x->tgt[i]);
    for (i = 0; i < BFM_GL_MAX_SNAPSHOTS; i++)
        if (x->snap[i].tex) x->DeleteTextures(1, &x->snap[i].tex);
    if (x->snap_fbo) x->DeleteFramebuffers(1, &x->snap_fbo);
    if (x->vram_tex) x->DeleteTextures(1, &x->vram_tex);
    if (x->white_tex) x->DeleteTextures(1, &x->white_tex);
    if (x->image_tex) x->DeleteTextures(1, &x->image_tex);
    if (x->vbo) x->DeleteBuffers(1, &x->vbo);
    if (x->vao) x->DeleteVertexArrays(1, &x->vao);
    if (x->empty_vao) x->DeleteVertexArrays(1, &x->empty_vao);
    if (x->prog) x->DeleteProgram(x->prog);
    if (x->blit) x->DeleteProgram(x->blit);
    free(x);
}

int bfm_gl_exec_scale(const BfmGlExec *x) { return x ? x->scale : 1; }

int bfm_gl_exec_set_scale(BfmGlExec *x, int scale) {
    int i;
    if (!x || scale < 1 || scale > 8) return BFM_PLAT_INVALID;
    if (scale == x->scale) return BFM_PLAT_OK;
    x->scale = scale;
    for (i = 1; i < 3; i++) target_free(x, &x->tgt[i]);
    return target_alloc(x, &x->tgt[0], BFM_GL_VRAM_W, BFM_GL_VRAM_H);
}

void bfm_gl_exec_sync_vram(BfmGlExec *x, BfmGlVram *v) {
    int y;
    if (!x || !v || !v->dirty) return;
    x->BindTexture(GL_TEXTURE_2D, x->vram_tex);
    x->PixelStorei(GL_UNPACK_ALIGNMENT, 2);
    for (y = v->dy0; y < v->dy1; y++)
        x->TexSubImage2D(GL_TEXTURE_2D, 0, v->dx0, y, v->dx1 - v->dx0, 1,
                         GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                         &v->px[(size_t)y * BFM_GL_VRAM_W + v->dx0]);
    x->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    v->dirty = 0;
}

/* The render target for a command target, sized for the batch. */
static Target *target_for(BfmGlExec *x, const BfmGlBatch *b, int target) {
    Target *t;
    int w, h;
    if (target == BFM_GL_TARGET_VRAM) return &x->tgt[0];
    if (target != BFM_GL_TARGET_SCENE0 && target != BFM_GL_TARGET_SCENE1) return NULL;
    t = &x->tgt[target];
    bfm_gl_batch_target_size(b, target, &w, &h);
    if ((t->w != w || t->h != h) && target_alloc(x, t, w, h) != BFM_PLAT_OK)
        return NULL;
    return t;
}

static void set_blend(BfmGlExec *x, int mode) {
    if (mode == x->cur_blend) return;
    x->cur_blend = mode;
    if (mode < 0) {
        x->Disable(GL_BLEND);
        return;
    }
    x->Enable(GL_BLEND);
    if (mode == BFM_GL_BLEND_DUAL) {
        /* dst * src1 + src (the shader premultiplies by the src factor);
         * alpha = the fragment's mask bit (src1.a = 0) */
        x->BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        x->BlendFuncSeparate(GL_ONE, GL_SRC1_COLOR, GL_ONE, GL_SRC1_ALPHA);
        return;
    }
    {
        BfmGlBlendState st = bfm_gl_blend_state(mode);
        /* colour blends; alpha (the mask bit) is written as the fragment's */
        x->BlendEquationSeparate(st.equation == BFM_GL_FUNC_REVERSE_SUBTRACT
                                     ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD,
                                 GL_FUNC_ADD);
        x->BlendFuncSeparate(st.src == BFM_GL_CONSTANT_COLOR ? GL_CONSTANT_COLOR : GL_ONE,
                             st.dst == BFM_GL_CONSTANT_COLOR ? GL_CONSTANT_COLOR : GL_ONE,
                             GL_ONE, GL_ZERO);
        x->BlendColor(st.constant, st.constant, st.constant, st.constant);
    }
}

/* Binds the draw program and its fixed state (after anything that changed
 * the program, VAO or texture units). */
static void draw_state(BfmGlExec *x) {
    x->cur_blend = -2;
    x->cur_pass = -2;
    x->cur_dither = -2;
    x->cur_win.w = -1;
    x->UseProgram(x->prog);
    x->Uniform1i(x->u_vram, 0);
    x->Uniform1i(x->u_repl, 1);
    x->ActiveTexture(GL_TEXTURE0);
    x->BindTexture(GL_TEXTURE_2D, x->vram_tex);
    x->BindVertexArray(x->vao);
}

/* Mask-check needs a stencil copy of the mask bits. The first time a
 * target sees a check, attach one and build it from the alpha channel
 * (which always holds the mask bit), through a temporary copy so the
 * target is never sampled while bound. From then on every draw keeps the
 * stencil in step. */
static int stencil_enable(BfmGlExec *x, Target *t) {
    int w = t->w * x->scale, h = t->h * x->scale;
    GLuint tmp = 0, tmp_fbo = 0;
    if (t->stencil) return BFM_PLAT_OK;
    x->GenRenderbuffers(1, &t->stencil);
    x->BindRenderbuffer(GL_RENDERBUFFER, t->stencil);
    x->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    x->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_RENDERBUFFER, t->stencil);
    if (x->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        x->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_RENDERBUFFER, 0);
        x->DeleteRenderbuffers(1, &t->stencil);
        t->stencil = 0;
        return BFM_PLAT_ERROR;
    }
    /* copy colour+alpha out */
    x->GenTextures(1, &tmp);
    x->BindTexture(GL_TEXTURE_2D, tmp);
    x->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    x->GenFramebuffers(1, &tmp_fbo);
    x->BindFramebuffer(GL_DRAW_FRAMEBUFFER, tmp_fbo);
    x->FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tmp, 0);
    x->BindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
    x->Disable(GL_SCISSOR_TEST);
    x->BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    /* stencil = 1 where alpha is set */
    x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    x->Viewport(0, 0, w, h);
    x->ClearStencil(0);
    x->Clear(GL_STENCIL_BUFFER_BIT);
    x->Enable(GL_STENCIL_TEST);
    x->StencilFunc(GL_ALWAYS, 1, 0xFF);
    x->StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    x->ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    x->Disable(GL_BLEND);
    x->UseProgram(x->blit);
    x->Uniform1i(x->b_tex, 0);
    x->Uniform1i(x->b_alpha, 1);
    x->Uniform4f(x->b_src, 0.0f, 1.0f, 1.0f, 0.0f);   /* row r -> row r */
    x->ActiveTexture(GL_TEXTURE0);
    x->BindTexture(GL_TEXTURE_2D, tmp);
    x->BindVertexArray(x->empty_vao);
    x->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    x->Uniform1i(x->b_alpha, 0);
    x->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    x->Disable(GL_STENCIL_TEST);
    x->DeleteFramebuffers(1, &tmp_fbo);
    x->DeleteTextures(1, &tmp);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return BFM_PLAT_OK;
}

/* SNAPSHOT: copy a target-local rect at device resolution. */
static void take_snapshot(BfmGlExec *x, Target *t, const BfmGlCmd *c) {
    Snapshot *sn;
    int s = x->scale, w = c->rect.w * s, h = c->rect.h * s;
    if (!c->snap || c->snap > BFM_GL_MAX_SNAPSHOTS) return;
    sn = &x->snap[c->snap - 1];
    if (!sn->tex) {
        x->GenTextures(1, &sn->tex);
        x->BindTexture(GL_TEXTURE_2D, sn->tex);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (sn->w != w || sn->h != h) {
        x->BindTexture(GL_TEXTURE_2D, sn->tex);
        x->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        sn->w = w;
        sn->h = h;
    }
    x->BindFramebuffer(GL_DRAW_FRAMEBUFFER, x->snap_fbo);
    x->FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            sn->tex, 0);
    x->BindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
    x->Disable(GL_SCISSOR_TEST);
    x->BlitFramebuffer(c->rect.x * s, c->rect.y * s, (c->rect.x + c->rect.w) * s,
                       (c->rect.y + c->rect.h) * s, 0, 0, w, h, GL_COLOR_BUFFER_BIT,
                       GL_NEAREST);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* pass: 0 all fragments, 1 bit clear, 2 bit set; by_written: the bit is
 * the written bit 15 instead of STP */
static void set_pass(BfmGlExec *x, int pass, int by_written) {
    int v = pass | (by_written << 4);
    if (v == x->cur_pass) return;
    x->cur_pass = v;
    x->Uniform1i(x->u_pass, pass);
    x->Uniform1i(x->u_pass_bit, by_written);
}

static void stencil_pass(BfmGlExec *x, int check, int ref) {
    /* check: pass only where the stored bit is 0; the REPLACE value is the
     * bit this pass writes (GREATER with ref 1 is "stencil < 1") */
    if (check) x->StencilFunc(ref ? GL_GREATER : GL_EQUAL, ref, 1);
    else x->StencilFunc(GL_ALWAYS, ref, 1);
    x->StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
}

static int readback_target(BfmGlExec *x, Target *t, const BfmPlatRect *r, uint16_t *out);

/* Draws one command list's commands for a bound target. win_vp: for the
 * window target, the viewport (x, y, w, h) in window pixels. */
static void run_cmds(BfmGlExec *x, const BfmGlBatch *b, int dither,
                     const int *win_vp) {
    size_t i;
    GLuint bound_repl = (GLuint)-1;
    int cur_target = -1, scale = x->scale, stencil = 0;
    Target *t = NULL;
    draw_state(x);
    for (i = 0; i < b->nc; i++) {
        const BfmGlCmd *c = &b->c[i];
        int w, h;
        if (c->kind == BFM_GL_CMD_SNAPSHOT || c->kind == BFM_GL_CMD_RESOLVE) {
            Target *src = target_for(x, b, c->target);
            if (!src) continue;
            if (c->kind == BFM_GL_CMD_SNAPSHOT) {
                take_snapshot(x, src, c);
            } else if (b->vram) {
                uint16_t *tmp = (uint16_t *)malloc((size_t)c->rect.w * c->rect.h * 2u);
                if (tmp && readback_target(x, src, &c->rect, tmp) == BFM_PLAT_OK) {
                    bfm_gl_vram_upload(b->vram, &c->vrect, tmp);
                    x->ActiveTexture(GL_TEXTURE0);
                    bfm_gl_exec_sync_vram(x, b->vram);
                }
                free(tmp);
            }
            draw_state(x);
            cur_target = -1;
            bound_repl = (GLuint)-1;
            continue;
        }
        if (c->target != cur_target) {
            if (c->target == BFM_GL_TARGET_WINDOW) {
                if (!win_vp) continue;
                x->BindFramebuffer(GL_FRAMEBUFFER, 0);
                x->Viewport(win_vp[0], win_vp[1], win_vp[2], win_vp[3]);
                x->Disable(GL_SCISSOR_TEST);
                x->Disable(GL_STENCIL_TEST);
                bfm_gl_batch_target_size(b, BFM_GL_TARGET_WINDOW, &w, &h);
                x->Uniform1i(x->u_flip, 1);
                x->Uniform1i(x->u_scale, 1);
                t = NULL;
                stencil = 0;
            } else {
                if (!(t = target_for(x, b, c->target))) continue;
                x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
                x->Viewport(0, 0, t->w * scale, t->h * scale);
                x->Enable(GL_SCISSOR_TEST);
                w = t->w;
                h = t->h;
                x->Uniform1i(x->u_flip, 0);
                x->Uniform1i(x->u_scale, scale);
                stencil = t->stencil != 0;
                if (stencil) x->Enable(GL_STENCIL_TEST);
                else x->Disable(GL_STENCIL_TEST);
            }
            x->Uniform2f(x->u_size, (float)w, (float)h);
            cur_target = c->target;
        }
        if (c->kind == BFM_GL_CMD_DRAW && c->mask_check && t && !t->stencil) {
            if (stencil_enable(x, t) == BFM_PLAT_OK) {
                draw_state(x);
                bound_repl = (GLuint)-1;
                x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
                x->Viewport(0, 0, t->w * scale, t->h * scale);
                x->Enable(GL_SCISSOR_TEST);
                x->Enable(GL_STENCIL_TEST);
                stencil = 1;
            }
        }
        if (c->target != BFM_GL_TARGET_WINDOW)
            x->Scissor(c->rect.x * scale, c->rect.y * scale, c->rect.w * scale,
                       c->rect.h * scale);
        if (c->kind == BFM_GL_CMD_FILL) {
            if (c->target == BFM_GL_TARGET_WINDOW) continue;
            x->ClearColor((float)(c->rgb & 0xFF) / 255.0f,
                          (float)((c->rgb >> 8) & 0xFF) / 255.0f,
                          (float)((c->rgb >> 16) & 0xFF) / 255.0f, 0.0f);
            if (stencil) x->ClearStencil(0);
            x->Clear(GL_COLOR_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0));
            continue;
        }
        if (!c->count) continue;
        {
            GLuint want = x->white_tex;
            if (c->snap && c->snap <= BFM_GL_MAX_SNAPSHOTS && x->snap[c->snap - 1].tex)
                want = x->snap[c->snap - 1].tex;
            else if (c->repl && b->repl && b->repl->r[c->repl - 1].live)
                want = b->repl->r[c->repl - 1].id;
            if (want != bound_repl) {
                x->ActiveTexture(GL_TEXTURE1);
                x->BindTexture(GL_TEXTURE_2D, want);
                x->ActiveTexture(GL_TEXTURE0);
                bound_repl = want;
            }
        }
        if (memcmp(&x->cur_win, &c->win, sizeof c->win) != 0) {
            x->Uniform4i(x->u_win, c->win.x, c->win.y, c->win.w, c->win.h);
            x->cur_win = c->win;
        }
        if (x->cur_dither != (dither && c->dither)) {
            x->cur_dither = dither && c->dither;
            x->Uniform1i(x->u_dither, x->cur_dither);
        }
        if (c->split_stp) {
            /* mode 2 textured: texels without STP opaque, then STP texels
             * subtracted; the command's prims all share mask_set */
            set_blend(x, -1);
            if (stencil) stencil_pass(x, c->mask_check, c->mask_set);
            set_pass(x, 1, 0);
            x->DrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
            set_blend(x, c->blend);
            if (stencil) stencil_pass(x, c->mask_check, 1);
            set_pass(x, 2, 0);
            x->DrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
        } else if (stencil && (c->textured || (c->mask_set && c->mask_clear))) {
            /* stencil upkeep: each fragment writes its bit 15 */
            set_blend(x, c->blend);
            stencil_pass(x, c->mask_check, 0);
            set_pass(x, 1, 1);
            x->DrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
            stencil_pass(x, c->mask_check, 1);
            set_pass(x, 2, 1);
            x->DrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
        } else {
            set_blend(x, c->blend);
            if (stencil) stencil_pass(x, c->mask_check, c->mask_set);
            set_pass(x, 0, 0);
            x->DrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
        }
    }
    set_blend(x, -1);
    x->Disable(GL_SCISSOR_TEST);
    x->Disable(GL_STENCIL_TEST);
    x->BindVertexArray(0);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void upload_verts(BfmGlExec *x, const BfmGlBatch *b) {
    x->BindBuffer(GL_ARRAY_BUFFER, x->vbo);
    x->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(b->nv * sizeof(BfmGlVertex)),
                  b->nv ? b->v : NULL, GL_STREAM_DRAW);
}

int bfm_gl_exec_run(BfmGlExec *x, const BfmGlBatch *b, int dither) {
    if (!x || !b) return BFM_PLAT_INVALID;
    if (!b->nc) return BFM_PLAT_OK;
    upload_verts(x, b);
    run_cmds(x, b, dither, NULL);
    note_err(x);
    return x->err ? BFM_PLAT_ERROR : BFM_PLAT_OK;
}

static int readback_target(BfmGlExec *x, Target *t, const BfmPlatRect *r, uint16_t *out) {
    uint8_t *buf;
    int s = x->scale, i, j;
    if (r->x < 0 || r->y < 0 || r->x + r->w > t->w || r->y + r->h > t->h)
        return BFM_PLAT_INVALID;
    buf = (uint8_t *)malloc((size_t)r->w * s * (size_t)r->h * s * 4u);
    if (!buf) return BFM_PLAT_ERROR;
    x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    x->ReadPixels(r->x * s, r->y * s, r->w * s, r->h * s, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
    for (j = 0; j < r->h; j++)
        for (i = 0; i < r->w; i++) {
            const uint8_t *p = buf + (((size_t)(j * s + s / 2) * (size_t)(r->w * s)) +
                                      (size_t)(i * s + s / 2)) * 4u;
            out[(size_t)j * r->w + i] = (uint16_t)((p[0] >> 3) | ((p[1] >> 3) << 5) |
                                                   ((p[2] >> 3) << 10) |
                                                   (p[3] >= 128 ? 0x8000 : 0));
        }
    free(buf);
    note_err(x);
    return x->err ? BFM_PLAT_ERROR : BFM_PLAT_OK;
}

int bfm_gl_exec_readback(BfmGlExec *x, const BfmGlBatch *b, int target,
                         const BfmPlatRect *r, uint16_t *out) {
    Target *t;
    if (!x || !b || !r || !out || r->w <= 0 || r->h <= 0) return BFM_PLAT_INVALID;
    if (!(t = target_for(x, b, target))) return BFM_PLAT_INVALID;
    return readback_target(x, t, r, out);
}

int bfm_gl_exec_readback_rgba(BfmGlExec *x, const BfmGlBatch *b, int target,
                              const BfmPlatRect *r, uint8_t *out) {
    Target *t;
    int s;
    if (!x || !b || !r || !out || r->w <= 0 || r->h <= 0) return BFM_PLAT_INVALID;
    if (!(t = target_for(x, b, target))) return BFM_PLAT_INVALID;
    if (r->x < 0 || r->y < 0 || r->x + r->w > t->w || r->y + r->h > t->h)
        return BFM_PLAT_INVALID;
    s = x->scale;
    x->BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    x->ReadPixels(r->x * s, r->y * s, r->w * s, r->h * s, GL_RGBA, GL_UNSIGNED_BYTE, out);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
    note_err(x);
    return x->err ? BFM_PLAT_ERROR : BFM_PLAT_OK;
}

void bfm_gl_exec_finish(BfmGlExec *x) {
    uint8_t px[4];
    /* a 1-pixel read is the portable "wait for the GPU" */
    if (!x) return;
    x->BindFramebuffer(GL_FRAMEBUFFER, x->tgt[0].fbo);
    x->ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

uint32_t bfm_gl_exec_texture(BfmGlExec *x, uint32_t w, uint32_t h,
                             const uint8_t *rgba) {
    GLuint t = 0;
    if (!x || !w || !h || !rgba) return 0;
    x->GenTextures(1, &t);
    x->BindTexture(GL_TEXTURE_2D, t);
    x->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, rgba);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    note_err(x);
    return t;
}

void bfm_gl_exec_texture_free(BfmGlExec *x, uint32_t id) {
    GLuint t = id;
    if (x && id) x->DeleteTextures(1, &t);
}

static void fit(int win_w, int win_h, double aspect, int vp[4]) {
    if (aspect <= 0.0) aspect = 4.0 / 3.0;
    if ((double)win_w / win_h > aspect) {
        vp[3] = win_h;
        vp[2] = (int)(win_h * aspect + 0.5);
    } else {
        vp[2] = win_w;
        vp[3] = (int)(win_w / aspect + 0.5);
    }
    vp[0] = (win_w - vp[2]) / 2;
    vp[1] = (win_h - vp[3]) / 2;
}

/* Clears the window and draws texture region (s0,t0)-(s1,t1), t0 = top,
 * into viewport vp. */
static void blit(BfmGlExec *x, GLuint tex, int win_w, int win_h, const int vp[4],
                 float s0, float t0, float s1, float t1) {
    x->BindFramebuffer(GL_FRAMEBUFFER, 0);
    x->Disable(GL_SCISSOR_TEST);
    x->Disable(GL_BLEND);
    x->Viewport(0, 0, win_w, win_h);
    x->ClearColor(0, 0, 0, 1);
    x->Clear(GL_COLOR_BUFFER_BIT);
    x->Viewport(vp[0], vp[1], vp[2], vp[3]);
    x->UseProgram(x->blit);
    x->Uniform1i(x->b_tex, 0);
    x->Uniform4f(x->b_src, s0, t0, s1, t1);
    x->ActiveTexture(GL_TEXTURE0);
    x->BindTexture(GL_TEXTURE_2D, tex);
    x->BindVertexArray(x->empty_vao);
    x->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    x->BindVertexArray(0);
}

int bfm_gl_exec_present_image(BfmGlExec *x, int w, int h, const uint8_t *rgba,
                              const BfmGlBatch *overlay, int win_w, int win_h,
                              double aspect) {
    int vp[4];
    if (!x || !rgba || w <= 0 || h <= 0 || win_w <= 0 || win_h <= 0)
        return BFM_PLAT_INVALID;
    if (!x->image_tex) {
        x->GenTextures(1, &x->image_tex);
        x->BindTexture(GL_TEXTURE_2D, x->image_tex);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        x->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    x->BindTexture(GL_TEXTURE_2D, x->image_tex);
    x->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    fit(win_w, win_h, aspect, vp);
    blit(x, x->image_tex, win_w, win_h, vp, 0.0f, 0.0f, 1.0f, 1.0f);
    if (overlay && overlay->nc) {
        upload_verts(x, overlay);
        run_cmds(x, overlay, 0, vp);
    }
    note_err(x);
    return x->err ? BFM_PLAT_ERROR : BFM_PLAT_OK;
}

int bfm_gl_exec_present(BfmGlExec *x, const BfmGlBatch *b,
                        const BfmGlBatch *overlay, int win_w, int win_h,
                        double aspect, int dither) {
    int slot, vp[4], ivp[4], tw;
    float s0, t0, s1, t1;
    Target *src;
    if (!x || !b || win_w <= 0 || win_h <= 0) return BFM_PLAT_INVALID;
    fit(win_w, win_h, aspect, vp);
    slot = bfm_gl_batch_scene_at(b, b->disp.x, b->disp.y);
    if (slot >= 0 && (src = target_for(x, b, BFM_GL_TARGET_SCENE0 + slot)) != NULL) {
        s0 = 0.0f; t0 = 0.0f; s1 = 1.0f; t1 = 1.0f;
        tw = src->w;
        /* the 4:3 display area inside the wide image, for the overlay */
        ivp[0] = vp[0] + (int)((double)vp[2] * b->margin / tw + 0.5);
        ivp[2] = (int)((double)vp[2] * b->disp.w / tw + 0.5);
    } else {
        src = &x->tgt[0];
        s0 = (float)b->disp.x / BFM_GL_VRAM_W;
        s1 = (float)(b->disp.x + b->disp.w) / BFM_GL_VRAM_W;
        t0 = (float)b->disp.y / BFM_GL_VRAM_H;
        t1 = (float)(b->disp.y + b->disp.h) / BFM_GL_VRAM_H;
        ivp[0] = vp[0];
        ivp[2] = vp[2];
    }
    ivp[1] = vp[1];
    ivp[3] = vp[3];
    blit(x, src->tex, win_w, win_h, vp, s0, t0, s1, t1);
    if (overlay && overlay->nc) {
        upload_verts(x, overlay);
        run_cmds(x, overlay, dither, ivp);
    }
    note_err(x);
    return x->err ? BFM_PLAT_ERROR : BFM_PLAT_OK;
}
