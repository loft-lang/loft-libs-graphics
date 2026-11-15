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
| [`gpu/`](gpu/) | `gpu` — drawing on a GPU through pipelines, passes and uniform blocks, with no graphics stack named | v0.1.0 |
| [`gpu_gl/`](gpu_gl/) | `gpu_gl` — the OpenGL backend of `gpu`: OpenGL 3.3, OpenGL ES 3 and WebGL2 | v0.1.0 |
| [`gpu_paint/`](gpu_paint/) | `gpu_paint` — rectangles, images and sprites at pixel positions, drawn through `gpu` | v0.1.0 |
| [`scene3d/`](scene3d/) | `scene3d` — a 3D scene a game's view gives once (tiles of faces, the cells' light, actors), drawn through `gpu` | v0.1.0 |
| [`gpu_record/`](gpu_record/) | `gpu_record` — a `gpu` backend that draws nothing and writes down every call, for tests | v0.1.0 |
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

## License

LGPL-3.0-or-later — see [LICENSE](LICENSE).
