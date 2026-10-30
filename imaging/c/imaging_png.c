/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * The imaging library's native part, in C99: PNG decoding and encoding by the vendored lodepng
 * (c/vendor/lodepng, zlib license), built by loft itself as a `[c] shim` and bound by the `#c`
 * declarations in src/imaging.loft.  Pure computation — memory in, memory out: loft reads and
 * writes the file (`read_bytes` / `write_bytes`), so one source serves the interpreter,
 * `--native`, the C backend (a 32-bit device included) and both wasm targets.
 *
 * A decode or an encode answers a handle (the result's address as an integer, 0 on failure);
 * loft asks its size, copies it out into a vector it sized, and frees it. */
#define LODEPNG_NO_COMPILE_DISK
#define LODEPNG_NO_COMPILE_CPP
#include "vendor/lodepng/lodepng.c"

#include <stdint.h>

typedef struct {
    unsigned char *bytes;
    size_t size;
    unsigned width, height;
} ip_result;

int64_t ip_decode(const uint8_t *png, int64_t n);
int64_t ip_encode(const uint8_t *rgba, int64_t n, int32_t width, int32_t height, int32_t alpha);
int64_t ip_width(int64_t h);
int64_t ip_height(int64_t h);
int64_t ip_size(int64_t h);
void ip_copy(int64_t h, uint8_t *out);
void ip_free(int64_t h);

static ip_result *ip_of(int64_t h) {
    return (ip_result *) (intptr_t) h;
}

static int64_t ip_handle(unsigned char *bytes, size_t size, unsigned w, unsigned h) {
    ip_result *r = (ip_result *) malloc(sizeof(ip_result));
    if (!r) {
        free(bytes);
        return 0;
    }
    r->bytes = bytes;
    r->size = size;
    r->width = w;
    r->height = h;
    return (int64_t) (intptr_t) r;
}

/* Any PNG colour type and depth, as 8-bit RGBA: palette and greyscale expanded, a tRNS colour
 * key made transparent, 16 bits taken to their high byte, a missing alpha opaque. */
int64_t ip_decode(const uint8_t *png, int64_t n) {
    unsigned char *px = NULL;
    unsigned w = 0, h = 0;
    if (n <= 0) return 0;
    if (lodepng_decode32(&px, &w, &h, png, (size_t) n) != 0 || px == NULL) {
        free(px);
        return 0;
    }
    return ip_handle(px, (size_t) w * h * 4u, w, h);
}

/* `width * height` RGBA pixels as an 8-bit PNG: RGBA when `alpha`, else RGB. */
int64_t ip_encode(const uint8_t *rgba, int64_t n, int32_t width, int32_t height, int32_t alpha) {
    unsigned char *png = NULL;
    size_t size = 0;
    unsigned err;
    LodePNGState state;
    if (width <= 0 || height <= 0 || n != (int64_t) width * height * 4) return 0;
    /* The file is 8-bit RGB when every pixel is opaque, 8-bit RGBA otherwise — the colour type the
     * caller asked for, never one lodepng picks.  Its default `auto_convert` re-chooses the mode
     * from the pixels (a palette, down to 1-bit, or grey), which keeps the pixels but not the
     * colour type this library promises. */
    lodepng_state_init(&state);
    state.encoder.auto_convert = 0;
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = alpha ? LCT_RGBA : LCT_RGB;
    state.info_png.color.bitdepth = 8;
    err = lodepng_encode(&png, &size, rgba, (unsigned) width, (unsigned) height, &state);
    lodepng_state_cleanup(&state);
    if (err != 0 || png == NULL) {
        free(png);
        return 0;
    }
    return ip_handle(png, size, (unsigned) width, (unsigned) height);
}

int64_t ip_width(int64_t h) { return h ? (int64_t) ip_of(h)->width : 0; }
int64_t ip_height(int64_t h) { return h ? (int64_t) ip_of(h)->height : 0; }
int64_t ip_size(int64_t h) { return h ? (int64_t) ip_of(h)->size : 0; }

void ip_copy(int64_t h, uint8_t *out) {
    if (h) memcpy(out, ip_of(h)->bytes, ip_of(h)->size);
}

void ip_free(int64_t h) {
    if (h) {
        free(ip_of(h)->bytes);
        free(ip_of(h));
    }
}
