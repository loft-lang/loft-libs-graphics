<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# loft-libs-graphics — graphics + geometry libraries for loft

Multi-package chunk repo for the graphics stack: drawing
primitives, image I/O, 2D geometry, mesh generation.  Each
subdirectory is an independent loft package published to the
registry under its own name.

Per the chunked-repo design in
[loft's lib_plans/12-library-extraction/](https://github.com/loft-lang/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md)
§ Chunk grouping.

## Packages

All four are published to the registry and installable today.

| Subdir | Package | Latest |
|---|---|---|
| [`graphics/`](graphics/) | `graphics` — drawing primitives, 2D canvas + 3D scene rendering, OpenGL/WebGL bindings | v0.5.5 |
| [`imaging/`](imaging/) | `imaging` — PNG load/save, image manipulation | v0.2.2 |
| [`gpu/`](gpu/) | `gpu` — drawing on a GPU through pipelines, passes and uniform blocks, with no graphics stack named | v0.4.0 |
| [`gpu_gl/`](gpu_gl/) | `gpu_gl` — the OpenGL backend of `gpu`: OpenGL 3.3, OpenGL ES 3 and WebGL2 | v0.3.1 |
| [`gpu_metal/`](gpu_metal/) | `gpu_metal` — the Metal backend of `gpu`: a Mac's GPU, in a window of its own | v0.2.0 |
| [`gpu_host/`](gpu_host/) | `gpu_host` — opens a `gpu` device on the backend this machine has: the one library a game names | v0.1.0 |
| [`gpu_paint/`](gpu_paint/) | `gpu_paint` — rectangles, images and sprites at pixel positions, drawn through `gpu` | v0.1.0 |
| [`scene3d/`](scene3d/) | `scene3d` — a 3D scene a game's view gives once (tiles of faces, the cells' light, actors), drawn through `gpu` | v0.1.0 |
| [`gpu_record/`](gpu_record/) | `gpu_record` — a `gpu` backend that draws nothing and writes down every call, for tests | v0.3.0 |
| [`tiles/`](tiles/) | `tiles` — tile sheets, map layers and sprites drawn at a surface's own resolution through a camera | v0.1.0 |
| [`shapes/`](shapes/) | `shapes` — 2D collision detection | v0.4.1 |
| [`gridmesh/`](gridmesh/) | `gridmesh` — chunk-local mesh generation primitives | v0.2.1 |

## Installing a package

```sh
loft install graphics      # 2D canvas + 3D scene rendering
loft install imaging       # PNG load/save + pixel operations
loft install shapes        # 2D collision detection
loft install gridmesh      # mesh generation primitives
```

Consumers never see the chunk structure — they install
per-package.

## Versioning + tags

Each package versions independently.  Git tags use the
**`<package>-v<version>`** convention to disambiguate sibling
packages in this multi-package repo (same as
`loft-libs-core`):

| Package + version | Git tag |
|---|---|
| graphics 0.5.5 | `graphics-v0.5.5` |
| imaging 0.2.2 | `imaging-v0.2.2` |
| shapes 0.4.1 | `shapes-v0.4.1` |
| gridmesh 0.2.1 | `gridmesh-v0.2.1` |

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
