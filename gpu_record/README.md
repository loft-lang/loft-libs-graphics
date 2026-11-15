<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu_record

A backend for [`gpu`](../gpu/README.md) that draws nothing and writes down every call it
receives.  It needs no display, so a program's drawing is tested on any machine and in every
build: the interpreter, a native program, wasm and a browser page give the same log.

```loft
use gpu;
use gpu_record;

gpu_record::reset();
dev = gpu::open(gpu_record::table(), 64, 48, "test") ?? return;
// … the program's drawing, with sources tagged "record" …
assert(gpu_record::log() == EXPECTED);
```

The log has one line a call.  Handles are numbered from 1 in the order they are made, so a log
is the same on every run:

```
open 1 64x48 "test"
pipeline 2 vertex 3 2 instance block 16x1@0 4x1@16 size 20 textures 1
  state shape 0 depth 2 blend 0 cull 0 into 0
begin 0 clear ff202020
  bind 2
  uniforms floats 20 first 1.000 last 1.000
  sample 0 texture 3
  vertices 4
  draw from 0 count 36 copies 0
end
present
```

| Call | What it answers |
|---|---|
| `table()` | The backend's table, for `gpu::open` |
| `log()` | Every call since the last `reset` |
| `reset()` | Forget the log and every object |
| `live()` | How many buffers, textures, targets and pipelines are held: 0 once their owners died |
| `refuse_source(on)` | Refuse a pipeline whose shader arrives as text, as a machine that compiles no shader while a program runs does; a source with empty text stands for one compiled ahead |
