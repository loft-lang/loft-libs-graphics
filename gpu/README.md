<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu

Drawing on a GPU through pipelines, passes and uniform blocks, with no graphics stack named.

A program built against `gpu` draws through whichever **backend** library the machine has:
each backend depends on `gpu` and answers a table of C functions (`c/gpu_backend.h`); `gpu`
depends on none of them.  `gpu_record` is the first backend: it draws nothing and writes down
every call, so drawing is tested on a machine with no display.

Built for consumers, none yet: `graphics` and `stage` move onto it next.

```loft
use gpu;

VIEW = gpu::Program {
  vertex:   [gpu::F3, gpu::F2],            // per vertex: a position and a texture coordinate
  instance: [],
  block:    [gpu::MAT4, gpu::F4],          // the uniform block: a matrix and a colour
  textures: 1,
  sources:  [gpu::source("glsl-330", VERTEX, FRAGMENT)],
};

dev   = gpu::open(table, 960, 720, "view") ?? return;   // `table` is a backend's `table()`
// or over the whole screen, its size the screen's: gpu::open_fullscreen(table, "view")
pipe  = dev.pipeline(VIEW, gpu::State { depth: gpu::DEPTH_TEST_WRITE }) ?? return;
atlas = dev.texture_from(pixels, 64, 64);
mesh  = dev.buffer(vertices);
u     = pipe.block();
while dev.poll() {
  u.mat4(0, mvp);
  u.f4(1, 1.0, 1.0, 1.0, 1.0);
  dev.begin_window(gpu::CLEAR, 0xFF202020);
  dev.bind(pipe);  dev.uniforms(u);  dev.sample(0, atlas);  dev.vertices(mesh);
  dev.draw(0, 36);
  dev.end();
  dev.present();
}
```

## The one rule

Everything a draw depends on is named in the draw's own pass: a pipeline, its block of
uniforms, its textures by slot, its buffers.  Nothing stays set from an earlier pass, and
`bind` forgets what the pipeline before it was given.

A call that breaks the rule is refused before the backend sees it, the same on every backend,
and `dev.refusals()` lists each one: a draw outside a pass, a pipeline built for another kind
of target than the pass draws into, a draw whose block or texture slot was not given.

## What it is made of

| Object | What it is |
|---|---|
| `Device` | One window and its GPU; everything made from it is freed when it closes |
| `Buffer`, `IndexBuffer` | Floats read per vertex or per drawn copy; vertex numbers |
| `Texture` | A colour image (8 bits a channel) or a depth image |
| `Target` | What a pass draws into other than the window |
| `Program` | What a shader reads, and its text for each backend it may meet |
| `Pipeline` | A program with its render state (`State`), built for one kind of target |
| `Block` | The values of a pipeline's uniform block, set by field position |

Render state is fixed for a pipeline's life: what it draws (`TRIANGLES`, `LINES`, `POINTS`),
`DEPTH_OFF`, `DEPTH_TEST`, `DEPTH_TEST_WRITE` or `DEPTH_WRITE`, `blend(source, destination)`,
`CULL_*`, and what it draws `INTO_*`.  Two states are
two pipelines.

`DEPTH_WRITE` writes a draw's depth and tests it against nothing: the draw covers what is there
and leaves its own depth behind.  With a scissor it is a clear of part of a target, depth
included.  A backend that cannot draw it answers no pipeline.

## What a host reads of a device

A host library gives a game its window, its input and its frame in one form on every machine,
whether the frame was drawn on the GPU or on the CPU.  For it a device also answers:

| Call | What it answers |
|---|---|
| `dev.keys()`, `dev.key_down(key)`, `pointer_x()`, `pointer_y()`, `buttons()` | What the window's keys and pointer hold now; `keys()` is 256 flags in `screen::keys()`'s form and codes |
| `dev.typed()`, then `dev.events()` | The bytes typed and the events queued since the last call (`EVENT_WORDS` numbers each) |
| `texture.put_rgb565(x, y, pixels, w, h)` | A frame a small machine's renderer drew, taken as it is: 5 bits red, 6 green, 5 blue |
| `dev.capture_of(target)` | A target's colour read back, the top row first, without showing it |

A game does not read input here: it asks its host.

## Uniform blocks

A block field is `F1`, `F2`, `F4`, `MAT4` or `f4s(n)` (an array of `F4`).  There is no
three-float and no integer field: declare `F4` and read three, and pass a count as a float.
The stacks disagree on where the field after a three-float one starts, so a block that had one
would be read differently on each.  `gpu::layout` gives each field's first float.

## From a 32-bit program

A program under loft's 32-bit regime (`#width 32`, as a small machine runs it) draws through
`gpu` as any other.  Two things do not fit its 32-bit `integer` and are declared `i64` here, so
they cross whole: a backend's table, and a colour (write it `0xFF202020l`).

## Conventions

The same on every backend: clip space has y up and depth from -1 to 1; a viewport's and a
scissor's origin is the bottom left; texture coordinate (0, 0) is the first pixel handed to
`texture_from` or `put`; a matrix is 16 floats, column-major; a colour is `0xAARRGGBB`.

A call takes effect where it is made.  A texture's `put`, `put_rgb565`, `nearest` or `mipmaps`
between two draws of a pass changes the second draw and leaves the first as it was drawn, also
when the texture is the one the pass draws into; new floats in a buffer do the same.

A window has no alpha.  It is cleared opaque whatever the colour says, no draw writes its alpha,
a blend by what the window holds (`DST_ALPHA`) finds 1, and `capture` reads every pixel opaque.
A target's texture has the alpha that was drawn.

## Writing a backend

Declare `gpu` as a dependency and write `#include "gpu/gpu_backend.h"`: loft puts a declared
dependency's headers where your C finds them, wherever the two packages are installed (a path
out of your package, `../../gpu/c/…`, names no file once it is installed).  Fill one
`gpu_backend`, and
export its address through a `table()` function (0 where your stack is absent).  Name the tag
your shading language's sources carry in `shading`.  A backend that draws `DEPTH_WRITE` (a depth
written without a test) puts `GPU_BACKEND_WRITES_DEPTH` in its table's `version`; one that
puts `GPU_BACKEND_VERSION` is never asked for such a pipeline.  `gpu` checks every call first, so a
backend sees a draw only inside a pass, with a pipeline built for that pass's target and all
it reads given.
