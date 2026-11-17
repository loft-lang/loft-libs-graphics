<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu_paint

Rectangles, images and sprites at pixel positions, drawn through [`gpu`](../gpu/README.md):
what the graphics library's `Painter` and `Sprites` draw, for any backend.

```loft
use gpu;
use gpu_paint;

p     = gpu_paint::painter(dev, gpu::INTO_WINDOW) ?? return;   // origin top-left, y down
sheet = gpu_paint::sprites(dev, pixels, 64, 32, 4, 2, gpu::INTO_WINDOW) ?? return;

dev.begin_window(gpu::CLEAR, 0xFF202020);
p.rect(dev, 10.0, 10.0, 100.0, 20.0, 1.0, 0.5, 0.0, 1.0);
p.image(dev, icon, 120.0, 10.0, 32.0, 32.0);
p.sprite(dev, sheet, 160.0, 10.0, 32.0, 32.0, 3);
dev.end();
```

Built for consumers, none yet: it stands beside graphics' own painter until that one goes.

## What holds

- **A painter draws inside the pass the caller began**, in call order, over whatever the pass
  holds.  Nothing it draws is tested against depth: a later rectangle covers an earlier one,
  and a HUD is never hidden behind a 3D scene.
- **It blends by a colour's alpha** (`p.blend`, true when a painter is made); switched off, a
  draw replaces what is there.  A fully transparent pixel of an image or a sprite is never
  drawn.
- **A painter is made for one kind of target** (`gpu::INTO_WINDOW`, `gpu::INTO_COLOUR`, …).
  Used in a pass into another kind it draws nothing, and `dev.refusals()` says so.
- **`resize(width, height)`** after the window changed size, or for a pass into a texture of
  another size.
- **A sheet's sprite 0 is the top left**, counting along the rows.  `sheet.draw(dev, mvp,
  index)` places one in a 3D scene, tested against depth and written to it.

| Call | What it draws |
|---|---|
| `painter(dev, into)` | — a painter, null on a backend this library has no shader source for |
| `p.rect(dev, x, y, w, h, r, g, b, a)` | A filled rectangle, channels 0.0 to 1.0 |
| `p.image(dev, texture, x, y, w, h)` | A texture stretched over the rectangle |
| `p.sprite(dev, sheet, x, y, w, h, index)` | One cell of a sheet |
| `sprites(dev, pixels, width, height, cols, rows, into)` | — a sheet from `0xAARRGGBB` pixels, the top row first |
| `sheet.draw(dev, mvp, index)` | One cell through a matrix of 16 floats, column-major |

## How it differs from graphics' painter

graphics' `Painter` draws at once into whatever its window was last told (`render_to`,
`viewport`) and blends where the window had blending on, by the factors the window was given.
This one takes none of that from an earlier call: the pass names the target, the painter
names its own blending.

## Backends

It carries a shader source for `gpu_gl` (OpenGL, OpenGL ES 3, WebGL2), one for `gpu_metal`
(a Mac's Metal) and an empty one for `gpu_record`, so a program's 2D drawing can be tested with
no display.  Each further backend adds a source here.

## Verified

`tests/same_as_graphics.loft` draws each scene through this library on `gpu_gl` and through
graphics' own `Painter` and `Sprites`, and compares the two captures byte for byte: opaque
and blended rectangles at whole and fractional positions, after a resize; images sampled
nearest and between their pixels with a transparent one; sprites by the painter and in a
scene with depth; a painter into a texture that another shows.  They pass on a Mac on the
interpreter, the C backend, the Rust backend and a shipped program, and in both writers'
browser pages.  `tests/record.loft` reads what a painter asks of a backend from the recording
one, on every machine.  `tests/same_on_metal.loft` draws the same four scenes on `gpu_metal`
and on `gpu_gl` and compares them byte for byte, on a Mac.
