/* A frame as a small game on the Tanmatsu draws it, timed: a 200x120 surface, two 32x20 layers
 * of 16 px tiles (a 192x176 sheet of 132 tiles, 27 colours, a quarter of it transparent), 16
 * sprites, and the present at 4x, rotated a quarter turn into an 800x480 RGB565 framebuffer.
 * Build: cc -O2 bench/frame.c -o frame && ./frame */
#include "../c/tiles.c"
#include <stdio.h>
#include <time.h>

static uint32_t seed = 12345u;
static uint32_t next(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

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
    return 0;
}
