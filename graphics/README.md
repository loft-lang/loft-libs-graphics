<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# graphics — a 2D canvas, windows, drawing on the GPU and text for loft

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
row-major `vector<u32>`, 4 bytes a pixel: the pixel at (x, y) is `data[y * width + x]`.  A
colour handed to a canvas keeps its low 32 bits.  Every solid
primitive **stores** its colour; only `blend_pixel` composites.  Span ends are **exclusive**,
and a reversed span draws nothing.

### Text

Fonts are read and rasterized in loft: a glyph's coverage is fontdue 0.9.3's, byte for byte
(what graphics 0.9 drew with), so text looks the same on every target.

### Native code and targets

No native part of its own.  The canvas, fonts and text are loft; a window and everything drawn
in it go through [`gpu`](../gpu/README.md), on its OpenGL backend
([`gpu_gl`](../gpu_gl/README.md): OpenGL 3.3 on Linux and macOS, OpenGL ES 3 on Android, WebGL2
in a page).  Which machines open a window, and how each is tested, is that backend's to say.
Without a window — `--native-wasm`, Windows, a machine with no display — `open_window` answers
null, and the canvas, fonts and PNG output answer as everywhere.

A shader is GLSL 3.30 with its uniforms declared one by one, each on a line of its own:
`float`, `vec2`, `vec3`, `vec4`, `mat4`, `int`, `sampler2D`, and arrays of `vec3` and `vec4`.
Anything else (a `mat3`, a struct, a uniform with a value of its own) is refused by name when
the shader is made, and `w.shader(…)` answers null.

**On macOS** a window lives on the main thread, as AppKit requires.  The interpreter and the C
backend run the program there; a `--native` program runs on its own big-stack thread, and its
window work is sent to the main thread, which loft's native `main` keeps in its run loop (a loft
from 2026-10-31 on — an older one is told so, and `open_window` answers null).  The drawable is
in points, as the window's size is: on a Retina screen the frame is scaled up, not drawn at the
screen's pixel density.

**On Android** the window is the screen, whatever size `open_window` asks for: read it with
`width()` and `height()`.  Each finger is a `TouchBegin` / `TouchMove` / `TouchEnd` with its
pointer id, and the first is also mouse button 1, so a program written for the mouse works on a
touch screen.  While the app is in the background the window has nothing to draw on, and `poll`
waits until it is back, where a rotation arrives as a `Resize`; Back ends the app, and `poll`
then answers false.  It needs a loft whose C backend builds Android apps.

**The suite** opens a window wherever one is expected and fails when none opens: `DISPLAY` says
so on X11 (`xvfb-run -a -s '-noreset -screen 0 800x600x24'`), and on a Mac, where nothing in the
environment says whether a window server is reachable, `GRAPHICS_TEST_WINDOW=1` does.

## Moving from 0.13

0.14 is a declared break (`api_compatible_with = "0.14.0"`).  The window draws through `gpu`
and holds its own state in loft — what `enable`, `blend`, `cull`, `viewport` and `scissor`
said, and the frame being drawn — where 0.13 kept it out of sight in C.  So a call that reads or
changes that state has the window in its hand, and nothing that changes is `const`:

```toml
graphics = ">=0.14.0"     # loft.toml; then delete loft.lock and run `loft install`
```

In the table `w` is the `Window`, `s` a `Shader`, `m` a `GpuMesh`, `t` a `Texture`, `p` a
`Painter`.

| 0.13 | 0.14 |
|---|---|
| `fn f(w: const Window, …)` that draws, clears or switches | `fn f(w: Window, …)` |
| `fn f(s: const Shader, …)` that calls `s.set_*` | `fn f(s: Shader, …)`: a shader's uniform values are its own state |
| `m.draw(s)`, `m.draw_mode(s, mode)`, `m.draw_indexed(s, mode)` | `m.draw(w, s)`, `m.draw_mode(w, s, mode)`, `m.draw_indexed(w, s, mode)` |
| `t.bind(0);` `s.set_int("uTex", 0);` `m.draw(s)` | `m.draw(w, s, t)`; a second texture follows it.  A draw is given the textures its shader samples, in the order the shader declares its samplers |
| `m.indices(list)` | `m.indices(w, list)` |
| `m.instance_attrib(records, location, components, stride, offset)` | `m.instance_attrib(location, components, offset)`: the layout of a record, declared once |
| `m.draw_instanced(s, copies)` | `m.draw_instanced(w, s, records, copies)`; a fifth argument is the float the first record starts at |
| `set.draw_all(s)` | `set.draw_all(w, s)` |
| `p.rect(x, …)`, `p.image(t, x, …)`, `p.sprite(sheet, x, …)` | `p.rect(w, x, …)`, `p.image(w, t, x, …)`, `p.sprite(w, sheet, x, …)` |
| `sheet.draw(mvp, index)` | `sheet.draw(w, mvp, index)` |
| `fb = w.framebuffer();` `fb.attach(colour);` `fb.attach(depth)` | `w.framebuffer(colour)`, `w.framebuffer_with_depth(colour, depth)`, `w.depth_framebuffer(depth)` |
| A framebuffer that dies while drawn into: the window is drawn into again | Say so: `w.render_to_window()` |
| `w.capture()` while drawing into a framebuffer | It reads the window and ends the drawing into the framebuffer: `w.render_to(fb)` again to go on |
| `w.point_size(n)` | The vertex shader writes `gl_PointSize` |
| `w.line_width(n)`, `w.finish()` | None |

Unchanged: `open_window`, `poll`, `events`, `key_down`, the mouse, `clear`, `swap`, `enable`,
`disable`, `blend`, `cull`, `depth_write`, `viewport`, `scissor`, `w.shader`, `w.upload`,
`w.quad`, the textures' own calls, `render_to`, `w.painter`, `w.sprites`, and the whole canvas
and text half.

**Why a draw names its window.**  A mesh cannot find its window by itself: a window put into a
record is copied, and anything that named the one it was made from would go on naming that.  So
the state is passed in the open, as `gpu` passes its device.

## Moving from 0.11

0.12 is a declared break (`api_compatible_with = "0.12.0"`) and needs a loft whose `u32` holds
all 32 bits.  `Canvas.data` is a `vector<u32>` and `Glyph.coverage` a `vector<u8>`, so a
canvas takes half the memory and a glyph an eighth.  Reading a pixel is unchanged: it widens to
`integer`.  A program that WRITES `data` directly, or passes it where a `vector<integer>` is
expected, changes:

```loft
d = &cv.data;
d[i] = rgba(r, g, b, a) & 4294967295;   // was: d[i] = rgba(r, g, b, a);
```

The mask says the colour fits in 32 bits; `set_pixel` and every drawing call do it for you.

## Moving from 0.9

Every release from 0.5.0 to 0.9.12 is deprecated: its native part is a Rust crate, which loft
no longer builds.  A program whose `loft.toml` or `loft.lock` holds one of them does not
install, and `loft install` says so.  It moves by asking for the current line and resolving
again:

```toml
graphics = ">=0.14.0"     # loft.toml; then delete loft.lock and run `loft install`
```

That crosses three declared breaks: 0.10 (below), 0.12 and 0.14 (the sections above).  The
tables below give each 0.9 call its 0.13 form; § Moving from 0.13 then takes it to 0.14.

**What changed in 0.10.**  0.9 kept one hidden window and gave out numbers: a shader, a mesh,
a texture and a font were each an `integer`, and `gl_delete_*` freed them.  From 0.10 each is a
value the program holds (`Window`, `Shader`, `GpuMesh`, `Texture`, `Framebuffer`, `Font`,
`Painter`, `Sprites`, `GroupMeshes`), every call is a method on the value it acts on, and a
value is freed when its owner lets it go.  A call that could fail answers null instead of 0 or
false: `open_window(…) ?? return`.

In the tables `w` is the `Window`, `s` a `Shader`, `m` a `GpuMesh`, `t` a `Texture`, `fb` a
`Framebuffer`, `p` a `Painter`, `f` a `Font`, `cv` a `Canvas`.

| 0.9, the window and its input | 0.13 |
|---|---|
| `gl_create_window(width, height, title)` → `boolean` | `open_window(width, height, title)` → `Window?` |
| `gl_create_fullscreen_window(title)` | `open_fullscreen(title)` → `Window?` |
| `gl_destroy_window()` | `w.close()`, or let `w` go |
| `gl_poll_events()` | `w.poll()` |
| `gl_next_event()` and `gl_event_key()`, `gl_event_x()` … | `for ev in w.events() { match ev { KeyDown { key, mods, repeat } => …, _ => {} } }` |
| `gl_key_pressed(key)` | `w.key_down(key)` |
| `gl_mouse_x()`, `gl_mouse_y()`, `gl_mouse_button()` | `w.mouse_x()`, `w.mouse_y()`, `w.mouse_buttons()` |
| `gl_mouse_wheel()` | the `Wheel { delta, x, y }` event |
| `gl_window_width()`, `gl_window_height()` | `w.width()`, `w.height()` |
| `gl_clear(color)`, `gl_swap_buffers()`, `gl_finish()` | `w.clear(color)`, `w.swap()`, `w.finish()` |
| `gl_screenshot(width, height, path)` | `w.capture().save_png(path)`, before `w.swap()` |
| `gl_set_fullscreen(on)` | none: a window is opened windowed or over the whole screen |

| 0.9, the render state | 0.13 |
|---|---|
| `gl_enable(cap)`, `gl_disable(cap)` | `w.enable(cap)`, `w.disable(cap)`; `w.enabled(cap)` reads it |
| `gl_blend_func(src, dst)` | `w.blend(src, dst)` |
| `gl_cull_face(face)`, `gl_depth_mask(write)` | `w.cull(face)`, `w.depth_write(write)` |
| `gl_viewport(x, y, w, h)`, `gl_scissor(x, y, w, h)` | `w.viewport(x, y, width, height)`, `w.scissor(x, y, width, height)` |
| `gl_line_width(width)`, `gl_point_size(size)` | `w.line_width(width)`, `w.point_size(size)` |

| 0.9, shaders and meshes | 0.13 |
|---|---|
| `gl_create_shader(vertex, fragment)` → `integer` | `w.shader(vertex, fragment)` → `Shader?` |
| `gl_use_shader(program)` | none: a draw names its shader, and `set_*` reaches the shader it is called on |
| `gl_set_uniform_float(program, name, v)` and `_int`, `_vec2`, `_vec3`, `_vec4`, `_mat4` | `s.set_float(name, v)`, `s.set_int`, `s.set_vec2`, `s.set_vec3`, `s.set_vec4`, `s.set_mat4` |
| `gl_upload_vertices(data, stride)` → `integer` | `w.upload(data, stride)` → `GpuMesh` |
| `gl_draw(vao, count)` | `m.draw(s)`: every vertex of the mesh |
| `gl_draw_mode(vao, count, mode)` | `m.draw_mode(s, mode)` |
| `gl_upload_indices(vao, list)` + `gl_draw_elements(vao, count, mode)` | `m.indices(list)` + `m.draw_indexed(s, mode)` |
| `gl_update_buffer(vbo, data)` | `m.update(data)` |
| `gl_upload_instance_buffer(data)` | `w.instances(data)` → `InstanceBuffer`; `update(data)` on it |
| `gl_instance_attrib(vao, ivbo, location, components, stride, offset)` | `m.instance_attrib(instances, location, components, stride, offset)` |
| `gl_draw_instanced(vao, count, copies)` | `m.draw_instanced(s, copies)` |
| `gl_draw_fullscreen_quad()` | `q = w.quad();` once, then `q.draw(s)` |
| `gl_delete_shader`, `gl_delete_vao` | none: the value is freed when its owner lets it go |
| `group_vbos_new(stride, mode)` | `group_meshes(stride, mode)` → `GroupMeshes` |
| `group_vbos_upsert(set, key, verts, …)` | `set.upsert(w, key, verts, min_x, min_z, max_x, max_z)` |
| `group_vbos_draw_all(set)`, `group_vbos_destroy(set)` | `set.draw_all(s)`; let `set` go |

| 0.9, textures and framebuffers | 0.13 |
|---|---|
| `gl_upload_canvas(cv.data, cv.width, cv.height)` → `integer` | `w.texture_from(cv)` → `Texture` |
| `gl_load_texture(path)` | `w.load_texture(path)` → `Texture?` |
| `gl_create_color_texture(width, height)`, `gl_create_depth_texture(width, height)` | `w.texture(width, height)`, `w.depth_texture(width, height)` |
| `gl_texture_subimage(tex, x, y, w, h, data)` | `t.put(x, y, cv)` |
| `gl_texture_filter(tex, nearest)`, `gl_generate_mipmap(tex)` | `t.filter(nearest)`, `t.mipmaps()` |
| `gl_bind_texture(tex, unit)` | `t.bind(unit)` |
| `gl_create_framebuffer()` | `w.framebuffer()` → `Framebuffer` |
| `gl_framebuffer_texture(fbo, attachment, tex)` | `fb.attach(t)`: a depth texture becomes the depth buffer, a colour texture the colour |
| `gl_bind_framebuffer(fbo)`, `gl_bind_framebuffer(0)` | `w.render_to(fb)`, `w.render_to_window()` |
| `gl_delete_texture`, `gl_delete_framebuffer` | none: the value is freed when its owner lets it go |

| 0.9, the painter, sprites and text | 0.13 |
|---|---|
| `create_painter_2d(screen_w, screen_h)` → `Painter2D` | `w.painter()` → `Painter?`, sized to the window; `p.resize(width, height)` after a `Resize` |
| `draw_rect_at(painter, x, y, w, h, r, g, b, a)` | `p.rect(x, y, w, h, r, g, b, a)` |
| `draw_texture_at(painter, tex, x, y, w, h)` | `p.image(t, x, y, w, h)` |
| `create_sprite_sheet(atlas, cols, rows, painter_vao(painter))` | `w.sprites(atlas, cols, rows)` → `Sprites?` |
| `draw_sprite_at(sheet, painter, x, y, w, h, index)` | `p.sprite(sheet, x, y, w, h, index)` |
| `draw_sprite(sheet, mvp, index)` | `sheet.draw(mvp, index)` |
| `gl_load_font(path)` → `integer`, 0 when it fails | `load_font(path)` → `Font?` |
| `gl_measure_text(font, content, size)` | `f.measure(content, size)` |
| `gl_text_height(font, size)`, `gl_font_ascent(font, size)` | `f.line_height(size)`, `f.ascent(size)` |
| `create_text_texture(font, content, size, color)` → `integer` | `w.text_texture(f, content, size, color)` → `Texture` |
| `draw_text(cv, font, content, size, x, y, color)` | `cv.draw_text(f, content, size, x, y, color)` |

**What left graphics.**  Sound (`audio_*`, `sfx_*`) is the `audio` package.  The scene renderer
(`Renderer`, `create_renderer`, `upload_scene`, `render_frame`, `render_loop` and its PBR and
shadow shaders) is gone with the `scene` module: meshes and scenes are
[`mesh3d`](https://github.com/loft-lang/loft-libs-assets/tree/main/mesh3d), and a program
draws them with its own shader.

**A path is the program's.**  `load_font` and `load_texture` read a relative path against the
directory of the program's main file (`source_dir()`), as 0.9 did.

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
