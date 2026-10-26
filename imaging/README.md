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
- `file(path).png() -> Image?` — decode.  Answers a COMPLETE image or `null`
  (a missing file, a directory, or bytes that do not decode) — there is no
  half-filled `Image`, and the `?` says to discharge it before use
  (`?? Image { }`, or an `if img == null` check).  Every PNG colour type loads
  (RGBA, greyscale, grey+alpha, palette, 16-bit) as 8-bit RGBA; **alpha is
  carried**, and a source without an alpha channel arrives opaque (`a == 255`).
- `img.save_png(path) -> boolean` — encode.  Writes RGB when every texel is
  opaque and RGBA otherwise.  `Image.name` is not used; the path argument
  decides where it lands.
- `px.value() -> integer` — the pixel packed as `0xRRGGBB` (no alpha);
  `px.rgba() -> integer` — `0xAARRGGBB`, the byte order `graphics::Canvas` uses;
  `px.opaque(cutoff = 128) -> boolean` — covered enough to count, for a
  collision proxy, a trim box or a colour key.

A guide: [docs/01-getting-started.loft](docs/01-getting-started.loft).

`Image.data` is one flat, row-major `vector<Pixel>`: the pixel at (x, y) is
`data[y * width + x]`, and `len(data) == width * height`.

The PNG codec is C99 (`c/imaging.c`): its own inflate and deflate, CRC-32 and
Adler-32, the five filters, every colour type and depth, tRNS and Adam7.  loft
builds it with `cc` as the package's `[c] shim` and the `#c` declarations in
`src/imaging.loft` bind it — no Rust crate and no rustc, one source for the
interpreter, `--native` and the C backend.  Reading answers 8-bit RGBA as the
`png` crate's `normalize_to_color8` did; writing picks RGB or RGBA from the
pixels, as before.

## Worked examples

The contracts a signature cannot state are demonstrated by running tests
(@PLN141): [tests/worked-examples.loft](tests/worked-examples.loft) —
`@IMG-001` whole-image-or-null plus the addressing rule, `@IMG-002` every colour
type arrives as 8-bit RGBA with alpha carried (opaque when the file has none), `@IMG-003` a pixel is replaced rather
than edited (and the local you read it into is a view of its slot), `@IMG-004`
`limit(0, 255)` is a range, not a clamp.

## Targets

| target | state |
|---|---|
| interpreter | ✓ suite green |
| `--native` | ✓ suite green |
| the C backend (`--emit-c`) | ✓ suite green — the shim compiles into the unit |
| `--native-wasm` / `--html` | ✗ not claimed since 0.5.0: a `#c` binding has no C ABI on a wasm target.  0.4 carried a Rust wasm bridge; a shim compiled to wasm with a C cross-compiler is the route back |

## Provenance

Part of the loft-libs-graphics chunk — loft's
[library extraction plan](https://github.com/loft-lang/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md).
