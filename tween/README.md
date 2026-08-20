<!--
Copyright (c) 2026 Jurjen Stellingwerff
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# tween — a value that arrives

```sh
loft install tween
```

## Why it exists

A tween is six lines of arithmetic that everyone writes and almost everyone writes with the
same two defects — both of which are silent, and both of which were measured before this
package was written rather than argued about afterwards.

**One: time kept in float seconds does not add up.** One second of it:

| | sums to |
|---|---|
| 30 frames of 1/30 s | `0.99999999999999989` |
| 60 frames of 1/60 s | `1.00000000000000133` |

Neither is 1.0 and neither is the other. So the same animation ends in two different places
at the two most ordinary frame rates — and the 30 Hz one **never arrives**: it stops a hair
short of its target and stays there for ever. "It lands on end − ε" and "it looks different
at 30 Hz" are one defect, not two.

Here a duration is an integer count of base units and `advance` adds integers. Sixty units
of 50 000 and thirty of 100 000 are both exactly 3 000 000, and there is nothing left for a
frame rate to change.

**Two: there are two ways to write a linear interpolation, and one of them misses the end.**
Over eight ordinary pairs, `a + (b − a)·t` lands on `b` five times; `a·(1 − t) + b·t` lands
eight times. And the miss is not always an ε — for `a = 1e17, b = 1.0` the first form answers
**0.0**, because `(b − a)` loses `b` entirely and adding it back to `a` cancels to nothing.
A camera pulled from a far coordinate to the origin arrives at zero instead of at one, with
`t` perfectly 1.0 the whole way.

## Surface

- `tween_new(from, to, duration, curve = Linear, shape = In) -> Tween`
- `Tween.advance(units) -> integer` (the **leftover**) · `.value()` · `.progress()` ·
  `.done()` · `.delay(units)` · `.rewind(lead = 0)`
- `track_new(mode = Chain) -> Track` · `Track.add(tw)` · `.advance(units) -> integer` ·
  `.done()` · `.active()` · `.value_at(i)` · `.rewind()`
- `ease(curve, shape, t) -> float` · `curve_at(curve, t)` · `curve_in(curve, t)` ·
  `lerp(a, b, t)`
- `Curve.` `Linear` `Quad` `Cubic` `Quart` `Quint` `Sine` `Expo` `Circ` `Back` `Elastic`
  `Bounce` · `Shape.` `In` `Out` `InOut` · `Play.` `Chain` `Parallel`

## The unit is yours, and that is why there is no dependency

This package never converts seconds to units and never names a rate — feed it whatever
integer unit your clock already runs in, as long as it is the same one everywhere. With
[`fixstep`](https://github.com/loft-lang/loft-libs-game/tree/main/fixstep) that is
`CLOCK_UNITS_PER_SECOND` (3 000 000 — which makes 24, 25, 30, 50, 60 and 120 Hz all whole
numbers) and its `clock_units_from_seconds`.

⚠ **That is also why `tween` declares no dependency on `fixstep`.** A second definition of
how long a second is would be two facts that can disagree, and the conversion already has a
home. The two compose without importing each other, and a tween animates a value on a server
just as happily as one on a screen.

```loft
use tween;
use fixstep;

fade = tween_new(0.0, 1.0, clock_units_from_seconds(0.25), Curve.Cubic, Shape.Out);

// in the frame loop, with whatever `clock_advance` already gave you
_ = fade.advance(elapsed_units);
panel_alpha = fade.value();
```

## Eleven curves, three directions, one endpoint rule

The usual easing set is thirty-three separate functions, each of which has to land on exactly
0 and exactly 1 — thirty-three chances to get an endpoint wrong, silently. It is not
hypothetical: the textbook exponential ease answers **0.0009765625 at t = 0**, and the
textbook sine ease answers **0.9999999999999999 at t = 1**, because `cos(π/2)` in binary is
`6.1e−17` and not zero.

So only the **In** direction of each curve is written down, `Out` and `InOut` are reflections
of it, and all three read the curve through one clamped accessor:

```
out(t)   = 1 − in(1 − t)
inout(t) = in(2t)/2  ·  1 − in(2 − 2t)/2
```

Eleven definitions cover thirty-three combinations, and every one of them is exact at both
ends because the clamp sits *underneath* the reflections. ⚠ In front of them is not the same
thing — the first cut clamped the entry point instead, and an in-out at exactly its midpoint
asks the raw curve for `in(1)`, which is the value that misses. It answered
`0.5000000000000001`.

⚠ **`Back` and `Elastic` leave `[0, 1]` on purpose** — they overshoot and come back, so
clamping the *result* of an ease undoes the effect you asked for. Their in-out variants are
gentler than Penner's namesakes, which tune a second constant for that case; one rule in one
place is the trade.

## Chains carry their leftover, which is the whole of sequencing

`advance` answers how much time it did **not** use, so a chain hands each link's overshoot to
the next. Dropping it instead — which is what a chain built on a one-shot timer does — makes
the chain's total duration a function of the frame rate. Ten links of 0.07 s:

| frame rate | dropping the leftover | carrying it |
|---|---|---|
| 60 Hz | 0.833 s | **0.700 s** |
| 30 Hz | 1.000 s | **0.700 s** |
| 24 Hz | 0.833 s | **0.708 s** |

⚠ 43 % long at 30 Hz. Carrying costs at most one frame for the **whole chain** — the 24 Hz
cell, whose frame does not divide the total — instead of up to one frame **per link**.

## What is deliberately not here

**No setter and no property.** A tween answers a number; writing it somewhere is the
consumer's. That is what lets the same type drive a camera, a colour channel, an audio gain
and a value on a server. Binding one to a scene-graph property is
[@PLN145](https://github.com/loft-lang/plans/issues/145) `C2`.

**No callbacks.** `advance` answers a leftover and `done` answers a question; a consumer
polls. Same rule as `fixstep`, and for the same reason: one type then serves a frame loop, a
server pump, a script runner and a test.

**No vector, colour or transform tween.** Those are N tweens sharing one clock, and N of
these is already that.

**No seconds and no frame rate.** See the unit note above.

## Admission

**Admissible loft.** No `#native`, no I/O, no unbounded loop: the only loop over a collection
is a `for` across a bound written down at the loop (`len(steps) + 1`, which is its exact
worst case). Nothing here needs to be trusted engine.

## Tests

```sh
loft test              # the interpreter
loft test --native     # and the compiled backend — both are gated
```

Twenty tests over three files, and the ones that matter are each paired with a **control that
computes the alternative and asserts it disagrees** — float seconds really do part at the two
rates, the other interpolation spelling really does miss, and a discarding chain really does
take 50 frames where this one takes 42. A gate that only agrees with itself proves nothing.
