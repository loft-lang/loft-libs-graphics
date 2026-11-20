<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu_gl

The OpenGL backend of [`gpu`](../gpu/README.md): OpenGL 3.3 on Linux (X11 and GLX), macOS's
OpenGL through AppKit, OpenGL ES 3 on Android and WebGL2 in a browser page.

A game does not name this library: its host package asks each backend in turn.  A program
that wants this one alone opens a device with its table:

```loft
use gpu;
use gpu_gl;

dev = gpu::open(gpu_gl::table(), 960, 720, "view") ?? return;
```

`table()` answers 0 on a machine this library has no window host for, and `open` answers null
where the machine has no OpenGL: both are how a backend says "not here".

`renderer()` is the OpenGL renderer of the window opened last, as its driver names it ("Apple
M3 Max", "llvmpipe (LLVM 19.1.7, 256 bits)", "Apple Software Renderer"); empty in a page.  A
test that measures another backend against this one reads it to know what it measures against.

Built for consumers, none yet: `graphics` and `stage` move onto `gpu` next.

## What a shader source for it follows

A program's source for this backend carries the tag `"glsl-330"` and is GLSL 3.30
(`#version 330 core`); OpenGL ES 3 and WebGL2 are given the same text under their own version
line.

| Part of the program | In the source |
|---|---|
| The inputs read per vertex | `layout(location = 0)` onwards, in the program's order |
| The inputs read per copy | The locations after them |
| The uniform block | The source's one `layout(std140) uniform` block, its fields in the program's order |
| Texture slot n | A sampler named `tex<n>`: `tex0`, `tex1`, … |

```glsl
#version 330 core
layout(location = 0) in vec3 pos;
layout(std140) uniform Block { vec4 color; float z; };   // the program's [F4, F1]
void main() { gl_Position = vec4(pos.xy, z, 1.0); }
```

In a browser page a `DEPTH_WRITE` pipeline is not built (`dev.pipeline` answers null): the page
gives this library no way to change what a depth is compared by.

When a pipeline is built the compiled program's block is compared with the program's
declaration: its size in whole vectors, and where each field starts.  A source that lays the
block out otherwise (a `vec3` field, a field too few) builds no pipeline, and stderr shows
both layouts.

## A checked build

OpenGL takes a wrong call in silence: a mistake in this backend, or in a shader, shows as a
wrong picture or not at all.  A checked build of this library says so instead.  It is a
compile-time choice, so the library as published holds none of it and pays nothing:

```
gpu_gl/c/checked.sh run <test command>     # build checked, run, build as committed again
```

In a checked build:

| Check | What it reports |
|---|---|
| After every call of this backend that reaches OpenGL | The error OpenGL kept, with the `gpu` call it came from |
| The driver's own messages, where it has them (Mesa; not Apple's OpenGL, not a page) | Its text for an error, undefined behaviour, a deprecated or an unportable use |
| Before every call that takes a handle | A buffer, texture, target or pipeline used after its release or after its device closed, and one released twice; the call is not made |
| A pipeline's memory | Overwritten when it is freed, so a read of it after cannot pass for a live one |

Each fault is counted (`gpu_gl::faults()`) and written to stderr.  `GPU_GL_DEBUG=abort` stops
the program at the first, which is how a test run fails on one (`checked.sh run` sets it);
`GPU_GL_DEBUG=0` turns a checked build's checks off.  `gpu_gl::checked()` says whether the
checks are compiled in and on.

It does not see memory errors inside this library's own C (an address sanitizer does), and a
handle whose address the allocator has given out again reads as the new object.

`c/checked.sh --check` fails when `c/checked.h` carries the define: it is never committed.

## Where its window code comes from

`c/gl_base.h` is the graphics library's `c/graphics_gl.c` with its names under another prefix,
so the two link into one program.  It is derived, never edited:

```
gpu_gl/c/derive_base.sh           # write gl_base.h again after graphics' file changed
gpu_gl/c/derive_base.sh --check   # exit 1 when the two have drifted apart
```

graphics keeps its own copy until its `Window` is a `gpu` device; from then this is the only
one.

## Verified

`tests/same_as_graphics.loft` draws each scene through `gpu` + `gpu_gl` and through the
graphics library's own calls, and compares the two captures byte for byte: depth and blending,
culling, scissor and viewport, a texture updated in place and sampled nearest, a texture
sampled between its pixels, copies read from an offset with an index list, lines, and a scene
drawn into a colour and depth target first.

| Host | State |
|---|---|
| macOS (OpenGL 4.1 through AppKit) | The eight tests pass on the interpreter, the C backend, a shipped program and the Rust backend |
| macOS, a GitHub runner (Apple Software Renderer) | The tests pass on the interpreter and the C backend, in the library CI (`.macos_window`) |
| A browser page (WebGL2, headless Chrome) | The eight tests pass in both writers' pages |
| Linux (X11 and GLX, under Xvfb: llvmpipe) | The tests pass on the interpreter and the C backend |
| Android (OpenGL ES 3) | Not run |

A page needs a loft whose page bridge has the four uniform-block calls (`loft-gl-wasm.js`,
in loft after 2026.11): an older loft refuses the page build and names the missing import.

In a page the two libraries share the page's one context, and graphics takes the render state
it finds: a program that mixes both sets what its graphics drawing relies on.
