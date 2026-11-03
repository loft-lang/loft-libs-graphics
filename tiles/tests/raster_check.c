/* The camera rasteriser checked in C, beside tests/tiles.loft: whole zooms against the 1x
 * drawing scaled, fractions of a unit, the half-open edge rule, and turned, stretched, flipped
 * and clipped sprites against a floating-point reference (differing only within 1e-4 of a
 * texel edge).  Run from tests/: cc -O2 -std=gnu99 raster_check.c -lm -o /tmp/rc && /tmp/rc */
#include "../c/tiles.c"
#include <stdio.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; if (fails < 30) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static tl_surf *S(int64_t h) { return (tl_surf *) tl_get(&tl_surfs, h); }
/* A sheet of tiles tw x th, every texel its own colour except a transparent corner texel. */
static int64_t sheet(int tw, int th, int tiles_across) {
    int w = tw * tiles_across, h = th, x, y; static uint8_t rgba[4 * 64 * 64];
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        uint8_t *p = rgba + 4 * (y * w + x); int k = (y * w + x);
        p[0] = (uint8_t) (8 * (k % 31) + 4); p[1] = (uint8_t) (4 * ((k / 31) % 63) + 2); p[2] = (uint8_t) (8 * (k % 7) + 100);
        p[3] = ((x % tw) == tw - 1 && y == 0) ? 0 : 255;
    }
    return tl_set_new(rgba, (int64_t) w * h * 4, w, h, tw, th, 0, 0);
}
static int same_scaled(int64_t small, int64_t big, int k) {
    tl_surf *a = S(small), *b = S(big); int x, y, bad = 0;
    for (y = 0; y < b->h; y++) for (x = 0; x < b->w; x++)
        if (b->px[y * b->w + x] != a->px[(y / k) * a->w + x / k]) bad++;
    return bad;
}
/* Float reference: the texel under pixel (i, j)'s centre, or -1. */
static int ref_texel(const tl_set *s, double zoom, double camx, double camy, double x, double y, double turn,
                     double pvx, double pvy, double sx, double sy, int flags, int i, int j, double *edge) {
    int fd = (flags >> 2) & 1, ow = fd ? s->th : s->tw, oh = fd ? s->tw : s->th; int32_t at[3];
    double ux = camx + (i + 0.5) / zoom, uy = camy + (j + 0.5) / zoom, a = turn * M_PI / 180.0;
    double dx = ux - (x + pvx), dy = uy - (y + pvy);
    double tu = pvx + (cos(a) * dx + sin(a) * dy) / sx, tv = pvy + (cos(a) * dy - sin(a) * dx) / sy;
    double fu = tu - floor(tu), fv = tv - floor(tv);
    *edge = fmin(fmin(fu, 1 - fu), fmin(fv, 1 - fv));
    if (tu < 0 || tv < 0 || tu >= ow || tv >= oh) { if (fabs(tu) < 1e-6 || fabs(tv) < 1e-6 || fabs(tu-ow) < 1e-6 || fabs(tv-oh) < 1e-6) *edge = 0; return -1; }
    tl_orient(s, flags, at);
    return at[0] + (int) floor(tu) * at[1] + (int) floor(tv) * at[2];
}
int main(void) {
    int64_t set = sheet(8, 8, 4), set2 = sheet(8, 6, 4), k, f, t;
    tl_set *ss = (tl_set *) tl_get(&tl_sets, set);
    CHECK(set && set2, "sheets");
    /* 1. whole zoom: draw / rect / map / ground / quarter turns at zoom 3 == 1x scaled 3 */
    for (f = 0; f < 8; f++) for (t = 0; t < 4; t++) {
        int64_t a = tl_surf_new(40, 30), b = tl_surf_new(120, 90);
        tl_camera(b, 3 << 16, 0, 0);
        tl_clear(a, 7); tl_clear(b, 7);
        tl_draw(a, set, (int32_t) t, -3, 5, (int32_t) f); tl_draw(b, set, (int32_t) t, -3, 5, (int32_t) f);
        tl_draw(a, set2, (int32_t) t, 30, -2, (int32_t) f | (3 << 8)); tl_draw(b, set2, (int32_t) t, 30, -2, (int32_t) f | (3 << 8));
        tl_rect(a, 10, 20, 7, 4, 1234); tl_rect(b, 10, 20, 7, 4, 1234);
        tl_draw_turned(a, set, 1, 14, 10, (int32_t) (90 * t), 3, 5, (int32_t) f); tl_draw_turned(b, set, 1, 14, 10, (int32_t) (90 * t), 3, 5, (int32_t) f);
        tl_draw_turned(a, set2, 2, 24, 12, (int32_t) (90 * t + 360), 2, 1, (int32_t) f); tl_draw_turned(b, set2, 2, 24, 12, (int32_t) (90 * t + 360), 2, 1, (int32_t) f);
        tl_draw_ground(a, set, 3, 2, 16, 0, 0, 0, 0, 0, 37, 90, 112, (int32_t) f); tl_draw_ground(b, set, 3, 2, 16, 0, 0, 0, 0, 0, 37, 90, 112, (int32_t) f);
        tl_draw_ground(a, set, 2, 22, 22, 0, 0, 0, 0, 16, 16, 16, 16, (int32_t) f); tl_draw_ground(b, set, 2, 22, 22, 0, 0, 0, 0, 16, 16, 16, 16, (int32_t) f);
        tl_draw(a, set, 0, 33, 1, (int32_t) f | 4096); /* ghost: texel parity vs surface parity differ by (x+y)&1 = 0 here */
        tl_draw(b, set, 0, 33, 1, (int32_t) f | 4096);
        CHECK(same_scaled(a, b, 3) == 0, "zoom 3 == 1x scaled, flags %d quarter %d: %d px differ", (int) f, (int) t, same_scaled(a, b, 3));
        tl_surf_free(a); tl_surf_free(b);
    }
    {   /* map */
        int32_t cells[12] = {1, 2, 3, 0, 4 | (1 << 24), 2 | (5 << 24), 1, 1, 3, 4, 2 | (2 << 24), 1};
        int64_t m = tl_map_new(cells, 12, 4, 3), a = tl_surf_new(30, 20), b = tl_surf_new(90, 60);
        tl_camera(b, 3 << 16, 0, 0);
        tl_draw_map(a, m, set, 5, -3); tl_draw_map(b, m, set, 5, -3);
        CHECK(same_scaled(a, b, 3) == 0, "map at zoom 3: %d differ", same_scaled(a, b, 3));
    }
    /* 2. a third of a unit is one pixel at zoom 3 */
    {
        int64_t a = tl_surf_new(60, 40), b = tl_surf_new(60, 40); int x, y, bad = 0;
        tl_camera(a, 3 << 16, 0, 0); tl_camera(b, 3 << 16, 0, 0);
        tl_sprite(a, set, 1, 2 * 256, 3 * 256, 0, 0, 0, 256, 256, 0);
        tl_sprite(b, set, 1, 2 * 256 + 85, 3 * 256 + 171, 0, 0, 0, 256, 256, 0); /* 85/256 ~ 1/3, 171/256 ~ 2/3 */
        for (y = 0; y < 40; y++) for (x = 0; x < 60; x++) {
            uint16_t va = S(a)->px[y * 60 + x], vb = (x + 1 < 60 && y + 2 < 40) ? S(b)->px[(y + 2) * 60 + x + 1] : 0;
            if (x + 1 < 60 && y + 2 < 40 && va != vb) bad++;
        }
        CHECK(bad == 0, "1/3 unit = 1 pixel at zoom 3: %d differ", bad);
    }
    /* 3. zoom 1.5: texel columns 1, 2, 1, 2 pixels wide */
    {
        int64_t a = tl_surf_new(12, 12); int i, want[12] = {0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7};
        tl_camera(a, 98304, 0, 0);
        tl_sprite(a, set, 1, 0, 0, 0, 0, 0, 256, 256, 0);
        for (i = 0; i < 12; i++) { uint16_t v = S(a)->px[5 * 12 + i]; uint16_t e = ss->pal[0][ss->px[64 + 3 * 8 + want[i]]]; CHECK(v == e, "zoom 1.5 column %d", i); }
    }
    /* 4./6. free angles, stretches, flips, cameras, clipping: against the float reference */
    {
        double zooms[4] = {1.0, 3.0, 4.0, 2.5}; int zi, n = 0, bad = 0, near = 0;
        for (zi = 0; zi < 4; zi++) for (f = 0; f < 8; f++) for (t = -50; t <= 370; t += 7) {
            int64_t a = tl_surf_new(70, 50), st = (f & 1) ? set2 : set; int i, j;
            tl_set *s = (tl_set *) tl_get(&tl_sets, st);
            double z = zooms[zi], camx = -1.25, camy = 0.5, x = llround(3.4 * 256) / 256.0, y = llround(2.1 * 256) / 256.0, pvx = 4.5, pvy = 7.25, sx = 1.0 + (t % 3) * 0.5, sy = 1.0 + (f % 2) * 0.75;
            int64_t zq = (int64_t) llround(z * 65536);
            tl_camera(a, zq, (int64_t) llround(camx * 256), (int64_t) llround(camy * 256));
            tl_clear(a, 0);
            tl_sprite(a, st, 2, (int64_t) llround(x * 256), (int64_t) llround(y * 256), t * 256 + 37, (int64_t) llround(pvx * 256), (int64_t) llround(pvy * 256), (int64_t) llround(sx * 256), (int64_t) llround(sy * 256), (int32_t) f);
            for (j = 0; j < 50; j++) for (i = 0; i < 70; i++) {
                double edge; int k2 = ref_texel(s, z, camx, camy, x, y, t + 37.0 / 256, pvx, pvy, sx, sy, (int) f, i, j, &edge);
                uint16_t want = 0, got = S(a)->px[j * 70 + i];
                if (zq == 65536 && camx == 0) continue;
                if (k2 >= 0) { uint8_t c = s->px[2 * s->tw * s->th + k2]; want = c ? s->pal[0][c] : 0; }
                n++;
                if (got != want) { if (edge < 1e-4) near++; else { bad++; if (bad < 5) printf("  z %.2f f %d t %d px %d,%d got %u want %u edge %g\n", z, (int) f, (int) t, i, j, got, want, edge); } }
            }
            tl_surf_free(a);
        }
        CHECK(bad == 0, "turned sprites vs float reference: %d of %d differ away from an edge (%d at an edge)", bad, n, near);
        printf("turned: %d pixels compared, %d differ within 1e-4 of a texel edge\n", n, near);
    }
    /* 5. camera at 5.5 == sprite at x - 5.5 */
    {
        int64_t a = tl_surf_new(50, 30), b = tl_surf_new(50, 30); int i, bad = 0;
        tl_camera(a, 2 << 16, (int64_t) (5.5 * 256), 0); tl_camera(b, 2 << 16, 0, 0);
        tl_sprite(a, set, 1, 9 * 256, 2 * 256, 20 * 256, 512, 512, 300, 256, 0);
        tl_sprite(b, set, 1, (int64_t) (3.5 * 256), 2 * 256, 20 * 256, 512, 512, 300, 256, 0);
        for (i = 0; i < 50 * 30; i++) if (S(a)->px[i] != S(b)->px[i]) bad++;
        CHECK(bad == 0, "camera 5.5 == sprite moved 5.5: %d differ", bad);
    }
    /* fill: centres in [0.25, 1.25) at zoom 4 -> columns 1..4 */
    {
        int64_t a = tl_surf_new(8, 8); int i;
        tl_camera(a, 4 << 16, 0, 0); tl_fill(a, 64, 0, 256, 512, 99);
        for (i = 0; i < 8; i++) CHECK(S(a)->px[i] == (i >= 1 && i <= 4 ? 99 : 0), "fill col %d", i);
    }
    /* a centre exactly on an edge belongs to what starts there (half-open): the camera half a
     * unit left at zoom 1 puts pixel i's centre at unit i; a sprite at x = 2 covers pixels
     * 2 .. 9, a fill from 10 to 12 covers 10 and 11 */
    {
        int64_t a = tl_surf_new(14, 4); int i;
        tl_camera(a, 65536, -128, 0); tl_clear(a, 5);
        tl_sprite(a, set, 1, 2 * 256, 0, 0, 0, 0, 256, 256, 0);
        tl_fill(a, 10 * 256, 0, 2 * 256, 256, 77);
        for (i = 0; i < 14; i++) {
            uint16_t got = S(a)->px[1 * 14 + i], want = 5, top = S(a)->px[i], want_top = 5;
            if (i >= 2 && i <= 9) {
                want = ss->pal[0][ss->px[64 + 1 * 8 + (i - 2)]];
                want_top = ss->px[64 + (i - 2)] ? ss->pal[0][ss->px[64 + (i - 2)]] : 5;
            }
            if (i >= 10 && i <= 11) want_top = 77;
            CHECK(got == want, "edge column %d row 1: got %u want %u", i, got, want);
            CHECK(top == want_top, "edge column %d row 0: got %u want %u", i, top, want_top);
        }
    }
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
