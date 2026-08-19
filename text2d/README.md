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
- `metrics_measured(wide_run, narrow_run, n, line_h)` · `metrics_builtin(scale)` ·
  `Metrics.width(s)` / `.fits(s, px)` / `.fit_text(s, px)` / `.advance()` /
  `.advance64()` / `.height()` / `.is_mono()`
- `Metrics.wrap(s, px) -> vector<text>` · `Metrics.align_x(line, bx, bw, align)` ·
  `ALIGN_LEFT` / `ALIGN_CENTRE` / `ALIGN_RIGHT` · `take_chars(s, n)`

⚠ `write_text`, not `draw_text`: `graphics` has a `draw_text` **method** on `Canvas`, and a
method spelling outranks a library's free function of the same name (loft#940). A distinct
verb costs nothing and is reachable.

## Measuring a real font — the metrics seam

This package has no GL context and no font file, so it cannot measure a real face itself.
The consumer measures **once at startup** through whichever backend resolved and hands the
numbers over; `Metrics` turns them into the answers a layout needs. That keeps `text2d`
headless while still serving a game that ships a real TTF — and `metrics_builtin(scale)`
answers through the same seam, so a layout written against it works with no font at all.

⚠ **Two runs, because one cannot answer the question.** Ten M's are ten M's in any font: a
single measurement gives the advance of an M and says nothing about whether the face is
fixed-pitch. `metrics_measured` takes `n` copies of a **wide** character and `n` of a
**narrow** one and compares them. This is not hypothetical — asking for
`DejaVuSansMono.ttf` gives a fixed-pitch face on the desktop and a **proportional fallback
in the browser**, because the browser resolves the base name to a CSS family it does not
know. The advance comes from the **wide** run: under fixed pitch the two agree, and when
they do not, the wider one over-estimates rather than overflowing.

⚠ **The advance is carried in 1/64 px, and that is not a detail.** DejaVu Sans Mono at 16 px
advances 9.6 px, which a whole-pixel field truncates to 9 — and the error is per
*character*, so it accumulates: a 60-character line comes out **36 px short**. The gate
states it as a property rather than a war story — that line measures 576 px, and **no
integer advance can produce 576 over 60 characters** — so a whole-pixel field provably
cannot hold this face. 1/64 is the unit fonts are hinted in and keeps the residual under one
pixel over any line a panel can hold.

⚠ **Widths round outward.** An over-estimate reserves a pixel too many and the text still
fits; an under-estimate spills out of a box that was just measured as fitting.

`Metrics.width` is the **advance** extent — what you reserve a box by. `text_width` is the
built-in face's **ink** extent — what you centre by. They differ by exactly the one trailing
gap the ink extent leaves off, and answer different questions.

`fit_text` returns the string **untouched** when it fits, so a caller cannot tell "fitted"
from "never measured" by looking for the marker. The marker is `".."` and not a single `…`:
the ellipsis is one code point but several bytes, and a marker whose own width is ambiguous
is the wrong thing to measure a truncation with.

## Wrapping and alignment

`wrap(s, px)` breaks greedily on measured advances; `align_x` places a line in a box.

⚠ **`len(text)` counts CHARACTERS; `s[a..b]` slices BYTES.** `"héllo"[0..5]` is `"héll"` —
four characters, not five. loft snaps a byte cut outward to a character boundary, so the
failure is not mojibake but something quieter: a break computed as a character count and
applied as a byte range **fits fewer characters than it measured**, silently, and only in
text that is not ASCII. Everything here counts and cuts in characters, and `take_chars(s, n)`
is public because every caller that slices text by a measured count needs it.

⚠ **A line always takes at least one character**, so wrapping terminates even in a box
narrower than the narrowest glyph. The alternatives are an infinite loop or dropping the
text, and a line that overflows and says so is better than either.

A word wider than the box is broken by characters rather than left to overflow. An explicit
`\n` breaks a line that would otherwise have fitted — it is the author's break, not a
suggestion. An empty string wraps to **one empty line**, because an empty document still has
a line to put a caret on.

⚠ **A line wider than its box starts at the box**, under every alignment. Centring or
right-aligning it arithmetically would push its beginning off the left edge, where it cannot
be clipped back into view; overflow to the right is recoverable, overflow to the left loses
the start of the text.

**The break table is per target and there is no shared one to assert.** Native measures a
real TTF and the browser measures whatever family resolved, so the same string breaks in
different places. What is cross-target is **self-consistency**: every line fits the box
*those* metrics measured. The hand-computed table is written against the built-in face — the
one target whose measurement is knowable without a font — and the property is gated over a
fixed-pitch face, a fractional-advance one and a proportional one.

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
