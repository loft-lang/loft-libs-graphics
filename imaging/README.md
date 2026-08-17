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

## Targets

| target | state |
|---|---|
| interpreter | ✓ suite green |
| `--native` | ✓ suite green |
| `--native-wasm` (headless WASI) | ✗ blocked upstream — see below |
| `--html` (browser) | ✓ bridge in `wasm/`, landed 0.2.1 |

The browser bridge routes `n_load_png` / `n_save_png` to the browser's
Image/OffscreenCanvas API through `loft_gl` host imports, and path-deps the
sibling loft checkout, so nothing needs publishing to crates.io for it to build.
(The note that used to stand here — "the browser bridge is not included, it
depends on an unpublished `loft-host-ffi`" — had been wrong since 0.2.1.)

`--native-wasm` does not build today, and not because of anything in this
package: any program that `use`s a package with a `[native] crate` emits a call
to `loft::native_call::build_store`, which the wasm build has gated out behind
the `native-extensions` feature, so `rustc` fails with `E0433`. A pure-loft
sibling (`shapes`) builds and runs on that target from the same tree, which is
what pins the cause. Tracked as loft-lang/loft#967.

**Parity note for 0.2.2.** The browser path has always produced exactly
`width * height` RGB pixels. Until 0.2.2 the native decoder did not — it cut the
decoder's raw output into three-byte pixels, which is right only for 8-bit RGB —
so the two targets silently disagreed on every other colour type. 0.2.2 folds
every colour type to RGB on the native side too, which is what makes
interpreter == native == browser true rather than merely asserted.

## Provenance

Extracted from the loft monorepo's `lib/imaging/` 2026-05-31 as part
of [@PLAN12](https://github.com/jjstwerff/loft/blob/main/doc/claude/lib_plans/12-library-extraction/README.md)
Phase 5b.
