// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// tween-reference — the pure-Rust twin of the `tween` package's performance pass
// (bench/bench.loft), one file built with `rustc -O`.  It computes the SAME workloads with
// the SAME arithmetic, in the same order, and prints the same rows, hash included: a row
// whose hash matches the loft build's is a like-for-like comparison, and only then is a
// routine's loft time judged against it.  No dependencies and no cleverness — plain
// idiomatic Rust, the speed an industry implementation reaches without effort.
//
//     rustc -O --edition=2021 bench/bench.rs -o bench/.build/stats_rs && bench/.build/stats_rs --n 50
//
// A port of src/easing.loft and src/tween.loft: the curve is a `match` with the same f64
// formulae (`powf`, `sin`, `cos`, `sqrt`, as loft's are), and a tween is a plain struct
// the chain `progress` -> `ease` -> `lerp` reads.  `black_box` guards the op's INPUT and
// the sink, never anything inside the kernel.
use std::hint::black_box;
use std::time::Instant;

const FNV_OFFSET: i64 = 2166136261;
const FNV_PRIME: i64 = 16777619;

fn fnv(h0: i64, v: &[i64]) -> i64 {
    let mut h = h0;
    for &x in v {
        let w = x & 0xFFFF_FFFF;
        for sh in [24, 16, 8, 0] {
            h = ((h ^ ((w >> sh) & 255)) * FNV_PRIME) & 0xFFFF_FFFF;
        }
    }
    h
}

fn micro(v: f64) -> i64 {
    (v * 1000000.0) as i64
}

struct Row {
    name: &'static str,
    iters: i64,
    us: i64,
    px: i64,
    hash: i64,
    sink: i64,
}

fn print_row(r: &Row) {
    let ns_op = r.us * 1000 / r.iters;
    let ns_px = if r.px > 0 { (r.us * 1000) as f64 / (r.iters * r.px) as f64 } else { 0.0 };
    println!("{}\t{}\t{}\t{}\t{}\t{:.3}\t{:x}", r.name, r.iters, r.us, ns_op, r.px, ns_px, r.hash);
}

// ── easing.loft ─────────────────────────────────────────────────────

#[derive(Clone, Copy)]
enum Curve {
    Linear,
    Quad,
    Cubic,
    Quart,
    Quint,
    Sine,
    Expo,
    Circ,
    Back,
    Elastic,
    Bounce,
}

#[derive(Clone, Copy)]
enum Shape {
    In,
    Out,
    InOut,
}

const CURVES: [Curve; 11] = [
    Curve::Linear,
    Curve::Quad,
    Curve::Cubic,
    Curve::Quart,
    Curve::Quint,
    Curve::Sine,
    Curve::Expo,
    Curve::Circ,
    Curve::Back,
    Curve::Elastic,
    Curve::Bounce,
];
const SHAPES: [Shape; 3] = [Shape::In, Shape::Out, Shape::InOut];

const HALF_PI: f64 = 1.5707963267948966;
const ELASTIC_P: f64 = 2.0943951023931953;
const BACK_C1: f64 = 1.70158;
const EXPO_ZERO: f64 = 0.0009765625;
const EXPO_SPAN: f64 = 0.9990234375;
const BOUNCE_N: f64 = 7.5625;
const BOUNCE_B1: f64 = 0.36363636363636365;
const BOUNCE_B2: f64 = 0.7272727272727273;
const BOUNCE_B3: f64 = 0.9090909090909091;
const BOUNCE_S2: f64 = 0.5454545454545454;
const BOUNCE_S3: f64 = 0.8181818181818182;
const BOUNCE_S4: f64 = 0.9545454545454546;

fn ease(c: Curve, s: Shape, t: f64) -> f64 {
    match s {
        Shape::In => curve_at(c, t),
        Shape::Out => 1.0 - curve_at(c, 1.0 - t),
        Shape::InOut => {
            if t < 0.5 {
                curve_at(c, t * 2.0) * 0.5
            } else {
                1.0 - curve_at(c, (1.0 - t) * 2.0) * 0.5
            }
        }
    }
}

fn curve_at(c: Curve, t: f64) -> f64 {
    if t <= 0.0 {
        return 0.0;
    }
    if t >= 1.0 {
        return 1.0;
    }
    curve_in(c, t)
}

/// loft's `x?` on a float: an unanswerable (NaN) result reads as 0.0.
fn or_zero(v: f64) -> f64 {
    if v.is_nan() { 0.0 } else { v }
}

fn pow2(x: f64) -> f64 {
    or_zero(2.0f64.powf(x))
}

fn curve_in(c: Curve, t: f64) -> f64 {
    match c {
        Curve::Linear => t,
        Curve::Quad => t * t,
        Curve::Cubic => t * t * t,
        Curve::Quart => t * t * t * t,
        Curve::Quint => t * t * t * t * t,
        Curve::Sine => 1.0 - (t * HALF_PI).cos(),
        Curve::Expo => or_zero((pow2(10.0 * t - 10.0) - EXPO_ZERO) / EXPO_SPAN),
        Curve::Circ => 1.0 - or_zero((1.0 - t * t).sqrt()),
        Curve::Back => (BACK_C1 + 1.0) * t * t * t - BACK_C1 * t * t,
        Curve::Elastic => -pow2(10.0 * t - 10.0) * ((t * 10.0 - 10.75) * ELASTIC_P).sin(),
        Curve::Bounce => 1.0 - bounce(1.0 - t),
    }
}

fn bounce(t: f64) -> f64 {
    if t < BOUNCE_B1 {
        return BOUNCE_N * t * t;
    }
    if t < BOUNCE_B2 {
        let b = t - BOUNCE_S2;
        return BOUNCE_N * b * b + 0.75;
    }
    if t < BOUNCE_B3 {
        let b = t - BOUNCE_S3;
        return BOUNCE_N * b * b + 0.9375;
    }
    let b = t - BOUNCE_S4;
    BOUNCE_N * b * b + 0.984375
}

// ── tween.loft ──────────────────────────────────────────────────────

fn lerp(a: f64, b: f64, t: f64) -> f64 {
    a * (1.0 - t) + b * t
}

#[derive(Clone, Copy)]
struct Tween {
    from: f64,
    to: f64,
    duration: i64,
    elapsed: i64,
    lead: i64,
    curve: Curve,
    shape: Shape,
}

fn tween_new(from: f64, to: f64, duration: i64, curve: Curve, shape: Shape) -> Tween {
    Tween { from, to, duration: duration.max(0), elapsed: 0, lead: 0, curve, shape }
}

impl Tween {
    fn delay(&mut self, units: i64) {
        self.lead = units.max(0);
    }

    fn advance(&mut self, units: i64) -> i64 {
        if units <= 0 {
            return 0;
        }
        let mut left = units;
        if self.lead > 0 {
            let eat = self.lead.min(left);
            self.lead -= eat;
            left -= eat;
            if left <= 0 {
                return 0;
            }
        }
        if self.duration <= 0 {
            return left;
        }
        let room = self.duration - self.elapsed;
        if room <= 0 {
            return left;
        }
        if left < room {
            self.elapsed += left;
            return 0;
        }
        self.elapsed = self.duration;
        left - room
    }

    fn progress(&self) -> f64 {
        if self.duration <= 0 || self.elapsed >= self.duration {
            return 1.0;
        }
        if self.elapsed <= 0 {
            return 0.0;
        }
        self.elapsed as f64 / self.duration as f64
    }

    fn value(&self) -> f64 {
        lerp(self.from, self.to, ease(self.curve, self.shape, self.progress()))
    }
}

// ── ease ────────────────────────────────────────────────────────────

const EASE_SAMPLES: i64 = 10000;

fn ease_sweep(off: f64) -> Vec<f64> {
    let mut out = Vec::with_capacity(33);
    for &c in &CURVES {
        for &s in &SHAPES {
            let mut sum = 0.0;
            for k in 0..EASE_SAMPLES {
                sum += ease(c, s, (k as f64 + off) / EASE_SAMPLES as f64);
            }
            out.push(sum);
        }
    }
    out
}

fn bench_ease(n: i64) -> Row {
    let t0 = Instant::now();
    let mut acc = 0.0;
    for r in 0..n {
        let off = if r & 1 == 0 { 0.25 } else { 0.75 };
        let sums = ease_sweep(black_box(off));
        acc += sums[0];
    }
    let us = t0.elapsed().as_micros() as i64;
    let one = ease_sweep(0.25);
    let ints: Vec<i64> = one.iter().map(|&v| micro(v)).collect();
    Row { name: "ease", iters: n, us, px: 33 * EASE_SAMPLES, hash: fnv(FNV_OFFSET, &ints),
          sink: micro(black_box(acc)) }
}

// ── value ───────────────────────────────────────────────────────────

const TWEEN_COUNT: i64 = 2000;
const FRAMES: i64 = 600;
const FRAME_UNITS: i64 = 50000;

fn make_tweens(bump: f64) -> Vec<Tween> {
    (0..TWEEN_COUNT)
        .map(|i| {
            let dur = (20 + (i * 37) % 600) * FRAME_UNITS + i;
            let mut tw = tween_new(i as f64 * 0.5 + bump, 100.0 - i as f64 * 0.25, dur,
                                   CURVES[(i % 11) as usize], SHAPES[((i / 11) % 3) as usize]);
            if i % 7 == 0 {
                tw.delay((i % 5) * FRAME_UNITS);
            }
            tw
        })
        .collect()
}

fn poll_frame(tws: &mut [Tween]) -> f64 {
    let mut sum = 0.0;
    for tw in tws.iter_mut() {
        tw.advance(FRAME_UNITS);
        sum += tw.value();
    }
    sum
}

fn bench_value(n: i64) -> Row {
    let t0 = Instant::now();
    let mut acc = 0.0;
    for r in 0..n {
        let mut tws = make_tweens(black_box((r & 1) as f64));
        for _ in 0..FRAMES {
            acc += poll_frame(&mut tws);
        }
    }
    let us = t0.elapsed().as_micros() as i64;
    let mut one = make_tweens(0.0);
    let ints: Vec<i64> = (0..FRAMES).map(|_| micro(poll_frame(&mut one))).collect();
    Row { name: "value", iters: n, us, px: TWEEN_COUNT * FRAMES, hash: fnv(FNV_OFFSET, &ints),
          sink: micro(black_box(acc)) }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut n: i64 = 20;
    let mut i = 1;
    while i < args.len() {
        if args[i] == "--n" && i + 1 < args.len() {
            n = args[i + 1].parse().unwrap_or(20);
            i += 1;
        }
        i += 1;
    }
    if n < 1 {
        n = 1;
    }
    let t0 = Instant::now();
    println!("routine\titers\tus\tns_op\tpx\tns_px\thash");
    let rows = [bench_value(n), bench_ease(n)];
    let mut sink = 0i64;
    for r in &rows {
        print_row(r);
        sink = sink.wrapping_add(r.sink);
    }
    println!("time: {}ms sink={}", t0.elapsed().as_millis(), sink);
}
