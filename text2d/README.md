<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# text2d — text that always draws

```sh
loft install text2d
```

## Why it exists

`graphics::draw_text` needs three things **at once**: a GL context, the native rasteriser,
and a font file. Under `loft test` the rasteriser answers *"native function not loaded"* — so
a repo that tests its UI headlessly answers by having no text. Two shipped surfaces in
`dryopea` did exactly that, each reaching the conclusion on its own: `hud.loft` draws its
digits as **rectangles**, and `picker.loft` shipped **without labels**.

A face baked in as data needs none of the three.

## Surface

- `write_text(canvas, s, x, y, colour, scale) -> integer` — draws, answers the width
- `write_centred(canvas, s, bx, by, bw, bh, colour, scale)`
- `text_width(s, scale)` · `line_height(scale)` · `blit_glyph(canvas, code, …)`
- `atlas_new(scale, capacity)` · `layout(s, x, y) -> vector<Quad>` ·
  `draw_quads(canvas, atlas, quads, colour)` · `atlas_writes()` · `atlas_glyph_count()`

⚠ `write_text`, not `draw_text`: `graphics` has a `draw_text` **method** on `Canvas`, and a
method spelling outranks a library's free function of the same name (loft#940). A distinct
verb costs nothing and is reachable.

## The glyph atlas

`create_text_texture` bakes one GPU texture per string, per size, per colour — so a score
that ticks uploads a texture every frame, which is why Brick Buster pre-baked digits 0-9 and
one texture per level numeral. A sheet is written **once**; a new string only re-lays-out
quads over it.

`atlas_writes()` counts sheet changes, which is exactly what a GL consumer uploads on — so a
caller can *assert* that a changing label costs nothing. Six hundred relayouts over ten
digits: ten writes, then zero.

A sheet holds exactly the `capacity` asked for, and a full sheet **refuses** — the quad
carries `-1` and draws nothing, so a caller sees a gap rather than another glyph's pixels.

## What the face is, and is not

A 5×7 bitmap set of 56 glyphs — space, digits, `A`–`Z`, common punctuation — carried as data
in `face.loft`. **It is a fallback, and the word is load-bearing**: its job is that text is
never invisible, not that it is beautiful. A real typeface arrives through the asset pack.

- **Lowercase folds to uppercase.** It renders rather than vanishing.
- **An unknown character reserves its width**, so a string never changes length because one
  glyph was missing.
- `text_width` is exact — *n* glyphs of 5 with a 1-pixel gap after all but the last — so a
  caller can centre with it and be right.

## Provenance

[@PLN145](https://github.com/loft-lang/plans/issues/145) `B0`.
