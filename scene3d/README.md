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
| Actors | `set_actors(actors, keep_seen)` | This frame's cards, each placed, sized, lit, turned and mirrored, facing the camera |
| The overlay | `set_overlay(dev, pixels, w, h, scale)` | A picture over everything, its clear pixels not drawn |
| The camera | `fixed_camera(px, py)`, `free_camera(eye, at, field)` | Parallel from the south, or in perspective from anywhere |

**A face** is, in `faces` (a `vector<single>`), one after another: the picture, the group it is hidden with (-1:
none), its corners n, the cell its light comes from (x, y, its layer, a factor; layer
`NO_LIGHT`: its corners' own light), then n times x, y, z, u, v, light.

**An actor** is eleven numbers: an id, the picture, the bottom middle (x, y, z) of the picture
as it stands unturned, its size in cells, its light, degrees turned clockwise, the point it
turns about (u, v) and whether it is mirrored.

## Torches

A view that asks `offers_torches()` and hears yes gives the cells' light **without** its
torches, and the torches themselves:

| Call | When | What it is |
|---|---|---|
| `set_torch_reach(d)` | A torch is new, has moved, or burns otherwise | Its id, the layer, a box of at most 15 x 15 cells and the light it gives each cell: 0 where a wall stops it |
| `set_torches(d)` | Every frame | Nine numbers a torch: its id, its place (x, y, z as a face's corners), the shade it starts at, its glow, its colour |
| `drop_torch(id)` | It is gone | — |

What is drawn:

- **Smoothly.**  A point is lit by the reach read between the middles of the four cells round
  it, times the torch's glow and colour: no step from one cell to the next.
- **Only where it reaches.**  A cell the reach does not name is not lit by that torch, however
  near: light does not pass a wall the rules stop it at.
- **The brightest wins, a channel at a time**: of the cell's own light and the four torches
  whose reach is strongest at that cell.  A fifth torch at a cell is not drawn.  The contract
  asks for the two strongest by reach times glow; a glow is between 0.86 and 1, so those two
  are among the four unless five torches reach one cell within a seventh of each other.
- **A flicker costs nine numbers.**  Nothing is written to a texture for a torch whose reach
  is as it was.
- **A card** is lit by the torches of the cell it stands on, and by its own light where that
  is brighter.

A scene holds 64 torches; a 65th reach is refused.  A torch's id is given as a single, which
holds a whole number exactly up to 16,777,216: two ids that round to one number are one torch.

## The cameras

- **Fixed**: a parallel projection from the south, a cell 48 pixels across, its ground 40
  deep, a layer 48 tall; the view gives the world pixel at the screen's top-left.  It is the
  camera every implementation offers.
- **Free**: an eye, a point looked at and a field of view, in perspective.  A view asks
  `offers_free_camera()` first: this implementation does, one on a CPU does not.

**A card faces the camera**, upright.  For the fixed camera it stands in the plane the camera
looks at.  For the free one it is turned about its foot so that its width lies across the
view: every card the same way, the picture's left on the left.

**The hero is never hidden**: the wall groups between the camera and the hero's card are cut
away, and shown again when they no longer are (`hidden_groups()` says which).
- For the fixed camera: a group whose triangles cover the card on the screen and reach nearer
  than it.
- For the free camera: a group that a line from the eye to the card goes through.  Five lines
  are tried: to the card's middle, and to the point halfway from the middle to each corner.
  So a wall that only meets the card's outer quarter is not cut away.

The cards and the cut-away follow the camera and the actors as they are now, whichever was
given last.

## What it does not do yet

- A picture is 16 x 16 RGB565, as `raster3d`'s; the contract's pictures of any size are not taken.
- Models (a mesh for an actor) are not drawn.
- The light map holds 256 x 256 cells on 8 layers and wraps: a scene wider than that reads a
  far cell's light for a near one.
- More than four torches a cell, and a torch's reach wider than 15 cells.

## Verified

- **Against the CPU implementation.**  `bench/against_raster3d.loft` draws one scene — lit
  ground, blocks whose faces are groups, a slanted triangle, three actors (the hero behind a
  block, one turned and mirrored, one turned about its middle), an overlay — through
  `raster3d` and through this library at 800 x 480, and compares the pictures.  Measured on a
  Mac (OpenGL 4.1), against `raster3d` at tile-world-game `dd62c5e`: of 384,000 pixels, 38
  differ by more than two steps of the CPU's five bits a channel (16 of 255); the rest are equal or within that, which is the CPU reducing a
  colour to RGB565 after it is lit.  The same block is cut away by both.  Four planted
  breaks (no cut-away, no cell light, the camera a pixel off, the overlay at half its scale)
  each put thousands of pixels out.  It is not under `tests/`: `raster3d` is in
  tile-world-game's `local/` and not yet a library this package can depend on.
- **In the game.**  tile-world-game's `tools/fixedshot.loft` (Orbury at night, the hero's torch,
  the fixed camera) against its CPU picture, on a Mac: 24 of 384,000 pixels differ by more than
  one step of RGB565.  It was 7,209 while a pixel on a texel's edge was left to the GPU.
- **By hand.**  `tests/pixels.loft`: a face takes its cell's light once it is given, and only
  its own cell's and layer's; an actor stands over the land, mirrored and turned; the overlay
  over both; the free camera puts a cell's four corners where a projection worked out by hand
  puts them.  `tests/record.loft` (no display): what a scene asks of a backend, and which
  walls are cut away and shown again.
- **The free camera** (`tests/pixels.loft`, `tests/record.loft`): a card seen from the east
  and from the north stands across the view at the size worked out by hand, its left on the
  left; a wall between the eye and the hero is cut away from above and level, one behind the
  hero, beside the line or at the card's edge is not; the hero is seen through a wall, and the
  wall again from behind.  Nine planted breaks each turn a test red.
- **Torches** (`tests/torches.loft`): the light at a pixel against the reach weighed by
  hand between its four cells — by the torch, at its cell's edge and just past it, past a wall,
  under the cell's own light, at half its glow, with five torches at a cell, on a card.
- On a Mac on the interpreter, the C backend, the Rust backend and a shipped program, and in
  both writers' browser pages.  Ten planted breaks each turn a test red.  Not run: X11.
