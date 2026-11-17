<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu_host

Opens a [`gpu`](../gpu/README.md) device on the backend this machine has.  A game depends on
this library and on `gpu`, and names no backend:

```loft
use gpu;
use gpu_host;

dev = gpu_host::open(960, 720, "view") ?? return;
```

## Which backend

Each backend is asked in turn, and the first that opens a window serves the program:

| Order | Backend | Where it answers |
|---|---|---|
| 1 | [`gpu_metal`](../gpu_metal/README.md) | A Mac |
| 2 | [`gpu_gl`](../gpu_gl/README.md) | Linux, Android, a browser page, and a Mac with no Metal |

Metal is first on a Mac because it draws the same picture as OpenGL at the same cost a frame
(`bench/backends.loft` measures both), and OpenGL on a Mac gets no further work from its maker.

| Call | What it does |
|---|---|
| `open(width, height, title)` | A window on the first backend that has one to give; null when none does |
| `open_fullscreen(title)` | The same over the whole screen |
| `backends()` | The names of the backends this machine has a table for, in the order asked |
| `open_on(backend, width, height, title)` | A window on that backend alone: for a test or a measurement that compares two |

The environment variable `GPU_HOST` names the one backend `open` and `open_fullscreen` ask
(`GPU_HOST=gpu_gl`): for comparing two backends with a program as it is, not for a game to set.

A program's shaders carry a source for each backend it may meet; a device of a backend a
program has no source for builds no pipeline (`dev.pipeline` answers null).

## Verified

`tests/host.loft`: the order, a backend nobody knows opening nothing, and one painter's picture
drawn through `open` and through each backend by name — the same byte for byte, with one pixel
checked by hand.  On a Mac both backends are there; elsewhere the test runs on the one there is.

`bench/backends.loft` times one scene on each backend and counts the pixels that differ.
