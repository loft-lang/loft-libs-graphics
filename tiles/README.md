<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# tiles — tile sheets, map layers and sprites, scaled up once per frame

A tile layer built for a small handheld (the Tanmatsu: a 32-bit core, 768 KB of fast SRAM, an
800x480 panel whose framebuffer lives in slower PSRAM) and the same on every target.

A frame is composed at 1x on a small RGB565 `surface` — 200x120 for a 4x scale on 800x480,
48 KB — by drawing map layers and sprites, and then `present`ed once: scaled by an integer and,
for the portrait panel, rotated a quarter turn.  Only the present touches the large buffer.

A sheet is converted once, when it is made: one byte per pixel (index 0 transparent, 1..255
RGB565 colours), tile after tile, so a Kenney 16 px pack (132 tiles) is 34 KB.  Orientation
follows Tiled: transpose (the diagonal flip), then mirror horizontally, then vertically — all
eight ways a tile can lie, as Tiled maps use them.

## Install

```sh
loft install tiles
```

## API

| Function | Notes |
|---|---|
| `sheet(rgba, width, height, tile_w, tile_h, spacing = 0, margin = 0) -> integer?` | null when the sizes do not fit or there are more than 255 colours once in RGB565 |
| `sheet_from_png(path, tile_w, tile_h, …)`, `sheet_from_image(img, …)` | through `imaging` |
| `surface(width, height) -> integer?`, `clear(surface, colour)` | colours are RGB565: `rgb(r, g, b)` |
| `rect(surface, x, y, w, h, colour)`, `frame(surface, x, y, w, h, colour)` | a filled rectangle and its one-pixel outline: a cursor, a selection, a palette's frame; clipped |
| `draw(surface, sheet, tile, x, y, flip_h, flip_v, flip_d)` | a sprite is a tile drawn this way; clipped, transparency skipped |
| `draw(..., shade)` / `SHADES` | a tile darker: shade 0 is as drawn, `SHADES - 1` (7) about a quarter of the light; each sheet keeps its palette at every shade, so a shade costs nothing |
| `draw_ground(surface, sheet, tile, x, y, lift, shade = (0, 0, 0, 0), flip_h, flip_v, flip_d)` | a tile as ground that slopes and is lit gradually: its corners (top-left, top-right, bottom-left, bottom-right) lifted `lift` pixels and darkened `shade` sixteenths of a shade (`SHADE_PARTS`); each column is stretched between its edges, so tiles sharing corner lifts meet without a seam, and the light is dithered between shades by a fixed 4x4 pattern; flat and in one whole shade it is `draw` |
| `draw_turned(surface, sheet, tile, x, y, degrees, pivot, flip_h, flip_v, flip_d, shade)` | a tile turned `degrees` clockwise about its pixel `pivot`: a character's lean, a weapon held at an angle or swung.  Whole quarter turns and three shears of whole rows and columns, so every pixel lands on exactly one pixel and the art keeps its pixels and colours at any angle; 0 degrees is `draw`.  About 17 000 (0°) to 25 000 (45°) instructions for a 16 px tile, against 3 500 for `draw` |
| `layer(cells, cols, rows) -> integer?`, `cell(tile, flips…)`, `set_cell`, `get_cell` | a cell is `(tile + 1) \| flags << 24`, 0 empty |
| `layer_from_tiled_csv(csv, cols, rows, firstgid = 1)`, `from_tiled(gid, firstgid)` | Tiled's flip bits included |
| `draw_layer(surface, layer, sheet, cam_x = 0, cam_y = 0)` | one call per layer, scrolled to the pixel |
| `present(surface, width, height, scale, rotate = false) -> vector<i32>` | the scaled (and rotated) frame, for tests; a device calls `tl_present565` with its framebuffer |
| `pixels(surface, width, height)`, `tile_count`, `colour_count`, `free_*` | |

## Speed

`bench/frame.c` draws the frame a small game draws: a 200x120 surface, two 32x20 layers of
16 px tiles with flips, 16 sprites, and a 4x present rotated into an 800x480 RGB565 buffer.
On the x86-64 machine it was written on, a frame took 2.9–5.4 ms under varying load, the
present most of it: memory traffic, one store per output pixel.  On the device the present's
writes go to PSRAM; that number needs the device.

The bench then draws the ground layer cell by cell, as plain tiles and as `draw_ground` with
every cell sloped and lit — the most a frame can hold.  Counted in instructions per pixel of
ground (x86-64, `-O2`; an in-order core like the Tanmatsu's runs about one a cycle):

| ground | instructions a pixel |
|---|---|
| `draw` | 13.6 |
| `draw_ground`, flat in one whole shade | 14.0 (it is `draw`) |
| `draw_ground`, sloped in one whole shade | 29 |
| `draw_ground`, light fading across the tile, flat or sloped | 39 |

A whole 200x120 frame of ground fading in light is about a million instructions more than plain
tiles.

## Targets

| target | state |
|---|---|
| interpreter, `--native` | ✓ suite |
| C backend (`--native-c`) | ✓ suite matches the interpreter on x86-64, 32-bit x86 and 32-bit RISC-V (under qemu) |
| `--native-wasm` / `--html` | the core builds for wasm32 freestanding |

## Provenance

The core is `c/tiles.c`, a `[c] shim` loft builds itself.
