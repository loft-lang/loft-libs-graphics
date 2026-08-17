<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# shapes — 2D collision detection for loft

Pure-loft library.  Defines `Rect`, `Circle`, and the overlap tests
and penetration depths built on them.  No drawing — the `Canvas`
wrappers that once lived here are in the `graphics` library.

## Install

```sh
loft install shapes
```

## API surface

See [src/shapes.loft](src/shapes.loft).  Major primitives:

- `Rect`, `Circle`, `Overlap` struct types.
- `rects_overlap(a, b) -> boolean`, `circles_overlap(a, b) -> boolean`,
  `rect_circle_overlap(rect, circle) -> boolean` — all STRICT, so shapes
  that merely touch do not overlap.
- `rect_overlap_depth(a, b) -> Overlap` — unsigned penetration on each axis.
- `aabb_overlap` / `aabb_depth_x` / `aabb_depth_y` — the same answers from raw
  coordinates, allocating nothing (safe in a hot loop).

## Worked examples

The contracts a signature cannot state are demonstrated by running tests
(@PLN141): [tests/worked-examples.loft](tests/worked-examples.loft) —
`@SHP-001` the strict touching-is-not-overlapping boundary, `@SHP-002` what the
depth numbers do and do not say, `@SHP-003` why a circle is not its bounding box.

## Provenance

Extracted from the loft monorepo's `lib/shapes/` 2026-05-24
as part of [@PLAN12](https://github.com/jjstwerff/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md)
Phase 5 (loft-libs-graphics chunk).
