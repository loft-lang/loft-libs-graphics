/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* gpu's C part: it holds the backend's table and passes each call on.  No graphics stack is
 * named here and no library is opened.  One backend serves a program. */
#include "gpu_backend.h"
#include <stddef.h>

static const gpu_backend *gp_tab = NULL;

/* Take a backend's table; 0 for a null table or one of a version this library does not know. */
int64_t gp_use(int64_t table) {
    const gpu_backend *t = (const gpu_backend *) (intptr_t) table;
    if (t == NULL || (t->version != GPU_BACKEND_VERSION && t->version != GPU_BACKEND_WRITES_DEPTH)) {
        return 0;
    }
    gp_tab = t;
    return 1;
}

const char *gp_name(void) { return gp_tab ? gp_tab->name : ""; }
const char *gp_shading(void) { return gp_tab ? gp_tab->shading : ""; }

int64_t gp_open(int64_t w, int64_t h, const char *title, int64_t fullscreen) {
    return gp_tab ? gp_tab->open(w, h, title, fullscreen) : 0;
}
int64_t gp_poll(int64_t dev) { return gp_tab ? gp_tab->poll(dev) : 0; }
int64_t gp_size(int64_t dev, int64_t which) { return gp_tab ? gp_tab->size(dev, which) : 0; }
void gp_close(int64_t dev) { if (gp_tab) { gp_tab->close(dev); } }
int64_t gp_event_count(int64_t dev) { return gp_tab ? gp_tab->event_count(dev) : 0; }
void gp_events(int64_t dev, int64_t *out, int64_t n) { if (gp_tab) { gp_tab->events(dev, out, n); } }
int64_t gp_key_down(int64_t dev, int64_t key) { return gp_tab ? gp_tab->key_down(dev, key) : 0; }
int64_t gp_pointer(int64_t dev, int64_t which) { return gp_tab ? gp_tab->pointer(dev, which) : 0; }
int64_t gp_text_size(int64_t dev) { return gp_tab ? gp_tab->text_size(dev) : 0; }
void gp_text_copy(int64_t dev, uint8_t *out, int64_t n) { if (gp_tab) { gp_tab->text_copy(dev, out, n); } }

int64_t gp_buffer(int64_t dev, const float *data, int64_t n) {
    return gp_tab ? gp_tab->buffer(dev, data, n) : 0;
}
int64_t gp_index_buffer(int64_t dev, const int64_t *idx, int64_t n) {
    return gp_tab ? gp_tab->index_buffer(dev, idx, n) : 0;
}
void gp_buffer_update(int64_t buf, const float *data, int64_t n) {
    if (gp_tab) { gp_tab->buffer_update(buf, data, n); }
}
int64_t gp_texture(int64_t dev, int64_t w, int64_t h, int64_t depth) {
    return gp_tab ? gp_tab->texture(dev, w, h, depth) : 0;
}
void gp_texture_put(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                    const uint32_t *px, int64_t n) {
    if (gp_tab) { gp_tab->texture_put(tex, x, y, w, h, px, n); }
}
void gp_texture_put565(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                       const int32_t *px, int64_t n) {
    if (gp_tab) { gp_tab->texture_put565(tex, x, y, w, h, px, n); }
}
void gp_texture_nearest(int64_t tex, int64_t on) { if (gp_tab) { gp_tab->texture_nearest(tex, on); } }
void gp_texture_mipmaps(int64_t tex) { if (gp_tab) { gp_tab->texture_mipmaps(tex); } }
int64_t gp_target(int64_t dev, int64_t colour, int64_t depth) {
    return gp_tab ? gp_tab->target(dev, colour, depth) : 0;
}

/* A pipeline from the program and state gpu.loft flattened into `d`:
 *   vertex_n, its floats…, instance_n, its floats…, block_n, (floats, count, offset)…,
 *   block_floats, textures, shape, depth, blend, cull, into. */
int64_t gp_pipeline(int64_t dev, const int64_t *d, int64_t n, const char *vertex,
                    const char *fragment) {
    gpu_program p;
    gpu_state s;
    int64_t at = 0, i;
    if (!gp_tab || n < 3) { return 0; }
    p.vertex_n = d[at++];
    if (p.vertex_n < 0 || p.vertex_n > GPU_MAX_INPUTS || at + p.vertex_n >= n) { return 0; }
    for (i = 0; i < p.vertex_n; i++) { p.vertex[i] = d[at++]; }
    p.instance_n = d[at++];
    if (p.instance_n < 0 || p.instance_n > GPU_MAX_INPUTS || at + p.instance_n >= n) { return 0; }
    for (i = 0; i < p.instance_n; i++) { p.instance[i] = d[at++]; }
    p.block_n = d[at++];
    if (p.block_n < 0 || p.block_n > GPU_MAX_FIELDS || at + 3 * p.block_n + 7 != n) { return 0; }
    for (i = 0; i < p.block_n; i++) {
        p.block[i].floats = d[at++];
        p.block[i].count = d[at++];
        p.block[i].offset = d[at++];
    }
    p.block_floats = d[at++];
    p.textures = d[at++];
    p.vertex_source = vertex;
    p.fragment_source = fragment;
    s.shape = d[at++];
    s.depth = d[at++];
    s.blend = d[at++];
    s.cull = d[at++];
    s.into = d[at++];
    /* A backend that does not say it writes a depth untested would test it: no pipeline. */
    if (s.depth == 3 && gp_tab->version < GPU_BACKEND_WRITES_DEPTH) { return 0; }
    return gp_tab->pipeline(dev, &p, &s);
}

void gp_release(int64_t handle) { if (gp_tab && handle) { gp_tab->release(handle); } }

void gp_begin(int64_t dev, int64_t target, int64_t load, int64_t colour) {
    if (gp_tab) { gp_tab->begin(dev, target, load, colour); }
}
void gp_bind(int64_t dev, int64_t pipeline) { if (gp_tab) { gp_tab->use(dev, pipeline); } }
void gp_uniforms(int64_t dev, const float *block, int64_t n) {
    if (gp_tab) { gp_tab->uniforms(dev, block, n); }
}
void gp_sample(int64_t dev, int64_t slot, int64_t tex) { if (gp_tab) { gp_tab->sample(dev, slot, tex); } }
void gp_vertices(int64_t dev, int64_t buf) { if (gp_tab) { gp_tab->vertices(dev, buf); } }
void gp_instances(int64_t dev, int64_t buf, int64_t first) {
    if (gp_tab) { gp_tab->instances(dev, buf, first); }
}
void gp_indices(int64_t dev, int64_t buf) { if (gp_tab) { gp_tab->indices(dev, buf); } }
void gp_viewport(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (gp_tab) { gp_tab->viewport(dev, x, y, w, h); }
}
void gp_scissor(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h, int64_t on) {
    if (gp_tab) { gp_tab->scissor(dev, x, y, w, h, on); }
}
void gp_draw(int64_t dev, int64_t first, int64_t count, int64_t indexed, int64_t copies) {
    if (gp_tab) { gp_tab->draw(dev, first, count, indexed, copies); }
}
void gp_end(int64_t dev) { if (gp_tab) { gp_tab->end(dev); } }
void gp_present(int64_t dev) { if (gp_tab) { gp_tab->present(dev); } }
void gp_capture(int64_t dev, int64_t target, uint32_t *out, int64_t n) {
    if (gp_tab) { gp_tab->capture(dev, target, out, n); }
}
