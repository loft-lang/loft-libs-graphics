<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu_metal

The Metal backend of [`gpu`](../gpu/README.md): a Mac's GPU through Metal, in an AppKit window
of its own.

A game does not name this library: [`gpu_host`](../gpu_host/README.md) asks each backend in
turn.  A program that wants this one alone opens a device with its table:

```loft
use gpu;
use gpu_metal;

dev = gpu::open(gpu_metal::table(), 960, 720, "view") ?? return;
```

`table()` answers 0 on a machine that is not a Mac, and `open` answers null on a Mac with no
Metal device: both are how a backend says "not here".

## What a shader source for it follows

A program's source for this backend carries the tag `"msl"` and is the Metal Shading Language.
Each of the two sources is compiled on its own, so both declare what they share (the block, the
struct that goes from one to the other).

| Part of the program | In the source |
|---|---|
| The vertex and the fragment function | `vertex_main` and `fragment_main` |
| The inputs read per vertex | `[[attribute(0)]]` onwards of the `[[stage_in]]` struct, in the program's order |
| The inputs read per copy | The attributes after them |
| The uniform block | A struct at `[[buffer(0)]]`, in either function, its fields in the program's order |
| Texture slot n | `[[texture(n)]]`, with its sampler at `[[sampler(n)]]`; a depth texture is a `depth2d<float>` |
| The position | Handed to `gpu_position(...)`, which this backend defines ahead of the source |

```metal
struct Block { float4 color; float z; };              // the program's [F4, F1]
struct In { float3 pos [[attribute(0)]]; };
struct Out { float4 position [[position]]; };
vertex Out vertex_main(In in [[stage_in]], constant Block &u [[buffer(0)]]) {
    Out o;
    o.position = gpu_position(float4(in.pos.xy, u.z, 1.0));
    return o;
}
```

- **`gpu_position`** takes a position in `gpu`'s clip space (y up, depth from −1 to 1), the same
  one a source for any other backend writes, and answers Metal's (depth from 0 to 1).  A vertex
  source that does not call it builds no pipeline.
- **The block is checked.**  When a pipeline is built the compiled program's block is compared
  with the program's declaration: its size in whole vectors, and where each field starts.  A
  source that lays it out otherwise (a `packed_float3`, a field too few, a field moved) builds
  no pipeline, and stderr shows both layouts.
- **A value marked `[[flat]]`** is taken from a face's first corner here and from its last on
  OpenGL: give a flat value the same number on all three.
- **A point** has the size the vertex function writes to `[[point_size]]`; one that writes none
  has no defined size.
- **A compile error** names its line in the source as it was given.

## How it keeps `gpu`'s conventions

`gpu` promises one picture on every backend, in OpenGL's terms.  What Metal has otherwise:

| `gpu` promises | Metal has | What this backend does |
|---|---|---|
| Depth from −1 to 1 in clip space | 0 to 1 | `gpu_position` halves it |
| A viewport's and a scissor's origin at the bottom left | The top left | The rows are counted from the other end |
| In a target, texture coordinate (0, 0) is the pixel drawn at clip (−1, −1) | That pixel is the texture's last row | A pass into a target is drawn upside down (`gpu_position`), with the front face turned to match |
| A scissor may leave the target | It may not, and may not be empty | It is cut to the target; with nothing left, nothing is drawn |
| A texture gets its smaller copies whenever `mipmaps` is called | A texture has its levels from the start | The first `mipmaps` moves the pixels into a texture that has them |
| New floats in a buffer leave what was drawn alone | A buffer is the program's own memory | While a frame still reads a buffer, new floats go into a second one; the two then alternate |
| `put` between two passes shows in the second only | A write is immediate | A `put` waits for the passes that read the texture |
| `put`, `nearest` and `mipmaps` between two draws of a pass change the second only | A pass's draws run when the pass ends | The pass is handed to the GPU as far as it came and goes on behind the call, with what it was told |
| A window has no alpha | A layer's frame has one | A window pipeline writes no alpha, and the window is cleared opaque |
| A window's pixels are there to read whenever no pass is open | A frame is the layer's again once it is shown | The window draws into a picture of its own; `present` shows a copy |

A colour texture is BGRA at 8 bits a channel, so a `0xAARRGGBB` word is its four bytes as they
are; a depth is a 32-bit float.  The window's size is in points, as `gpu_gl`'s is.

## Where its window code comes from

The window, its keys, pointer and typed text are AppKit as the graphics library's C drives it
(the macOS part of `graphics/c/graphics_gl.c`), written out here with a Metal layer where that
has an OpenGL context: the same key codes and events.  It is a second copy by hand, not derived
by a script as `gpu_gl`'s is, because the two differ in every function that touches the context.

## Verified

On an Apple M3 Max; the tests on the interpreter, the C backend and the Rust backend, the
timed scene also as a shipped program (one executable that links the system library alone):

- **`tests/same_as_gl.loft`** draws each scene through `gpu` on this backend and on `gpu_gl`,
  with the same loft code, and compares the two captures: one triangle with slanted edges and
  a colour a corner; depth tested and written, tested only, and blending; culling, a scissor
  (also one that leaves the window) and a viewport; a texture put between two passes, RGB565,
  sampled nearest and between its pixels, and drawn small through its smaller copies; copies
  read from an offset, an index list, floats replaced inside and between passes; lines and
  points; a colour and depth target shown and read back; a depth alone and a colour alone; a
  texture put, re-filtered and given its smaller copies between two draws of a pass, also the
  pass's own colour; a window drawn with alpha and blended by what it holds; a frame read
  before and after it was presented; viewports that reach outside the window and a target.
  Every scene is the same byte for byte.
- **`tests/host.loft`** checks against values worked out by hand: an RGB565 frame's colours, a
  target read back, a block larger than a frame copies, each source this backend refuses, a
  device closed before what it made, and frames presented one after another.
- **The tolerance** between this backend and `gpu_gl`: none for a scene whose edges lie on pixel
  borders.  In perspective, a pixel whose middle a face's edge passes through may show the face
  on the other side: 20 to 80 of 691,200 pixels a view in the views measured (at most 0.012 %),
  as many as either backend differs from itself between a window and a target.  The guard and its numbers are in
  `scene3d/tests/backends.loft`.
- **Planted breaks**: 25 changes to `c/gpu_metal.c`, one at a time (the depth left in `gpu`'s
  range, a target not turned, rows counted from the wrong end, a filter, a blend factor, a
  kept pass cleared, new floats written where a frame still reads, …), each turn a test red;
  two of them (a scissor not cut to the target, a large block copied into the frame) only under
  Metal's own validation (`MTL_DEBUG_LAYER=1`), under which the whole suite passes.
- **`tests/host_checks.c`**, built by hand as its first lines say, includes this backend's C:
  two devices drawn on two threads under the thread sanitizer report no race, and a window
  whose application is hidden stays open and is shown again.
- **Timed** by `gpu_host/bench/backends.loft`, a town of 48 x 32 cells with 64 moving actors in
  a 960 x 720 window, as a shipped program: a frame costs 181 to 184 µs here and 185 to 206 µs on
  `gpu_gl`.  A frame shown waits for the display it is on.

| Host | State |
|---|---|
| A Mac with Apple silicon | As above |
| A Mac with an Intel processor | Not run: the C compiles for it, and its GPU's memory is not the program's, which the texture read-back allows for |
| Any other machine | `table()` is 0 |
