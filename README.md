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
[loft's lib_plans/12-library-extraction/](https://github.com/jjstwerff/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md)
§ Chunk grouping.

## Packages

| Subdir | Package | Status |
|---|---|---|
| [`shapes/`](shapes/) | `shapes` — 2D shape drawing + collision detection | v0.1.0 (extracted 2026-05-24) |
| [`gridmesh/`](gridmesh/) | `gridmesh` — chunk-local mesh generation primitives | v0.1.0 (extracted 2026-05-24) |
| `graphics/` | `graphics` — drawing primitives, OpenGL bindings | TODO (blocked on `Type::Reference` codegen forwarding for store-aware GL functions) |
| `imaging/` | `imaging` — PNG load/save, image manipulation | TODO (blocked on same codegen feature as graphics) |

## Installing a package

```sh
loft install shapes        # 2D shapes + collision
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
| shapes 0.1.0 | `shapes-v0.1.0` |
| gridmesh 0.1.0 | `gridmesh-v0.1.0` |

## License

LGPL-3.0-or-later — see [LICENSE](LICENSE).
