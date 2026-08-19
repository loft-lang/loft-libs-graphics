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
- `draw_list() -> vector<DrawRect>` · `render(list, canvas)` · `opaque(colour)`
- `render_stage(canvas)` — the stage's own path, honouring per-node alpha
- `pick(x, y)` · `hits(idx, x, y)` · `to_local(idx, x, y)` · `press` / `release` /
  `released_on` / `capture` · `add_mask(w, h, alpha)`
- `batches() -> vector<Batch>` · `pack_instances() -> vector<single>` ·
  `INSTANCE_STRIDE` / `OFF_AFFINE` / `OFF_UV` / `OFF_RGBA`

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

## The draw list

`UiRect` / `DrawRect` / `DrawText` copy their field names and types **exactly** from
`lavition_ui`, which already produces `vector<DrawRect>` from `panel_draw_list`. Matching a
proven command shape rather than minting a rival one is deliberate: a game's UI and an
editor's UI have to reach the GPU by one path, or the batcher sees two.

⚠ `DrawRect` carries **0xRRGGBB** while `graphics::Canvas` reads **0xAARRGGBB**, where a
missing top byte means *fully transparent* — the `@GFX-001` trap. `render` adds the alpha at
the boundary, which is why a 0xRRGGBB colour paints instead of vanishing.

## Batching

`batches()` splits the scene into runs of **consecutive** nodes sharing an atlas, and
`pack_instances()` packs every visible node into one float buffer at `INSTANCE_STRIDE`
(a 2×3 affine, a uv rect, an rgba). A batch's `b_first`/`b_count` index that buffer directly.

⚠ **It merges adjacent runs and never sorts.** Gathering every command of one atlas together
would be faster and would draw the wrong picture, because overlapping translucent draws must
composite in call order. The cost is that interleaving two atlases sprite-by-sprite gives one
instance per run — a **packing** problem, cured upstream by putting sprites that draw near
each other on one page.

## Picking

`pick` walks the draw order **backwards**, so the node drawn last is tested first and
insertion order breaks a tie exactly as it does when drawing — one order, two consumers.
`to_local` inverts the world affine in closed form, so rotation and scale are handled without
a general matrix inversion.

⚠ **Picking samples ALPHA.** Give a node a mask and its shape becomes its art rather than its
rectangle: a click over a transparent texel reaches whatever is behind it. You can see through
the tree, so you can click through it. A node with no mask is solid.

⚠ **A release belongs to the press.** `release` answers the node that was pressed wherever the
pointer has drifted to; `released_on` is the separate question a button asks. Without that
split a button fires when you press it and slide away, and fails to fire when you press it and
drift a pixel.

## Compositing

**Premultiplied throughout.** `pack_instances` multiplies rgb by alpha and `gl_render` blends
with `(ONE, ONE_MINUS_SRC_ALPHA)`; `render_stage` does the same arithmetic on a software
canvas. Straight alpha darkens every anti-aliased edge under linear filtering, and this stack
is made of overlapping soft-edged sprites.

`render(list, canvas)` stays the path for a `lavition_ui` command list, which carries
0xRRGGBB and no alpha by design. The two agree wherever alpha is 1, and there is a test that
says so — two render paths drifting into two different pictures is the failure worth guarding.

## Clipping

A node with `pl_clips` bounds its whole subtree. Clips **inherit and intersect**, so nesting
two gives their overlap and never the inner one alone — which is what makes a panel inside a
scrolling list composable. The idiom for a clip region is sized plus `pl_visible: false`: it
needs a rectangle to define the bound and no paint of its own.

⚠ The bound is **axis-aligned**. A rotated clipper clips to its bounding box, because a
hardware scissor is axis-aligned and there is no honest way to pretend otherwise.

⚠ **A clip change breaks a batch.** A run is `(atlas, clip)`, not atlas alone — a scissor is
state exactly as a texture is, and grouping by atlas would draw the second clip's content
under the first's scissor.

## Worked examples

`@STG-001` the origin is the anchor · `@STG-002` a parent must already exist ·
`@STG-003` `compose` is the caller's to run ·
`@STG-004` the batcher never reorders ·
`@STG-005` a release belongs to the press · `@STG-006` a click falls through a hole ·
`@STG-007` a clip inherits and intersects — [tests/worked-examples.loft](tests/worked-examples.loft).

## Provenance

[@PLN144](https://github.com/loft-lang/plans/issues/144) arc A — the 2-D stage.
