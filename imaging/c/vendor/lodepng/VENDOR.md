# lodepng, vendored

- Source: https://github.com/lvandeve/lodepng, commit `ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a`
  (2026-05-28).
- License: zlib (`LICENSE`, unchanged).
- Files: `lodepng.h` unchanged; `lodepng.c` is upstream's `lodepng.cpp` renamed, which lodepng
  documents as the way to build it as C.  No other change.
- SHA-256 of the upstream files: `lodepng.cpp`
  `d98e1f40d303c1038a096ebf93b413a565a91cf2c72b9d2fa5c625c4279c3cb6`, `lodepng.h`
  `23c27abb06883ed98184d16d0b20771b526dca1e8e13236c2397316769c0dc8b`.
- Built through `../../imaging_png.c`, which sets `LODEPNG_NO_COMPILE_DISK` and
  `LODEPNG_NO_COMPILE_CPP` and includes `lodepng.c`: no file I/O and no C++, so the same source
  builds for the host, the C backend and both wasm targets.
