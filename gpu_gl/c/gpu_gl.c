/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The OpenGL backend of `gpu`: OpenGL 3.3 on Linux (X11 + GLX), macOS's OpenGL through AppKit,
 * OpenGL ES 3 on Android and WebGL2 in a page, behind the table gpu/c/gpu_backend.h declares.
 *
 * The window hosts, the GL function table and the texture and framebuffer code are the
 * graphics library's own (gl_base.h, derived from its C by derive_base.sh).  What is written
 * here is what `gpu` adds to them: a pipeline (a program with its inputs, its uniform block
 * and its render state), buffers named in the pass, and the pass itself.
 *
 * What a shader source for this backend follows (tag "glsl-330"):
 *   - the inputs read per vertex are `layout(location = 0..)` in the program's order, the
 *     inputs read per copy follow them;
 *   - the uniform block is the program's one `layout(std140) uniform` block;
 *   - the sampler of texture slot n is named `tex<n>` (`tex0`, `tex1`, ...). */
#include "gl_base.h"
#include "gpu/gpu_backend.h"
#include "checked.h" /* GPU_GL_CHECKED or nothing: c/checked.sh sets it for a checked test run */

#if defined(GQ_X11) || defined(GQ_COCOA) || defined(GQ_BROWSER) || defined(GQ_ANDROID)

#define GQ_UNIFORM_BUFFER 0x8A11
#define GQ_MAX_SLOTS 16
#define GQ_ACTIVE_UNIFORM_BLOCKS 0x8A36
#define GQ_UNIFORM_OFFSET 0x8A3B
#define GQ_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#define GQ_UNIFORM_BLOCK_ACTIVE_UNIFORMS 0x8A42
#define GQ_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES 0x8A43

/* The GL entry points a uniform block needs, which graphics' table does not hold. */
typedef struct {
    void (*glUniformBlockBinding)(unsigned, unsigned, unsigned);
    void (*glBindBufferBase)(unsigned, unsigned, unsigned);
    void (*glGetActiveUniformBlockiv)(unsigned, unsigned, unsigned, int *);
    void (*glGetActiveUniformsiv)(unsigned, int, const unsigned *, unsigned, int *);
    /* What a draw's depth is compared by; NULL in a page, which gives this library none. */
    void (*glDepthFunc)(unsigned);
#if defined(GPU_GL_CHECKED)
    /* For the checked build: the error a call left, and the driver's own messages where it has
     * them (KHR_debug: Mesa, not Apple's OpenGL).  NULL in a page. */
    unsigned (*glGetError)(void);
    void (*glDebugMessageCallback)(void (*)(unsigned, unsigned, unsigned, unsigned, int,
                                            const char *, const void *),
                                   const void *);
#endif
} gq_block_api;

#if defined(GQ_BROWSER)
GQ_IMPORT(gg_glUniformBlockBinding) void gq_glUniformBlockBinding(unsigned, unsigned, unsigned);
GQ_IMPORT(gg_glBindBufferBase) void gq_glBindBufferBase(unsigned, unsigned, unsigned);
GQ_IMPORT(gg_glGetActiveUniformBlockiv)
void gq_glGetActiveUniformBlockiv(unsigned, unsigned, unsigned, int *);
GQ_IMPORT(gg_glGetActiveUniformsiv)
void gq_glGetActiveUniformsiv(unsigned, int, const unsigned *, unsigned, int *);
#endif

struct gq_pipe;

/* What a device holds beyond its window: the block entry points, a vertex array to upload
 * index lists under, and of the pass in progress its pipeline, the texture named for each
 * slot and the slot named last (OpenGL's active unit); and where the window's frame is. */
typedef struct gq_dev {
    gq_window *w;
    gq_block_api more;
    int more_ok;
    unsigned scratch;
    struct gq_pipe *bound;
    int in_pass, unit;
    int64_t dropped; /* the pass's target, released inside the pass: it goes when that ends */
    int shown; /* the window's frame was presented and nothing was drawn in it since */
    unsigned unit_tex[GQ_MAX_SLOTS];
#if defined(GQ_X11)
    /* The frame last presented, copied before the swap (see `gq_keep_frame`). */
    void (*copy_tex)(unsigned, int, int, int, int, int, int, int); /* glCopyTexSubImage2D */
    unsigned kept_tex, kept_fbo;
    int kept_w, kept_h;
#endif
    struct gq_dev *next;
} gq_dev;

/* A pipeline: the linked program, its vertex array, its uniform buffer, and what the program
 * declared. */
typedef struct gq_pipe {
    gq_res *shader; /* holds the window */
    unsigned vao, ubo;
    gpu_state state;
    int vertex_n, vertex[GPU_MAX_INPUTS], vertex_floats;
    int instance_n, instance[GPU_MAX_INPUTS], instance_floats;
    struct gq_pipe *next;
} gq_pipe;

static gq_dev *gq_devs = NULL;
static gq_pipe *gq_pipes = NULL;

static int gq_bind_block_api(gq_dev *d) {
    gq_window *w = d->w;
    (void) w;
#if defined(GQ_X11)
#define GQ_MORE(name)                                                                             \
    (*(void **) (&d->more.name) = w->xa.glXGetProcAddressARB((const unsigned char *) #name),     \
     d->more.name != NULL)
#elif defined(GQ_COCOA)
#define GQ_MORE(name) (*(void **) (&d->more.name) = dlsym(gc.gl, #name), d->more.name != NULL)
#elif defined(GQ_ANDROID)
#define GQ_MORE(name) (*(void **) (&d->more.name) = dlsym(w->libgles, #name), d->more.name != NULL)
#else
#define GQ_MORE(name) (d->more.name = gq_##name, 1)
#endif
#if !defined(GQ_BROWSER)
    (void) GQ_MORE(glDepthFunc);
#if defined(GPU_GL_CHECKED)
    (void) GQ_MORE(glGetError);
    (void) GQ_MORE(glDebugMessageCallback);
#endif
#endif
    return GQ_MORE(glUniformBlockBinding) && GQ_MORE(glBindBufferBase) &&
           GQ_MORE(glGetActiveUniformBlockiv) && GQ_MORE(glGetActiveUniformsiv);
}

/* The OpenGL renderer of the window opened last, as its driver names it; empty in a page. */
static char gq_renderer_text[256];

static void gq_note_renderer(gq_dev *d) {
    const unsigned char *(*get)(unsigned) = NULL;
    const unsigned char *name;
    gq_window *w = d->w;
    (void) w;
#if defined(GQ_X11)
    *(void **) (&get) = w->xa.glXGetProcAddressARB((const unsigned char *) "glGetString");
#elif defined(GQ_COCOA)
    *(void **) (&get) = dlsym(gc.gl, "glGetString");
#elif defined(GQ_ANDROID)
    *(void **) (&get) = dlsym(w->libgles, "glGetString");
#endif
    if (!get || !gq_use(w, "gpu open")) return;
    name = get(0x1F01u); /* GL_RENDERER */
    if (!name) return;
    strncpy(gq_renderer_text, (const char *) name, sizeof gq_renderer_text - 1);
    gq_renderer_text[sizeof gq_renderer_text - 1] = 0;
}

const char *gq_renderer(void) {
    return gq_renderer_text;
}

static gq_dev *gq_dev_of(int64_t dev) {
    gq_window *w = gq_of(dev);
    gq_dev *d;
    for (d = gq_devs; d; d = d->next) {
        if (d->w == w) return d;
    }
    return NULL;
}

static gq_pipe *gq_pipe_of(int64_t h) {
    gq_pipe *p;
    for (p = gq_pipes; p; p = p->next) {
        if ((int64_t) (intptr_t) p == h) return p;
    }
    return NULL;
}

/* ── the checked build ─────────────────────────────────────────────────────────────────────── */

#if defined(GPU_GL_CHECKED)
/* Compiled in only when `c/checked.h` defines GPU_GL_CHECKED (`c/checked.sh`): the library as it
 * ships holds none of this and pays nothing for it.  In a checked build every call of the table
 * that reaches OpenGL is followed by a look at the error OpenGL kept, the driver's own messages
 * are turned on where it has them, and every handle a call uses is looked up first; each fault
 * is counted and written to stderr with the call it came from.  OpenGL takes a wrong call in
 * silence, so without this a mistake here shows as a wrong picture or not at all.
 * `GPU_GL_DEBUG=0` turns the checks of a checked build off, `abort` stops the program at the
 * first fault; `gq_check` does the first from the program. */
static int gq_checking = -1; /* -1 not asked yet, 0 off, 1 report, 2 abort */
static int64_t gq_fault_count = 0;
static const char *gq_now = "";       /* the table call being made */
static unsigned (*gq_error_of)(void); /* glGetError of the device opened last */
static int gq_driver_talks = 0;       /* the driver reports its own errors: do not count twice */

static int gq_check_mode(void) {
    if (gq_checking < 0) {
        const char *e = getenv("GPU_GL_DEBUG");
        gq_checking = e && !strcmp(e, "0") ? 0 : e && !strcmp(e, "abort") ? 2 : 1;
    }
    return gq_checking;
}

void gq_check(int64_t on) {
    if (on) { if (gq_check_mode() == 0) gq_checking = 1; } else gq_checking = 0;
}

int64_t gq_faults(void) {
    return gq_fault_count;
}

static void gq_fault(const char *what, const char *detail) {
    gq_fault_count++;
    fprintf(stderr, "gpu_gl: OpenGL %s in `%s`%s%s\n", what, gq_now, detail ? ": " : "",
            detail ? detail : "");
    if (gq_checking == 2) abort();
}

/* The driver's own word on a call (KHR_debug).  An error, undefined behaviour, and what is
 * deprecated or not portable are faults; its notes on speed and its notices are not. */
static void gq_driver_said(unsigned source, unsigned type, unsigned id, unsigned severity,
                           int length, const char *message, const void *user) {
    (void) id; (void) severity; (void) length; (void) user;
    /* A shader that does not compile is no fault of a call: the pipeline answers none and says
     * why itself.  (GL_DEBUG_SOURCE_SHADER_COMPILER) */
    if (source == 0x8248) return;
    if (type == 0x824C) gq_fault("error", message);
    else if (type == 0x824E) gq_fault("undefined behaviour", message);
    else if (type == 0x824D) gq_fault("deprecated use", message);
    else if (type == 0x824F) gq_fault("unportable use", message);
}

static const char *gq_error_name(unsigned e) {
    switch (e) {
    case 0x0500: return "error (invalid enum)";
    case 0x0501: return "error (invalid value)";
    case 0x0502: return "error (invalid operation)";
    case 0x0505: return "error (out of memory)";
    case 0x0506: return "error (invalid framebuffer operation)";
    default: return "error";
    }
}

/* After a table call: take every error OpenGL kept.  Where the driver talks they were reported
 * as they happened, with its own text. */
static void gq_after(void) {
    int i;
    if (gq_error_of) {
        for (i = 0; i < 16; i++) {
            unsigned e = gq_error_of();
            if (!e) break;
            if (!gq_driver_talks) gq_fault(gq_error_name(e), NULL);
        }
    }
    gq_now = "";
}

/* What the checked mode knows of every handle this backend gave out, and whether a call may
 * use or release one: gpu's register, which every backend's checked build shares. */
#define GPU_REGISTER_FAULT(detail) gq_fault("misuse", detail)
#define GPU_REGISTER_NOW gq_now
#include "gpu/gpu_register.h"
#define gq_born gpu_register_born
#define gq_live gpu_register_live
#define gq_may_release gpu_register_may_release
#define gq_device_closed gpu_register_device_closed
#endif /* GPU_GL_CHECKED */

/* ── the host part ─────────────────────────────────────────────────────────────────────────── */

static int64_t gq_t_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    int64_t h = gq_open(width, height, title, fullscreen);
    gq_dev *d;
    if (!h) return 0;
    d = (gq_dev *) calloc(1, sizeof(gq_dev));
    if (!d) {
        gq_close(h);
        return 0;
    }
    d->w = gq_of(h);
    d->more_ok = gq_bind_block_api(d);
    gq_note_renderer(d);
#if defined(GQ_X11) || defined(GQ_COCOA)
    /* A point has the size its vertex stage writes, as on every other stack: desktop OpenGL
     * reads `gl_PointSize` only when told to (GL_PROGRAM_POINT_SIZE), OpenGL ES always. */
    d->w->api.glEnable(0x8642);
#endif
#if defined(GPU_GL_CHECKED)
    if (gq_check_mode()) {
        gq_error_of = d->more.glGetError;
        gq_driver_talks = 0;
        if (d->more.glDebugMessageCallback) {
            d->w->api.glEnable(0x92E0); /* GL_DEBUG_OUTPUT */
            d->w->api.glEnable(0x8242); /* GL_DEBUG_OUTPUT_SYNCHRONOUS: reported inside the call */
            d->more.glDebugMessageCallback(gq_driver_said, NULL);
            /* a driver without the extension leaves an error for the enables; then it is silent */
            gq_driver_talks = !d->more.glGetError || d->more.glGetError() == 0;
            if (d->more.glGetError) while (d->more.glGetError()) {}
        }
    }
#endif
#if defined(GQ_X11)
    *(void **) (&d->copy_tex) =
        d->w->xa.glXGetProcAddressARB((const unsigned char *) "glCopyTexSubImage2D");
#endif
    d->next = gq_devs;
    gq_devs = d;
    return h;
}

static int64_t gq_t_size(int64_t dev, int64_t which) {
    return which ? gq_height(dev) : gq_width(dev);
}

static void gq_t_close(int64_t dev) {
    gq_dev **at, *d;
    for (at = &gq_devs; (d = *at) != NULL; at = &d->next) {
        if (d->w == gq_of(dev)) {
#if defined(GQ_X11)
            if ((d->kept_tex || d->kept_fbo) && gq_use(d->w, "gpu close")) {
                if (d->kept_fbo) d->w->api.glDeleteFramebuffers(1, &d->kept_fbo);
                if (d->kept_tex) d->w->api.glDeleteTextures(1, &d->kept_tex);
            }
#endif
            *at = d->next;
            if (d->dropped) gq_release(d->dropped);
            free(d);
            break;
        }
    }
    gq_close(dev);
}

/* ── buffers, textures, targets ────────────────────────────────────────────────────────────── */

static int64_t gq_t_buffer(int64_t dev, const float *data, int64_t n) {
    return gq_instances(dev, data, n);
}

/* A vertex array must be bound while an index list is uploaded; the device keeps one for it. */
static int gq_scratch(gq_dev *d) {
    if (!d->scratch) d->w->api.glGenVertexArrays(1, &d->scratch);
    d->w->api.glBindVertexArray(d->scratch);
    return d->scratch != 0;
}

static int64_t gq_t_index_buffer(int64_t dev, const int64_t *idx, int64_t n) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *r;
    uint32_t *words;
    int64_t i;
    gq_api *gl;
    if (!d || n < 0 || !gq_use(d->w, "gpu index_buffer")) return 0;
    words = (uint32_t *) malloc((size_t) (n > 0 ? n : 1) * sizeof(uint32_t));
    if (!words) return 0;
    for (i = 0; i < n; i++) words[i] = (uint32_t) idx[i];
    r = gq_new_res(d->w, GQ_BUFFER);
    if (!r) {
        free(words);
        return 0;
    }
    gl = &d->w->api;
    gq_scratch(d);
    gl->glGenBuffers(1, &r->id);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->id);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, (intptr_t) n * 4, words, GL_STATIC_DRAW);
    gl->glBindVertexArray(d->bound ? d->bound->vao : 0);
    r->indices = (int) n;
    free(words);
    return (int64_t) (intptr_t) r;
}

/* A colour texture is clamped at its edges whatever it was made for, as `gpu` promises. */
static int64_t gq_t_texture(int64_t dev, int64_t width, int64_t height, int64_t depth) {
    int64_t h = gq_texture(dev, width, height, depth);
    gq_res *r = gq_res_of(h);
    if (r && !depth) {
        gq_api *gl = &r->w->api;
        gl->glBindTexture(GL_TEXTURE_2D, r->id);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    return h;
}

static int64_t gq_t_target(int64_t dev, int64_t colour, int64_t depth) {
    int64_t fb = gq_framebuffer(dev);
    gq_res *f = gq_res_of(fb), *size = gq_res_of(colour ? colour : depth);
    if (!f) return 0;
    if (colour) gq_framebuffer_attach(fb, colour);
    if (depth) gq_framebuffer_attach(fb, depth);
    if (size) {
        f->width = size->width;
        f->height = size->height;
    }
    return fb;
}

/* OpenGL writes a texture, sets its filter and builds its smaller copies through the active
 * unit, and leaves that unit empty.  A pass in progress named a texture for it: that one is
 * bound again, so the pass's next draw reads what it was told to. */
static void gq_rebind(int64_t tex) {
    gq_res *r = gq_res_of(tex);
    gq_dev *d;
    if (!r || r->kind != GQ_TEXTURE) return;
    for (d = gq_devs; d; d = d->next) {
        if (d->w == r->w && d->in_pass) r->w->api.glBindTexture(GL_TEXTURE_2D, d->unit_tex[d->unit]);
    }
}

static void gq_t_texture_put(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                             const uint32_t *px, int64_t n) {
    gq_texture_put(tex, x, y, w, h, px, n);
    gq_rebind(tex);
}

static void gq_t_texture_nearest(int64_t tex, int64_t on) {
    gq_texture_filter(tex, on);
    gq_rebind(tex);
}

static void gq_t_texture_mipmaps(int64_t tex) {
    gq_texture_mipmaps(tex);
    gq_rebind(tex);
}

/* RGB565 values as 0xAARRGGBB, each opaque, a channel's high bits repeated in its low ones so
 * the brightest value is 255. */
static void gq_t_texture_put565(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                                const int32_t *px, int64_t n) {
    uint32_t *wide;
    int64_t i, count = w * h;
    if (count <= 0 || n < count) return;
    wide = (uint32_t *) malloc((size_t) count * sizeof(uint32_t));
    if (!wide) return;
    for (i = 0; i < count; i++) {
        uint32_t v = (uint32_t) px[i];
        uint32_t r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;
        wide[i] = 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
    }
    gq_t_texture_put(tex, x, y, w, h, wide, count);
    free(wide);
}

/* The window's frame after it was presented.  OpenGL shows it by swapping two buffers, so the
 * one drawn into next holds an older frame; where the frame itself still is differs:
 *   - X11: the front buffer is not kept for reading (Mesa under Xvfb reads it back black), so
 *     the device copies the frame into a texture of its own as it presents it
 *     (`gq_keep_frame`), and a capture reads that copy;
 *   - a Mac: the frame is the front buffer, read as it is (measured there with gpu_metal's
 *     comparison scenes). */
#if defined(GQ_X11)
/* Copy the window's frame, as drawn, into the device's own texture before it is swapped away:
 * one copy on the GPU, no wait for it.  A texture of the window's size, made again when the
 * window changed size. */
static void gq_keep_frame(gq_dev *d) {
    gq_window *w = d->w;
    gq_api *gl = &w->api;
    if (!d->copy_tex || w->width <= 0 || w->height <= 0 || !gq_use(w, "gpu present")) return;
    if (!d->kept_tex || d->kept_w != w->width || d->kept_h != w->height) {
        if (!d->kept_tex) gl->glGenTextures(1, &d->kept_tex);
        gl->glBindTexture(GL_TEXTURE_2D, d->kept_tex);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w->width, w->height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, NULL);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (!d->kept_fbo) gl->glGenFramebuffers(1, &d->kept_fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, d->kept_fbo);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   d->kept_tex, 0);
        d->kept_w = w->width;
        d->kept_h = w->height;
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glReadBuffer(GL_BACK);
    gl->glBindTexture(GL_TEXTURE_2D, d->kept_tex);
    d->copy_tex(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w->width, w->height);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, w->target);
}
#endif

#if defined(GQ_X11) || defined(GQ_COCOA)
/* The frame last presented, 0xAARRGGBB, the top row first; opaque, as a window has no alpha. */
static void gq_read_shown(gq_dev *d, uint32_t *out, int64_t n) {
    gq_window *w = d->w;
    unsigned char *rgba;
    int64_t x, y;
    if (!gq_use(w, "gpu capture") || n != (int64_t) w->width * w->height || n <= 0) return;
#if defined(GQ_X11)
    if (!d->kept_fbo || d->kept_w != w->width || d->kept_h != w->height) return;
#endif
    rgba = (unsigned char *) malloc((size_t) n * 4u);
    if (!rgba) return;
#if defined(GQ_X11)
    w->api.glBindFramebuffer(GL_FRAMEBUFFER, d->kept_fbo);
    w->api.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    w->api.glReadBuffer(GL_COLOR_ATTACHMENT0);
    w->api.glReadPixels(0, 0, w->width, w->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
#else
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    w->api.glReadBuffer(GL_FRONT);
    w->api.glReadPixels(0, 0, w->width, w->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    w->api.glReadBuffer(GL_BACK);
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
#endif
    for (y = 0; y < w->height; y++) {
        const unsigned char *row = rgba + (size_t) (w->height - 1 - y) * w->width * 4u;
        for (x = 0; x < w->width; x++) {
            const unsigned char *p = row + x * 4;
            out[y * w->width + x] = 0xFF000000u | ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
        }
    }
    free(rgba);
}
#endif

static void gq_t_present(int64_t dev) {
    gq_dev *d = gq_dev_of(dev);
#if defined(GQ_X11)
    if (d) gq_keep_frame(d);
#endif
    gq_swap(dev);
    if (d) d->shown = 1;
}

/* The pixels of `target`'s colour, or the window's, the top row first. */
static void gq_t_capture(int64_t dev, int64_t target, uint32_t *out, int64_t n) {
    gq_window *w = gq_of(dev);
    gq_res *f = gq_res_of(target);
    unsigned char *rgba;
    int64_t x, y;
    if (!f) {
#if defined(GQ_X11) || defined(GQ_COCOA)
        gq_dev *d = gq_dev_of(dev);
        if (d && d->shown) {
            gq_read_shown(d, out, n);
            return;
        }
#endif
        gq_read_pixels(dev, out, n);
        return;
    }
    if (f->kind != GQ_FRAMEBUFFER || !f->has_colour || !gq_use(w, "gpu capture")) return;
    if (n != (int64_t) f->width * f->height || n <= 0) return;
    rgba = (unsigned char *) malloc((size_t) n * 4u);
    if (!rgba) return;
    w->api.glBindFramebuffer(GL_FRAMEBUFFER, f->id);
    w->api.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    w->api.glReadBuffer(GL_COLOR_ATTACHMENT0);
    w->api.glReadPixels(0, 0, f->width, f->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
    for (y = 0; y < f->height; y++) {
        const unsigned char *row = rgba + (size_t) (f->height - 1 - y) * f->width * 4u;
        for (x = 0; x < f->width; x++) {
            const unsigned char *p = row + x * 4;
            out[y * f->width + x] = ((uint32_t) p[3] << 24) | ((uint32_t) p[0] << 16) |
                                    ((uint32_t) p[1] << 8) | p[2];
        }
    }
    free(rgba);
}

/* ── pipelines ─────────────────────────────────────────────────────────────────────────────── */

/* Whether the linked program's uniform block is laid out as the layer laid `p` out: the same
 * size in whole vectors, and every field the driver reports starting where a declared field starts.  Says both
 * on stderr when not. */
static int gq_block_matches(gq_dev *d, unsigned program, const gpu_program *p) {
    int size = 0, count = 0, i, j, ok = 1;
    unsigned indices[GPU_MAX_FIELDS * 2];
    int offsets[GPU_MAX_FIELDS * 2];
    d->more.glGetActiveUniformBlockiv(program, 0, GQ_UNIFORM_BLOCK_DATA_SIZE, &size);
    d->more.glGetActiveUniformBlockiv(program, 0, GQ_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &count);
    if (count < 0 || count > GPU_MAX_FIELDS * 2) count = 0;
    if (count > 0) {
        d->more.glGetActiveUniformBlockiv(program, 0, GQ_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,
                                          (int *) indices);
        d->more.glGetActiveUniformsiv(program, count, indices, GQ_UNIFORM_OFFSET, offsets);
    }
    /* A driver may report the size as it is or rounded up to a whole vector (WebGL2 reports
     * 20 bytes where OpenGL reports 32 for a vector and a float): compare the rounded one. */
    if ((size + 15) / 16 * 16 != (int) p->block_floats * 4) ok = 0;
    for (i = 0; ok && i < count; i++) {
        int found = 0;
        for (j = 0; j < (int) p->block_n; j++) {
            if (offsets[i] == (int) p->block[j].offset * 4) found = 1;
        }
        if (!found) ok = 0;
    }
    if (ok) return 1;
    fprintf(stderr, "gpu_gl: the shader's uniform block is not laid out as the program declares "
                    "it.\n  declared: %d bytes, fields at",
            (int) p->block_floats * 4);
    for (j = 0; j < (int) p->block_n; j++) fprintf(stderr, " %d", (int) p->block[j].offset * 4);
    fprintf(stderr, "\n  compiled: %d bytes, fields at", size);
    for (i = 0; i < count; i++) fprintf(stderr, " %d", offsets[i]);
    fprintf(stderr, "\n");
    return 0;
}

static int64_t gq_t_pipeline(int64_t dev, const gpu_program *p, const gpu_state *s) {
    gq_dev *d = gq_dev_of(dev);
    gq_api *gl;
    gq_pipe *pipe;
    gq_res *shader;
    int64_t sh;
    int blocks = 0, i;
    if (!d || !gq_use(d->w, "gpu pipeline")) return 0;
    if (!d->more_ok) {
        fprintf(stderr, "gpu_gl: this OpenGL has no uniform blocks\n");
        return 0;
    }
    if (s->depth == 3 && !d->more.glDepthFunc) {
        fprintf(stderr, "gpu_gl: this OpenGL cannot write a depth without testing it\n");
        return 0;
    }
    sh = gq_shader(dev, p->vertex_source, p->fragment_source);
    if (!sh) return 0;
    shader = gq_res_of(sh);
    gl = &d->w->api;
    gl->glGetProgramiv(shader->id, GQ_ACTIVE_UNIFORM_BLOCKS, &blocks);
    if (blocks > 1 || (blocks == 1 && p->block_n == 0)) {
        fprintf(stderr, "gpu_gl: the shader has %d uniform block(s) and the program declares %s\n",
                blocks, p->block_n ? "one" : "none");
        gq_release(sh);
        return 0;
    }
    if (blocks == 1 && !gq_block_matches(d, shader->id, p)) {
        gq_release(sh);
        return 0;
    }
    pipe = (gq_pipe *) calloc(1, sizeof(gq_pipe));
    if (!pipe) {
        gq_release(sh);
        return 0;
    }
    pipe->shader = shader;
    pipe->state = *s;
    pipe->vertex_n = (int) p->vertex_n;
    for (i = 0; i < pipe->vertex_n; i++) {
        pipe->vertex[i] = (int) p->vertex[i];
        pipe->vertex_floats += pipe->vertex[i];
    }
    pipe->instance_n = (int) p->instance_n;
    for (i = 0; i < pipe->instance_n; i++) {
        pipe->instance[i] = (int) p->instance[i];
        pipe->instance_floats += pipe->instance[i];
    }
    gl->glGenVertexArrays(1, &pipe->vao);
    gl->glUseProgram(shader->id);
    if (blocks == 1) {
        gl->glGenBuffers(1, &pipe->ubo);
        d->more.glUniformBlockBinding(shader->id, 0, 0);
    }
    for (i = 0; i < (int) p->textures; i++) {
        char name[16];
        int at;
        snprintf(name, sizeof name, "tex%d", i);
        at = gl->glGetUniformLocation(shader->id, name);
        if (at >= 0) gl->glUniform1i(at, i);
    }
    pipe->next = gq_pipes;
    gq_pipes = pipe;
    return (int64_t) (intptr_t) pipe;
}

static void gq_t_release(int64_t h) {
    gq_pipe **at, *p;
    for (at = &gq_pipes; (p = *at) != NULL; at = &p->next) {
        if ((int64_t) (intptr_t) p == h) {
            gq_window *w = p->shader->w;
            gq_dev *d;
            *at = p->next;
            for (d = gq_devs; d; d = d->next) {
                if (d->bound == p) d->bound = NULL;
            }
#if defined(GQ_BROWSER)
            if (1) {
#else
            if (!w->closed && gq_use(w, "a pipeline's drop")) {
#endif
                w->api.glDeleteVertexArrays(1, &p->vao);
                if (p->ubo) w->api.glDeleteBuffers(1, &p->ubo);
            }
            gq_release((int64_t) (intptr_t) p->shader);
#if defined(GPU_GL_CHECKED)
            /* what is read of it after this is nothing a draw could take for real */
            if (gq_checking > 0) memset(p, 0xDD, sizeof *p);
#endif
            free(p);
            return;
        }
    }
    if (1) { /* a texture a pass named is not bound again once it is gone */
        gq_res *r = gq_res_of(h);
        gq_dev *d;
        int i;
        /* The target a pass draws into is released when that pass ends: deleting the bound
         * framebuffer would turn the pass's remaining draws to the window. */
        for (d = gq_devs; r && r->kind == GQ_FRAMEBUFFER && d; d = d->next) {
            if (d->w == r->w && d->in_pass && !d->dropped && d->w->target == r->id) {
                d->dropped = h;
                return;
            }
        }
        for (d = gq_devs; r && r->kind == GQ_TEXTURE && d; d = d->next) {
            for (i = 0; i < GQ_MAX_SLOTS; i++) {
                if (d->w == r->w && d->unit_tex[i] == r->id) d->unit_tex[i] = 0;
            }
        }
    }
    gq_release(h);
}

/* ── a frame ───────────────────────────────────────────────────────────────────────────────── */

/* The pass no longer draws into its target: one released meanwhile is released now. */
static void gq_pass_over(int64_t dev, gq_dev *d) {
    int64_t h = d->dropped;
    if (!h) return;
    d->dropped = 0;
    gq_render_to(dev, 0);
    gq_release(h);
}

static void gq_t_begin(int64_t dev, int64_t target, int64_t load, int64_t colour) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *f;
    if (!d || !gq_use(d->w, "gpu begin")) return;
    if (target && target == d->dropped) return; /* released: nothing may name it again */
    gq_pass_over(dev, d);             /* a pass begun inside another ends that one */
    f = gq_res_of(target);
    d->bound = NULL;
    d->in_pass = 1;
    d->unit = 0;
    memset(d->unit_tex, 0, sizeof d->unit_tex);
    gq_render_to(dev, target);
    gq_viewport(dev, 0, 0, f ? f->width : d->w->width, f ? f->height : d->w->height);
    gq_capability(dev, 4, 0);
    d->w->api.glColorMask(1, 1, 1, 1);
    if (load == 0) {
        gq_depth_write(dev, 1); /* a clear writes only the depth a draw may write */
        gq_clear(dev, f ? colour : colour | (int64_t) 0xFF000000u);
    }
    /* a window has no alpha: it is cleared opaque and no draw writes it */
    if (!f) {
        d->w->api.glColorMask(1, 1, 1, 0);
        d->shown = 0;
    }
}

static void gq_t_use(int64_t dev, int64_t pipeline) {
    gq_dev *d = gq_dev_of(dev);
    gq_pipe *p = gq_pipe_of(pipeline);
    if (!d || !p || !gq_use(d->w, "gpu bind")) return;
    /* OpenGL writes a depth only while its test is on: an untested write is a test that always
     * passes. */
    gq_capability(dev, 1, p->state.depth != 0);
    if (d->more.glDepthFunc) d->more.glDepthFunc(p->state.depth == 3 ? 0x0207 /* GL_ALWAYS */ : 0x0201 /* GL_LESS */);
    gq_depth_write(dev, p->state.depth >= 2);
    gq_capability(dev, 2, p->state.blend != 0);
    if (p->state.blend) gq_blend(dev, (p->state.blend - 1) / 8, (p->state.blend - 1) % 8);
    gq_capability(dev, 3, p->state.cull != 0);
    if (p->state.cull) gq_cull(dev, p->state.cull == 2);
    d->w->api.glUseProgram(p->shader->id);
    d->w->api.glBindVertexArray(p->vao);
    d->bound = p;
}

static void gq_t_uniforms(int64_t dev, const float *block, int64_t n) {
    gq_dev *d = gq_dev_of(dev);
    if (!d || !d->bound || !d->bound->ubo || !gq_use(d->w, "gpu uniforms")) return;
    d->w->api.glBindBuffer(GQ_UNIFORM_BUFFER, d->bound->ubo);
    d->w->api.glBufferData(GQ_UNIFORM_BUFFER, (intptr_t) n * 4, block, GL_DYNAMIC_DRAW);
    d->more.glBindBufferBase(GQ_UNIFORM_BUFFER, 0, d->bound->ubo);
}

static void gq_t_sample(int64_t dev, int64_t slot, int64_t tex) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *r = gq_res_of(tex);
    gq_texture_bind(tex, slot);
    if (d && r && r->kind == GQ_TEXTURE && slot >= 0 && slot < GQ_MAX_SLOTS) {
        d->unit = (int) slot;
        d->unit_tex[slot] = r->id;
    }
}

/* Feed `n` inputs from the bound array buffer, numbered from `first_location`, `stride`
 * floats a record starting at float `base`, advancing per vertex or per copy. */
static void gq_feed(gq_api *gl, int first_location, const int *floats, int n, int stride,
                    int64_t base, unsigned per_copy) {
    int i, at = 0;
    for (i = 0; i < n; i++) {
        gl->glVertexAttribPointer((unsigned) (first_location + i), floats[i], GL_FLOAT, 0,
                                  stride * 4, (const void *) (intptr_t) ((base + at) * 4));
        gl->glEnableVertexAttribArray((unsigned) (first_location + i));
        gl->glVertexAttribDivisor((unsigned) (first_location + i), per_copy);
        at += floats[i];
    }
}

static void gq_t_vertices(int64_t dev, int64_t buf) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *b = gq_res_of(buf);
    gq_pipe *p;
    if (!d || !d->bound || !b || b->kind != GQ_BUFFER || !gq_use(d->w, "gpu vertices")) return;
    p = d->bound;
    d->w->api.glBindBuffer(GL_ARRAY_BUFFER, b->id);
    gq_feed(&d->w->api, 0, p->vertex, p->vertex_n, p->vertex_floats, 0, 0);
}

static void gq_t_instances(int64_t dev, int64_t buf, int64_t first_float) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *b = gq_res_of(buf);
    gq_pipe *p;
    if (!d || !d->bound || !b || b->kind != GQ_BUFFER || !gq_use(d->w, "gpu instances")) return;
    p = d->bound;
    d->w->api.glBindBuffer(GL_ARRAY_BUFFER, b->id);
    gq_feed(&d->w->api, p->vertex_n, p->instance, p->instance_n, p->instance_floats, first_float, 1);
}

static void gq_t_indices(int64_t dev, int64_t buf) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *b = gq_res_of(buf);
    if (!d || !d->bound || !b || b->kind != GQ_BUFFER || !gq_use(d->w, "gpu indices")) return;
    d->w->api.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->id);
}

static void gq_t_scissor(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h, int64_t on) {
    if (on) gq_scissor(dev, x, y, w, h);
    gq_capability(dev, 4, on != 0);
}

static void gq_t_draw(int64_t dev, int64_t first, int64_t count, int64_t indexed, int64_t copies) {
    gq_dev *d = gq_dev_of(dev);
    gq_api *gl;
    unsigned shape;
    if (!d || !d->bound || count <= 0 || !gq_use(d->w, "gpu draw")) return;
    gl = &d->w->api;
    shape = d->bound->state.shape == 1 ? GL_LINES : d->bound->state.shape == 2 ? GL_POINTS
                                                                               : GL_TRIANGLES;
    if (indexed && copies > 0) {
        gl->glDrawElementsInstanced(shape, (int) count, GL_UNSIGNED_INT, (const void *) 0,
                                    (int) copies);
    } else if (indexed) {
        gl->glDrawElements(shape, (int) count, GL_UNSIGNED_INT, (const void *) 0);
    } else if (copies > 0) {
        gl->glDrawArraysInstanced(shape, (int) first, (int) count, (int) copies);
    } else {
        gl->glDrawArrays(shape, (int) first, (int) count);
    }
}

static void gq_t_end(int64_t dev) {
    gq_dev *d = gq_dev_of(dev);
    if (!d || !gq_use(d->w, "gpu end")) return;
    d->w->api.glBindVertexArray(0);
    d->w->api.glColorMask(1, 1, 1, 1);
    d->bound = NULL;
    d->in_pass = 0;
    gq_pass_over(dev, d);
}

static const gpu_backend gq_tab = {
#if defined(GQ_BROWSER)
    GPU_BACKEND_VERSION, "gpu_gl", "glsl-330",
#else
    GPU_BACKEND_WRITES_DEPTH, "gpu_gl", "glsl-330",
#endif
    gq_t_open, gq_poll, gq_event_count, gq_events, gq_t_size, gq_t_close,
    gq_key_down, gq_mouse, gq_text_size, gq_text_copy,
    gq_t_buffer, gq_t_index_buffer, gq_buffer_update, gq_t_texture, gq_t_texture_put,
    gq_t_texture_put565,
    gq_t_texture_nearest, gq_t_texture_mipmaps, gq_t_target, gq_t_pipeline, gq_t_release,
    gq_t_begin, gq_t_use, gq_t_uniforms, gq_t_sample, gq_t_vertices, gq_t_instances,
    gq_t_indices, gq_viewport, gq_t_scissor, gq_t_draw, gq_t_end, gq_t_present, gq_t_capture,
};

#if defined(GPU_GL_CHECKED)
/* The checked table: each call that reaches OpenGL, then `gq_after`. */
#define GQ_V(name, params, call)                                                                  \
    static void gq_c_##name params {                                                             \
        gq_now = #name;                                                                          \
        call;                                                                                    \
        gq_after();                                                                              \
    }
#define GQ_R(name, params, call)                                                                  \
    static int64_t gq_c_##name params {                                                          \
        int64_t r;                                                                               \
        gq_now = #name;                                                                          \
        r = call;                                                                                \
        gq_after();                                                                              \
        return r;                                                                                \
    }
/* The same with the handles a call uses looked up first (`gq_live`): a dead one is a fault and
 * the call is not made. */
#define GQ_VG(name, params, guard, call)                                                          \
    static void gq_c_##name params {                                                             \
        gq_now = #name;                                                                          \
        if (guard) { call; }                                                                     \
        gq_after();                                                                              \
    }
GQ_R(open, (int64_t w, int64_t h, const char *t, int64_t f), gq_t_open(w, h, t, f))
static void gq_c_close(int64_t d) {
    gq_device_closed(d);
    gq_t_close(d);
}
GQ_R(buffer, (int64_t d, const float *p, int64_t n), gq_born(gq_t_buffer(d, p, n), d, "buffer"))
GQ_R(index_buffer, (int64_t d, const int64_t *p, int64_t n), gq_born(gq_t_index_buffer(d, p, n), d, "index buffer"))
GQ_VG(buffer_update, (int64_t b, const float *p, int64_t n), gq_live(b), gq_buffer_update(b, p, n))
GQ_R(texture, (int64_t d, int64_t w, int64_t h, int64_t z), gq_born(gq_t_texture(d, w, h, z), d, "texture"))
GQ_VG(texture_put, (int64_t t, int64_t x, int64_t y, int64_t w, int64_t h, const uint32_t *p, int64_t n),
      gq_live(t), gq_t_texture_put(t, x, y, w, h, p, n))
GQ_VG(texture_put565, (int64_t t, int64_t x, int64_t y, int64_t w, int64_t h, const int32_t *p, int64_t n),
      gq_live(t), gq_t_texture_put565(t, x, y, w, h, p, n))
GQ_VG(texture_nearest, (int64_t t, int64_t on), gq_live(t), gq_t_texture_nearest(t, on))
GQ_VG(texture_mipmaps, (int64_t t), gq_live(t), gq_t_texture_mipmaps(t))
static int64_t gq_c_target(int64_t d, int64_t c, int64_t z) {
    int64_t r = 0;
    gq_now = "target";
    if (gq_live(c) && gq_live(z)) r = gq_born(gq_t_target(d, c, z), d, "target");
    gq_after();
    return r;
}
GQ_R(pipeline, (int64_t d, const gpu_program *p, const gpu_state *s), gq_born(gq_t_pipeline(d, p, s), d, "pipeline"))
GQ_VG(release, (int64_t h), gq_may_release(h), gq_t_release(h))
GQ_VG(begin, (int64_t d, int64_t t, int64_t l, int64_t c), gq_live(t), gq_t_begin(d, t, l, c))
GQ_VG(bind, (int64_t d, int64_t p), gq_live(p), gq_t_use(d, p))
GQ_V(uniforms, (int64_t d, const float *b, int64_t n), gq_t_uniforms(d, b, n))
GQ_VG(sample, (int64_t d, int64_t sl, int64_t t), gq_live(t), gq_t_sample(d, sl, t))
GQ_VG(vertices, (int64_t d, int64_t b), gq_live(b), gq_t_vertices(d, b))
GQ_VG(instances, (int64_t d, int64_t b, int64_t f), gq_live(b), gq_t_instances(d, b, f))
GQ_VG(indices, (int64_t d, int64_t b), gq_live(b), gq_t_indices(d, b))
GQ_V(viewport, (int64_t d, int64_t x, int64_t y, int64_t w, int64_t h), gq_viewport(d, x, y, w, h))
GQ_V(scissor, (int64_t d, int64_t x, int64_t y, int64_t w, int64_t h, int64_t on), gq_t_scissor(d, x, y, w, h, on))
GQ_V(draw, (int64_t d, int64_t f, int64_t c, int64_t i, int64_t k), gq_t_draw(d, f, c, i, k))
GQ_V(end, (int64_t d), gq_t_end(d))
GQ_V(present, (int64_t d), gq_t_present(d))
GQ_VG(capture, (int64_t d, int64_t t, uint32_t *o, int64_t n), gq_live(t), gq_t_capture(d, t, o, n))

/* For this library's own tests: one buffer of the device opened last released twice and then
 * given to a draw, straight through the checked table, as no program can through `gpu`.  The
 * faults it adds (two, with the checks on) show the register catches them. */
int64_t gq_check_self(void) {
    static const float one[3] = {0.0f, 0.0f, 0.0f};
    int64_t before = gq_fault_count, b;
    int64_t dev = gq_devs ? (int64_t) (intptr_t) gq_devs->w : 0;
    if (!dev || gq_check_mode() == 0 || gq_checking == 2) return 0;
    b = gq_c_buffer(dev, one, 3);
    gq_c_release(b);
    gq_c_release(b);
    gq_c_vertices(dev, b);
    return gq_fault_count - before;
}

static const gpu_backend gq_tab_checked = {
#if defined(GQ_BROWSER)
    GPU_BACKEND_VERSION, "gpu_gl", "glsl-330",
#else
    GPU_BACKEND_WRITES_DEPTH, "gpu_gl", "glsl-330",
#endif
    gq_c_open, gq_poll, gq_event_count, gq_events, gq_t_size, gq_c_close,
    gq_key_down, gq_mouse, gq_text_size, gq_text_copy,
    gq_c_buffer, gq_c_index_buffer, gq_c_buffer_update, gq_c_texture, gq_c_texture_put,
    gq_c_texture_put565,
    gq_c_texture_nearest, gq_c_texture_mipmaps, gq_c_target, gq_c_pipeline, gq_c_release,
    gq_c_begin, gq_c_bind, gq_c_uniforms, gq_c_sample, gq_c_vertices, gq_c_instances,
    gq_c_indices, gq_c_viewport, gq_c_scissor, gq_c_draw, gq_c_end, gq_c_present, gq_c_capture,
};

int64_t gq_checked(void) {
    return gq_check_mode() != 0;
}
#else /* the library as it ships: no check, and nothing to pay for one */

void gq_check(int64_t on) {
    (void) on;
}

int64_t gq_faults(void) {
    return 0;
}

int64_t gq_checked(void) {
    return 0;
}

int64_t gq_check_self(void) {
    return 0;
}
#endif /* GPU_GL_CHECKED */

/* This backend's table: OpenGL is looked for when a device opens, so the table is there on
 * every machine this file has a window host for, and `open` answers 0 where the stack is not. */
int64_t gq_table(void) {
#if defined(GPU_GL_CHECKED)
    if (gq_check_mode()) return (int64_t) (intptr_t) &gq_tab_checked;
#endif
    return (int64_t) (intptr_t) &gq_tab;
}

#else /* no window host on this machine */

int64_t gq_table(void) {
    return 0;
}

const char *gq_renderer(void) {
    return "";
}

void gq_check(int64_t on) {
    (void) on;
}

int64_t gq_faults(void) {
    return 0;
}

int64_t gq_checked(void) {
    return 0;
}

int64_t gq_check_self(void) {
    return 0;
}

#endif
