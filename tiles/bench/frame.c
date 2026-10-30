/* A frame as a small game on the Tanmatsu draws it, timed: a 200x120 surface, two 32x20 layers
 * of 16 px tiles (a 192x176 sheet of 132 tiles, 27 colours, a quarter of it transparent), 16
 * sprites, and the present at 4x, rotated a quarter turn into an 800x480 RGB565 framebuffer.
 * Then the same frame with its ground layer drawn cell by cell, twice: as plain tiles, and as
 * ground (tl_draw_ground) with every cell sloped and lit — corner lifts of 0..8 pixels and
 * shades of 0..63 sixteenths, shared between neighbouring cells — the most a frame can have.
 * Build: cc -O2 bench/frame.c -o frame && ./frame */
#include "../c/tiles.c"
#include <stdio.h>
#include <time.h>

static uint32_t seed = 12345u;
static uint32_t next(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

/* Corner (i, j) of the ground grid: its lift and its darkness, fixed per corner and frame. */
static int32_t corner_lift(int32_t i, int32_t j, int32_t f) { return (int32_t) ((i * 7 + j * 3 + f) % 9); }
static int32_t corner_dark(int32_t i, int32_t j, int32_t f) { return (int32_t) ((i * 11 + j * 5 + f) % 64); }

static double since(struct timespec t0, int frames) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((double) (t1.tv_sec - t0.tv_sec) * 1e3 + (double) (t1.tv_nsec - t0.tv_nsec) / 1e6) / frames;
}

int main(void) {
    static uint8_t rgba[192 * 176 * 4];
    static int32_t cells[2][32 * 20];
    static uint16_t fb[480 * 800];
    int i, f, frames = 600;
    int64_t sheet, surf, layer[2];
    struct timespec t0, t1;
    double ms;
    for (i = 0; i < 192 * 176; i++) {
        uint32_t c = next() % 28; /* 27 colours and transparent */
        rgba[4 * i] = (uint8_t) (c * 9);
        rgba[4 * i + 1] = (uint8_t) (c * 5);
        rgba[4 * i + 2] = (uint8_t) (255 - c * 9);
        rgba[4 * i + 3] = c == 0 ? 0 : 255;
    }
    sheet = tl_set_new(rgba, (int64_t) sizeof rgba, 192, 176, 16, 16, 0, 0);
    for (i = 0; i < 32 * 20; i++) {
        cells[0][i] = (int32_t) (next() % 132 + 1) | (int32_t) ((next() % 8) << 24);
        cells[1][i] = next() % 3 == 0 ? (int32_t) (next() % 132 + 1) : 0;
    }
    layer[0] = tl_map_new(cells[0], 32 * 20, 32, 20);
    layer[1] = tl_map_new(cells[1], 32 * 20, 32, 20);
    surf = tl_surf_new(200, 120);
    if (!sheet || !layer[0] || !layer[1] || !surf) { puts("setup failed"); return 1; }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (f = 0; f < frames; f++) {
        int32_t cam_x = f % 312, cam_y = (f / 2) % 200;
        tl_clear(surf, 0);
        tl_draw_map(surf, layer[0], sheet, cam_x, cam_y);
        tl_draw_map(surf, layer[1], sheet, cam_x, cam_y);
        for (i = 0; i < 16; i++) tl_draw(surf, sheet, (f + i) % 132, (i * 37 + f) % 200 - 8, (i * 23) % 120 - 8, i % 8);
        tl_present565(surf, fb, 480, 4, 1);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = ((double) (t1.tv_sec - t0.tv_sec) * 1e3 + (double) (t1.tv_nsec - t0.tv_nsec) / 1e6) / frames;
    printf("%.3f ms per frame (%d frames); fb[0]=%u\n", ms, frames, (unsigned) fb[0]);
    {
        int ground;
        for (ground = 0; ground < 2; ground++) {
            int32_t cx, cy;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (f = 0; f < frames; f++) {
                int32_t cam_x = f % 312, cam_y = (f / 2) % 200, ox = cam_x % 16, oy = cam_y % 16;
                tl_clear(surf, 0);
                for (cy = 0; cy < 9; cy++) {
                    for (cx = 0; cx < 14; cx++) {
                        int32_t t = (cells[0][((cam_y / 16 + cy) % 20) * 32 + (cam_x / 16 + cx) % 32] & 0xFFFFFF) - 1;
                        int32_t x = cx * 16 - ox, y = cy * 16 - oy;
                        if (t < 0) continue;
                        if (ground) {
                            tl_draw_ground(surf, sheet, t, x, y,
                                           corner_lift(cx, cy, f), corner_lift(cx + 1, cy, f),
                                           corner_lift(cx, cy + 1, f), corner_lift(cx + 1, cy + 1, f),
                                           corner_dark(cx, cy, f), corner_dark(cx + 1, cy, f),
                                           corner_dark(cx, cy + 1, f), corner_dark(cx + 1, cy + 1, f), 0);
                        } else {
                            tl_draw(surf, sheet, t, x, y, 0);
                        }
                    }
                }
                tl_draw_map(surf, layer[1], sheet, cam_x, cam_y);
                for (i = 0; i < 16; i++) tl_draw(surf, sheet, (f + i) % 132, (i * 37 + f) % 200 - 8, (i * 23) % 120 - 8, i % 8);
                tl_present565(surf, fb, 480, 4, 1);
            }
            ms = since(t0, frames);
            printf("%.3f ms per frame, the ground %s; fb[0]=%u\n", ms,
                   ground ? "sloped and lit in every cell" : "as plain tiles, cell by cell", (unsigned) fb[0]);
        }
    }
    return 0;
}
