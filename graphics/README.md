<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# graphics — a 2D canvas, windows, OpenGL and text for loft

## Install

```sh
loft install graphics
```

## Surface

Everything is a value the program holds: a window, a shader or a texture is closed or deleted
when its owner lets it go, and nothing is kept behind a number or a hidden "current" window.

```loft
w = open_window(800, 600, "game") ?? return;   // Window: the OS window and its GL context
font = load_font("ui.ttf") ?? return;           // Font: a TrueType face, read in loft
p = w.painter() ?? return;                      // Painter: rectangles and images at pixels
while w.poll() {
  for ev in w.events() {
    match ev {
      KeyDown { key, mods, repeat } => press(key),
      Resize { width, height } => p.resize(width, height),
      _ => {}
    }
  }
  w.clear(rgb(20, 20, 30));
  p.rect(10.0, 10.0, 200.0, 24.0, 0.9, 0.5, 0.1, 1.0);
  w.swap();
}                                               // the window closes here
```

- `Canvas` — a 2-D pixel surface: `set_pixel` / `get_pixel` / `clear` / `blend_pixel` /
  `fill_rect` / `hline` / `vline` / `draw_rect` / `draw_line` / `draw_circle` / `fill_circle` /
  `fill_ellipse` / `draw_ellipse` / `draw_bezier` / `draw_aa_line` / `fill_triangle`, the
  resamplers `resize_lanczos` / `resize_bicubic`, `draw_text(font, …)`, and `save_png`.
- `Window` — `open_window` / `open_fullscreen`, `poll`, `events()` (an `Event` per input: keys,
  text, mouse, wheel, resize), `key_down` / `mouse_x` / `mouse_y` / `mouse_buttons`, `clear`,
  `swap`, `capture` (the frame as a `Canvas`), the render state (`enable` / `disable`, `blend`,
  `cull`, `depth_write`, `viewport`, `scissor`), and `close`.
- On a window: `Shader` (`shader`, uniforms through `set_float` … `set_mat4`), `GpuMesh`
  (`upload`, `draw`, `draw_mode`, `indices` + `draw_indexed`, `update`, instancing through an
  `InstanceBuffer`), `Texture` (`texture`, `depth_texture`, `texture_from(canvas)`,
  `load_texture(png)`, `text_texture`), `Framebuffer` (`attach`, `render_to`), `quad`.
- `Painter` (`rect`, `image`, `sprite` at pixel positions, in call order over whatever the frame
  holds — a HUD is never hidden behind 3-D), `Sprites` (a sheet of sprites), `GroupMeshes` (a
  mesh in groups re-uploaded one at a time).
- `Window.enabled(cap)` answers whether a switch is on (a window opens with depth testing on), so
  a pass can hand the window back as it found it.
- `Font` — `load_font`, `measure`, `ascent`, `line_height`, `rasterize` (a glyph's coverage).

The 3-D maths, meshes and scenes are the [`mesh3d`](https://github.com/loft-lang/loft-libs-assets/tree/main/mesh3d)
package and glTF output is [`glb`](https://github.com/loft-lang/loft-libs-assets/tree/main/glb);
a program that wants them imports them itself (`use mesh3d::*;`, `use glb::*;`).

A guide: [docs/01-getting-started.loft](docs/01-getting-started.loft).

### Colours and the canvas

A colour is one `integer` packed **0xAARRGGBB** — build it with `rgba` / `rgb` rather than a
hex literal, which leaves the alpha byte at 0 (fully transparent).  `Canvas.data` is a flat,
row-major `vector<integer>`: the pixel at (x, y) is `data[y * width + x]`.  Every solid
primitive **stores** its colour; only `blend_pixel` composites.  Span ends are **exclusive**,
and a reversed span draws nothing.

### Text

Fonts are read and rasterized in loft: a glyph's coverage is fontdue 0.9.3's, byte for byte
(what graphics 0.9 drew with), so text looks the same on every target.

### Native code and targets

No Rust crate: the native part is `c/graphics_gl.c`, a `[c] shim` loft compiles itself.

| Target | Window | Tested by |
|---|---|---|
| `--interpret`, `--native`, `--native-c` on Linux | X11 + GLX, `libX11.so.6` and `libGL.so.1` opened at run time (no development package) | the suite under Xvfb (`-noreset`); `desktop/input.sh` injects input through XTest |
| `--html` (Rust and C pages) | the page's canvas and WebGL2, through loft's `loft_gl` page imports — a loft whose pages carry them (main after loft `d26929886`); an older loft refuses the page at build | `browser/check.sh` in headless Chrome |
| `--interpret`, `--native`, `--native-c` on macOS | AppKit and an `NSOpenGLContext` (3.2 core, served as GL 4.1), through the Objective-C runtime opened at run time (no SDK) | the suite on a macOS runner with `GRAPHICS_TEST_WINDOW=1`; `desktop/input_mac.sh` posts the same input sequence into the app's own queue |
| `--native-wasm` | none: `open_window` answers null | the suite's canvas, font and PNG half |
| Windows | not yet: `open_window` answers null | — |

Everything without a window — the canvas, fonts, PNG output — answers the same on every target.

**On macOS** a window lives on the main thread, as AppKit requires.  The interpreter and the C
backend run the program there; a `--native` program runs on its own big-stack thread, and its
window work is sent to the main thread, which loft's native `main` keeps in its run loop (a loft
from 2026-10-31 on — an older one is told so, and `open_window` answers null).  The drawable is
in points, as the window's size is: on a Retina screen the frame is scaled up, not drawn at the
screen's pixel density.

**The suite** opens a window wherever one is expected and fails when none opens: `DISPLAY` says
so on X11 (`xvfb-run -a -s '-noreset -screen 0 800x600x24'`), and on a Mac, where nothing in the
environment says whether a window server is reachable, `GRAPHICS_TEST_WINDOW=1` does.

## Moving from 0.9

0.10 is a declared break (`api_compatible_with = "0.10.0"`): a program whose `loft.lock` holds
a 0.9 release stays there through `loft update`, which names 0.10.0 as held back, until
`loft.toml` asks for `>=0.10.0`.  A fresh install with no lock takes 0.10.  The calls map as: `gl_create_window` → `open_window`;
`gl_poll_events` / `gl_next_event` → `poll` + `events()`; `gl_key_pressed` → `key_down`;
`gl_create_shader` + `gl_set_uniform_*` → `shader` + `set_*`; `gl_upload_vertices` + `gl_draw`
→ `upload` + `draw`; `gl_create_framebuffer` / `gl_create_color_texture` → `framebuffer` /
`texture`; `create_painter_2d` / `draw_rect_at` / `draw_texture_at` → `painter` / `rect` /
`image`; `create_text_texture` → `text_texture`; `gl_load_font` → `load_font`;
`group_vbos_*` → `group_meshes`.  Audio left graphics: no consumer used it.

## Worked examples

The contracts a signature cannot state are demonstrated by running tests
(@PLN141): [tests/worked-examples.loft](tests/worked-examples.loft) —
`@GFX-001` the alpha byte a hex literal forgets, `@GFX-002` store versus
composite, `@GFX-003` why `get_pixel`'s 0 is not a bounds test, `@GFX-004`
half-open spans that are never normalised, `@GFX-005` how `save_png` picks RGB
or RGBA off the pixels.

## Provenance

Part of the loft-libs-graphics chunk — loft's
[library extraction plan](https://github.com/loft-lang/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md).
0.10's object API and C shim: [@PLN208](https://github.com/loft-lang/plans/issues/208).
