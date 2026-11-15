/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The table a GPU backend exports and `gpu` calls through.
 *
 * A backend declares `gpu` as a dependency, includes this file as "gpu/gpu_backend.h" (loft
 * puts a declared dependency's headers where a package's C finds them) and answers the
 * address of one `gpu_backend` (0 where its stack is absent on this machine).  Handles are the
 * backend's own numbers, 0 for none.  `gpu` checks every call before it arrives here: a
 * backend sees a draw only inside a pass, with a pipeline built for that pass's target, its
 * block and every texture slot set. */
#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H
#include <stdint.h>

#define GPU_BACKEND_VERSION 2
#define GPU_MAX_INPUTS 16
#define GPU_MAX_FIELDS 32

/* One field of the uniform block.  `floats` is 1, 2, 4 or 16 (a matrix) for each element,
 * `count` the array length (1 for a plain field), `offset` its first float in the block. */
typedef struct {
    int64_t floats;
    int64_t count;
    int64_t offset;
} gpu_field;

/* What a shader reads, and its text in this backend's shading language. */
typedef struct {
    int64_t vertex_n;                    /* inputs read once per vertex */
    int64_t vertex[GPU_MAX_INPUTS];      /* floats in each: 1 to 4 */
    int64_t instance_n;                  /* inputs read once per drawn copy */
    int64_t instance[GPU_MAX_INPUTS];
    int64_t block_n;
    gpu_field block[GPU_MAX_FIELDS];
    int64_t block_floats;                /* the block's whole size, a multiple of 4 */
    int64_t textures;                    /* texture slots 0 .. textures-1 */
    const char *vertex_source;
    const char *fragment_source;
} gpu_program;

/* Render state, fixed for a pipeline's life.  The numbers are gpu.loft's constants. */
typedef struct {
    int64_t shape;      /* 0 triangles, 1 lines, 2 points */
    int64_t depth;      /* 0 off, 1 test, 2 test and write */
    int64_t blend;      /* 0 off, else 1 + 8 * source factor + destination factor */
    int64_t cull;       /* 0 none, 1 back, 2 front; counter-clockwise is the front */
    int64_t into;       /* 0 the window, 1 colour, 2 colour and depth, 3 depth */
} gpu_state;

typedef struct {
    int64_t version;    /* GPU_BACKEND_VERSION */
    const char *name;   /* for a diagnostic, never for a branch */
    const char *shading; /* the source tag this backend takes */
    /* the host part */
    int64_t (*open)(int64_t width, int64_t height, const char *title, int64_t fullscreen);
    int64_t (*poll)(int64_t dev);
    int64_t (*event_count)(int64_t dev);
    void (*events)(int64_t dev, int64_t *out, int64_t n);
    int64_t (*size)(int64_t dev, int64_t which);
    void (*close)(int64_t dev);
    /* what the window's keys, pointer and keyboard hold now: a key code is down or not;
     * `which` 0 the pointer's x, 1 its y, 2 its buttons (1 left, 2 right, 4 middle); the
     * bytes of the queued text events, to copy before `events` empties the queue */
    int64_t (*key_down)(int64_t dev, int64_t key);
    int64_t (*pointer)(int64_t dev, int64_t which);
    int64_t (*text_size)(int64_t dev);
    void (*text_copy)(int64_t dev, uint8_t *out, int64_t n);
    /* resources */
    int64_t (*buffer)(int64_t dev, const float *data, int64_t n);
    int64_t (*index_buffer)(int64_t dev, const int64_t *idx, int64_t n);
    void (*buffer_update)(int64_t buf, const float *data, int64_t n);
    int64_t (*texture)(int64_t dev, int64_t width, int64_t height, int64_t depth);
    void (*texture_put)(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                        const uint32_t *px, int64_t n);
    /* the same from RGB565 values (5 bits red, 6 green, 5 blue), each opaque */
    void (*texture_put565)(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                           const int32_t *px, int64_t n);
    void (*texture_nearest)(int64_t tex, int64_t on);
    void (*texture_mipmaps)(int64_t tex);
    int64_t (*target)(int64_t dev, int64_t colour, int64_t depth);
    int64_t (*pipeline)(int64_t dev, const gpu_program *program, const gpu_state *state);
    void (*release)(int64_t handle);
    /* a frame */
    void (*begin)(int64_t dev, int64_t target, int64_t load, int64_t colour);
    void (*use)(int64_t dev, int64_t pipeline);
    void (*uniforms)(int64_t dev, const float *block, int64_t n);
    void (*sample)(int64_t dev, int64_t slot, int64_t tex);
    void (*vertices)(int64_t dev, int64_t buf);
    void (*instances)(int64_t dev, int64_t buf, int64_t first_float);
    void (*indices)(int64_t dev, int64_t buf);
    void (*viewport)(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h);
    void (*scissor)(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h, int64_t on);
    void (*draw)(int64_t dev, int64_t first, int64_t count, int64_t indexed, int64_t copies);
    void (*end)(int64_t dev);
    void (*present)(int64_t dev);
    /* the pixels of `target` (0: the window), 0xAARRGGBB, the top row first */
    void (*capture)(int64_t dev, int64_t target, uint32_t *out, int64_t n);
} gpu_backend;
#endif
