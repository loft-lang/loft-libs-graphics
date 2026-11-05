# lodepng, vendored

- Source: https://github.com/lvandeve/lodepng, commit `7b2893e35891c0c85bee5595caafaf3c2044b409`
  (2026-11-02).
- License: zlib (`LICENSE`, unchanged).
- Files: `lodepng.h` unchanged; `lodepng.c` is upstream's `lodepng.cpp` renamed, which lodepng
  documents as the way to build it as C.  No other change.
- SHA-256 of the upstream files: `lodepng.cpp`
  `32cdd7a395b5476a3fe4615305a99066dc4d37aaea6988643cca8b4eccc352cc`, `lodepng.h`
  `a49b98b45ea603a05e2a77bd6b0320e7535a9c6e18aef8ce904692a3e02b87ab`.
- Built through `../../imaging_png.c`, which sets `LODEPNG_NO_COMPILE_DISK` and
  `LODEPNG_NO_COMPILE_CPP` and includes `lodepng.c`: no file I/O and no C++, so the same source
  builds for the host, the C backend and both wasm targets.
