/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * The tiles library's core, in C99: tile sheets as 8-bit palette indices, a 1x RGB565 surface
 * to compose a frame on, map layers drawn in one call, and an integer upscale (optionally
 * rotated a quarter turn for a portrait panel) into a framebuffer.  Built by loft itself as a
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
} tl_set;

typedef struct {
    uint16_t *px;
    int32_t w, h;
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
    if (d && s) tl_blit(d, s, tile, x, y, flags & 0x1F07);
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
    cx0 = tl_floor_div(cam_x, s->tw);
    cy0 = tl_floor_div(cam_y, s->th);
    cx1 = tl_floor_div(cam_x + d->w - 1, s->tw);
    cy1 = tl_floor_div(cam_y + d->h - 1, s->th);
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= m->cols) cx1 = m->cols - 1;
    if (cy1 >= m->rows) cy1 = m->rows - 1;
    for (cy = cy0; cy <= cy1; cy++) {
        for (cx = cx0; cx <= cx1; cx++) {
            int32_t cell = m->cells[cy * m->cols + cx];
            if (cell == 0) continue;
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
