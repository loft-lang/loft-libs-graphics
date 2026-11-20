<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# gpu_vulkan

The Vulkan backend of [`gpu`](../gpu/README.md): Vulkan 1.3 on Linux, in an X11 window
(Xorg or XWayland).

A game does not name this library: [`gpu_host`](../gpu_host/README.md) asks each backend in
turn, Vulkan after OpenGL until the two are timed on a Linux machine's own display (its
README), and `GPU_HOST=gpu_vulkan` asks this one alone.  A program that wants this one opens a
device with its table:

```loft
use gpu;
use gpu_vulkan;

dev = gpu::open(gpu_vulkan::table(), 960, 720, "view") ?? return;
```

`table()` answers 0 off Linux, and `open` answers null where no Vulkan driver draws into the
window: both are how a backend says "not here".  A driver is taken when it has Vulkan 1.3,
`VK_KHR_push_descriptor` and `VK_EXT_depth_clip_control`, as Mesa's drivers (RADV, ANV, NVK,
lavapipe) and NVIDIA's have; a GPU of its own is preferred, then one sharing the CPU's memory,
and the CPU itself (lavapipe) last.  Nothing is needed to build it: the Vulkan loader and X11
are opened when a device opens.

## A program's source for it

Vulkan takes a shader only as SPIR-V.  A program's source for this backend carries the tag
`"spirv"` and is a SPIR-V 1.3 module (Vulkan 1.1 and later) as base64 text, not written by hand:
`spirv.py` writes it from the program's `"glsl-330"` source, so a program is written once.

```loft
const FLAT_V: text = `
#version 330 core
layout(location = 0) in vec3 pos;
layout(std140) uniform Block {{ vec4 color; float z; }};
void main() {{ gl_Position = vec4(pos.xy, z, 1.0); }}
`;

// SPIR-V of FLAT_V (vertex), written by gpu_vulkan/spirv.py: do not edit.
const FLAT_V_SPV: text = `AwIjBwAGAQALAAgA…`;
```

```
gpu_vulkan/spirv.py FILE.loft ...           # write every SPIR-V constant again
gpu_vulkan/spirv.py --check FILE.loft ...   # exit 1 when one is not what its GLSL gives
```

A shader written for `graphics`, with its uniforms declared one by one, is marked `// SPIR-V of
VERT with FRAG (vertex|fragment), as graphics hands them on, written by gpu_vulkan/spirv.py: do
not edit.`  The script then first makes it into the form `graphics` gives `gpu`, by running
`graphics/tools/rewritten.loft`: the code the library itself runs.

It needs `glslangValidator` and `spirv-val` (Debian and Ubuntu: `glslang-tools`,
`spirv-tools`).  What the GLSL follows is `gpu_gl`'s rule, with three things more:

| Part of the program | In the GLSL |
|---|---|
| The inputs read per vertex, then per copy | `layout(location = 0)` onwards, in the program's order |
| The uniform block | The source's one `layout(std140) uniform` block (set 0, binding 0 in the SPIR-V) |
| Texture slot n | A `sampler2D` named `tex<n>` (set 0, binding 1 + n in the SPIR-V) |
| Values from the vertex to the fragment stage | Declared in the same order in both stages: they are numbered by that order |
| A uniform outside the block | None: Vulkan has no place for one, and `spirv.py` refuses it |
| Points | The vertex stage writes `gl_PointSize` (1.0 for the size OpenGL draws) |

When a pipeline is built, the SPIR-V is read before the driver sees it: its uniform block is
compared with the program's (its size in whole vectors, and where each field starts and how many
bytes it holds), its textures with the program's slots and its inputs with the program's.  A
module that does not fit builds no pipeline, and stderr says how.

## How it keeps `gpu`'s conventions

Every image is stored with its first row at the bottom, as OpenGL stores it, so a target drawn
into and then sampled shows the same way up on both, and a capture turns the rows over on the
way out.  Clip depth runs from -1 to 1 (`VK_EXT_depth_clip_control`) and the depth test is
`LESS`.  The window is an image of its own, copied into the swapchain at `present`: it keeps its
pixels from one frame to the next, and its alpha stays 1 as on an OpenGL window, which has no
alpha channel.

Everything happens in the order of the calls: a buffer, a texture or the uniform block written
between two draws of a pass is read as it was at each draw.  Work is recorded into one command
buffer a frame, two frames in flight; `capture` waits for the frame it reads.

## Verified

`tests/same_as_gl.loft` draws each scene through `gpu_gl` and through this backend, from one
program, and compares the two captures: depth and blending; culling of back and front faces,
scissor and viewport; a texture updated in place and sampled nearest, and sampled between its
pixels; copies read from an offset with an index list; lines; points; a scene drawn into a
colour and depth target first; a target's colour and alpha read back; a depth target sampled as
a texture; a matrix and an array in the block; a buffer, a texture and the block rewritten
between draws of one pass; mipmaps; a presented window.  `tests/host.loft` is `gpu_gl`'s, with
the colours worked out by hand.  The same pixels on both stacks except:

| Scene | Allowed | Why |
|---|---|---|
| A texture between its pixels | 1 in a channel | Each stack weighs two neighbours with its own precision (RADV against llvmpipe's OpenGL: 1005 pixels by 1) |
| Lines | 4 pixels | Where a steep line steps on a pixel's edge (lavapipe: 4; RADV: none) |
| Points | 2 pixels | A point a hair from a pixel's edge (lavapipe: the one point; RADV: none) |

| Host | State |
|---|---|
| Linux, X11 (Xvfb), RADV (AMD Radeon 890M) | The 18 tests pass on the interpreter, the Rust backend and the C backend, with the Khronos validation layer and its synchronisation checks reporting no error |
| Linux, X11 (Xvfb), lavapipe | The same |
| Linux, a desktop X server or XWayland | Not run |

The libraries that draw through `gpu` carry this backend's SPIR-V, and run their pixel tests
through it as well as through `gpu_gl`: `scene3d`'s against numbers worked out by hand, all
the same on both; `gpu_paint`'s against graphics' painter, with what differs from llvmpipe's
OpenGL measured in its README.

Under Xvfb a GPU's driver presents only through Mesa's software path:
`MESA_VK_WSI_DEBUG=sw`.  Without it RADV answers "not here" and lavapipe is taken.  To run the
suite on one driver with the validation layer:

```
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LAYER_VALIDATE_SYNC=true \
xvfb-run -a loft --tests tests --lib ..
```

The layer's one warning there, `ShaderOutputNotConsumed`, is the depth-only pass: its shader
writes a colour the pass has no image for, and that write is discarded, as OpenGL discards it.

## Where its declarations come from

`c/vk_decl.h` declares the part of Vulkan this backend uses under Vulkan's own names, and
`c/x11_host.h` the part of Xlib, with the window's input reported as graphics' X11 host
reports it (`gpu_gl/c/gl_base.h`): the same key codes, buttons, wheel steps, text and resize
events.  The Vulkan declarations are checked against the Khronos header:

```
gpu_vulkan/c/check_decl.py <dir holding vulkan/vulkan_core.h>   # exit 1 when one differs
```
