<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gridmesh — chunk-local mesh-generation primitives for loft

Pure-loft library.  Reusable building blocks for chunk-local,
bounded-extent grid→mesh generation: spatial-index neighbour
queries, neighbour gather, bounded mesh accumulation keyed by
owning cell, dirty-region / per-chunk rebuild.

Used by world-building algorithms (wall placement, edge
rounding, surface generation) that run as local routines over
a limited set of world chunks and produce meshes that don't
extend much outside their chunk.

## Install

```sh
loft install gridmesh
```

## API surface

See [src/gridmesh.loft](src/gridmesh.loft) for the full reference.

- **The grid** — `build_index(xs, ys)` the coordinate → cell-index lookup;
  `idx_at` (-1 for an empty coordinate), `nbr_count_idx`; `step_x` / `step_y`
  walk a hex axis and are always used as a pair.  Coordinates are hex offset
  coordinates, every odd row shifted half a cell.
- **The field** — `field_new(0, xs, ys, chunk_shift, halo_k)` cuts the cells into
  chunks of `1 << chunk_shift` a side.  `all_inputs` is the full-build work-list;
  each `ChunkInput` carries the cells it OWNS (emit for these) and the halo cells
  across its border (read, never emit).
- **Edits** — `field_add_cell`, `field_mark_dirty`, `field_remove_cell` mark the
  chunks an edit made stale; `collect_dirty_inputs` is the rebuild work-list, and
  `clear_dirty` comes after the rebuild.  `all_groups` / `collect_dirty_groups`
  do the same for render groups of several chunks.
- **The mesh** — `seg_mesh_new`, `emit_segment` (a segment owned by a cell),
  `seg_mesh_append`, `seg_len`.

A guide: [docs/01-getting-started.loft](docs/01-getting-started.loft).  The
contracts a signature cannot state are running tests in
[tests/worked-examples.loft](tests/worked-examples.loft) — `@GRM-001` the edit →
collect → rebuild → clear cycle, `@GRM-002` owned versus halo cells, `@GRM-003`
cell indices stay stable across a removal, `@GRM-004` a dirty group lists its
clean chunks too, `@GRM-005` walking the grid.

## Provenance

Part of the loft-libs-graphics chunk — loft's
[library extraction plan](https://github.com/loft-lang/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md).
