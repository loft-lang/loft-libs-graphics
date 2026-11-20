<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# loft-libs-graphics — the graphics libraries of loft

Each directory here is a loft package of its own, published to the registry under its name
and versioned on its own.  A program installs the ones it uses (`loft install <name>`) and
never sees this repository.

## Which library for which need

| You want to | Use | It rests on |
|---|---|---|
| Paint pixels into an image in memory, load a font, draw text, save a PNG — no window, no GPU | `graphics` (its canvas), `imaging`, `text2d`, `drawing` | nothing of the machine |
| Build a 2D game scene: nodes you move, batches, clipping, light | `stage` | `graphics` |
| Open a window and draw with your own shaders and meshes, uniforms set by name | `graphics` | `gpu` |
| Draw rectangles, images and sprites at pixel positions on any GPU | `gpu_paint` | `gpu` |
| Draw a 3D scene of tiles, light and actors on any GPU | `scene3d` | `gpu` |
| Write a renderer of your own that runs on every GPU | `gpu`, opened through `gpu_host` | a backend |
| Test what a program asks of the GPU, with no display | `gpu_record` | — |

`gpu` is the one layer that touches the machine, and it names no graphics stack: a backend
(`gpu_gl`, `gpu_vulkan`, `gpu_metal`) is a library of its own, and `gpu_host` opens a device
on the one this machine has.  Everything above `gpu` is loft, and runs wherever a backend
does.  A window of `graphics` is OpenGL unless the program carries its shaders for another
backend (`graphics/README.md` § Other backends).

## Packages

<!-- packages:begin -->
| Package | Version | What it is |
|---|---|---|
| **Drawing on a GPU** | | |
| [`gpu`](gpu/) | 0.4.3 | Drawing on a GPU through pipelines, passes and uniform blocks, with no graphics stack named: a backend library beneath it draws |
| [`gpu_gl`](gpu_gl/) | 0.3.5 | The OpenGL backend of gpu: OpenGL 3.3 on Linux and macOS, OpenGL ES 3 on Android, WebGL2 in a page |
| [`gpu_vulkan`](gpu_vulkan/) | 0.2.3 | The Vulkan backend of gpu: Vulkan 1.3 on Linux, in an X11 window |
| [`gpu_metal`](gpu_metal/) | 0.2.3 | The Metal backend of gpu: a Mac's GPU through Metal, in a window of its own |
| [`gpu_record`](gpu_record/) | 0.3.1 | A GPU backend that draws nothing and writes down every call it receives: for testing a program's drawing on a machine with no display |
| [`gpu_host`](gpu_host/) | 0.1.1 | Opens a gpu device on the backend this machine has: Metal on a Mac, OpenGL elsewhere and where Metal is absent, Vulkan on Linux by name |
| **Built on `gpu`** | | |
| [`gpu_paint`](gpu_paint/) | 0.2.1 | Rectangles, images and sprites at pixel positions, drawn through gpu: a painter and sprite sheets for any backend |
| [`scene3d`](scene3d/) | 0.6.2 | A 3D scene a game's view gives once — tiles of faces, the cells' light, actors — drawn through gpu |
| [`graphics`](graphics/) | 0.14.3 | A 2D canvas, windows, drawing on the GPU and text for loft (meshes and glTF are mesh3d and glb) |
| [`stage`](stage/) | 0.21.2 | A retained 2-D scene for loft: a node tree you mutate, with transforms, clipping, batching, pointer routing and widget states |
| **Pixels, images and text** | | |
| [`imaging`](imaging/) | 0.5.6 | PNG load/save and pixel manipulation for loft: one C source on every target, no Rust crate |
| [`drawing`](drawing/) | 0.5.3 | Render a .draw scene — a sprite authored as lines of text — pixel-identically to the Python renderer it replaces |
| [`text2d`](text2d/) | 0.4.9 | Text that always draws — a built-in face, a glyph atlas, measured metrics, wrapping and alignment, with no font file and no GL context |
| [`tiles`](tiles/) | 0.7.2 | Tile sheets, map layers and sprites drawn at a surface's own resolution through a camera |
| **Geometry and motion** | | |
| [`shapes`](shapes/) | 0.5.5 | 2D collision detection |
| [`gridmesh`](gridmesh/) | 0.2.8 | Chunk-local mesh generation primitives |
| [`tween`](tween/) | 0.1.3 | Property tweens in exact integer time — eleven easing curves, chains and groups |
<!-- packages:end -->

The table is written from each package's `loft.toml` by `./packages.py`; `./packages.py
--check` says when it is behind.

A release is tagged `<package>-v<version>` (`graphics-v0.14.1`), since the packages share
this repository.

## Where the drawing is tested

A test that draws needs a display, and returns at once where it finds none.  A package says
where its tests draw with a marker file at its root, holding the reason:

| Marker | What the library CI gives the package |
|---|---|
| `.x11_window` | An X server that draws in memory (Xvfb) and Mesa's software OpenGL and Vulkan, on the Linux runner |
| `.macos_window` | A second run on a macOS runner with windows on: a paravirtual Metal and Apple's software OpenGL |

On both, `GRAPHICS_TEST_MUST_DRAW=1` makes a test that finds no display fail; each test file's
`display()` asks it.  No runner has a GPU of its own, so a comparison that is exact on a GPU
allows a software renderer its rounding there (`gpu_gl::renderer()` says which one draws).

The exact comparison is run by hand, on a Mac with a GPU, before a release of a `gpu` package:
[`gpu_on_a_mac.sh`](gpu_on_a_mac.sh) runs their tests on the three backends, `gpu_metal`'s
under Metal's validation, and its two devices on two threads under the thread sanitizer.

## License

LGPL-3.0-or-later — see [LICENSE](LICENSE).
