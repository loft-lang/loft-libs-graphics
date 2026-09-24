// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// shapes-reference — the pure-Rust twin of the `shapes` package's performance pass
// (bench/bench.loft), one file built with `rustc -O`.  It computes the SAME workload with
// the SAME arithmetic, in the same order, and prints the same row, hash included: a row
// whose hash matches the loft build's is a like-for-like comparison, and only then is the
// routine's loft time judged against it (@FR-Perf-Weight).  No dependencies and no
// cleverness — plain idiomatic Rust, the speed an industry implementation reaches without
// effort, which is exactly what the bar should be.
//
//     rustc -O --edition=2021 bench/bench.rs -o bench/.build/stats_rs && bench/.build/stats_rs --n 2
//
// `proxies_overlap` is a port of src/shapes.loft: the same strict rect test, the same loop
// order and the same early exit, written as nested `iter().any()`.  `black_box` guards the
// op's INPUT (the repetition number) and the sink — never anything inside the kernel.
use std::hint::black_box;
use std::time::Instant;

const FNV_OFFSET: i64 = 2166136261;
const FNV_PRIME: i64 = 16777619;

const PROXIES: i64 = 500;
const BOXES: i64 = 16;

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

// ── src/shapes.loft ─────────────────────────────────────────────────

struct Rect {
    rx: f64,
    ry: f64,
    rw: f64,
    rh: f64,
}

struct Proxy {
    boxes: Vec<Rect>,
}

fn rects_overlap(a: &Rect, b: &Rect) -> bool {
    a.rx < b.rx + b.rw && a.rx + a.rw > b.rx && a.ry < b.ry + b.rh && a.ry + a.rh > b.ry
}

impl Proxy {
    fn proxy_hits(&self, r: &Rect) -> bool {
        self.boxes.iter().any(|b| rects_overlap(b, r))
    }

    fn proxies_overlap(&self, other: &Proxy) -> bool {
        self.boxes.iter().any(|b| other.proxy_hits(b))
    }
}

// ── The row ─────────────────────────────────────────────────────────

fn make_proxies() -> Vec<Proxy> {
    (0..PROXIES)
        .map(|k| {
            let px = ((k * 97) % 1500) as f64;
            let py = ((k * 61) % 900) as f64;
            Proxy {
                boxes: (0..BOXES)
                    .map(|j| Rect { rx: px + (j as f64) * 3.0, ry: py + (((j * 5 + k) % 7) as f64),
                                    rw: 3.0, rh: 20.0 + (((j * 11 + k) % 9) as f64) })
                    .collect(),
            }
        })
        .collect()
}

fn overlap_op(ps: &[Proxy]) -> [i64; 2] {
    let mut hits = 0i64;
    let mut sig = 0i64;
    for i in 0..ps.len() {
        let a = &ps[i];
        for j in (i + 1)..ps.len() {
            if a.proxies_overlap(&ps[j]) {
                hits += 1;
                sig = (sig + i as i64 * 1000 + j as i64) & 0xFFFF_FFFF;
            }
        }
    }
    [hits, sig]
}

fn bench_overlap(n: i64) -> Row {
    let mut ps = make_proxies();
    let x = ps[0].boxes[0].rx;
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        let r = black_box(r);
        ps[0].boxes[0].rx = x + (r & 1) as f64;
        sink = sink.wrapping_add(overlap_op(&ps)[0]);
    }
    let us = t0.elapsed().as_micros() as i64;
    let sink = black_box(sink);
    ps[0].boxes[0].rx = x;
    Row { name: "proxies_overlap", iters: n, us, px: PROXIES * (PROXIES - 1) / 2,
          hash: fnv(FNV_OFFSET, &overlap_op(&ps)), sink }
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
    let rows = [bench_overlap(n)];
    let mut sink = 0i64;
    for row in &rows {
        print_row(row);
        sink = sink.wrapping_add(row.sink);
    }
    println!("time: {}ms sink={}", t0.elapsed().as_millis(), sink);
}
