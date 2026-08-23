<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# drawing — a sprite you can write down

```sh
loft install drawing
```

```
size 256x256
Background transparent
name blade
Poly (0.48,0.10) (0.52,0.10) (0.52,0.70) (0.48,0.70) rgb=190,196,206
Circle (0.50,0.78) r=0.06 rgb=120,90,50
Poly (0.30,0.72)@7 (0.70,0.72)@7 stroke=86,62,38
```

```loft
use drawing;

sk = render_file("sword.draw", "sword.png");
for u in sk.unparsed { println("sword.draw {u}") }
```

## Why it exists

A sprite made of text diffs, reviews, and can be edited by something that cannot see. That
is not a new idea — a corpus of `.draw` scenes already exists, and the art in it was drawn
by a Python renderer. This is that renderer, in loft, so a `.draw` scene is content the
language can build rather than a build step that needs Python beside it.

**The whole package is measured by one thing: it draws the picture the Python renderer
draws, pixel for pixel.** Not approximately. Every sprite in that corpus already looks the
way it looks; "close enough" means all of them quietly change the first time they are
re-rendered, and nobody would be able to say which change was intended. So the gate is a
byte diff against the original renderer over the whole corpus, and it is green: **28 of 28
scenes, 0 pixels different**.

Three things fall out of that, and each of them cost a probe to learn.

**The rasteriser is Pillow's, and it could not be `graphics`'.** `graphics::fill_polygon`
is a good polygon filler that obeys a different rule — it interpolates crossings in integer
arithmetic and fills an inclusive span; Pillow fills the pixels whose CENTRES are inside,
in 32-bit float. Measured over 400 random polygons, the two agree on **4**, and on
`graphics`' own reference triangle they differ by 35 pixels. Neither is wrong. But a `.draw`
scene is *defined* by what the oracle renders, so this package carries its own filler.

**The 32-bit float is load-bearing.** Pillow keeps an edge's slope and the scanline
crossings in C `float`, and two exact-equality tests read that rounding — "is this crossing
an integer?" and "do these two edges cross at the same x?". Widen them to `float` (64-bit)
and 12 of 500 random polygons rasterise differently. Every one of those values is a `single`
here.

**The picture is drawn at 3x and resampled with Lanczos.** The supersample factor and the
filter are part of the contract a scene signs, not an implementation choice: a scene drawn
once at final size is a different picture whatever else is right.
`graphics::resize_lanczos` is byte-identical to `Image.resize(…, LANCZOS)`, which is why
this package can reach the oracle's answer at all.

## The grammar

Coordinates are FRACTIONS of the paper — origin top-left, y down — so a scene is
resolution-independent and `size` is the only place a pixel count appears.

| | |
|---|---|
| `size WxH` | the paper |
| `Background transparent` · `Background topc=R,G,B botc=R,G,B` · `Background top=L bottom=L` | transparent (the sprite case), a colour ramp, or a grey one |
| `name <element>` | tag the marks that follow, so they can be measured |
| `Line (x,y)[@w] - (x,y)[@w] [w=N]` | one segment |
| `Circle (cx,cy) r=R [n=N] [flat=F] [<fill>]` | a round mark, `n` segments (28), squashed by `flat` |
| `Poly (x,y)[~][@w] … [w=N] [stroke=R,G,B] [<fill>]` | the workhorse: filled if it names a fill, a pen stroke if it does not |
| `landmark <name> = <value>` · `check …` | read and carried; the report channel itself is not in this release |
| `# …` | a comment, and a searchable note |

`<fill>` is `rgb=R,G,B` or `fill=L` (grey, 0..1). A point may carry `~` (it curves — the
tangent is half the neighbour chord, so a segment between two corners stays exactly
straight) and `@N` (the pen width AT that point, which makes a stroke taper).

## What this release does not draw

`grad=` and `radial=` fills, and the `Petals` / `Fronds` array marks. They **parse** — so
they cannot be misread as something else — and every one of them is listed in
`Sketch.deferred` with the line it came from. A caller therefore knows the picture is short
of a mark instead of finding out by eye.

A line that no command accepts at all is different, and lands in `Sketch.unparsed`: a
typo'd mark has to read as a syntax problem, not as a geometry one.

## Surface

- `parse_scene(src) -> Sketch` · `render(sk) -> graphics::Canvas` ·
  `render_file(src_path, out_png) -> Sketch`
- `Sketch.` `sw` `sh` `transparent` `ops` `elems` `landmarks` `checks` `unparsed` `deferred`
- `Op.` `kind` (`Sky` / `Fill` / `Stroke`) `pts` `paint` `widths` `w` `color` `color2` —
  ⚠ `pts` are paper FRACTIONS, never pixels
- `Paint.` `pk` (`Stroked` / `Solid` / `Linear` / `Radial`) `c1` `c2` `spec`
- `Elem.` `ename` `seen` `bx0` `by0` `bx1` `by1` — `seen` is false for an element that was
  named and never drawn, which is an absence rather than a box at the origin
- `circle_pts` · `smooth_pts` · `smooth_vals` · `smooth_applies` · `read_points` · `grey`
- `raster::` `fill_poly` · `wide_line` · `thin_line` · `round_up` · `round_down` · `pt` ·
  `Pt` — the Pillow-compatible rasteriser, public because anything that wants to agree with
  the same oracle needs it. Reach it with a **second `use`, in this order**:

  ```loft
  use drawing;
  use raster;          // ⚠ AFTER `use drawing;` — see below
  ```

  and qualify as `raster::fill_poly`. `raster` is a sibling module of this package rather
  than a package of its own, so nothing resolves it until `drawing`'s entry file has been
  loaded and pulled it in — put it first and you get *"Library 'raster' not found"*. The
  two-level `drawing::raster::…` spelling does not parse at all.

⚠ The parsed scene is a `Sketch`, not a `Scene`: `mesh3d::Scene` owns that name and
`graphics` depends on mesh3d, so the collision is a hard error — the same trap
`graphics::Coord` hit with `Point`.

## Targets

| target | state |
|---|---|
| interpreter | ✓ suite green, and the 28-scene corpus gate |
| `--native` | ✓ suite green, and the 28-scene corpus gate |
| `--native-wasm` (headless WASI) | ✗ blocked by a dependency — see below |
| `--html` (browser) | compiles; the render path is pure loft, but see below |

This package is pure loft — no `#native`, no inline `#rust` — so its own code has nothing
target-specific in it. Both limits come from `graphics`, which it draws through.

`--native-wasm` does not build: `graphics`' native crate links glutin/GL, which is not
wasm-clean, so the cross-build fails before anything of this package is reached (`imaging`
is blocked on the same target for a different upstream reason,
[loft#967](https://github.com/loft-lang/loft/issues/967)).

`--html` emits a page. Everything this package computes — `parse_scene`, `render`, and the
whole rasteriser — is pure loft and runs there, because `Canvas`, `rgba`, `resize_lanczos`
and the pixel primitives are pure loft too. The one native it touches is
`graphics::save_png`, reached only from `render_file`, and `graphics` ships no browser
bridge — so a page renders into a `Canvas` and cannot write a PNG *file*. That is a
sensible shape for a browser anyway, but it is **not gated**: no headless-page test runs
here yet, so treat the browser column as "compiles and should work", not as measured.

## Provenance

The line grammar and every rendering decision come from `crawler/tools/draw.py`; the
rasteriser is ported from Pillow 10.2.0 (`src/libImaging/Draw.c` and `src/_imaging.c`).
The corpus gate and the findings behind this package are @PLN146 arc W.
