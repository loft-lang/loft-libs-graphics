/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * The tiles library's core, in C99: tile sheets as 8-bit palette indices, RGB565 surfaces drawn
 * at their own resolution through a camera (§ Drawing at the surface's resolution), map layers
 * drawn in one call, and an integer upscale (optionally rotated a quarter turn for a portrait
 * panel) into a framebuffer.  Built by loft itself as a
 * `[c] shim`, bound by the `#c` declarations in src/tiles.loft.  Pure computation: a
 * platform's present (the Tanmatsu's DSI framebuffer) is a layer that calls `tl_present565`
 * with its own buffer.
 *
 * Why this shape (a 32-bit handheld, 768 KB of fast internal SRAM, a framebuffer in PSRAM):
 *   - a sheet is converted ONCE, at load, to one byte per pixel: index 0 is transparent and
 *     1..255 name RGB565 colours, so Kenney's 16 px packs take 34 KB, and a tile's 256 bytes
 *     are contiguous — the sheet is stored tile after tile, not row after row;
 *   - a frame is composed at 1x (200x120 at a 4x scale is 48 KB), where every write is cheap,
 *     and only the upscale touches the large framebuffer, once per pixel, in long runs: each
 *     source row is expanded once and copied `scale - 1` more times.
 *
 * Orientation, as Tiled defines it: a tile is transposed (the diagonal flip) first, then
 * mirrored horizontally, then vertically.  `flags`: bit 0 horizontal, bit 1 vertical, bit 2
 * diagonal; bits 8..11 the shade; bit 12 a ghost (every other pixel, in a checkerboard fixed to
 * the surface, so what is behind shows through).  A map cell is 0 for empty, else
 * `(tile + 1) | flags << 24`.
 *
 * Handles are indices + 1 into a table per kind; 0 is "none" (loft null). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Shades: a tile drawn darker is drawn through a darker copy of its sheet's palette, made once
 * when the sheet is made, so shading costs nothing per pixel.  Level 0 is the tile as it is;
 * each level after multiplies the channels by TL_SHADE[level] / 256, down to about a quarter,
 * so the darkest shade still shows what is there. */
#define TL_SHADES 8
static const uint16_t TL_SHADE[TL_SHADES] = {256, 208, 168, 136, 110, 89, 72, 58};

typedef struct {
    uint8_t *px;        /* count tiles of tw * th indices, tile after tile */
    int32_t tw, th, count, colours;
    uint16_t pal[TL_SHADES][256];  /* pal[level][0] unused: index 0 is transparent */
    unsigned tex, pal_tex;         /* a display surface's GPU copies, made when first drawn */
    int32_t across;                /* tiles per row of `tex` */
} tl_set;

typedef struct {
    uint16_t *px;
    int32_t w, h;
    int64_t zoom, cam_x, cam_y;   /* the camera: pixels per unit (Q16), the top-left point (units, Q8) */
    int32_t display;              /* 1: a display surface, always drawn through its camera;
                                   * 2: one the GPU draws, straight onto the display */
} tl_surf;

typedef struct {
    int32_t *cells;
    int32_t cols, rows;
} tl_map;

typedef struct {
    void **items;
    int64_t count;
} tl_table;

static tl_table tl_sets, tl_surfs, tl_maps;

static int64_t tl_put(tl_table *t, void *item) {
    int64_t i;
    void **grown;
    for (i = 0; i < t->count; i++) {
        if (t->items[i] == NULL) {
            t->items[i] = item;
            return i + 1;
        }
    }
    grown = (void **) realloc(t->items, (size_t) (t->count + 1) * sizeof(void *));
    if (!grown) return 0;
    t->items = grown;
    t->items[t->count++] = item;
    return t->count;
}

static void *tl_get(const tl_table *t, int64_t h) {
    return (h >= 1 && h <= t->count) ? t->items[h - 1] : NULL;
}

static uint16_t tl_565(uint32_t r, uint32_t g, uint32_t b) {
    return (uint16_t) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* ── Sheets ── */

int64_t tl_set_new(const uint8_t *rgba, int64_t n, int32_t w, int32_t h, int32_t tw, int32_t th,
                   int32_t spacing, int32_t margin);
int64_t tl_set_count(int64_t set);
int64_t tl_set_colours(int64_t set);
void tl_set_free(int64_t set);

/* A sheet from `w * h` RGBA pixels cut into `tw * th` tiles, `margin` pixels round the sheet
 * and `spacing` between tiles.  A pixel with alpha below 128 is transparent.  0 when the sizes
 * do not fit or the sheet has more than 255 colours once in RGB565. */
int64_t tl_set_new(const uint8_t *rgba, int64_t n, int32_t w, int32_t h, int32_t tw, int32_t th,
                   int32_t spacing, int32_t margin) {
    int32_t cols, rows, t, x, y, level;
    uint8_t *index_of;
    tl_set *s;
    int64_t handle;
    if (w <= 0 || h <= 0 || tw <= 0 || th <= 0 || spacing < 0 || margin < 0) return 0;
    if (n < (int64_t) w * h * 4) return 0;
    cols = (w - 2 * margin + spacing) / (tw + spacing);
    rows = (h - 2 * margin + spacing) / (th + spacing);
    if (cols <= 0 || rows <= 0) return 0;
    s = (tl_set *) calloc(1, sizeof(tl_set));
    index_of = (uint8_t *) calloc(65536, 1); /* RGB565 -> palette index, 0 = not seen yet */
    if (!s || !index_of) {
        free(s);
        free(index_of);
        return 0;
    }
    s->tw = tw;
    s->th = th;
    s->count = cols * rows;
    s->px = (uint8_t *) malloc((size_t) s->count * (size_t) tw * (size_t) th);
    if (!s->px) {
        free(s);
        free(index_of);
        return 0;
    }
    for (t = 0; t < s->count; t++) {
        int32_t x0 = margin + (t % cols) * (tw + spacing), y0 = margin + (t / cols) * (th + spacing);
        uint8_t *dst = s->px + (size_t) t * (size_t) tw * (size_t) th;
        for (y = 0; y < th; y++) {
            for (x = 0; x < tw; x++) {
                const uint8_t *p = rgba + ((size_t) (y0 + y) * (size_t) w + (size_t) (x0 + x)) * 4u;
                uint16_t c;
                if (p[3] < 128) {
                    dst[y * tw + x] = 0;
                    continue;
                }
                c = tl_565(p[0], p[1], p[2]);
                if (index_of[c] == 0) {
                    if (s->colours == 255) {
                        free(s->px);
                        free(s);
                        free(index_of);
                        return 0;
                    }
                    s->colours++;
                    s->pal[0][s->colours] = c;
                    index_of[c] = (uint8_t) s->colours;
                }
                dst[y * tw + x] = index_of[c];
            }
        }
    }
    free(index_of);
    for (level = 1; level < TL_SHADES; level++) {
        int32_t k;
        for (k = 1; k <= s->colours; k++) {
            uint32_t c = s->pal[0][k], f = TL_SHADE[level];
            uint32_t r = (((c >> 11) & 31) * f) >> 8, g = (((c >> 5) & 63) * f) >> 8, b = ((c & 31) * f) >> 8;
            s->pal[level][k] = (uint16_t) ((r << 11) | (g << 5) | b);
        }
    }
    handle = tl_put(&tl_sets, s);
    if (handle == 0) {
        free(s->px);
        free(s);
    }
    return handle;
}

int64_t tl_set_count(int64_t set) {
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    return s ? s->count : 0;
}

int64_t tl_set_colours(int64_t set) {
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    return s ? s->colours : 0;
}

void tl_set_free(int64_t set) {
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    if (!s) return;
    free(s->px);
    free(s);
    tl_sets.items[set - 1] = NULL;
}

/* ── Surfaces ── */

/* Under a camera other than one pixel per unit at (0, 0), each call below is rasterised at the
 * surface's resolution (§ Drawing at the surface's resolution). */
static int tl_plain(const tl_surf *d);
static void tl_fill_at(tl_surf *d, int64_t x, int64_t y, int64_t w, int64_t h, uint16_t c);
static void tl_sprite_at(tl_surf *d, const tl_set *s, int32_t tile, int64_t x, int64_t y, int64_t turn,
                         int64_t px, int64_t py, int64_t sx, int64_t sy, int32_t flags);
static void tl_ground_at(tl_surf *d, const tl_set *s, int32_t tile, int64_t x, int64_t y,
                         const int32_t lift[4], const int32_t dark[4], int32_t flags);
#if defined(__wasm__) && defined(LOFT_BROWSER)
/* A page: a display surface is drawn by the GPU, straight onto the canvas (§ The GPU). */
#define TL_GL 1
static void tl_gl_clear(tl_surf *d, uint16_t c);
static void tl_gl_pixels(tl_surf *d, int32_t *out);
#endif

int64_t tl_surf_new(int32_t w, int32_t h);
void tl_surf_free(int64_t surf);
void tl_clear(int64_t surf, int32_t c565);
void tl_rect(int64_t surf, int64_t x, int64_t y, int64_t w, int64_t h, int32_t c565);
void tl_pixels(int64_t surf, int32_t *out);

int64_t tl_surf_new(int32_t w, int32_t h) {
    tl_surf *s;
    int64_t handle;
    if (w <= 0 || h <= 0 || (int64_t) w * h > 16777216) return 0;
    s = (tl_surf *) malloc(sizeof(tl_surf));
    if (!s) return 0;
    s->px = (uint16_t *) calloc((size_t) w * (size_t) h, sizeof(uint16_t));
    if (!s->px) {
        free(s);
        return 0;
    }
    s->w = w;
    s->h = h;
    s->zoom = 65536;
    s->cam_x = s->cam_y = 0;
    s->display = 0;
    handle = tl_put(&tl_surfs, s);
    if (handle == 0) {
        free(s->px);
        free(s);
    }
    return handle;
}

void tl_surf_free(int64_t surf) {
    tl_surf *s = (tl_surf *) tl_get(&tl_surfs, surf);
    if (!s) return;
    free(s->px);
    free(s);
    tl_surfs.items[surf - 1] = NULL;
}

void tl_clear(int64_t surf, int32_t c565) {
    tl_surf *s = (tl_surf *) tl_get(&tl_surfs, surf);
    int64_t i, n;
    if (!s) return;
#if defined(TL_GL)
    if (s->display == 2) {
        tl_gl_clear(s, (uint16_t) c565);
        return;
    }
#endif
    n = (int64_t) s->w * s->h;
    for (i = 0; i < n; i++) s->px[i] = (uint16_t) c565;
}

/* The `w * h` rectangle with its top-left at (x, y) filled with one colour, clipped to the
 * surface; nothing when `w` or `h` is not positive.  The sizes are 64-bit so that a rectangle
 * reaching far past the surface is clipped, never wrapped: `x > s->w - w` is `x + w > s->w`
 * without the sum. */
void tl_rect(int64_t surf, int64_t x, int64_t y, int64_t w, int64_t h, int32_t c565) {
    tl_surf *s = (tl_surf *) tl_get(&tl_surfs, surf);
    int64_t x0, y0, x1, y1, u, v;
    if (!s || w <= 0 || h <= 0) return;
    if (!tl_plain(s)) {
        if (w > ((int64_t) 1 << 32)) w = (int64_t) 1 << 32;
        if (h > ((int64_t) 1 << 32)) h = (int64_t) 1 << 32;
        if (x < -((int64_t) 1 << 32) || x > ((int64_t) 1 << 32) || y < -((int64_t) 1 << 32) || y > ((int64_t) 1 << 32)) return;
        tl_fill_at(s, x * 65536, y * 65536, w * 65536, h * 65536, (uint16_t) c565);
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x > s->w - w ? s->w : x + w;
    y1 = y > s->h - h ? s->h : y + h;
    for (v = y0; v < y1; v++) {
        uint16_t *q = s->px + (size_t) v * (size_t) s->w;
        for (u = x0; u < x1; u++) q[u] = (uint16_t) c565;
    }
}

void tl_pixels(int64_t surf, int32_t *out) {
    tl_surf *s = (tl_surf *) tl_get(&tl_surfs, surf);
    int64_t i, n;
    if (!s) return;
#if defined(TL_GL)
    if (s->display == 2) {
        tl_gl_pixels(s, out);
        return;
    }
#endif
    n = (int64_t) s->w * s->h;
    for (i = 0; i < n; i++) out[i] = s->px[i];
}

/* ── Drawing ── */


/* Tile `tile` of `set` with its top-left at (x, y) on `surf`, oriented by `flags`.  Its
 * footprint is tw x th, or th x tw when transposed; whatever falls outside the surface is
 * skipped, and so is a transparent pixel. */
static void tl_blit(tl_surf *d, const tl_set *s, int32_t tile, int32_t x, int32_t y, int32_t flags) {
    int32_t fh = flags & 1, fv = (flags >> 1) & 1, fd = (flags >> 2) & 1;
    int32_t level = (flags >> 8) & 15, ghost = (flags >> 12) & 1;
    const uint16_t *pal = s->pal[level < TL_SHADES ? level : TL_SHADES - 1];
    int32_t ow = fd ? s->th : s->tw, oh = fd ? s->tw : s->th;
    int32_t u0 = x < 0 ? -x : 0, v0 = y < 0 ? -y : 0;
    int32_t u1 = d->w - x < ow ? d->w - x : ow, v1 = d->h - y < oh ? d->h - y : oh;
    int32_t u, v, step;
    const uint8_t *src;
    if (tile < 0 || tile >= s->count || u0 >= u1 || v0 >= v1) return;
    src = s->px + (size_t) tile * (size_t) s->tw * (size_t) s->th;
    /* Undo vertical, then horizontal, then the transpose: destination (u, v) reads source
     * (sx, sy).  Along a destination row u grows, which moves the source by `step`. */
    step = fd ? (fh ? -s->tw : s->tw) : (fh ? -1 : 1);
    for (v = v0; v < v1; v++) {
        int32_t vv = fv ? oh - 1 - v : v;
        int32_t uu = fh ? ow - 1 - u0 : u0;
        int32_t sx = fd ? vv : uu, sy = fd ? uu : vv;
        const uint8_t *p = src + sy * s->tw + sx;
        uint16_t *q = d->px + (size_t) (y + v) * (size_t) d->w + (size_t) (x + u0);
        for (u = u0; u < u1; u++) {
            uint8_t i = *p;
            if (i && !(ghost && ((x + u + y + v) & 1))) *q = pal[i];
            q++;
            p += step;
        }
    }
}

void tl_draw(int64_t surf, int64_t set, int32_t tile, int32_t x, int32_t y, int32_t flags);

void tl_draw(int64_t surf, int64_t set, int32_t tile, int32_t x, int32_t y, int32_t flags) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    if (!d || !s) return;
    if (!tl_plain(d)) {
        tl_sprite_at(d, s, tile, (int64_t) x * 256, (int64_t) y * 256, 0, 0, 0, 256, 256, flags & 0x1F07);
        return;
    }
    tl_blit(d, s, tile, x, y, flags & 0x1F07);
}

/* ── Ground: a tile bent to a surface and lit across it ── */

/* floor(a / b) for b > 0. */
static int32_t tl_floor_div(int32_t a, int32_t b) {
    int32_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

/* The 4x4 ordered-dither thresholds, 0..15: where a pixel lies between two shades, it takes the
 * darker one when its sixteenths of a shade exceed the threshold at its place in the tile. */
static const uint8_t TL_BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* A value interpolated between `a` and `b` across w steps at each step's centre and rounded half
 * up, floor((a * (2w - 2i - 1) + b * (2i + 1) + w) / 2w) for i = i0, i0 + 1, …: q, with r its
 * remainder over 2w, stepped exactly with additions — two divisions to start, none after. */
typedef struct {
    int32_t q, r, qs, rs, d;
} tl_lerp;

static void tl_lerp_start(tl_lerp *l, int32_t a, int32_t b, int32_t w, int32_t i0) {
    int32_t num = a * (2 * w - 2 * i0 - 1) + b * (2 * i0 + 1) + w, step = 2 * (b - a);
    l->d = 2 * w;
    l->q = tl_floor_div(num, l->d);
    l->r = num - l->q * l->d;
    l->qs = tl_floor_div(step, l->d);
    l->rs = step - l->qs * l->d;
}

static void tl_lerp_step(tl_lerp *l) {
    l->q += l->qs;
    l->r += l->rs;
    if (l->r >= l->d) {
        l->r -= l->d;
        l->q++;
    }
}

void tl_draw_ground(int64_t surf, int64_t set, int32_t tile, int32_t x, int32_t y,
                    int32_t lift_tl, int32_t lift_tr, int32_t lift_bl, int32_t lift_br,
                    int32_t dark_tl, int32_t dark_tr, int32_t dark_bl, int32_t dark_br, int32_t flags);

/* Tile `tile` laid on a surface whose corners are lifted `lift_*` pixels (up is positive) above
 * the tile's place at (x, y), and darkened `dark_*` sixteenths of a shade.  A column of the tile
 * stays a column: column u (0 .. width - 1) runs from its top edge, lifted by the two top
 * corners' lifts interpolated at the column's centre (u + 1/2) / width and rounded half up, to
 * its bottom edge, lifted alike by the two bottom corners; its rows are the tile's rows
 * stretched or squeezed between the two, each pixel taking the tile row under its centre.  So
 * two tiles whose shared corners have the same lifts meet without a gap or an overlap.  The
 * darkness is interpolated the same way, along the top and bottom edges at the column, then
 * down it at each tile row's centre; it is clamped to the darkest shade at the corners, and
 * split between the two nearest shades by a 4x4 ordered dither fixed to the tile's own pixels —
 * a gradual light that does not crawl as the view moves.  Orientation as for tl_draw (the shade
 * bits of `flags` are not used); transparent pixels and whatever falls off the surface are
 * skipped.  Integer arithmetic only, so every target draws the same pixels.  A tile that does
 * not bend, in one whole shade, is tl_draw's own loop; otherwise each column is set up with
 * additions, and each pixel costs a plain draw's work, plus one add and a shift for its shade
 * where the light changes down the column. */
void tl_draw_ground(int64_t surf, int64_t set, int32_t tile, int32_t x, int32_t y,
                    int32_t lift_tl, int32_t lift_tr, int32_t lift_bl, int32_t lift_br,
                    int32_t dark_tl, int32_t dark_tr, int32_t dark_bl, int32_t dark_br, int32_t flags) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    int32_t fh = flags & 1, fv = (flags >> 1) & 1, fd = (flags >> 2) & 1;
    int32_t ow, oh, u, darkest = (TL_SHADES - 1) * 16;
    tl_lerp top_lift, bottom_lift, top_dark, bottom_dark;
    const uint8_t *src;
    if (!d || !s || tile < 0 || tile >= s->count) return;
    /* Clamped at the corners, every darkness interpolated between them stays in range too; and
     * the darkest whole shade has no fraction to dither, so a shade never passes TL_SHADES - 1. */
    dark_tl = dark_tl < 0 ? 0 : (dark_tl > darkest ? darkest : dark_tl);
    dark_tr = dark_tr < 0 ? 0 : (dark_tr > darkest ? darkest : dark_tr);
    dark_bl = dark_bl < 0 ? 0 : (dark_bl > darkest ? darkest : dark_bl);
    dark_br = dark_br < 0 ? 0 : (dark_br > darkest ? darkest : dark_br);
    if (!tl_plain(d)) {
        int32_t lift[4], dark[4];
        lift[0] = lift_tl; lift[1] = lift_tr; lift[2] = lift_bl; lift[3] = lift_br;
        dark[0] = dark_tl; dark[1] = dark_tr; dark[2] = dark_bl; dark[3] = dark_br;
        tl_ground_at(d, s, tile, x, y, lift, dark, flags & 7);
        return;
    }
    /* Nothing bends and one whole shade: the plain draw, the same pixels. */
    if ((lift_tl | lift_tr | lift_bl | lift_br) == 0 && dark_tl == dark_tr && dark_tl == dark_bl &&
        dark_tl == dark_br && (dark_tl & 15) == 0) {
        tl_blit(d, s, tile, x, y, (flags & 7) | ((dark_tl >> 4) << 8));
        return;
    }
    ow = fd ? s->th : s->tw;
    oh = fd ? s->tw : s->th;
    src = s->px + (size_t) tile * (size_t) s->tw * (size_t) s->th;
    tl_lerp_start(&top_lift, lift_tl, lift_tr, ow, 0);
    tl_lerp_start(&bottom_lift, lift_bl, lift_br, ow, 0);
    tl_lerp_start(&top_dark, dark_tl, dark_tr, ow, 0);
    tl_lerp_start(&bottom_dark, dark_bl, dark_br, ow, 0);
    for (u = 0; u < ow; u++, tl_lerp_step(&top_lift), tl_lerp_step(&bottom_lift), tl_lerp_step(&top_dark),
                           tl_lerp_step(&bottom_dark)) {
        int32_t px = x + u, rows = 2 * oh;
        int32_t top, n, k, k1, q, r, uu, vstride, bias[4];
        tl_lerp dark;
        const uint8_t *p;
        const uint16_t *pal;
        uint16_t *out;
        if (px < 0 || px >= d->w) continue;
        top = y - top_lift.q;
        n = oh - bottom_lift.q + top_lift.q;
        if (n <= 0) continue;
        k = top < 0 ? -top : 0;
        k1 = d->h - top < n ? d->h - top : n;
        if (k >= k1) continue;
        /* The tile's column under this one: row v of it at p + v * vstride. */
        uu = fh ? ow - 1 - u : u;
        if (fd) {
            p = src + (size_t) uu * (size_t) s->tw;
            vstride = 1;
        } else {
            p = src + uu;
            vstride = s->tw;
        }
        if (fv) {
            p += (oh - 1) * vstride;
            vstride = -vstride;
        }
        /* The tile row under pixel k's centre, floor((2k + 1) * oh / (2n)): q, with r its
         * remainder over 2n (an unclipped, unstretched column starts at row 0, remainder oh). */
        if (k == 0 && n == oh) {
            q = 0;
            r = oh;
        } else {
            q = ((2 * k + 1) * oh) / (2 * n);
            r = ((2 * k + 1) * oh) % (2 * n);
        }
        p += q * vstride;
        out = d->px + (size_t) (top + k) * (size_t) d->w + (size_t) px;
        if (top_dark.q == bottom_dark.q && (top_dark.q & 15) == 0) {
            /* One whole shade down the column: one palette, no dither. */
            pal = s->pal[top_dark.q >> 4];
            for (;;) {
                uint8_t i = *p;
                if (i) *out = pal[i];
                if (++k >= k1) break;
                out += d->w;
                r += rows;
                while (r >= 2 * n) {
                    r -= 2 * n;
                    p += vstride;
                }
            }
            continue;
        }
        /* Tile row v's shade at darkness dq: the whole shade dq >> 4, one darker when the
         * fraction dq & 15 exceeds the dither threshold t — which is (dq + 15 - t) >> 4,
         * exactly, for a fraction and a threshold in 0 .. 15: one add and a shift. */
        bias[0] = 15 - TL_BAYER[0][u & 3];
        bias[1] = 15 - TL_BAYER[1][u & 3];
        bias[2] = 15 - TL_BAYER[2][u & 3];
        bias[3] = 15 - TL_BAYER[3][u & 3];
        tl_lerp_start(&dark, top_dark.q, bottom_dark.q, oh, q);
        if (n == oh) {
            /* Neither stretched nor squeezed: pixel k shows tile row k. */
            for (;;) {
                uint8_t i = *p;
                if (i) *out = s->pal[(dark.q + bias[k & 3]) >> 4][i];
                if (++k >= k1) break;
                out += d->w;
                p += vstride;
                tl_lerp_step(&dark);
            }
            continue;
        }
        pal = s->pal[(dark.q + bias[q & 3]) >> 4];
        for (;;) {
            uint8_t i = *p;
            if (i) *out = pal[i];
            if (++k >= k1) break;
            out += d->w;
            r += rows;
            if (r >= 2 * n) {
                /* At most one tile row on while the column is stretched, more when squeezed. */
                do {
                    r -= 2 * n;
                    q++;
                    p += vstride;
                    tl_lerp_step(&dark);
                } while (r >= 2 * n);
                pal = s->pal[(dark.q + bias[q & 3]) >> 4];
            }
        }
    }
}

/* ── Turned sprites ── */

/* sin(d) and tan(d / 2) for d = 0 .. 45 degrees, times 65536, rounded: the three shears of a
 * turn by d (Paeth: x by -tan(d/2), y by sin(d), x by -tan(d/2)). */
static const int32_t TL_SIN[46] = {
    0, 1144, 2287, 3430, 4572, 5712, 6850, 7987, 9121, 10252, 11380, 12505,
    13626, 14742, 15855, 16962, 18064, 19161, 20252, 21336, 22415, 23486, 24550, 25607,
    26656, 27697, 28729, 29753, 30767, 31772, 32768, 33754, 34729, 35693, 36647, 37590,
    38521, 39441, 40348, 41243, 42126, 42995, 43852, 44695, 45525, 46341
};
static const int32_t TL_TAN_HALF[46] = {
    0, 572, 1144, 1716, 2289, 2861, 3435, 4008, 4583, 5158, 5734, 6310,
    6888, 7467, 8047, 8628, 9210, 9794, 10380, 10967, 11556, 12146, 12739, 13333,
    13930, 14529, 15130, 15734, 16340, 16949, 17560, 18175, 18792, 19413, 20036, 20663,
    21294, 21928, 22566, 23208, 23853, 24503, 25157, 25815, 26478, 27146
};

/* How far from the pivot a turned sprite's shear tables reach: enough for a 32 px tile turned
 * about any of its pixels. */
#define TL_TURN_REACH 160

/* round(f * v / 65536), halves up. */
static int32_t tl_shear(int32_t f, int32_t v) {
    return tl_floor_div(f * v + 32768, 65536);
}

void tl_draw_turned(int64_t surf, int64_t set, int32_t tile, int32_t x, int32_t y, int32_t degrees,
                    int32_t pivot_x, int32_t pivot_y, int32_t flags);

/* Tile `tile`, oriented by `flags` as tl_draw orients it and placed as tl_draw places it at
 * (x, y), then turned `degrees` clockwise about its pixel (pivot_x, pivot_y).  The turn is
 * whole quarter turns and a rest of -44 .. 45 degrees; the rest is three shears, each moving
 * whole rows or columns by whole pixels, so every pixel of the tile lands on exactly one pixel
 * of the surface — no holes, no doubled pixels, the tile's colours kept: what pixel art needs at
 * small angles (a lean, a wobble) and in a swing.  Integer arithmetic only (tables of sines and
 * half-angle tangents), so every target draws the same pixels; 0 degrees is tl_draw.  Each
 * pixel of the turned tile's bounding box is mapped back through the three shears into the
 * tile: about twice a plain draw's pixels, each a few multiplications.  The shade is flags'
 * as for tl_draw; transparent pixels and whatever falls off the surface are skipped. */
void tl_draw_turned(int64_t surf, int64_t set, int32_t tile, int32_t x, int32_t y, int32_t degrees,
                    int32_t pivot_x, int32_t pivot_y, int32_t flags) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    int32_t fh = flags & 1, fv = (flags >> 1) & 1, fd = (flags >> 2) & 1;
    int32_t level = (flags >> 8) & 15, ghost = (flags >> 12) & 1;
    int32_t ow, oh, turn, quarters, rest, a, b, i, k, x0 = 0, x1 = 0, y0 = 0, y1 = 0, dx, dy, m;
    int32_t lo_u = 0, hi_u = 0, lo_v = 0, hi_v = 0, at[3];
    /* The shears' shifts, round(a * t) and round(b * t) for t = -m .. m, made once: then a
     * pixel costs lookups, not divisions.  A sprite too large for them computes each. */
    int32_t shift_a[TL_TURN_REACH * 2 + 1], shift_b[TL_TURN_REACH * 2 + 1];
    const uint16_t *pal;
    const uint8_t *src;
    if (!d || !s || tile < 0 || tile >= s->count) return;
    if (!tl_plain(d)) {
        /* At the surface's resolution a turn is a true rotation, about the pivot pixel's centre. */
        tl_sprite_at(d, s, tile, (int64_t) x * 256, (int64_t) y * 256, (int64_t) degrees * 256,
                     (int64_t) pivot_x * 256 + 128, (int64_t) pivot_y * 256 + 128, 256, 256, flags & 0x1F07);
        return;
    }
    pal = s->pal[level < TL_SHADES ? level : TL_SHADES - 1];
    ow = fd ? s->th : s->tw;
    oh = fd ? s->tw : s->th;
    src = s->px + (size_t) tile * (size_t) s->tw * (size_t) s->th;
    turn = degrees % 360;
    if (turn < 0) turn += 360;
    /* Whole quarter turns and a rest of -44 .. 45 degrees. */
    quarters = ((turn + 44) / 90) % 4;
    rest = turn - 90 * ((turn + 44) / 90);
    a = rest < 0 ? TL_TAN_HALF[-rest] : -TL_TAN_HALF[rest];
    b = rest < 0 ? -TL_SIN[-rest] : TL_SIN[rest];
    /* The tile quarter-turned about its pivot (a quarter turn clockwise takes (u, v) to (-v, u)):
     * where its corners go, which is the box (lo_u .. hi_u, lo_v .. hi_v) it fills; and where
     * the tile's index of a pixel of that box is, at (0, 0) and one step along each axis — the
     * quarter turns and the flips are one linear map, so the index is at[0] + u * at[1] +
     * v * at[2]. */
    for (i = 0; i < 7; i++) {
        /* i 0..3: the corners; 4..6: the points (0, 0), (1, 0) and (0, 1) of the turned box. */
        int32_t u, v, t;
        if (i < 4) {
            u = (i & 1) ? ow - 1 - pivot_x : -pivot_x;
            v = (i & 2) ? oh - 1 - pivot_y : -pivot_y;
            for (k = 0; k < quarters; k++) {
                t = u;
                u = -v;
                v = t;
            }
            if (i == 0 || u < lo_u) lo_u = u;
            if (i == 0 || u > hi_u) hi_u = u;
            if (i == 0 || v < lo_v) lo_v = v;
            if (i == 0 || v > hi_v) hi_v = v;
            u += tl_shear(a, v);
            v += tl_shear(b, u);
            u += tl_shear(a, v);
            if (i == 0 || u < x0) x0 = u;
            if (i == 0 || u > x1) x1 = u;
            if (i == 0 || v < y0) y0 = v;
            if (i == 0 || v > y1) y1 = v;
        } else {
            int32_t uu, vv;
            u = i == 5 ? 1 : 0;
            v = i == 6 ? 1 : 0;
            for (k = 0; k < quarters; k++) {
                t = u;
                u = v;
                v = -t;
            }
            u += pivot_x;
            v += pivot_y;
            vv = fv ? oh - 1 - v : v;
            uu = fh ? ow - 1 - u : u;
            at[i - 4] = (fd ? uu : vv) * s->tw + (fd ? vv : uu);
        }
    }
    at[1] -= at[0];
    at[2] -= at[0];
    /* Every value a shear multiplies stays within the box widened by the largest shift. */
    m = x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0;
    m += (x0 < 0 ? -x0 : x0) + (y0 < 0 ? -y0 : y0) + 4;
    if (m <= TL_TURN_REACH) {
        for (i = -m; i <= m; i++) {
            shift_a[i + TL_TURN_REACH] = tl_shear(a, i);
            shift_b[i + TL_TURN_REACH] = tl_shear(b, i);
        }
    } else {
        m = 0;
    }
    for (dy = y0 - 1; dy <= y1 + 1; dy++) {
        int32_t sy = y + pivot_y + dy;
        uint16_t *row;
        if (sy < 0 || sy >= d->h) continue;
        row = d->px + (size_t) sy * (size_t) d->w;
        for (dx = x0 - 1; dx <= x1 + 1; dx++) {
            int32_t sx = x + pivot_x + dx, u, v;
            uint8_t c;
            if (sx < 0 || sx >= d->w) continue;
            /* Back through the shears, into the quarter-turned tile's box. */
            if (m) {
                u = dx - shift_a[dy + TL_TURN_REACH];
                v = dy - shift_b[u + TL_TURN_REACH];
                u -= shift_a[v + TL_TURN_REACH];
            } else {
                u = dx - tl_shear(a, dy);
                v = dy - tl_shear(b, u);
                u -= tl_shear(a, v);
            }
            if (u < lo_u || u > hi_u || v < lo_v || v > hi_v) continue;
            c = src[at[0] + u * at[1] + v * at[2]];
            if (c && !(ghost && ((sx + sy) & 1))) row[sx] = pal[c];
        }
    }
}

/* ── Drawing at the surface's resolution ──
 *
 * A surface has a camera: `zoom` surface pixels per unit (Q16) and the point, in units (Q8),
 * at its top-left.  A new surface's camera is one pixel per unit at (0, 0), where every call
 * draws exactly what it always drew.  Under any other camera a primitive is rasterised at the
 * surface's own pixels: a pixel shows what lies under its centre, mapped back through the
 * camera into units and from there into the tile (turned, stretched, flipped) — nearest texel,
 * so pixel art stays crisp at every zoom and angle.  Integer arithmetic only, so every target
 * draws the same pixels for the same calls and the same surface size. */

/* sin(d) for d = 0 .. 90 degrees, times 2^30, rounded. */
static const int32_t TL_SIN90[91] = {
    0, 18739379, 37473049, 56195305, 74900443, 93582766, 112236583, 130856211,
    149435979, 167970228, 186453311, 204879599, 223243478, 241539355, 259761657, 277904834,
    295963357, 313931728, 331804471, 349576144, 367241333, 384794656, 402230767, 419544355,
    436730145, 453782903, 470697435, 487468587, 504091252, 520560366, 536870912, 553017922,
    568996477, 584801711, 600428808, 615873009, 631129609, 646193961, 661061475, 675727625,
    690187940, 704438018, 718473518, 732290163, 745883746, 759250125, 772385229, 785285058,
    797945680, 810363241, 822533958, 834454122, 846120104, 857528349, 868675383, 879557810,
    890172315, 900515665, 910584710, 920376381, 929887697, 939115760, 948057759, 956710970,
    965072759, 973140576, 980911966, 988384560, 995556083, 1002424350, 1008987269, 1015242840,
    1021189159, 1026824413, 1032146887, 1037154959, 1041847103, 1046221891, 1050277989, 1054014162,
    1057429273, 1060522280, 1063292242, 1065738315, 1067859754, 1069655912, 1071126243, 1072270298,
    1073087729, 1073578288, 1073741824
};

#define TL_TURN (360 * 256)

/* sin of `a` 256ths of a degree, times 2^24: the table at whole degrees, linear between. */
static int64_t tl_sin24(int64_t a) {
    int64_t sign = 1, d, f, v;
    a %= TL_TURN;
    if (a < 0) a += TL_TURN;
    if (a >= 180 * 256) {
        a -= 180 * 256;
        sign = -1;
    }
    if (a > 90 * 256) a = 180 * 256 - a;
    d = a >> 8;
    f = a & 255;
    v = TL_SIN90[d];
    if (f) v += ((int64_t) (TL_SIN90[d + 1] - TL_SIN90[d]) * f) >> 8;
    return sign * ((v + 32) >> 6);
}

static int tl_plain(const tl_surf *d) {
    return !d->display && d->zoom == 65536 && d->cam_x == 0 && d->cam_y == 0;
}

/* The point, in units (Q16), under the centre of surface pixel column (or row) `i`. */
static int64_t tl_centre(int64_t cam, int64_t zoom, int64_t i) {
    return cam * 256 + (((int64_t) 2 * i + 1) << 31) / zoom;
}

/* The whole unit a Q16 value lies in: floor(v / 65536). */
static int32_t tl_unit(int64_t v) {
    return (int32_t) (v >= 0 ? v / 65536 : -((-v + 65535) / 65536));
}

/* The first of the `n` columns (or rows) whose centre is at `at` (Q16 units) or past it. */
static int32_t tl_first(int64_t cam, int64_t zoom, int64_t at, int32_t n) {
    int64_t off = at - cam * 256, i;
    if (off <= 0) i = 0;
    else if (off >= ((int64_t) n << 16) * 65536 / zoom + 65536) i = n;
    else i = (off * zoom) >> 32;
    if (i > n) i = n;
    while (i > 0 && tl_centre(cam, zoom, i - 1) >= at) i--;
    while (i < n && tl_centre(cam, zoom, i) < at) i++;
    return (int32_t) i;
}

/* Where the oriented tile's texels (0, 0), (1, 0) and (0, 1) are in its sheet tile: texel
 * (u, v) is at at[0] + u * at[1] + v * at[2]. */
static void tl_orient(const tl_set *s, int32_t flags, int32_t at[3]) {
    int32_t fh = flags & 1, fv = (flags >> 1) & 1, fd = (flags >> 2) & 1, k;
    int32_t ow = fd ? s->th : s->tw, oh = fd ? s->tw : s->th;
    for (k = 0; k < 3; k++) {
        int32_t u = k == 1, v = k == 2;
        int32_t vv = fv ? oh - 1 - v : v, uu = fh ? ow - 1 - u : u;
        at[k] = (fd ? uu : vv) * s->tw + (fd ? vv : uu);
    }
    at[1] -= at[0];
    at[2] -= at[0];
}

#if defined(TL_GL)
/* ── The GPU: a page's display surface ──
 *
 * Each primitive is one quad over the box of pixels it can reach, and the fragment shader
 * colours a pixel by tl_sprite_at's and tl_ground_at's rule: the pixel's centre mapped back
 * through the camera into the tile, the nearest texel, its colour from the sheet's palette at
 * the shade.  The ground's darkness and dither are the same integer formulas.  The arithmetic is
 * the GPU's floating point, so a pixel whose centre lies within a hair of a texel edge may show
 * the neighbouring texel; everything else is the CPU's picture.  The sheet goes up once, as its
 * palette indices (in the red channel) and its palette at every shade, both read texel-exact. */
#define TL_IMPORT(name) __attribute__((import_module("loft_gl"), import_name(#name)))
TL_IMPORT(gg_js_size) int tl_js_size(int which);
TL_IMPORT(gg_glViewport) void tl_glViewport(int, int, int, int);
TL_IMPORT(gg_glClearColor) void tl_glClearColor(float, float, float, float);
TL_IMPORT(gg_glClear) void tl_glClear(unsigned);
TL_IMPORT(gg_glDisable) void tl_glDisable(unsigned);
TL_IMPORT(gg_glCreateShader) unsigned tl_glCreateShader(unsigned);
TL_IMPORT(gg_glShaderSource) void tl_glShaderSource(unsigned, int, const char *const *, const int *);
TL_IMPORT(gg_glCompileShader) void tl_glCompileShader(unsigned);
TL_IMPORT(gg_glCreateProgram) unsigned tl_glCreateProgram(void);
TL_IMPORT(gg_glAttachShader) void tl_glAttachShader(unsigned, unsigned);
TL_IMPORT(gg_glLinkProgram) void tl_glLinkProgram(unsigned);
TL_IMPORT(gg_glUseProgram) void tl_glUseProgram(unsigned);
TL_IMPORT(gg_glGetUniformLocation) int tl_glGetUniformLocation(unsigned, const char *);
TL_IMPORT(gg_glUniform1i) void tl_glUniform1i(int, int);
TL_IMPORT(gg_glUniform2f) void tl_glUniform2f(int, float, float);
TL_IMPORT(gg_glUniform3f) void tl_glUniform3f(int, float, float, float);
TL_IMPORT(gg_glUniform4f) void tl_glUniform4f(int, float, float, float, float);
TL_IMPORT(gg_glGenTextures) void tl_glGenTextures(int, unsigned *);
TL_IMPORT(gg_glBindTexture) void tl_glBindTexture(unsigned, unsigned);
TL_IMPORT(gg_glActiveTexture) void tl_glActiveTexture(unsigned);
TL_IMPORT(gg_glTexImage2D) void tl_glTexImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
TL_IMPORT(gg_glTexParameteri) void tl_glTexParameteri(unsigned, unsigned, int);
TL_IMPORT(gg_glGenVertexArrays) void tl_glGenVertexArrays(int, unsigned *);
TL_IMPORT(gg_glBindVertexArray) void tl_glBindVertexArray(unsigned);
TL_IMPORT(gg_glDrawArrays) void tl_glDrawArrays(unsigned, int, int);
TL_IMPORT(gg_glReadPixels) void tl_glReadPixels(int, int, int, int, unsigned, unsigned, void *);
TL_IMPORT(gg_glBindFramebuffer) void tl_glBindFramebuffer(unsigned, unsigned);

#define TL_GL_TEXTURE_2D 0x0DE1
#define TL_GL_TEXTURE0 0x84C0
#define TL_GL_RGBA 0x1908
#define TL_GL_UNSIGNED_BYTE 0x1401
#define TL_GL_NEAREST 0x2600
#define TL_GL_CLAMP_TO_EDGE 0x812F
#define TL_GL_MAG_FILTER 0x2800
#define TL_GL_MIN_FILTER 0x2801
#define TL_GL_WRAP_S 0x2802
#define TL_GL_WRAP_T 0x2803
#define TL_GL_COLOR_BUFFER_BIT 0x4000
#define TL_GL_TRIANGLE_STRIP 0x0005
#define TL_GL_VERTEX_SHADER 0x8B31
#define TL_GL_FRAGMENT_SHADER 0x8B30
#define TL_GL_DEPTH_TEST 0x0B71
#define TL_GL_BLEND 0x0BE2
#define TL_GL_SCISSOR_TEST 0x0C11
#define TL_GL_CULL_FACE 0x0B44
#define TL_GL_FRAMEBUFFER 0x8D40

/* The quad: the box (left, top, right, bottom) of surface pixels, the surface's first row at
 * the top. */
static const char *const tl_vs =
    "#version 330 core\n"
    "uniform vec2 u_size;\n"
    "uniform vec4 u_box;\n"
    "void main() {\n"
    "  vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "  vec2 p = mix(u_box.xy, u_box.zw, c);\n"
    "  gl_Position = vec4(p.x / u_size.x * 2.0 - 1.0, 1.0 - p.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "}\n";
/* u_kind 0 a fill, 1 an upright sprite, 2 a turned one, 3 ground.  u_cam: zoom, then the
 * top-left point in units.  The tile: u_t (its first texel in u_sheet, the oriented width and
 * height), u_at (the oriented tile's texel (u, v) is at at.x + u at.y + v at.z of the sheet's
 * tile, whose width is at.w), u_f (the shade, the ghost). */
static const char *const tl_fs =
    "#version 330 core\n"
    "uniform sampler2D u_sheet;\n"
    "uniform sampler2D u_pal;\n"
    "uniform vec2 u_size;\n"
    "uniform vec3 u_cam;\n"
    "uniform int u_kind;\n"
    "uniform vec4 u_colour, u_a, u_b, u_d, u_t, u_at, u_f;\n"
    "out vec4 o_color;\n"
    "const int BAYER[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n"
    "int mixat(int a, int b, int w, int i) { return (a * (2 * w - 2 * i - 1) + b * (2 * i + 1) + w) / (2 * w); }\n"
    "void main() {\n"
    "  if (u_kind == 0) { o_color = u_colour; return; }\n"
    "  float i = floor(gl_FragCoord.x), j = u_size.y - 1.0 - floor(gl_FragCoord.y);\n"
    "  vec2 p = u_cam.yz + (vec2(i, j) + 0.5) / u_cam.x;\n"
    "  int ow = int(u_t.z), oh = int(u_t.w), col, row, level = int(u_f.x);\n"
    "  if (u_kind == 1) {\n"
    "    col = min(int(floor((p.x - u_a.x) / u_a.z)), ow - 1);\n"
    "    row = min(int(floor((p.y - u_a.y) / u_a.w)), oh - 1);\n"
    "  } else if (u_kind == 2) {\n"
    "    vec2 d = p - u_a.xy;\n"
    "    float u = u_a.z + (u_b.x * d.x + u_b.y * d.y) / u_b.z, v = u_a.w + (u_b.x * d.y - u_b.y * d.x) / u_b.w;\n"
    "    if (u < 0.0 || v < 0.0 || u >= float(ow) || v >= float(oh)) discard;\n"
    "    col = int(floor(u));\n"
    "    row = int(floor(v));\n"
    "  } else {\n"
    "    float a = p.x - u_a.x;\n"
    "    float top = u_a.y - u_a.z - (u_a.w - u_a.z) * a / float(ow);\n"
    "    float bottom = u_a.y + float(oh) - u_b.x - (u_b.y - u_b.x) * a / float(ow);\n"
    "    if (bottom <= top || p.y < top || p.y >= bottom) discard;\n"
    "    col = min(int(floor(a)), ow - 1);\n"
    "    row = min(int(floor((p.y - top) * float(oh) / (bottom - top))), oh - 1);\n"
    "    int dt = mixat(int(u_d.x), int(u_d.y), ow, col), db = mixat(int(u_d.z), int(u_d.w), ow, col);\n"
    "    level = (mixat(dt, db, oh, row) + 15 - BAYER[(row & 3) * 4 + (col & 3)]) >> 4;\n"
    "  }\n"
    "  if (u_f.y > 0.5 && ((col + row) & 1) == 1) discard;\n"
    "  int at = int(u_at.x) + col * int(u_at.y) + row * int(u_at.z), tw = int(u_at.w);\n"
    "  int idx = int(texelFetch(u_sheet, ivec2(int(u_t.x) + at % tw, int(u_t.y) + at / tw), 0).r * 255.0 + 0.5);\n"
    "  if (idx == 0) discard;\n"
    "  o_color = texelFetch(u_pal, ivec2(idx, level), 0);\n"
    "}\n";

static unsigned tl_prog = 0, tl_vao = 0;
static int tl_u_size, tl_u_box, tl_u_cam, tl_u_kind, tl_u_colour, tl_u_a, tl_u_b, tl_u_d, tl_u_t, tl_u_at, tl_u_f;
static tl_surf *tl_gl_bound = NULL;    /* the surface whose camera the uniforms hold */
static int64_t tl_gl_zoom = 0, tl_gl_cx = 0, tl_gl_cy = 0;

static unsigned tl_shader(unsigned kind, const char *src) {
    unsigned sh = tl_glCreateShader(kind);
    tl_glShaderSource(sh, 1, &src, NULL);
    tl_glCompileShader(sh);
    return sh;
}

/* An RGB565 colour widened as `screen` widens it. */
static void tl_widen(uint16_t c, uint8_t *out) {
    out[0] = (uint8_t) (((c >> 11) & 31) * 255 / 31);
    out[1] = (uint8_t) (((c >> 5) & 63) * 255 / 63);
    out[2] = (uint8_t) ((c & 31) * 255 / 31);
    out[3] = 255;
}

static void tl_gl_texture(unsigned *tex, int w, int h, const uint8_t *rgba) {
    tl_glGenTextures(1, tex);
    tl_glBindTexture(TL_GL_TEXTURE_2D, *tex);
    tl_glTexParameteri(TL_GL_TEXTURE_2D, TL_GL_MAG_FILTER, TL_GL_NEAREST);
    tl_glTexParameteri(TL_GL_TEXTURE_2D, TL_GL_MIN_FILTER, TL_GL_NEAREST);
    tl_glTexParameteri(TL_GL_TEXTURE_2D, TL_GL_WRAP_S, TL_GL_CLAMP_TO_EDGE);
    tl_glTexParameteri(TL_GL_TEXTURE_2D, TL_GL_WRAP_T, TL_GL_CLAMP_TO_EDGE);
    tl_glTexImage2D(TL_GL_TEXTURE_2D, 0, TL_GL_RGBA, w, h, 0, TL_GL_RGBA, TL_GL_UNSIGNED_BYTE, rgba);
}

/* The sheet on the GPU, made the first time a display surface draws from it. */
static int tl_gl_sheet(tl_set *s) {
    int32_t rows, w, h, t, i;
    uint8_t *rgba;
    if (s->tex) return 1;
    s->across = 4096 / s->tw < 1 ? 1 : 4096 / s->tw;
    if (s->across > s->count) s->across = s->count;
    rows = (s->count + s->across - 1) / s->across;
    w = s->across * s->tw;
    h = rows * s->th;
    rgba = (uint8_t *) calloc((size_t) w * (size_t) h, 4);
    if (!rgba) return 0;
    for (t = 0; t < s->count; t++) {
        int32_t x0 = (t % s->across) * s->tw, y0 = (t / s->across) * s->th;
        for (i = 0; i < s->tw * s->th; i++) {
            rgba[(((size_t) (y0 + i / s->tw)) * (size_t) w + (size_t) (x0 + i % s->tw)) * 4] =
                s->px[(size_t) t * (size_t) s->tw * (size_t) s->th + (size_t) i];
        }
    }
    tl_gl_texture(&s->tex, w, h, rgba);
    free(rgba);
    rgba = (uint8_t *) calloc(256 * TL_SHADES, 4);
    if (!rgba) return 0;
    for (t = 0; t < TL_SHADES; t++) {
        for (i = 1; i < 256; i++) tl_widen(s->pal[t][i], rgba + ((size_t) t * 256 + (size_t) i) * 4);
    }
    tl_gl_texture(&s->pal_tex, 256, TL_SHADES, rgba);
    free(rgba);
    return 1;
}

/* Ready to draw on `d`: the program, the canvas as the target, the surface's camera. */
static int tl_gl_begin(tl_surf *d) {
    if (!tl_prog) {
        tl_prog = tl_glCreateProgram();
        tl_glAttachShader(tl_prog, tl_shader(TL_GL_VERTEX_SHADER, tl_vs));
        tl_glAttachShader(tl_prog, tl_shader(TL_GL_FRAGMENT_SHADER, tl_fs));
        tl_glLinkProgram(tl_prog);
        tl_glGenVertexArrays(1, &tl_vao);
        tl_u_size = tl_glGetUniformLocation(tl_prog, "u_size");
        tl_u_box = tl_glGetUniformLocation(tl_prog, "u_box");
        tl_u_cam = tl_glGetUniformLocation(tl_prog, "u_cam");
        tl_u_kind = tl_glGetUniformLocation(tl_prog, "u_kind");
        tl_u_colour = tl_glGetUniformLocation(tl_prog, "u_colour");
        tl_u_a = tl_glGetUniformLocation(tl_prog, "u_a");
        tl_u_b = tl_glGetUniformLocation(tl_prog, "u_b");
        tl_u_d = tl_glGetUniformLocation(tl_prog, "u_d");
        tl_u_t = tl_glGetUniformLocation(tl_prog, "u_t");
        tl_u_at = tl_glGetUniformLocation(tl_prog, "u_at");
        tl_u_f = tl_glGetUniformLocation(tl_prog, "u_f");
        tl_glUseProgram(tl_prog);
        tl_glUniform1i(tl_glGetUniformLocation(tl_prog, "u_sheet"), 0);
        tl_glUniform1i(tl_glGetUniformLocation(tl_prog, "u_pal"), 1);
        tl_gl_bound = NULL;
    }
    tl_glUseProgram(tl_prog);
    tl_glBindVertexArray(tl_vao);
    if (tl_gl_bound != d || tl_gl_zoom != d->zoom || tl_gl_cx != d->cam_x || tl_gl_cy != d->cam_y) {
        tl_glBindFramebuffer(TL_GL_FRAMEBUFFER, 0);
        tl_glViewport(0, 0, d->w, d->h);
        tl_glDisable(TL_GL_DEPTH_TEST);
        tl_glDisable(TL_GL_BLEND);
        tl_glDisable(TL_GL_SCISSOR_TEST);
        tl_glDisable(TL_GL_CULL_FACE);
        tl_glUniform2f(tl_u_size, (float) d->w, (float) d->h);
        tl_glUniform3f(tl_u_cam, (float) ((double) d->zoom / 65536.0), (float) ((double) d->cam_x / 256.0),
                       (float) ((double) d->cam_y / 256.0));
        tl_gl_bound = d;
        tl_gl_zoom = d->zoom;
        tl_gl_cx = d->cam_x;
        tl_gl_cy = d->cam_y;
    }
    return 1;
}

static void tl_gl_quad(int32_t i0, int32_t j0, int32_t i1, int32_t j1) {
    if (i0 >= i1 || j0 >= j1) return;
    tl_glUniform4f(tl_u_box, (float) i0, (float) j0, (float) i1, (float) j1);
    tl_glDrawArrays(TL_GL_TRIANGLE_STRIP, 0, 4);
}

/* The tile's uniforms: where it is in the sheet's texture and how it is oriented. */
static void tl_gl_tile(const tl_set *s, int32_t tile, int32_t flags, int32_t ow, int32_t oh, int32_t level) {
    int32_t at[3];
    tl_orient(s, flags, at);
    tl_glActiveTexture(TL_GL_TEXTURE0);
    tl_glBindTexture(TL_GL_TEXTURE_2D, s->tex);
    tl_glActiveTexture(TL_GL_TEXTURE0 + 1);
    tl_glBindTexture(TL_GL_TEXTURE_2D, s->pal_tex);
    tl_glUniform4f(tl_u_t, (float) ((tile % s->across) * s->tw), (float) ((tile / s->across) * s->th), (float) ow,
                   (float) oh);
    tl_glUniform4f(tl_u_at, (float) at[0], (float) at[1], (float) at[2], (float) s->tw);
    tl_glUniform4f(tl_u_f, (float) level, (float) ((flags >> 12) & 1), 0.0f, 0.0f);
}

static void tl_gl_clear(tl_surf *d, uint16_t c) {
    uint8_t rgba[4];
    tl_gl_begin(d);
    tl_widen(c, rgba);
    tl_glClearColor(rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, 1.0f);
    tl_glClear(TL_GL_COLOR_BUFFER_BIT);
}

/* The canvas read back, RGB565 as the CPU surface keeps it: a widened channel narrows back
 * exactly (ceil(v * 31 / 255) undoes floor(c * 255 / 31)). */
static void tl_gl_pixels(tl_surf *d, int32_t *out) {
    size_t n = (size_t) d->w * (size_t) d->h, k;
    int32_t y, x;
    uint8_t *rgba = (uint8_t *) malloc(n * 4);
    if (!rgba) return;
    tl_glBindFramebuffer(TL_GL_FRAMEBUFFER, 0);
    tl_glReadPixels(0, 0, d->w, d->h, TL_GL_RGBA, TL_GL_UNSIGNED_BYTE, rgba);
    for (y = 0; y < d->h; y++) {
        for (x = 0; x < d->w; x++) {
            const uint8_t *p = rgba + ((size_t) (d->h - 1 - y) * (size_t) d->w + (size_t) x) * 4;
            k = (size_t) y * (size_t) d->w + (size_t) x;
            out[k] = (int32_t) ((((p[0] * 31 + 254) / 255) << 11) | (((p[1] * 63 + 254) / 255) << 5) |
                                ((p[2] * 31 + 254) / 255));
        }
    }
    free(rgba);
}
#endif

/* Scratch for a primitive's column table, grown as a surface needs (one thread). */
static int32_t *tl_cols = NULL;
static int32_t tl_cols_cap = 0;

static int tl_cols_fit(int32_t n) {
    if (n > tl_cols_cap) {
        int32_t *p = (int32_t *) realloc(tl_cols, (size_t) n * sizeof(int32_t));
        if (!p) return 0;
        tl_cols = p;
        tl_cols_cap = n;
    }
    return 1;
}

/* The rectangle of units (x, y) .. (x + w, y + h), Q16, in one colour. */
static void tl_fill_at(tl_surf *d, int64_t x, int64_t y, int64_t w, int64_t h, uint16_t c) {
    int32_t i0 = tl_first(d->cam_x, d->zoom, x, d->w), i1 = tl_first(d->cam_x, d->zoom, x + w, d->w);
    int32_t j0 = tl_first(d->cam_y, d->zoom, y, d->h), j1 = tl_first(d->cam_y, d->zoom, y + h, d->h);
    int32_t i, j;
#if defined(TL_GL)
    if (d->display == 2) {
        uint8_t rgba[4];
        tl_gl_begin(d);
        tl_widen(c, rgba);
        tl_glUniform1i(tl_u_kind, 0);
        tl_glUniform4f(tl_u_colour, rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, 1.0f);
        tl_gl_quad(i0, j0, i1, j1);
        return;
    }
#endif
    for (j = j0; j < j1; j++) {
        uint16_t *q = d->px + (size_t) j * (size_t) d->w;
        for (i = i0; i < i1; i++) q[i] = c;
    }
}

/* Tile `tile` with its top-left at (x, y) units (Q8), oriented by `flags`, stretched by
 * (sx, sy) (Q8, 256 = its own size) about its point (px, py) (texels, Q8) and turned `turn`
 * 256ths of a degree clockwise about it.  The shade and the ghost are `flags`'; a ghost leaves
 * every other texel out, so it turns and stretches with the tile. */
static void tl_sprite_at(tl_surf *d, const tl_set *s, int32_t tile, int64_t x, int64_t y, int64_t turn,
                         int64_t px, int64_t py, int64_t sx, int64_t sy, int32_t flags) {
    int32_t fd = (flags >> 2) & 1, level = (flags >> 8) & 15, ghost = (flags >> 12) & 1;
    int32_t ow = fd ? s->th : s->tw, oh = fd ? s->tw : s->th, at[3], i, j;
    const uint16_t *pal = s->pal[level < TL_SHADES ? level : TL_SHADES - 1];
    const uint8_t *src;
    int64_t z = d->zoom, cx, cy;
    if (tile < 0 || tile >= s->count || sx <= 0 || sy <= 0) return;
    src = s->px + (size_t) tile * (size_t) s->tw * (size_t) s->th;
    tl_orient(s, flags, at);
    /* The pivot in units, Q16. */
    cx = x * 256 + px * 256;
    cy = y * 256 + py * 256;
    turn %= TL_TURN;
    if (turn < 0) turn += TL_TURN;
    if (turn == 0) {
        /* Upright: which texel a pixel shows depends on its column alone across and its row
         * alone down, so each is worked out once. */
        int64_t left = cx - px * sx, top = cy - py * sy, tw = sx * 256, th = sy * 256;
        int32_t i0 = tl_first(d->cam_x, z, left, d->w), i1 = tl_first(d->cam_x, z, left + ow * tw, d->w);
        int32_t j0 = tl_first(d->cam_y, z, top, d->h), j1 = tl_first(d->cam_y, z, top + oh * th, d->h);
        if (i0 >= i1 || j0 >= j1) return;
#if defined(TL_GL)
        if (d->display == 2) {
            if (!tl_gl_sheet((tl_set *) s) || !tl_gl_begin(d)) return;
            tl_glUniform1i(tl_u_kind, 1);
            tl_glUniform4f(tl_u_a, (float) ((double) left / 65536.0), (float) ((double) top / 65536.0),
                           (float) ((double) tw / 65536.0), (float) ((double) th / 65536.0));
            tl_gl_tile(s, tile, flags, ow, oh, level < TL_SHADES ? level : TL_SHADES - 1);
            tl_gl_quad(i0, j0, i1, j1);
            return;
        }
#endif
        if (!tl_cols_fit(i1 - i0)) return;
        for (i = i0; i < i1; i++) {
            int64_t u = (tl_centre(d->cam_x, z, i) - left) / tw;
            tl_cols[i - i0] = (int32_t) (u < ow ? u : ow - 1);
        }
        for (j = j0; j < j1; j++) {
            int64_t v = (tl_centre(d->cam_y, z, j) - top) / th;
            int32_t row = (int32_t) (v < oh ? v : oh - 1);
            const uint8_t *p = src + at[0] + row * at[2];
            uint16_t *q = d->px + (size_t) j * (size_t) d->w;
            for (i = i0; i < i1; i++) {
                int32_t col = tl_cols[i - i0];
                uint8_t c = p[col * at[1]];
                if (c && !(ghost && ((col + row) & 1))) q[i] = pal[c];
            }
        }
        return;
    }
    {
        /* Turned: the texel under a pixel's centre is the pivot plus the pixel's offset from it
         * turned back and un-stretched.  Along a row it moves by a fixed step, in 2^-32 texels. */
        int64_t c = tl_sin24(turn + 90 * 256), sn = tl_sin24(turn);
        int64_t step_u = (c << 32) / (z * sx), step_v = -(sn << 32) / (z * sy);
        int64_t pu = px << 24, pv = py << 24, ou = (int64_t) ow << 32, ov = (int64_t) oh << 32;
        int64_t lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
        int32_t i0, i1, j0, j1, k;
        /* The turned tile's corners, in surface pixels: the box to visit. */
        for (k = 0; k < 4; k++) {
            int64_t lx = (((k & 1) ? (int64_t) ow * 256 : 0) - px) * sx;   /* Q16 units */
            int64_t ly = (((k & 2) ? (int64_t) oh * 256 : 0) - py) * sy;
            int64_t ux = cx + ((c * lx - sn * ly) >> 24) - d->cam_x * 256;
            int64_t uy = cy + ((sn * lx + c * ly) >> 24) - d->cam_y * 256;
            int64_t qx = (ux * z) >> 32, qy = (uy * z) >> 32;
            if (k == 0 || qx < lo_x) lo_x = qx;
            if (k == 0 || qx > hi_x) hi_x = qx;
            if (k == 0 || qy < lo_y) lo_y = qy;
            if (k == 0 || qy > hi_y) hi_y = qy;
        }
        i0 = (int32_t) (lo_x - 1 < 0 ? 0 : lo_x - 1);
        j0 = (int32_t) (lo_y - 1 < 0 ? 0 : lo_y - 1);
        i1 = (int32_t) (hi_x + 2 > d->w ? d->w : hi_x + 2);
        j1 = (int32_t) (hi_y + 2 > d->h ? d->h : hi_y + 2);
#if defined(TL_GL)
        if (d->display == 2) {
            if (!tl_gl_sheet((tl_set *) s) || !tl_gl_begin(d)) return;
            tl_glUniform1i(tl_u_kind, 2);
            tl_glUniform4f(tl_u_a, (float) ((double) cx / 65536.0), (float) ((double) cy / 65536.0),
                           (float) ((double) px / 256.0), (float) ((double) py / 256.0));
            tl_glUniform4f(tl_u_b, (float) ((double) c / 16777216.0), (float) ((double) sn / 16777216.0),
                           (float) ((double) sx / 256.0), (float) ((double) sy / 256.0));
            tl_gl_tile(s, tile, flags, ow, oh, level < TL_SHADES ? level : TL_SHADES - 1);
            tl_gl_quad(i0, j0, i1, j1);
            return;
        }
#endif
        for (j = j0; j < j1; j++) {
            int64_t dx = tl_centre(d->cam_x, z, i0) - cx, dy = tl_centre(d->cam_y, z, j) - cy;
            int64_t u = pu + (c * dx + sn * dy) / sx, v = pv + (c * dy - sn * dx) / sy;
            uint16_t *q = d->px + (size_t) j * (size_t) d->w;
            for (i = i0; i < i1; i++, u += step_u, v += step_v) {
                int32_t col, row;
                uint8_t t;
                if (u < 0 || v < 0 || u >= ou || v >= ov) continue;
                col = (int32_t) (u >> 32);
                row = (int32_t) (v >> 32);
                t = src[at[0] + col * at[1] + row * at[2]];
                if (t && !(ghost && ((col + row) & 1))) q[i] = pal[t];
            }
        }
    }
}

/* A value between `a` and `b` at the centre of step i of w, rounded half up: tl_lerp's value. */
static int32_t tl_mix(int32_t a, int32_t b, int32_t w, int32_t i) {
    return tl_floor_div(a * (2 * w - 2 * i - 1) + b * (2 * i + 1) + w, 2 * w);
}

/* tl_draw_ground under a camera: (x, y) and the lifts in units.  The tile's top and bottom edges
 * run straight between their corners' lifts, at the surface's resolution; each pixel between
 * them shows the texel under its centre, and each texel keeps tl_draw_ground's darkness (its
 * corners' interpolated at the texel's centre, dithered by its place in the tile), so the light
 * stays with the art while the edges are as fine as the surface. */
static void tl_ground_at(tl_surf *d, const tl_set *s, int32_t tile, int64_t x, int64_t y,
                         const int32_t lift[4], const int32_t dark[4], int32_t flags) {
    int32_t fd = (flags >> 2) & 1, ow = fd ? s->th : s->tw, oh = fd ? s->tw : s->th, at[3];
    int32_t i, i0, i1, col = -1, shade[64];
    int64_t z = d->zoom, left = x * 65536, top0 = y * 65536, w16 = (int64_t) ow * 65536;
    const uint8_t *src;
    if (tile < 0 || tile >= s->count || oh > 64) return;
    src = s->px + (size_t) tile * (size_t) s->tw * (size_t) s->th;
    tl_orient(s, flags, at);
    i0 = tl_first(d->cam_x, z, left, d->w);
    i1 = tl_first(d->cam_x, z, left + w16, d->w);
#if defined(TL_GL)
    if (d->display == 2) {
        /* Every row an edge can reach, the lowest top to the highest bottom. */
        int32_t lo = lift[0] > lift[1] ? lift[0] : lift[1], hi = lift[2] < lift[3] ? lift[2] : lift[3];
        int32_t j0 = tl_first(d->cam_y, z, top0 - (int64_t) lo * 65536, d->h);
        int32_t j1 = tl_first(d->cam_y, z, top0 + (int64_t) (oh - hi) * 65536, d->h);
        if (!tl_gl_sheet((tl_set *) s) || !tl_gl_begin(d)) return;
        tl_glUniform1i(tl_u_kind, 3);
        tl_glUniform4f(tl_u_a, (float) x, (float) y, (float) lift[0], (float) lift[1]);
        tl_glUniform4f(tl_u_b, (float) lift[2], (float) lift[3], 0.0f, 0.0f);
        tl_glUniform4f(tl_u_d, (float) dark[0], (float) dark[1], (float) dark[2], (float) dark[3]);
        tl_gl_tile(s, tile, flags, ow, oh, 0);
        tl_gl_quad(i0, j0 > 0 ? j0 - 1 : 0, i1, j1 < d->h ? j1 + 1 : d->h);
        return;
    }
#endif
    for (i = i0; i < i1; i++) {
        int64_t a = tl_centre(d->cam_x, z, i) - left, top, bottom, n;
        int32_t u = (int32_t) (a >> 16), j, j0, j1, k;
        if (u >= ow) u = ow - 1;
        if (u != col) {
            /* The darkness of each texel of this tile column, as tl_draw_ground lights it. */
            int32_t dt = tl_mix(dark[0], dark[1], ow, u), db = tl_mix(dark[2], dark[3], ow, u);
            col = u;
            for (k = 0; k < oh; k++) shade[k] = (tl_mix(dt, db, oh, k) + 15 - TL_BAYER[k & 3][u & 3]) >> 4;
        }
        top = top0 - (int64_t) lift[0] * 65536 - (int64_t) (lift[1] - lift[0]) * a / ow;
        bottom = top0 + (int64_t) oh * 65536 - (int64_t) lift[2] * 65536 - (int64_t) (lift[3] - lift[2]) * a / ow;
        n = bottom - top;
        if (n <= 0) continue;
        j0 = tl_first(d->cam_y, z, top, d->h);
        j1 = tl_first(d->cam_y, z, bottom, d->h);
        for (j = j0; j < j1; j++) {
            int64_t v = (tl_centre(d->cam_y, z, j) - top) * oh / n;
            int32_t row = (int32_t) (v < oh ? v : oh - 1);
            uint8_t t = src[at[0] + u * at[1] + row * at[2]];
            if (t) d->px[(size_t) j * (size_t) d->w + (size_t) i] = s->pal[shade[row]][t];
        }
    }
}

int64_t tl_display_new(int32_t w, int32_t h);

/* A surface for the display, `w` x `h` pixels: always drawn through its camera, and where the
 * machine draws faster another way — a page's GPU, when the canvas is that size — drawn that
 * way, straight onto the display, with `tl_pixels` reading the display back. */
int64_t tl_display_new(int32_t w, int32_t h) {
    int64_t handle = tl_surf_new(w, h);
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, handle);
    if (!d) return 0;
    d->display = 1;
#if defined(TL_GL)
    if (tl_js_size(0) == w && tl_js_size(1) == h) d->display = 2;
#endif
    return handle;
}

int64_t tl_on_display(int64_t surf);

/* 1 when the surface is drawn straight onto the display: nothing needs to be shown. */
int64_t tl_on_display(int64_t surf) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    return d && d->display == 2;
}

void tl_camera(int64_t surf, int64_t zoom, int64_t cam_x, int64_t cam_y);
void tl_sprite(int64_t surf, int64_t set, int32_t tile, int64_t x, int64_t y, int64_t turn, int64_t pivot_x,
               int64_t pivot_y, int64_t scale_x, int64_t scale_y, int32_t flags);
void tl_fill(int64_t surf, int64_t x, int64_t y, int64_t w, int64_t h, int32_t c565);

/* Set the camera: `zoom` pixels per unit (Q16, 1/256 .. 4096) with the point (cam_x, cam_y)
 * (units, Q8) at the surface's top-left. */
void tl_camera(int64_t surf, int64_t zoom, int64_t cam_x, int64_t cam_y) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    if (!d || zoom < 256 || zoom > ((int64_t) 4096 << 16)) return;
    if (cam_x < -((int64_t) 1 << 40) || cam_x > ((int64_t) 1 << 40)) return;
    if (cam_y < -((int64_t) 1 << 40) || cam_y > ((int64_t) 1 << 40)) return;
    d->zoom = zoom;
    d->cam_x = cam_x;
    d->cam_y = cam_y;
}

/* A sprite: see tl_sprite_at.  Positions and sizes Q8 units, the turn 256ths of a degree. */
void tl_sprite(int64_t surf, int64_t set, int32_t tile, int64_t x, int64_t y, int64_t turn, int64_t pivot_x,
               int64_t pivot_y, int64_t scale_x, int64_t scale_y, int32_t flags) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    if (!d || !s || scale_x > 65536 || scale_y > 65536) return;
    if (x < -((int64_t) 1 << 40) || x > ((int64_t) 1 << 40) || y < -((int64_t) 1 << 40) || y > ((int64_t) 1 << 40)) return;
    tl_sprite_at(d, s, tile, x, y, turn, pivot_x, pivot_y, scale_x, scale_y, flags & 0x1F07);
}

/* The rectangle (x, y, w, h), Q8 units, filled in one colour at the surface's resolution. */
void tl_fill(int64_t surf, int64_t x, int64_t y, int64_t w, int64_t h, int32_t c565) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    if (!d || w <= 0 || h <= 0) return;
    if (w > ((int64_t) 1 << 40)) w = (int64_t) 1 << 40;
    if (h > ((int64_t) 1 << 40)) h = (int64_t) 1 << 40;
    if (x < -((int64_t) 1 << 40) || x > ((int64_t) 1 << 40) || y < -((int64_t) 1 << 40) || y > ((int64_t) 1 << 40)) return;
    tl_fill_at(d, x * 256, y * 256, w * 256, h * 256, (uint16_t) c565);
}

/* ── Maps ── */

int64_t tl_map_new(const int32_t *cells, int64_t n, int32_t cols, int32_t rows);
void tl_map_free(int64_t map);
void tl_map_set(int64_t map, int32_t x, int32_t y, int32_t cell);
int64_t tl_map_get(int64_t map, int32_t x, int32_t y);
void tl_draw_map(int64_t surf, int64_t map, int64_t set, int32_t cam_x, int32_t cam_y);

int64_t tl_map_new(const int32_t *cells, int64_t n, int32_t cols, int32_t rows) {
    tl_map *m;
    int64_t handle;
    if (cols <= 0 || rows <= 0 || n != (int64_t) cols * rows) return 0;
    m = (tl_map *) malloc(sizeof(tl_map));
    if (!m) return 0;
    m->cells = (int32_t *) malloc((size_t) n * sizeof(int32_t));
    if (!m->cells) {
        free(m);
        return 0;
    }
    memcpy(m->cells, cells, (size_t) n * sizeof(int32_t));
    m->cols = cols;
    m->rows = rows;
    handle = tl_put(&tl_maps, m);
    if (handle == 0) {
        free(m->cells);
        free(m);
    }
    return handle;
}

void tl_map_free(int64_t map) {
    tl_map *m = (tl_map *) tl_get(&tl_maps, map);
    if (!m) return;
    free(m->cells);
    free(m);
    tl_maps.items[map - 1] = NULL;
}

void tl_map_set(int64_t map, int32_t x, int32_t y, int32_t cell) {
    tl_map *m = (tl_map *) tl_get(&tl_maps, map);
    if (m && x >= 0 && y >= 0 && x < m->cols && y < m->rows) m->cells[y * m->cols + x] = cell;
}

int64_t tl_map_get(int64_t map, int32_t x, int32_t y) {
    tl_map *m = (tl_map *) tl_get(&tl_maps, map);
    if (!m || x < 0 || y < 0 || x >= m->cols || y >= m->rows) return 0;
    return m->cells[y * m->cols + x];
}

/* Every cell of `map` that shows on `surf` when the map's pixel (cam_x, cam_y) is the surface's
 * top-left.  Cells are tw x th apart: a transposed non-square tile overlaps its neighbour. */
void tl_draw_map(int64_t surf, int64_t map, int64_t set, int32_t cam_x, int32_t cam_y) {
    tl_surf *d = (tl_surf *) tl_get(&tl_surfs, surf);
    tl_map *m = (tl_map *) tl_get(&tl_maps, map);
    tl_set *s = (tl_set *) tl_get(&tl_sets, set);
    int32_t cx0, cy0, cx1, cy1, cx, cy;
    if (!d || !m || !s) return;
    if (!tl_plain(d)) {
        /* The cells under the surface's first and last pixel centres, and each drawn as a sprite. */
        int32_t u0 = tl_unit(tl_centre(d->cam_x, d->zoom, 0)) + cam_x;
        int32_t u1 = tl_unit(tl_centre(d->cam_x, d->zoom, d->w - 1)) + cam_x;
        int32_t v0 = tl_unit(tl_centre(d->cam_y, d->zoom, 0)) + cam_y;
        int32_t v1 = tl_unit(tl_centre(d->cam_y, d->zoom, d->h - 1)) + cam_y;
        cx0 = tl_floor_div(u0, s->tw);
        cy0 = tl_floor_div(v0, s->th);
        cx1 = tl_floor_div(u1, s->tw);
        cy1 = tl_floor_div(v1, s->th);
    } else {
        cx0 = tl_floor_div(cam_x, s->tw);
        cy0 = tl_floor_div(cam_y, s->th);
        cx1 = tl_floor_div(cam_x + d->w - 1, s->tw);
        cy1 = tl_floor_div(cam_y + d->h - 1, s->th);
    }
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= m->cols) cx1 = m->cols - 1;
    if (cy1 >= m->rows) cy1 = m->rows - 1;
    for (cy = cy0; cy <= cy1; cy++) {
        for (cx = cx0; cx <= cx1; cx++) {
            int32_t cell = m->cells[cy * m->cols + cx];
            if (cell == 0) continue;
            if (!tl_plain(d)) {
                tl_sprite_at(d, s, (cell & 0xFFFFFF) - 1, (int64_t) (cx * s->tw - cam_x) * 256,
                             (int64_t) (cy * s->th - cam_y) * 256, 0, 0, 0, 256, 256, (cell >> 24) & 7);
                continue;
            }
            tl_blit(d, s, (cell & 0xFFFFFF) - 1, cx * s->tw - cam_x, cy * s->th - cam_y,
                    (cell >> 24) & 7);
        }
    }
}

/* ── Present ── */

void tl_present565(int64_t surf, uint16_t *fb, int32_t stride, int32_t scale, int32_t rotate);
void tl_present(int64_t surf, int32_t *out, int64_t n, int32_t scale, int32_t rotate);

/* `n` source pixels, `sstep` apart, each written `scale` times in a row from `q`.  The common
 * scales are their own loops — at 4x one source pixel is one 64-bit store — because a repeat
 * count the compiler cannot see is most of the cost of a present. */
static void tl_expand(uint16_t *q, const uint16_t *src, ptrdiff_t sstep, int32_t n, int32_t scale) {
    int32_t i, r;
    switch (scale) {
    case 1:
        for (i = 0; i < n; i++) q[i] = src[i * sstep];
        break;
    case 2:
        for (i = 0; i < n; i++) {
            uint32_t v = (uint32_t) src[i * sstep] * 0x00010001u;
            memcpy(q + 2 * i, &v, sizeof v);
        }
        break;
    case 3:
        for (i = 0; i < n; i++) {
            uint16_t c = src[i * sstep];
            q[3 * i] = c;
            q[3 * i + 1] = c;
            q[3 * i + 2] = c;
        }
        break;
    case 4:
        for (i = 0; i < n; i++) {
            uint64_t v = (uint64_t) src[i * sstep] * UINT64_C(0x0001000100010001);
            memcpy(q + 4 * i, &v, sizeof v);
        }
        break;
    default:
        for (i = 0; i < n; i++) {
            uint16_t c = src[i * sstep];
            for (r = 0; r < scale; r++) *q++ = c;
        }
    }
}

/* `surf` scaled by the integer `scale` into `fb`, `stride` pixels apart row to row.  Unrotated
 * the result is (w * scale) x (h * scale); rotated a quarter turn clockwise — the surface's
 * top-left lands at the top-right, for a panel mounted portrait — it is (h * scale) x
 * (w * scale).  Each output row is expanded once and copied for its `scale - 1` repeats. */
void tl_present565(int64_t surf, uint16_t *fb, int32_t stride, int32_t scale, int32_t rotate) {
    const tl_surf *s = (const tl_surf *) tl_get(&tl_surfs, surf);
    const uint16_t *px;
    int32_t w, h, ow, oh, k, r;
    if (!s || scale <= 0) return;
    px = s->px;
    w = s->w;
    h = s->h;
    ow = (rotate ? h : w) * scale;
    oh = (rotate ? w : h) * scale;
    for (k = 0; k < (rotate ? w : h); k++) {
        uint16_t *row = fb + (size_t) k * (size_t) scale * (size_t) stride;
        if (!rotate) {
            tl_expand(row, px + (size_t) k * (size_t) w, 1, w, scale);
        } else {
            /* Output row k reads source column k, from the bottom row up. */
            tl_expand(row, px + (size_t) (h - 1) * (size_t) w + (size_t) k, -(ptrdiff_t) w, h, scale);
        }
        for (r = 1; r < scale && k * scale + r < oh; r++) {
            memcpy(row + (size_t) r * (size_t) stride, row, (size_t) ow * sizeof(uint16_t));
        }
    }
}

/* The same into loft's `vector<i32>`, `n` = the output's pixel count, packed (stride = width). */
void tl_present(int64_t surf, int32_t *out, int64_t n, int32_t scale, int32_t rotate) {
    tl_surf *s = (tl_surf *) tl_get(&tl_surfs, surf);
    uint16_t *fb;
    int64_t i;
    if (!s || scale <= 0 || n != (int64_t) s->w * s->h * scale * scale) return;
    fb = (uint16_t *) malloc((size_t) n * sizeof(uint16_t));
    if (!fb) return;
    tl_present565(surf, fb, (rotate ? s->h : s->w) * scale, scale, rotate);
    for (i = 0; i < n; i++) out[i] = fb[i];
    free(fb);
}
