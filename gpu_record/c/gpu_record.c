/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* A GPU backend that draws nothing: each call it receives becomes one line of a log the
 * program reads back.  Handles are numbered from 1 in the order they are made, so a log is
 * the same on every run and every machine. */
#include "gpu/gpu_backend.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define REC_LOG 65536
#define REC_HANDLES 1024

static char rec_text[REC_LOG];
static int64_t rec_len = 0;
static int64_t rec_next = 1;
/* What each handle is: 0 free, 'd' a device, 'b' buffer, 'i' index buffer, 't' texture,
 * 'r' target, 'p' pipeline; and the device it belongs to. */
static char rec_kind[REC_HANDLES];
static int64_t rec_owner[REC_HANDLES];
static int64_t rec_width[REC_HANDLES];
static int64_t rec_height[REC_HANDLES];
static int64_t rec_no_source = 0;

static void rec_line(const char *format, ...) {
    va_list ap;
    int n;
    if (rec_len >= REC_LOG - 256) { return; }
    va_start(ap, format);
    n = vsnprintf(rec_text + rec_len, 255, format, ap);
    va_end(ap);
    if (n > 254) { n = 254; }
    if (n > 0) { rec_len += n; }
    rec_text[rec_len++] = '\n';
    rec_text[rec_len] = 0;
}

static int64_t rec_new(char kind, int64_t owner) {
    int64_t h = rec_next;
    if (h >= REC_HANDLES) { return 0; }
    rec_next++;
    rec_kind[h] = kind;
    rec_owner[h] = owner;
    return h;
}

static int rec_is(int64_t h, char kind) { return h > 0 && h < REC_HANDLES && rec_kind[h] == kind; }

static int64_t rec_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    int64_t h = rec_new('d', 0);
    if (h) { rec_width[h] = width; rec_height[h] = height; }
    rec_line("open %lld %lldx%lld \"%s\"%s", (long long) h, (long long) width, (long long) height,
             title, fullscreen ? " fullscreen" : "");
    return h;
}
static int64_t rec_poll(int64_t dev) { return rec_is(dev, 'd'); }
static int64_t rec_event_count(int64_t dev) { (void) dev; return 0; }
static void rec_events(int64_t dev, int64_t *out, int64_t n) { (void) dev; (void) out; (void) n; }
static int64_t rec_size(int64_t dev, int64_t which) {
    if (!rec_is(dev, 'd')) { return 0; }
    return which ? rec_height[dev] : rec_width[dev];
}
static int64_t rec_key_down(int64_t dev, int64_t key) { (void) dev; (void) key; return 0; }
static int64_t rec_pointer(int64_t dev, int64_t which) { (void) dev; (void) which; return 0; }
static int64_t rec_text_size(int64_t dev) { (void) dev; return 0; }
static void rec_text_copy(int64_t dev, uint8_t *out, int64_t n) { (void) dev; (void) out; (void) n; }
static void rec_close(int64_t dev) {
    int64_t h;
    if (!rec_is(dev, 'd')) { return; }
    for (h = 1; h < REC_HANDLES; h++) {
        if (rec_kind[h] && rec_owner[h] == dev) { rec_kind[h] = 0; }
    }
    rec_kind[dev] = 0;
    rec_line("close %lld", (long long) dev);
}

static int64_t rec_buffer(int64_t dev, const float *data, int64_t n) {
    int64_t h = rec_new('b', dev);
    (void) data;
    rec_line("buffer %lld floats %lld", (long long) h, (long long) n);
    return h;
}
static int64_t rec_index_buffer(int64_t dev, const int64_t *idx, int64_t n) {
    int64_t h = rec_new('i', dev);
    (void) idx;
    rec_line("index_buffer %lld count %lld", (long long) h, (long long) n);
    return h;
}
static void rec_buffer_update(int64_t buf, const float *data, int64_t n) {
    (void) data;
    if (rec_is(buf, 'b')) { rec_line("buffer_update %lld floats %lld", (long long) buf, (long long) n); }
}
static int64_t rec_texture(int64_t dev, int64_t width, int64_t height, int64_t depth) {
    int64_t h = rec_new('t', dev);
    rec_line("texture %lld %lldx%lld %s", (long long) h, (long long) width, (long long) height,
             depth ? "depth" : "colour");
    return h;
}
static void rec_texture_put(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                            const uint32_t *px, int64_t n) {
    if (!rec_is(tex, 't')) { return; }
    rec_line("texture_put %lld at %lld,%lld %lldx%lld first %08x", (long long) tex, (long long) x,
             (long long) y, (long long) w, (long long) h, n > 0 ? (unsigned) px[0] : 0u);
}
static void rec_texture_put565(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                               const int32_t *px, int64_t n) {
    if (!rec_is(tex, 't')) { return; }
    rec_line("texture_put565 %lld at %lld,%lld %lldx%lld first %04x", (long long) tex,
             (long long) x, (long long) y, (long long) w, (long long) h,
             n > 0 ? (unsigned) px[0] & 0xFFFFu : 0u);
}
static void rec_texture_nearest(int64_t tex, int64_t on) {
    if (rec_is(tex, 't')) { rec_line("texture_nearest %lld %s", (long long) tex, on ? "on" : "off"); }
}
static void rec_texture_mipmaps(int64_t tex) {
    if (rec_is(tex, 't')) { rec_line("texture_mipmaps %lld", (long long) tex); }
}
static int64_t rec_target(int64_t dev, int64_t colour, int64_t depth) {
    int64_t h = rec_new('r', dev);
    rec_line("target %lld colour %lld depth %lld", (long long) h, (long long) colour, (long long) depth);
    return h;
}
static int64_t rec_pipeline(int64_t dev, const gpu_program *p, const gpu_state *s) {
    char line[200];
    int at = 0;
    int64_t i, h;
    if (rec_no_source && (p->vertex_source[0] || p->fragment_source[0])) {
        rec_line("pipeline refused: a shader given as source");
        return 0;
    }
    h = rec_new('p', dev);
    at += snprintf(line + at, sizeof line - at, "pipeline %lld vertex", (long long) h);
    for (i = 0; i < p->vertex_n; i++) { at += snprintf(line + at, sizeof line - at, " %lld", (long long) p->vertex[i]); }
    at += snprintf(line + at, sizeof line - at, " instance");
    for (i = 0; i < p->instance_n; i++) { at += snprintf(line + at, sizeof line - at, " %lld", (long long) p->instance[i]); }
    at += snprintf(line + at, sizeof line - at, " block");
    for (i = 0; i < p->block_n && at < 150; i++) {
        at += snprintf(line + at, sizeof line - at, " %lldx%lld@%lld", (long long) p->block[i].floats,
                       (long long) p->block[i].count, (long long) p->block[i].offset);
    }
    rec_line("%s size %lld textures %lld", line, (long long) p->block_floats, (long long) p->textures);
    rec_line("  state shape %lld depth %lld blend %lld cull %lld into %lld", (long long) s->shape,
             (long long) s->depth, (long long) s->blend, (long long) s->cull, (long long) s->into);
    return h;
}
static void rec_release(int64_t h) {
    if (h <= 0 || h >= REC_HANDLES || !rec_kind[h] || rec_kind[h] == 'd') { return; }
    rec_kind[h] = 0;
    rec_line("release %lld", (long long) h);
}

static void rec_begin(int64_t dev, int64_t target, int64_t load, int64_t colour) {
    (void) dev;
    if (load) { rec_line("begin %lld keep", (long long) target); }
    else { rec_line("begin %lld clear %08x", (long long) target, (unsigned) colour); }
}
static void rec_use(int64_t dev, int64_t pipeline) { (void) dev; rec_line("  bind %lld", (long long) pipeline); }
static long long rec_milli(float x) { return (long long) (x * 1000.0f + (x < 0 ? -0.5f : 0.5f)); }
static void rec_uniforms(int64_t dev, const float *block, int64_t n) {
    (void) dev;
    /* thousandths, written with whole numbers: a wasm build's printf formats no float */
    long long a = n > 0 ? rec_milli(block[0]) : 0, b = n > 0 ? rec_milli(block[n - 1]) : 0;
    rec_line("  uniforms floats %lld first %s%lld.%03lld last %s%lld.%03lld", (long long) n,
             a < 0 ? "-" : "", (a < 0 ? -a : a) / 1000, (a < 0 ? -a : a) % 1000,
             b < 0 ? "-" : "", (b < 0 ? -b : b) / 1000, (b < 0 ? -b : b) % 1000);
}
static void rec_sample(int64_t dev, int64_t slot, int64_t tex) {
    (void) dev;
    rec_line("  sample %lld texture %lld", (long long) slot, (long long) tex);
}
static void rec_vertices(int64_t dev, int64_t buf) { (void) dev; rec_line("  vertices %lld", (long long) buf); }
static void rec_instances(int64_t dev, int64_t buf, int64_t first) {
    (void) dev;
    rec_line("  instances %lld from %lld", (long long) buf, (long long) first);
}
static void rec_indices(int64_t dev, int64_t buf) { (void) dev; rec_line("  indices %lld", (long long) buf); }
static void rec_viewport(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void) dev;
    rec_line("  viewport %lld,%lld %lldx%lld", (long long) x, (long long) y, (long long) w, (long long) h);
}
static void rec_scissor(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h, int64_t on) {
    (void) dev;
    if (on) { rec_line("  scissor %lld,%lld %lldx%lld", (long long) x, (long long) y, (long long) w, (long long) h); }
    else { rec_line("  scissor off"); }
}
static void rec_draw(int64_t dev, int64_t first, int64_t count, int64_t indexed, int64_t copies) {
    (void) dev;
    rec_line("  draw from %lld count %lld%s copies %lld", (long long) first, (long long) count,
             indexed ? " indexed" : "", (long long) copies);
}
static void rec_end(int64_t dev) { (void) dev; rec_line("end"); }
static void rec_present(int64_t dev) { (void) dev; rec_line("present"); }
static void rec_capture(int64_t dev, int64_t target, uint32_t *out, int64_t n) {
    (void) dev;
    if (n > 0) { memset(out, 0, (size_t) n * sizeof *out); }
    rec_line("capture %lld pixels %lld", (long long) target, (long long) n);
}

static const gpu_backend rec_tab = {
    GPU_BACKEND_WRITES_DEPTH, "gpu_record", "record",
    rec_open, rec_poll, rec_event_count, rec_events, rec_size, rec_close,
    rec_key_down, rec_pointer, rec_text_size, rec_text_copy,
    rec_buffer, rec_index_buffer, rec_buffer_update, rec_texture, rec_texture_put,
    rec_texture_put565,
    rec_texture_nearest, rec_texture_mipmaps, rec_target, rec_pipeline, rec_release,
    rec_begin, rec_use, rec_uniforms, rec_sample, rec_vertices, rec_instances, rec_indices,
    rec_viewport, rec_scissor, rec_draw, rec_end, rec_present, rec_capture,
};

int64_t rec_table(void) { return (int64_t) (intptr_t) &rec_tab; }

/* The log so far. */
const char *rec_log(void) { return rec_text; }

/* Forget the log and every handle, and number handles from 1 again. */
void rec_reset(void) {
    rec_len = 0;
    rec_text[0] = 0;
    rec_next = 1;
    memset(rec_kind, 0, sizeof rec_kind);
    rec_no_source = 0;
}

/* How many buffers, textures, targets and pipelines are held now. */
int64_t rec_live(void) {
    int64_t h, n = 0;
    for (h = 1; h < REC_HANDLES; h++) {
        if (rec_kind[h] && rec_kind[h] != 'd') { n++; }
    }
    return n;
}

/* Refuse a pipeline whose shader arrives as text, as a machine that compiles none would. */
void rec_refuse_source(int64_t on) { rec_no_source = on; }
