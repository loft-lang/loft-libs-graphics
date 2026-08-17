<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# imaging — PNG load/save + pixel manipulation for loft

## Install

```sh
loft install imaging
```

## Surface

- `Image` / `Pixel` types.
- `file(path).png() -> Image` — decode.  Answers a COMPLETE image or `null`;
  there is no half-filled `Image`.  Every PNG colour type loads (RGBA,
  greyscale, grey+alpha, palette, 16-bit), folded to 8-bit RGB — **alpha is
  discarded**.
- `img.save_png(path) -> boolean` — encode as 8-bit RGB.  `Image.name` is not
  used; the path argument decides where it lands.
- `px.value() -> integer` — the pixel packed as `0xRRGGBB`.

`Image.data` is one flat, row-major `vector<Pixel>`: the pixel at (x, y) is
`data[y * width + x]`, and `len(data) == width * height`.

Native code (cdylib `loft_imaging`) backs the PNG codec via the `png`
crate.

## Worked examples

The contracts a signature cannot state are demonstrated by running tests
(@PLN141): [tests/worked-examples.loft](tests/worked-examples.loft) —
`@IMG-001` whole-image-or-null plus the addressing rule, `@IMG-002` every colour
type arrives as RGB with alpha dropped, `@IMG-003` a pixel is replaced rather
than edited (and the local you read it into is a view of its slot), `@IMG-004`
`limit(0, 255)` is a range, not a clamp.

## Stage A constraints

This chunk-resident release ships **interpreter + native** targets.
The browser-WASM bridge (`--html`) is **not** included — it depends
on an unpublished `loft-host-ffi` crate; deferred until that crate
ships on crates.io.  No `--html` consumer currently uses imaging,
so this is a deferred follow-up, not a regression.

## Provenance

Extracted from the loft monorepo's `lib/imaging/` 2026-05-31 as part
of [@PLAN12](https://github.com/jjstwerff/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md)
Phase 5b.
