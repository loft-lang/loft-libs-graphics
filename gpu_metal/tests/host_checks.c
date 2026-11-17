/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* What the loft tests cannot reach of the Metal backend, checked on its C itself: two devices
 * drawn on two threads, and a window whose application is hidden.  Not part of `loft test`;
 * on a Mac with a display, from this package's directory:
 *
 *   mkdir -p /tmp/gm_inc/gpu && cp ../gpu/c/gpu_backend.h /tmp/gm_inc/gpu/
 *   cc -g -fsanitize=thread -I/tmp/gm_inc tests/host_checks.c -framework CoreFoundation \
 *      -o /tmp/gm_host_checks && /tmp/gm_host_checks
 *
 * It prints "ok" and exits 0; a race is the sanitizer's own report and a non-zero exit. */
#include "../c/gpu_metal.c"
#include <CoreFoundation/CoreFoundation.h>

static int done, failed;

static void hide(void *arg) {
    (void) arg;
    gm_v_o(gm.app, "hide:", NULL);
}

static void unhide(void *arg) {
    (void) arg;
    gm_v_o(gm.app, "unhide:", NULL);
}

/* 200 frames of everything a frame does, beside another thread doing the same. */
static void *draws(void *arg) {
    const gpu_backend *b = (const gpu_backend *) (intptr_t) gm_table();
    int64_t dev = b->open(64, 48, (const char *) arg, 0), frame, buf, tex;
    float corners[9] = {0};
    uint32_t px[16];
    int64_t events[64];
    if (!dev) {
        fprintf(stderr, "no device\n");
        __sync_fetch_and_add(&failed, 1);
        __sync_fetch_and_add(&done, 1);
        return NULL;
    }
    buf = b->buffer(dev, corners, 9);
    tex = b->texture(dev, 4, 4, 0);
    for (frame = 0; frame < 200; frame++) {
        memset(px, (int) frame, sizeof px);
        b->poll(dev);
        b->events(dev, events, 64);
        b->key_down(dev, 65);
        b->pointer(dev, 0);
        b->texture_put(tex, 0, 0, 4, 4, px, 16);
        b->texture_nearest(tex, frame & 1);
        b->buffer_update(buf, corners, 9);
        b->begin(dev, 0, 0, 0xFF203040);
        b->end(dev);
        b->present(dev);
    }
    b->release(tex);
    b->release(buf);
    b->close(dev);
    __sync_fetch_and_add(&done, 1);
    return NULL;
}

/* A window stays open while its application is hidden, and is shown again after. */
static void *hidden(void *arg) {
    const gpu_backend *b = (const gpu_backend *) (intptr_t) gm_table();
    int64_t dev = b->open(64, 48, "hidden", 0), frame, open = 1, frames_hidden = 0;
    (void) arg;
    for (frame = 0; dev && frame < 90 && open; frame++) {
        if (frame == 30) gm_on_main(hide, NULL);
        if (frame == 60) gm_on_main(unhide, NULL);
        open = b->poll(dev);
        if (gm_of(dev)->hidden) frames_hidden++;
        b->begin(dev, 0, 0, 0xFF203040);
        b->end(dev);
        b->present(dev);
    }
    if (!dev || !open || frame != 90 || frames_hidden < 20 || frames_hidden > 40) {
        fprintf(stderr, "hidden: open %d after %d frames, %d of them hidden\n", (int) open, (int) frame,
                (int) frames_hidden);
        __sync_fetch_and_add(&failed, 1);
    }
    if (dev) b->close(dev);
    __sync_fetch_and_add(&done, 1);
    return NULL;
}

static void run(void *(*job)(void *), int threads) {
    pthread_t t[2];
    int i;
    done = 0;
    for (i = 0; i < threads; i++) pthread_create(&t[i], NULL, job, i ? "two" : "one");
    while (__sync_fetch_and_add(&done, 0) < threads) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, 0);
    for (i = 0; i < threads; i++) pthread_join(t[i], NULL);
}

int main(void) {
    run(draws, 2);
    run(hidden, 1);
    if (failed) return 1;
    printf("ok\n");
    return 0;
}
