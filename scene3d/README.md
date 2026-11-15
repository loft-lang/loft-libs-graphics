<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# scene3d

A 3D scene a game's view gives once, drawn through [`gpu`](../gpu/README.md).

It is the GPU side of tile-world-game's scene contract (`SCENE_CONTRACT.md` there): a view
builds tiles of faces, the cells' light and the actors of a frame; an implementation draws
them.  The contract's CPU implementation, `raster3d`, draws the same scene on a machine with
no GPU (the Tanmatsu).  This library takes the same arrays `raster3d` takes — single
precision, as a 32-bit program holds them — so one view feeds either.

```loft
use gpu;
use scene3d;

scene = scene3d::new_scene(dev, gpu::INTO_WINDOW, 0xFF94BFE6) ?? return;
stone = scene.add_picture(texels);                 // 16 x 16 RGB565, NONE where it is clear
scene.set_tile(dev, key, faces);                   // a square of cells, whole
scene.set_light([layer, x0, y0, w, h, 0.4, 0.9]);  // the cells' light, as data
// each frame
scene.set_actors(actors, hero);                    // the hero is never hidden
scene.fixed_camera(px, py);                        // or scene.free_camera(eye, at, 60.0)
scene.draw(dev);
```

Built for consumers, none yet: tile-world-game's view is its first.

## What a scene holds

Units: x east, y up in layers, z south; a cell is 1 across, a layer 1 high.

| Part | Call | What it is |
|---|---|---|
| Pictures | `add_picture(texels)` | 16 x 16 RGB565 texels; its number, for a face or an actor |
| Tiles | `set_tile(dev, key, faces)`, `drop_tile(key)` | Faces under a key, given again whole when anything in them changes |
| The cells' light | `set_light(box)` | A light for each cell of a box on a layer; faces lit from those cells follow it, no face is given again |
| Actors | `set_actors(actors, keep_seen)` | This frame's cards, each placed, sized, lit, turned and mirrored |
| The overlay | `set_overlay(dev, pixels, w, h, scale)` | A picture over everything, its clear pixels not drawn |
| The camera | `fixed_camera(px, py)`, `free_camera(eye, at, field)` | Parallel from the south, or in perspective from anywhere |

**A face** is, in `faces` (a `vector<single>`), one after another: the picture, the group it is hidden with (-1:
none), its corners n, the cell its light comes from (x, y, its layer, a factor; layer
`NO_LIGHT`: its corners' own light), then n times x, y, z, u, v, light.

**An actor** is eleven numbers: an id, the picture, the bottom middle (x, y, z) of the picture
as it stands unturned, its size in cells, its light, degrees turned clockwise, the point it
turns about (u, v) and whether it is mirrored.

## The cameras

- **Fixed**: a parallel projection from the south, a cell 48 pixels across, its ground 40
  deep, a layer 48 tall; the view gives the world pixel at the screen's top-left.  It is the
  camera every implementation offers.
- **Free**: an eye, a point looked at and a field of view, in perspective.  A view asks
  `offers_free_camera()` first: this implementation does, one on a CPU does not.

**The hero is never hidden** from the fixed camera: every wall group whose triangles cover the
hero's card on the screen and reach nearer than it is cut away, and shown again when it no
longer does (`hidden_groups()` says which).  The free camera cuts no wall yet, and cards stand
facing south for both.

## What it does not do yet

- A picture is 16 x 16 RGB565, as `raster3d`'s; the contract's pictures of any size are not taken.
- Models (a mesh for an actor) are not drawn.
- The light map holds 256 x 256 cells on 8 layers and wraps: a scene wider than that reads a
  far cell's light for a near one.
- The cut-away for the free camera, and cards turned to face it.

## Verified

- **Against the CPU implementation.**  `bench/against_raster3d.loft` draws one scene — lit
  ground, blocks whose faces are groups, a slanted triangle, three actors (the hero behind a
  block, one turned and mirrored, one turned about its middle), an overlay — through
  `raster3d` and through this library at 800 x 480, and compares the pictures.  Measured on a
  Mac (OpenGL 4.1), against `raster3d` at tile-world-game `dd62c5e`: of 384,000 pixels, 23
  differ by more than two steps of the CPU's five bits a channel (16 of 255); the rest are equal or within that, which is the CPU reducing a
  colour to RGB565 after it is lit.  The same block is cut away by both.  Four planted
  breaks (no cut-away, no cell light, the camera a pixel off, the overlay at half its scale)
  each put thousands of pixels out.  It is not under `tests/`: `raster3d` is in
  tile-world-game's `local/` and not yet a library this package can depend on.
- **By hand.**  `tests/pixels.loft`: a face takes its cell's light once it is given, and only
  its own cell's and layer's; an actor stands over the land, mirrored and turned; the overlay
  over both; the free camera puts a cell's four corners where a projection worked out by hand
  puts them.  `tests/record.loft` (no display): what a scene asks of a backend, and which
  walls are cut away and shown again.
- On a Mac on the interpreter, the C backend, the Rust backend and a shipped program, and in
  both writers' browser pages.  Ten planted breaks each turn a test red.  Not run: X11.
