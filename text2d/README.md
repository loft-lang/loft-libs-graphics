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
- `text_width(s, scale)` · `line_height(scale)`

⚠ `write_text`, not `draw_text`: `graphics` has a `draw_text` **method** on `Canvas`, and a
method spelling outranks a library's free function of the same name (loft#940). A distinct
verb costs nothing and is reachable.

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
