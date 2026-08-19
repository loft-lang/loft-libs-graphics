<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# stage — a retained 2-D scene for loft

A game is a **tree you mutate**, not a frame loop you draw.

```sh
loft install stage
```

## Surface

- `Stage` / `stage_new()` / `node_count()`
- `Place` — where a node goes and how, with **declared defaults** (scale is `1.0`,
  not the zero an omitted field would take)
- `node_add(parent, place) -> integer` — the index, or `-1` if the parent does not
  already exist
- `compose()` — derive every world transform in one forward pass
- `world_point(idx, lx, ly)` / `world_origin(idx)` — where a local point landed

## The two things to know

**A node is anchored by its ORIGIN.** `pl_ox`/`pl_oy` name a point in the node's own
units, and `compose` puts exactly that point at `(pl_x, pl_y)` under any rotation and
scale. Put it at a sprite's feet and the sprite stands up from where it was placed —
the footprint decides where it sits, the artwork decides nothing.

**The tree is a flat array, and a parent must have a lower index than its children.**
A child names its parent by index, never by reference: a parent holding children while
a child points back is a dependency cycle in loft's ownership model, and the flat form
is also the instance buffer a batched renderer wants. `node_add` refuses a forward
reference rather than trusting the caller, because `compose` is a single forward pass
and would otherwise read an unwritten parent and answer *plausibly*.

## Worked examples

`@STG-001` the origin is the anchor · `@STG-002` a parent must already exist ·
`@STG-003` `compose` is the caller's to run — [tests/worked-examples.loft](tests/worked-examples.loft).

## Provenance

[@PLN144](https://github.com/loft-lang/plans/issues/144) arc A — the 2-D stage.
