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
- `add_sequence(first, count, fps, mode) -> integer` · `advance(dt_us)` ·
  `frame_of(idx)` · `restart(idx)` · `LOOP` / `ONCE` / `PINGPONG`
- `add_light(x, y, radius, colour, power)` / `move_light` / `light_count()` ·
  `set_ambient(level)` / `ambient()` · `lit_colour(idx)` · `light_reach(light, x, y)`
- `add_view(x, y, w, h)` / `view_count()` / `set_view_rect` / `set_view_camera` ·
  `view_camera_x` / `view_camera_y` / `view_offset` · `render_view(view, canvas)` ·
  `pick_in(view, x, y)` / `hits_in` / `in_view_rect` · `sync_instances()` · `scissor_of`
- `set_projection(mode)` / `projection()` · `TOP_DOWN` / `SIDE_ON` ·
  `add_named_sequence(name, …)` · `face(idx, angle)` · `set_action(idx, action)` ·
  `facing_of` / `action_of` / `rotation_of` / `mirrored` · `facing_name(angle)`
- `batches() -> vector<Batch>` · `pack_instances() -> vector<single>` ·
  `INSTANCE_STRIDE` / `OFF_AFFINE` / `OFF_CELL` / `OFF_RGBA` / `OFF_SWAY`

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

## Depth, layers and the 2.5-D cue

A node carries a **`layer`** (an ordering band) and a **`depth`** (distance *into* the screen —
**larger draws first**, so `depth = -y` puts a lower sprite in front). Layer outranks depth
entirely, so background / world / UI are bands rather than one number every node must get
right. Ties break by insertion order and the sort is **stable**, so two sprites on one
footprint never swap between frames.

`depth_cue(near, far, far_scale, far_haze, haze_colour)` makes distance read as **smaller and
less distinct** — and it scales about the **origin**, so a mob's feet stay planted on its tile
while its body shrinks. Off by default, and off is an identical picture.

⚠ **The sprite occupies local `[0,w] × [0,h]`, and the origin is a point inside it** — not the
box's corner. Put it at `(w/2, h)` and the sprite stands up from its feet, centred on where it
was placed.

## Ambient motion

`pl_sway` gives a node an amplitude; `set_time(t)` ticks one clock. The **phase is derived
from position**, so neighbours are out of step and a field looks like a field rather than like
the ground moving — and **nothing is stored or stepped**, so five hundred swaying tufts are a
buffer uploaded once plus a uniform. There is a test that 100 ticks over 500 sprites change
**not one float**.

⚠ **Sway is visual only.** It displaces what is drawn and never the footprint, so a swaying
tree keeps its depth, its bounds and its hit area — one that re-sorted as it swayed would
flicker past its neighbours.

## Animation

A sequence is a run of atlas cells played at a rate — `add_sequence(first, count, fps,
mode)` — and a node points at one through `pl_seq`.  `advance(dt_us)` steps every animated
node; `frame_of(idx)` says which cell it shows; `restart(idx)` puts it back to the
beginning, which is what an idle → walk change needs.

`LOOP` wraps.  `ONCE` stops on the last frame and stays there rather than running off the
end of the atlas.  `PINGPONG` reverses at both ends with a period of **2n−2**, not 2n — the
two end frames are each shown once per cycle, so four frames run `0 1 2 3 2 1`.  A period
of 2n shows each end twice and reads as a limp; it is the classic off-by-one and it has its
own test.

⚠ **`advance` takes the simulation's own delta, in integer MICROSECONDS.**  It reads no
clock, so a cycle is identical at any frame rate and replayable under a recorded input
stream — the property co-op determinism rests on.  The units are integer because a game
reaches 0.8 s by adding 0.1 s eight times, and in floating-point seconds that sum is
`0.7999999999999999`, which truncates to frame 7 where 8 is right: an eight-frame walk
cycle hitches once per stride.  Summing integer microseconds cannot do that, and there is a
test that ticks exactly that case.

⚠ **The instance attribute is a FRAME INDEX, not a uv rect.**  An animating sprite dirties
**one** float per frame instead of four, which is what keeps *upload only what changed*
meaningful with a screen full of walking mobs — the packed grid (`cols`, `rows`) is static
and uploads once.  Deriving the uv from that grid is the shader's job; today the GL path
draws untextured quads, so the attribute is packed and bound but not yet read.

## Facings — the projection picks the model

An author writes one call — `face(node, angle)`, *this thing points that way* — and
**the stage decides what that costs**.  That is the whole point: the same mob code serves
a top-down view and a side-on one.

**`TOP_DOWN`** (the default) turns one sprite continuously off the 2×3 affine that already
exists.  A sprite authored in a locked orientation — crawler's *front = up* — is what makes
that legal, and it means **15° steps cost no atlas entries**: 24 facings are one cell turned
24 ways, not 24 cells.  Pre-rotated frames never exist.

**`SIDE_ON`** cannot turn a standing sprite into another facing — turn it by π and it lies
on its head — so the facing picks a sequence from an `(action, facing)` table and **mirrors
at most**.  Mirroring is a negated x scale, and because the origin is the anchor it leaves
the footprint and the origin exactly where they were; there is a test comparing both against
the unmirrored bounds.  North and south have no mirror partner — a front view flipped is
still a front view — so they fall back instead of borrowing.

⚠ **A facing change mid-walk keeps the frame phase.**  Switching sequences carries the
elapsed over, or a mob turning a corner snaps its legs back to frame 0 and stutters at
exactly the moment a player is watching it.  `restart` stays the one way to go back.

The compass is four facings in screen space, y down: angle 0 is east, `+π/2` is south.  The
edges are **half-open** like the picking rects — 45° belongs to south — so no angle is
claimed by two facings.

### Resolution is by name, and it falls back rather than failing

`add_named_sequence` registers under a name; a node carries `pl_key`.  Resolution walks
**most specific first**:

`{key}_{action}_{facing}` → the opposite facing, **mirrored** → `{key}_{action}` →
`{key}_{facing}` → `{key}` → *keep what it had*.

A missing sprite leaves the mob drawn with what it already has — the rule crawler's
`<key>.png` loader keeps — so a half-authored mob is a visibly wrong sprite rather than a
blank or a stopped frame.  Names resolve when a facing or an action **changes**, never per
frame.

The projection belongs at setup.  It may move later — a mob that faces each frame lands in
the new model by itself — but nothing re-derives on the switch.

## The camera

`set_camera(x, y)` and `set_parallax(layer, factor)`. **Flat scrolling is every factor at
1.0** — the two scroll modes are one mechanism, and the flat one is proved pixel-identical to
having placed every node that much further left, not maintained as a second path.

⚠ **Applied at draw time, never baked into node positions.** A pan is one uniform pair per
run; baking it would rewrite every node and re-upload the whole instance buffer on every
scrolled frame — the retained tree's entire budget spent on standing still. There is a test
that 100 camera moves change **not one float** of the packed buffer.

⚠ **Parallax translates; it does not resize.** Distance-as-smaller is `depth_cue`'s job,
which scales about the origin so a sprite's feet stay planted. And picking **un-applies the
camera per layer** — one screen point is a different world point in each.

## Light

`add_light(x, y, radius, colour, power)` puts a point light in the world; every sprite
samples every light **at its own origin** and the answer folds into A5's tint attribute.
So lighting costs a multiply per sprite — **no pass, no framebuffer, and no change to draw
order** — which is the whole feature for a flat-lit 2-D game.

⚠ **Ambient is 1.0 by default**, so a game that never asks for light cannot be dimmed by
this existing. `set_ambient(0.2)` is how a night scene starts, before a torch goes in it.

⚠ **The sample point is the origin — the footprint, not the artwork.** Two mobs standing on
one tile take the same light however tall they are. Sampling a corner would light a tall
sprite as though it stood further away, which is the 2.5-D wrong picture wearing a different
hat.

⚠ **Light composes with the material, it does not replace it.** A red sprite under a blue
light goes nearly black, not blue — the light multiplies A5's tint rather than throwing it
away. Channels are clamped, so standing between two torches gives you the material rather
than a wrapped byte that comes out dark.

The falloff is **linear in the radius**, `1 - d/r`, clamped to nothing beyond it. That is a
deliberate choice: this phase's gate is hand-computed values, and a curve nobody can
hand-compute is a gate nobody maintains. A light out of range contributes nothing and never
*subtracts* — `light_reach` owns that bound and is tested on it directly, because
`lit_colour` skips a zero contribution as work avoided rather than as a guard.

⚠ **Unlike the camera, a light dirties the instance buffer.** It changes what is *in* the
data rather than how it is looked at, so a moving torch re-packs every frame. Taking that
cost off the per-sprite path is exactly what a light-map pass is for.

## Several views over one stage

A **view** is a camera and the screen rect it paints into.  A fresh stage already has one —
`set_camera` *is* view 0's camera — so an unsplit game is the degenerate case rather than a
second code path, exactly as parallax 1.0 is for scrolling.

```
v = st.add_view(400, 0, 400, 600);   // right half of the screen
st.set_view_camera(v, player2_x, player2_y);
st.render_view(v, canvas);           // or gl_render, which draws every view
```

⚠ **A view is presentation, never world.**  The scene cannot tell how many are looking at
it: adding views adds no nodes and moves nothing.  That is the split this exists to enforce —
the world is deterministic and shared, the presentation is local and free, so window size,
camera and ambient motion may differ per viewer and *must* be allowed to.

⚠ **Packed once, drawn N times.**  A second view costs draw calls, never another instance
upload — P2's *the camera is a uniform* carried to N cameras.  `sync_instances()` is the one
answer to *when does the buffer get rewritten*, so a count of uploads and the GL path cannot
disagree about it.

Every view shares one screen-space projection: a view is placed by **offsetting into its rect
and scissoring to it**, not by remapping the clip volume.  The software and GL paths then stay
directly comparable — the parity rig A2 and A3b rest on — and a sprite is the same size in
every view rather than quietly zoomed by a viewport of a different shape.

Picking follows the view the point is in: `pick_in(view, x, y)` un-applies **that** view's
camera, per layer, and a point outside the view's own rect is not that view's to answer
whatever its camera would map it to.  `pick` remains view 0's pick.

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
`@STG-007` a clip inherits and intersects ·
`@STG-008` ambient motion is free and visual only — [tests/worked-examples.loft](tests/worked-examples.loft).

## Provenance

[@PLN144](https://github.com/loft-lang/plans/issues/144) arc A — the 2-D stage.
