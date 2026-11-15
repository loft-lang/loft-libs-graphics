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
#include "../../gpu/c/gpu_backend.h"

#if defined(GQ_X11) || defined(GQ_COCOA) || defined(GQ_BROWSER) || defined(GQ_ANDROID)

#define GQ_UNIFORM_BUFFER 0x8A11
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
 * index lists under, and the pipeline bound in the pass in progress. */
typedef struct gq_dev {
    gq_window *w;
    gq_block_api more;
    int more_ok;
    unsigned scratch;
    struct gq_pipe *bound;
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
    return GQ_MORE(glUniformBlockBinding) && GQ_MORE(glBindBufferBase) &&
           GQ_MORE(glGetActiveUniformBlockiv) && GQ_MORE(glGetActiveUniformsiv);
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
            *at = d->next;
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
    gq_texture_put(tex, x, y, w, h, wide, count);
    free(wide);
}

/* The pixels of `target`'s colour, or the window's, the top row first. */
static void gq_t_capture(int64_t dev, int64_t target, uint32_t *out, int64_t n) {
    gq_window *w = gq_of(dev);
    gq_res *f = gq_res_of(target);
    unsigned char *rgba;
    int64_t x, y;
    if (!f) {
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
            free(p);
            return;
        }
    }
    gq_release(h);
}

/* ── a frame ───────────────────────────────────────────────────────────────────────────────── */

static void gq_t_begin(int64_t dev, int64_t target, int64_t load, int64_t colour) {
    gq_dev *d = gq_dev_of(dev);
    gq_res *f = gq_res_of(target);
    if (!d || !gq_use(d->w, "gpu begin")) return;
    d->bound = NULL;
    gq_render_to(dev, target);
    gq_viewport(dev, 0, 0, f ? f->width : d->w->width, f ? f->height : d->w->height);
    gq_capability(dev, 4, 0);
    if (load == 0) {
        gq_depth_write(dev, 1); /* a clear writes only the depth a draw may write */
        gq_clear(dev, colour);
    }
}

static void gq_t_use(int64_t dev, int64_t pipeline) {
    gq_dev *d = gq_dev_of(dev);
    gq_pipe *p = gq_pipe_of(pipeline);
    if (!d || !p || !gq_use(d->w, "gpu bind")) return;
    gq_capability(dev, 1, p->state.depth != 0);
    gq_depth_write(dev, p->state.depth == 2);
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
    (void) dev;
    gq_texture_bind(tex, slot);
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
    d->bound = NULL;
}

static const gpu_backend gq_tab = {
    GPU_BACKEND_VERSION, "gpu_gl", "glsl-330",
    gq_t_open, gq_poll, gq_event_count, gq_events, gq_t_size, gq_t_close,
    gq_key_down, gq_mouse, gq_text_size, gq_text_copy,
    gq_t_buffer, gq_t_index_buffer, gq_buffer_update, gq_t_texture, gq_texture_put,
    gq_t_texture_put565,
    gq_texture_filter, gq_texture_mipmaps, gq_t_target, gq_t_pipeline, gq_t_release,
    gq_t_begin, gq_t_use, gq_t_uniforms, gq_t_sample, gq_t_vertices, gq_t_instances,
    gq_t_indices, gq_viewport, gq_t_scissor, gq_t_draw, gq_t_end, gq_swap, gq_t_capture,
};

/* This backend's table: OpenGL is looked for when a device opens, so the table is there on
 * every machine this file has a window host for, and `open` answers 0 where the stack is not. */
int64_t gq_table(void) {
    return (int64_t) (intptr_t) &gq_tab;
}

#else /* no window host on this machine */

int64_t gq_table(void) {
    return 0;
}

#endif
