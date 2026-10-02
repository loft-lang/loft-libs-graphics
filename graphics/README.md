<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# graphics — 2D canvas + 3D rendering for loft

## Install

```sh
loft install graphics
```

## Surface

### The canvas and the window

- `Canvas` — a 2-D pixel surface with `set_pixel` / `get_pixel` / `clear` /
  `blend_pixel` / `fill_rect` / `hline` / `vline` / `draw_rect` / `draw_line` /
  `draw_circle` / `fill_circle` / `fill_ellipse` / `draw_ellipse` / `draw_bezier` /
  `draw_aa_line` / `fill_triangle`, the resamplers `resize_lanczos` /
  `resize_bicubic`, and PNG output via `save_png`.
- OpenGL bindings (`gl_create_window` / `gl_create_fullscreen_window` / shaders /
  VAOs / textures / FBOs / `gl_draw` / `gl_clear` / `gl_swap_buffers`); sprite
  sheets (`SpriteSheet` + `draw_sprite`); `Painter2D` for fixed-function 2D draws
  over GL.

The 3-D maths, meshes and scenes are the [`mesh3d`](https://github.com/loft-lang/loft-libs-assets/tree/main/mesh3d)
package and glTF output is [`glb`](https://github.com/loft-lang/loft-libs-assets/tree/main/glb).
`graphics` depends on both for its own use and does not pass their names on, so
a program that wants `mat4_identity` or `save_scene_glb` imports them itself:
`use mesh3d::*;`, `use glb::*;`.

A guide: [docs/01-getting-started.loft](docs/01-getting-started.loft).

### Audio

`audio_load` a WAV or OGG, then `audio_play(clip, volume, looping, pan, start)`
— everything past `volume` has a default, so `audio_play(clip, 0.8)` still means
what it always meant.  A playback answers a handle for `audio_stop`,
`audio_set_volume`, `audio_set_pan` and `audio_seek`, and `audio_stop_all` ends
everything at once.

The desktop and the browser are ONE contract, and where the two could differ this
package takes the Web Audio answer, because that side has a specification and
rodio has a mixer:

- **Pan** is `StereoPannerNode`'s law — equal-power for a mono clip, so a sweep
  holds its loudness through the middle instead of dipping; a stereo clip is
  narrowed toward the near side rather than having the far one muted.
- **`start` skips into the first pass only.**  A looping clip then repeats whole,
  which is what `start(when, offset)` with `loop` does in a browser.
- **A handle is never handed out twice.**  Slots are reused as sounds finish, but
  the handle carries which use it belongs to, so `audio_stop` on a finished sound
  stops nothing rather than stopping whatever took its place.
- **A looping clip cannot seek**, and `audio_seek` answers false rather than
  leaving the caller believing it moved: repeating needs a buffered source, which
  has no earlier position to go back to.

The chiptune helpers (`sfx_beep` / `sfx_chirp` / `sfx_descend` / `sfx_noise`)
synthesise into `audio_play_raw` and need no file at all.

### Colours and the canvas

A colour is one `integer` packed **0xAARRGGBB** — build it with `rgba` / `rgb`
rather than a hex literal, which leaves the alpha byte at 0 (fully transparent).
`Canvas.data` is a flat, row-major `vector<integer>`: the pixel at (x, y) is
`data[y * width + x]`.  Every solid primitive **stores** its colour; only
`blend_pixel` composites.  Span ends are **exclusive**, and a reversed span draws
nothing.

### Native code

`loft_graphics_native` cdylib backs the GL + PNG + font + audio calls via
`glutin` / `gl` / `winit` / `fontdue` / `png` / `image` / `rodio`.

### Targets

The package builds and runs on `--interpret`, `--native` and `--native-wasm`,
and the three agree: the same canvas program answers the same pixel on each.
There is **no `--html` path** — that needs a `[wasm.bridge]` retargeting the
`loft_gl_*` calls onto the browser's WebGL2 runtime, which is designed but
parked (loft-lang/plans @PLN111).  What differs between the three targets is not
which calls exist — every one is present everywhere — but whether a **window**
does.

The software canvas, the mesh and scene maths, `save_png` and the font metrics
need no display, so they compute the same answers on every target; the wasm
build writes a real PNG through WASI.  The `gl_*` and `loft_audio_*` calls need
a display server and a host audio device, which `--native-wasm` has neither of.
There they answer what their signatures already document for "no window":
`gl_create_window` and `gl_poll_events` return `false`, handle-returning calls
return `0`, and setters do nothing.  So a program that checks
`gl_create_window` — as the declaration asks — learns on that target that it
has no window, instead of the build refusing or the page dying at load.

That split is why `winit` and `glutin` are the only dependencies under
`[target.'cfg(not(target_arch = "wasm32"))'.dependencies]`: they are the two
that cannot build for wasm32 at all.  While they were unconditional, the whole
package was off `--native-wasm`, including the canvas and PNG halves that never
wanted a window.  `native/tests/headless_safety.rs` holds both sides of the
contract — the GL surface staying safe with no context, and the dependency list
staying wasm-clean.

## Worked examples

The contracts a signature cannot state are demonstrated by running tests
(@PLN141): [tests/worked-examples.loft](tests/worked-examples.loft) —
`@GFX-001` the alpha byte a hex literal forgets, `@GFX-002` store versus
composite, `@GFX-003` why `get_pixel`'s 0 is not a bounds test, `@GFX-004`
half-open spans that are never normalised, `@GFX-005` how `save_png` picks RGB
or RGBA off the pixels.

They cover the software-canvas half — the half CI can run.  The `gl_*` bindings
need a window and have no CI demonstrator, so they carry no tags rather than
tags pointing at a test that cannot exercise them.

## Provenance

Part of the loft-libs-graphics chunk — loft's
[library extraction plan](https://github.com/loft-lang/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md).
