/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The handle register of a backend's CHECKED build: what the backend knows of every handle it
 * gave out — which device made it, what it is, and whether it was released or its device
 * closed.  A released handle is a freed pointer; used again it reads memory that may hold
 * anything, and nothing says so.  With the register the call is refused and named instead.  An
 * address the allocator gives out again is a new handle and reads as live: what is caught is
 * every use between a release and that.
 *
 * A backend includes this only inside its checked build (`#if defined(GPU_<NAME>_CHECKED)`), so
 * the library as it ships holds none of it.  Before the include it defines
 *
 *     GPU_REGISTER_FAULT(detail)   count and report one misuse (`detail` is a C string)
 *     GPU_REGISTER_NOW             the name of the table call being made (a `const char *`
 *                                  that stays valid: a string literal)
 *
 * Everything here is `static`: one register per backend, none shared between them. */
#ifndef GPU_REGISTER_H
#define GPU_REGISTER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int64_t h, dev;
    const char *kind;
    const char *ended_in; /* the call that released it; NULL while live */
    int closed;           /* its device closed: only its release is in order */
} gpu_known;
static gpu_known *gpu_knowns = NULL;
static int gpu_known_n = 0, gpu_known_room = 0;

static gpu_known *gpu_known_of(int64_t h) {
    int i;
    for (i = 0; i < gpu_known_n; i++) {
        if (gpu_knowns[i].h == h) return &gpu_knowns[i];
    }
    return NULL;
}

/* Handle `h` was made by device `dev` just now; answers `h`.  0 (nothing was made) is not
 * registered. */
static int64_t gpu_register_born(int64_t h, int64_t dev, const char *kind) {
    gpu_known *k;
    if (!h) return h;
    k = gpu_known_of(h);
    if (!k) {
        if (gpu_known_n == gpu_known_room) {
            int room = gpu_known_room ? gpu_known_room * 2 : 64;
            gpu_known *more = (gpu_known *) realloc(gpu_knowns, (size_t) room * sizeof(gpu_known));
            if (!more) return h;
            gpu_knowns = more;
            gpu_known_room = room;
        }
        k = &gpu_knowns[gpu_known_n++];
    }
    k->h = h;
    k->dev = dev;
    k->kind = kind;
    k->ended_in = NULL;
    k->closed = 0;
    return h;
}

/* Whether a call may use handle `h` (0 is "none" and always may). */
static int gpu_register_live(int64_t h) {
    gpu_known *k = h ? gpu_known_of(h) : NULL;
    char why[160];
    if (!h) return 1;
    if (!k) {
        GPU_REGISTER_FAULT("a handle this backend never gave out");
        return 0;
    }
    if (k->ended_in) {
        snprintf(why, sizeof why, "a %s used after `%s` released it", k->kind, k->ended_in);
        GPU_REGISTER_FAULT(why);
        return 0;
    }
    if (k->closed) {
        snprintf(why, sizeof why, "a %s used after its device closed", k->kind);
        GPU_REGISTER_FAULT(why);
        return 0;
    }
    return 1;
}

/* A release: once, and also in order after the device closed. */
static int gpu_register_may_release(int64_t h) {
    gpu_known *k = h ? gpu_known_of(h) : NULL;
    char why[160];
    if (!h) return 0;
    if (!k) {
        GPU_REGISTER_FAULT("the release of a handle this backend never gave out");
        return 0;
    }
    if (k->ended_in) {
        snprintf(why, sizeof why, "a %s released twice (first in `%s`)", k->kind, k->ended_in);
        GPU_REGISTER_FAULT(why);
        return 0;
    }
    k->ended_in = GPU_REGISTER_NOW;
    return 1;
}

/* Device `dev` closed: what it made may only be released from here on. */
static void gpu_register_device_closed(int64_t dev) {
    int i;
    for (i = 0; i < gpu_known_n; i++) {
        if (gpu_knowns[i].dev == dev) gpu_knowns[i].closed = 1;
    }
}

#endif /* GPU_REGISTER_H */
