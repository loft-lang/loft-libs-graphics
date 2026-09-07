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

**A gradient is a small ramp ENLARGED, and the enlargement is visible.** `draw.py` computes
every gradient on a 100×100 grid and resizes it to the shape's bounding box — and
`Image.resize(size)` with no `resample=` argument is **bicubic**, a filter the call site
never names. Bicubic has negative lobes, so the result overshoots: a hard 40→200 step
enlarged 4× spans 28..212. Computing the ramp directly at final size is smoother and
wrong, so `graphics::resize_bicubic` reproduces the two steps instead.

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
| `Fronds (x,y)-(x,y) n=N len=L [len2= w= w2= ang= ang2= mirror= jitter= field= fray= bow= seed= depth= sub= stroke=]` | a seeded, non-uniform array of tapered strokes rooted along a spine |
| `Brush <name> hair [w=12] [period=48] [seed=1] [gap=0.35]` · `Brush <name> file=<png> [period=]` | a footprint for `Lock`: the built-in split-bristle image, or an authored PNG (rows along the stroke, columns across) |
| `Lock (x,y)[~] … [brush=] [w0=2] [w=10] [swell=0.3] [body=0.8] [tips=3] [tipvar=0.35] [spread=8] [seed=1] [rgb=] [dark=] [lit=] [light=x,y,z] [alpha=1] [flip=1]` | one lock of hair or tuft of fur: the brush dragged root→tip, pinched at `w0`, swelling to `w`, ending in `tips` spikes of uneven length; shaded `dark` underneath, `rgb` on the crest, `lit` toward the light; painted OVER what is beneath, so lay locks back to front |
| `landmark <name> = <value>` · `check …` | read and carried; the report channel itself is not in this release |
| `# …` | a comment, and a searchable note |

`<fill>` is one of:

| | |
|---|---|
| `rgb=R,G,B` · `fill=L` | solid (colour, or grey 0..1) |
| `grad=R,G,B>R,G,B` `[dir=ax,ay,bx,by]` | a linear ramp; without `dir=`, top to bottom over the shape's bounding box |
| `radial=R,G,B>R,G,B` `[at=cx,cy,r]` | a ramp from a centre outwards; without `at=`, the bounding box's centre and half its longer side |

They are tried in that order and the most specific KIND wins — a line carrying both `rgb=`
and `radial=` is radial, whichever was written last.

A point may carry `~` (it curves — the tangent is half the neighbour chord, so a segment
between two corners stays exactly straight) and `@N` (the pen width AT that point, which
makes a stroke taper).

## The brush

A `Lock` is the one mark that is not a filled shape: an IMAGE is dragged along the path.
The footprint's columns map across the stroke, stretched to the local width, its rows along
it, tiled every `period` px; the built-in `hair` footprint is channels of bristles with a
share of thin strands that fade in and out, so a drag lays broken parallel streaks and a
frayed silhouette. The stroke is built in its own layer — each pixel keeps the sample from
the centreline it is closest to across, so the body and its spikes join without a seam — and
composited over the canvas once, so a lock covers the locks laid before it. Across the width
it is shaded as a half-cylinder against `light=`: `dark` on the underside, `rgb` on the
crest, `lit` on the flank facing the light. `dark=` is a colour, not a factor, because the
shadow of white hair is blue in some styles. The construction is
[src/brush.loft](src/brush.loft); its bytes are `draw.py`'s, pinned by `tests/lock.loft`.
The rules it enforces are named in crawler's `formal/draw.md` and cited from the code
as `@FR-…`.

## What this release does not draw

The `Petals` array mark. It **parses** — so it cannot be misread as something else — and is
listed in `Sketch.deferred` with the line it came from. A caller therefore knows the picture
is short of a mark instead of finding out by eye.

A line that no command accepts at all is different, and lands in `Sketch.unparsed`: a
typo'd mark has to read as a syntax problem, not as a geometry one.

## Surface

- `parse_scene(src) -> Sketch` · `parse_scene_at(src, base_dir) -> Sketch` (what a
  `Brush … file=` path is relative to) · `render(sk) -> graphics::Canvas` ·
  `render_file(src_path, out_png) -> Sketch`
- `Sketch.` `sw` `sh` `transparent` `ops` `elems` `landmarks` `checks` `unparsed` `deferred`
  `brushes` `base_dir`
- `Op.` `kind` (`Sky` / `Fill` / `Stroke` / `Lock`) `pts` `paint` `widths` `w` `color`
  `color2` `style` `brush` — ⚠ `pts` are paper FRACTIONS, never pixels
- `brush::` `Brush` · `LockStyle` · `Layer` · `hair_brush` · `load_brush` · `lock_layer` ·
  `composite_layer` · `scaled` — the brush stroke, a sibling module like `raster` below
  (`use brush;` after `use drawing;`); `noise::` `seed_hash` · `seed_wave` · `PI` — the
  seeded hash the corpus is defined by, which `drawing::hash01` / `lowfreq` forward to
- `Paint.` `pk` (`Stroked` / `Solid` / `Linear` / `Radial`) `c1` `c2` `spec`
- `Elem.` `ename` `seen` `bx0` `by0` `bx1` `by1` — `seen` is false for an element that was
  named and never drawn, which is an absence rather than a box at the origin
- `circle_pts` · `smooth_pts` · `smooth_vals` · `smooth_applies` · `read_points` · `grey`
- `PaintKind.` `Stroked` `Solid` `Linear` `Radial` — `Paint.spec` is `ax,ay,bx,by` for a
  linear fill and `cx,cy,r` for a radial one, or **empty** meaning "derive it from the
  shape's own bounding box"
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

## Performance — the pass

Every public routine has to **pull its weight**: a benchmark row, and — for the routines
that define the picture — a **pure-Rust reference** it is judged against, so that "is loft
fast enough here?" is a measurement and not an opinion. The pass is `bench/`:

| | |
|---|---|
| `bench/bench.loft` | every routine on a fixed workload: `name  iters  µs  ns/op  px  ns/px  hash` — the hash is FNV-1a-32 of the output, and a `sink` folds each iteration's result so no backend can drop the work |
| `bench/bench.rs` | the same workloads with the same arithmetic in the same order, one file built with `rustc -O` exactly as loft's own `bench/` builds its references — plain Rust, no cleverness, the speed an industry implementation reaches without effort |
| `bench/compare.py` | joins the three lanes (Rust, `loft --native-release`, the interpreter with `LOFT_NO_NATIVE_LIBS=1`), best of 3 for the judged lanes, and FAILS a routine whose hashes disagree or whose native time is over `--bar` (4×). It knows nothing about the routines — any package printing the same rows can use it unchanged |

```sh
python3 bench/compare.py                    # ~5 min: the interpreter lane is the slow one
python3 bench/compare.py --skip-interp      # the verdict alone, ~2 min
```

The hash agreement is the precondition: a row whose lanes disagree is not one algorithm,
and its speeds are not comparable. **All fourteen rows agree** — including Pillow's
rasteriser with its f32 crossings, the frond array and the lock — which is also the third
validation of the algorithms after draw.py and the goldens.

**Measured 2026-09-07** (best of 3, `rustc -O` vs `loft --native-release` with the libraries
as their auto-built cdylibs, i.e. what a consumer gets; one shared Linux box, so the
absolute numbers are directional and the ratios are the point):

| routine | Rust ns/op | loft-native ns/op | native / Rust | interp / native |
|---|---:|---:|---:|---:|
| `hash` (100 000 `hash01`) | 162,320 | 1,771,660 | **10.9** | 75 |
| `hair_brush` | 13,260 | 57,200 | **4.3** | 62 |
| `smooth_pts` (61 pts) | 220 | 57,640 | **262** | 7 |
| `fronds` (depth 2, 1296 pts) | 43,620 | 2,149,200 | **49** | 6 |
| `lock_layer` (22 080 px) | 1,032,340 | 30,850,860 | **30** | 38 |
| `lock_layer` curved (38 250 px) | 779,600 | 26,213,820 | **34** | 20 |
| `composite_layer` | 63,100 | 1,656,000 | **26** | 61 |
| `fill_poly` circle (31 497 px) | 34,620 | 594,740 | **17** | 35 |
| `fill_poly` pentagram | 13,460 | 225,100 | **17** | 34 |
| `wide_line` | 4,500 | 78,340 | **17** | 38 |
| `parse_scene` | — | 338,700 | — | 18 |
| `render` 120×60 lock scene | — | 64,696,320 | — | 50 |
| `render` 64×64 marks scene | — | 17,788,320 | — | 87 |
| `resize_lanczos` 768²→256² (`graphics`) | — | 280,735,840 | — | 63 |

**The verdict is FAIL on every judged routine, and the finding is loft's, not this
package's.** The algorithms are the same to the byte; the gap is the loft native runtime
on this workload class — vectors of floats and structs in tight loops, `??`-discharged
arithmetic, per-call crossings into a library's cdylib (the `hash` row is 100 000 of
those) — the class loft's own `PERFORMANCE.md` measures at 18–25× on matrix / sort and
names **N1** (the `codegen_runtime` / `DbRef` indirection). A sprite plain Rust renders in
1 ms takes loft 30 ms: fine for a build step, not for anything that draws at runtime.
Recorded as the open deviation `D-draw-2` in crawler's `formal/draw.md` and filed
upstream; the pass is the reproduction, and closes the deviation the day every judged
routine is within the bar. `LOFT_PROFILE=1` on the interpreter lane attributes the brush
time to `raster_segment`'s per-pixel projection — the algorithm's own hot loop, shared with
the Rust port — and 84 % of the whole interpreted run to `graphics`' resample.

## Targets

| target | state |
|---|---|
| interpreter | ✓ suite green, and the 28-scene corpus gate |
| `--native` | ✓ suite green, and the 28-scene corpus gate |
| `--native-wasm` (headless WASI) | ✗ blocked by a dependency — see below |
| `--html` (browser) | compiles; the render path is pure loft, but see below |

This package is pure loft — no `#native`, no inline `#rust` — so its own code has nothing
target-specific in it. Both limits come from `graphics`, which it draws through, and from
`imaging`, whose native PNG decoder a `Brush … file=` footprint needs.

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
