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

Native code (cdylib `loft_imaging`) backs the PNG codec via the `png`
crate.

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
| `--native-wasm` (headless WASI) | ✓ builds and decodes as the interpreter does |
| `--html` (browser) | ✓ bridge in `wasm/`, landed 0.2.1 |

The browser bridge routes `n_load_png` / `n_save_png` to the browser's
Image/OffscreenCanvas API through `loft_gl` host imports, and path-deps the
sibling loft checkout, so nothing needs publishing to crates.io for it to build.

## Provenance

Part of the loft-libs-graphics chunk — loft's
[library extraction plan](https://github.com/loft-lang/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md).
