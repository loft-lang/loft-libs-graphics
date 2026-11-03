<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# tiles — tile sheets, map layers and sprites, drawn at a surface's own resolution

A tile layer built for a small handheld (the Tanmatsu: a 32-bit core, an 800x480 panel) and the
same on every target.

A surface is drawn at its own resolution through a **camera**: how many surface pixels one unit
is, and which point is at its top-left.  Every primitive — a sprite placed, turned and stretched
by fractions of a unit, a filled rectangle, sloping ground, a map layer — is placed in units and
rasterised at the surface's pixels, each pixel showing what lies under its centre (the nearest
texel, so pixel art stays crisp at every zoom and angle).  A surface the size of the display
therefore draws a game of 16 px tiles at the display's full resolution, and zooming in on two
figures or out over the land is a change of camera.  Integer arithmetic only: every target draws
the same pixels for the same calls.

Without a camera a surface is one pixel per unit, the small frame `present` scales up whole
(200x120 at 4x for 800x480) and every call draws what it drew before cameras existed.

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
| `display_surface(width, height) -> integer?`, `on_display(surface)` | a surface for the display: always drawn through its camera, and in a page drawn by the GPU straight onto the canvas (the same picture as the CPU's, pixel for pixel on the checked scene); `on_display` says the frame is on the screen already, and `pixels` reads it back |
| `camera(surface, zoom, x = 0.0, y = 0.0)` | `zoom` surface pixels per unit (any fraction, 1/256 .. 4096), the point (x, y) at the top-left; what is drawn after it is placed in units and drawn at the surface's resolution |
| `sprite(surface, sheet, tile, x, y, turn = 0.0, pivot_x = 0.0, pivot_y = 0.0, scale_x = 1.0, scale_y = 1.0, flips…, shade, ghost)` | a tile placed, turned `turn` degrees clockwise and stretched about its point `pivot` (in the tile's pixels), every value fractional; a true rotation at the surface's resolution |
| `fill(surface, x, y, w, h, colour)` | a rectangle in units, fractional: the pixels whose centres lie inside it |
| `rect(surface, x, y, w, h, colour)`, `frame(surface, x, y, w, h, colour)` | a filled rectangle and its one-pixel outline: a cursor, a selection, a palette's frame; clipped |
| `draw(surface, sheet, tile, x, y, flip_h, flip_v, flip_d)` | a sprite is a tile drawn this way; clipped, transparency skipped |
| `draw(..., shade)` / `SHADES` | a tile darker: shade 0 is as drawn, `SHADES - 1` (7) about a quarter of the light; each sheet keeps its palette at every shade, so a shade costs nothing |
| `draw(..., shade, ghost)`, `draw_turned(..., shade, ghost)` | a ghost: every other pixel only — of the surface at one pixel per unit, of the tile under a camera, so it turns and stretches with it — and what is behind shows through: a figure seen through the wall in front of it |
| `draw_ground(surface, sheet, tile, x, y, lift, shade = (0, 0, 0, 0), flip_h, flip_v, flip_d)` | a tile as ground that slopes and is lit gradually: its corners (top-left, top-right, bottom-left, bottom-right) lifted `lift` pixels and darkened `shade` sixteenths of a shade (`SHADE_PARTS`); each column is stretched between its edges, so tiles sharing corner lifts meet without a seam, and the light is dithered between shades by a fixed 4x4 pattern; flat and in one whole shade it is `draw` |
| `draw_turned(surface, sheet, tile, x, y, degrees, pivot, flip_h, flip_v, flip_d, shade)` | a tile turned `degrees` clockwise about its pixel `pivot`: a character's lean, a weapon held at an angle or swung.  Under a camera, `sprite`'s true rotation about the pivot pixel's centre.  At one pixel per unit, whole quarter turns and three shears of whole rows and columns, so every pixel lands on exactly one pixel and the art keeps its pixels and colours at any angle; 0 degrees is `draw`.  About 17 000 (0°) to 25 000 (45°) instructions for a 16 px tile, against 3 500 for `draw` |
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
