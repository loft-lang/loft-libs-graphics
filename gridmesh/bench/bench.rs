// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// gridmesh-reference — the pure-Rust reference for the `gridmesh` package's performance
// pass (bench/bench.loft), one file built with `rustc -O`.  It computes the SAME workloads
// with the SAME algorithm, in the same order, and prints the same rows, hash included: a
// row whose hash matches the loft build's is a like-for-like comparison, and only then is
// a routine's loft time judged against it.  No dependencies and no cleverness — plain
// idiomatic Rust with std's `HashMap` / `HashSet` where gridmesh keys a `hash<…>`, the
// speed an industry implementation reaches without effort.
//
//     rustc -O --edition=2021 bench/bench.rs -o bench/.build/stats_rs && bench/.build/stats_rs --n 20
//
// Each routine is a port of its loft original in src/gridmesh.loft: integers are i64 like
// loft's, `/` and `%` truncate as loft's do, and the segment mesh stores `u8` and `f32`.
// Keyed results are hashed order-free (per-entry hashes summed), as bench.loft does.
use std::collections::{HashMap, HashSet};
use std::hint::black_box;
use std::time::Instant;

const FNV_OFFSET: i64 = 2166136261;
const FNV_PRIME: i64 = 16777619;

fn fnv1(h0: i64, x: i64) -> i64 {
    let w = x & 0xFFFF_FFFF;
    let mut h = h0;
    for sh in [24, 16, 8, 0] {
        h = ((h ^ ((w >> sh) & 255)) * FNV_PRIME) & 0xFFFF_FFFF;
    }
    h
}

fn fnv(h0: i64, v: &[i64]) -> i64 {
    v.iter().fold(h0, |h, &x| fnv1(h, x))
}

fn milli(v: f64) -> i64 {
    (v * 1000.0) as i64
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
    if r.sink == i64::MIN {
        println!("(unreachable — keeps the sink alive)");
    }
}

fn timed<F: FnMut(i64) -> i64>(n: i64, mut f: F) -> (i64, i64) {
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        sink = sink.wrapping_add(f(r));
    }
    (t0.elapsed().as_micros() as i64, black_box(sink))
}

// ── src/gridmesh.loft: coordinates and the spatial index ────────────

fn enc_coord(x: i64, y: i64) -> i64 {
    (x + 1048576) * 2097152 + (y + 1048576)
}

fn build_index(xs: &[i64], ys: &[i64]) -> HashMap<i64, i64> {
    let mut cidx = HashMap::new();
    for i in 0..xs.len() {
        cidx.insert(enc_coord(xs[i], ys[i]), i as i64);
    }
    cidx
}

fn axial_dq(d: i64) -> i64 {
    match d {
        0 | 5 => 1,
        1 | 4 => 0,
        _ => -1,
    }
}

fn axial_dr(d: i64) -> i64 {
    match d {
        0 => 0,
        1 | 2 => 1,
        3 => 0,
        _ => -1,
    }
}

fn step_x(x: i64, y: i64, d: i64, k: i64) -> i64 {
    let mut par = y - (y / 2) * 2;
    if par < 0 {
        par += 2;
    }
    let q2 = (x - (y - par) / 2) + k * axial_dq(d);
    let r2 = y + k * axial_dr(d);
    let mut p2 = r2 - (r2 / 2) * 2;
    if p2 < 0 {
        p2 += 2;
    }
    q2 + (r2 - p2) / 2
}

fn step_y(y: i64, d: i64, k: i64) -> i64 {
    y + k * axial_dr(d)
}

fn idx_at(cidx: &HashMap<i64, i64>, x: i64, y: i64) -> i64 {
    match cidx.get(&enc_coord(x, y)) {
        Some(&ix) => ix,
        None => -1,
    }
}

// ── The chunk field ─────────────────────────────────────────────────

#[derive(Clone, Copy)]
struct ChunkKey {
    ck: i64,
    cx: i64,
    cy: i64,
}

fn chunk_div(v: i64, cs: i64) -> i64 {
    let vr = v % cs;
    if vr < 0 { (v - vr - cs) / cs } else { (v - vr) / cs }
}

fn chunk_loc(v: i64, cs: i64) -> i64 {
    let vr = v % cs;
    if vr < 0 { vr + cs } else { vr }
}

fn chunk_of(x: i64, y: i64, shift: i64) -> ChunkKey {
    let cs = 1 << shift;
    let cx = chunk_div(x, cs);
    let cy = chunk_div(y, cs);
    ChunkKey { ck: enc_coord(cx, cy), cx, cy }
}

struct ChunkBucket {
    cx: i64,
    cy: i64,
    cell_ixs: Vec<i64>,
}

struct ChunkField {
    xs: Vec<i64>,
    ys: Vec<i64>,
    cidx: HashMap<i64, i64>,
    chunk_shift: i64,
    halo_k: i64,
    dirty: HashMap<i64, ChunkKey>,
    buckets: HashMap<i64, ChunkBucket>,
}

fn field_new(xs: &[i64], ys: &[i64], chunk_shift: i64, halo_k: i64) -> ChunkField {
    let mut f = ChunkField {
        xs: xs.to_vec(),
        ys: ys.to_vec(),
        cidx: build_index(xs, ys),
        chunk_shift,
        halo_k,
        dirty: HashMap::new(),
        buckets: HashMap::new(),
    };
    for i in 0..xs.len() {
        let k = chunk_of(xs[i], ys[i], chunk_shift);
        f.buckets
            .entry(k.ck)
            .or_insert_with(|| ChunkBucket { cx: k.cx, cy: k.cy, cell_ixs: Vec::new() })
            .cell_ixs
            .push(i as i64);
    }
    f
}

fn mark_borders(f: &mut ChunkField, x: i64, y: i64) {
    let cs = 1 << f.chunk_shift;
    let (cx, cy) = (chunk_div(x, cs), chunk_div(y, cs));
    let (lx, ly) = (chunk_loc(x, cs), chunk_loc(y, cs));
    let k = f.halo_k;
    let dxlo = if lx < k { -1 } else { 0 };
    let dxhi = if lx >= cs - k { 1 } else { 0 };
    let dylo = if ly < k { -1 } else { 0 };
    let dyhi = if ly >= cs - k { 1 } else { 0 };
    for ddx in dxlo..=dxhi {
        for ddy in dylo..=dyhi {
            let (ncx, ncy) = (cx + ddx, cy + ddy);
            let ck = enc_coord(ncx, ncy);
            f.dirty.insert(ck, ChunkKey { ck, cx: ncx, cy: ncy });
        }
    }
}

fn field_add_cell(f: &mut ChunkField, x: i64, y: i64) {
    let ix = f.xs.len() as i64;
    f.xs.push(x);
    f.ys.push(y);
    f.cidx.insert(enc_coord(x, y), ix);
    let k = chunk_of(x, y, f.chunk_shift);
    f.buckets
        .entry(k.ck)
        .or_insert_with(|| ChunkBucket { cx: k.cx, cy: k.cy, cell_ixs: Vec::new() })
        .cell_ixs
        .push(ix);
    mark_borders(f, x, y);
}

fn field_mark_dirty(f: &mut ChunkField, x: i64, y: i64) {
    mark_borders(f, x, y);
}

struct ChunkInput {
    key: ChunkKey,
    cell_ixs: Vec<i64>,
    halo_ixs: Vec<i64>,
}

fn gather_halo(f: &ChunkField, cell_ixs: &[i64], this_ck: i64, halo_k: i64) -> Vec<i64> {
    let mut halo = Vec::new();
    let mut seen = HashSet::new();
    for &ci in cell_ixs {
        let (cx, cy) = (f.xs[ci as usize], f.ys[ci as usize]);
        for d in 0..6 {
            for k in 1..=halo_k {
                let nb = idx_at(&f.cidx, step_x(cx, cy, d, k), step_y(cy, d, k));
                if nb >= 0 && !seen.contains(&nb) {
                    let nk = chunk_of(f.xs[nb as usize], f.ys[nb as usize], f.chunk_shift);
                    if nk.ck != this_ck {
                        seen.insert(nb);
                        halo.push(nb);
                    }
                }
            }
        }
    }
    halo
}

fn collect_dirty_inputs(f: &ChunkField, halo_k: i64) -> Vec<ChunkInput> {
    let mut out = Vec::new();
    for dk in f.dirty.values() {
        if let Some(b) = f.buckets.get(&dk.ck) {
            let halo = gather_halo(f, &b.cell_ixs, dk.ck, halo_k);
            out.push(ChunkInput {
                key: ChunkKey { ck: dk.ck, cx: b.cx, cy: b.cy },
                cell_ixs: b.cell_ixs.clone(),
                halo_ixs: halo,
            });
        }
    }
    out
}

// ── The segment mesh ────────────────────────────────────────────────

#[derive(Default)]
struct SegMesh {
    kinds: Vec<u8>,
    x0s: Vec<f32>,
    y0s: Vec<f32>,
    z0s: Vec<f32>,
    x1s: Vec<f32>,
    y1s: Vec<f32>,
    z1s: Vec<f32>,
    colors: Vec<u8>,
    cell_ix: Vec<i64>,
}

#[allow(clippy::too_many_arguments)]
fn emit_segment(m: &mut SegMesh, kind: i64, x0: f64, y0: f64, z0: f64, x1: f64, y1: f64, z1: f64,
                color: i64, owner: i64) {
    m.kinds.push((kind & 255) as u8);
    m.x0s.push(x0 as f32);
    m.y0s.push(y0 as f32);
    m.z0s.push(z0 as f32);
    m.x1s.push(x1 as f32);
    m.y1s.push(y1 as f32);
    m.z1s.push(z1 as f32);
    m.colors.push((color & 255) as u8);
    m.cell_ix.push(owner);
}

// ── The rows ────────────────────────────────────────────────────────

fn fnv_index(cidx: &HashMap<i64, i64>) -> i64 {
    let sum = cidx.iter().fold(0, |s, (&ck, &ix)| (s + fnv1(fnv1(FNV_OFFSET, ck), ix)) & 0xFFFF_FFFF);
    fnv1(fnv1(FNV_OFFSET, cidx.len() as i64), sum)
}

const BI_W: i64 = 400;
const BI_CELLS: i64 = 100000;

fn bench_build_index(n: i64) -> Row {
    let xa: Vec<i64> = (0..BI_CELLS).map(|i| i % BI_W).collect();
    let xb: Vec<i64> = (0..BI_CELLS).map(|i| i % BI_W + 1).collect();
    let ys: Vec<i64> = (0..BI_CELLS).map(|i| i / BI_W).collect();
    let (us, sink) = timed(n, |r| {
        let xs = if r & 1 == 0 { &xa } else { &xb };
        build_index(black_box(xs), black_box(&ys)).len() as i64
    });
    let one = build_index(&xa, &ys);
    Row { name: "build_index", iters: n, us, px: BI_CELLS, hash: fnv_index(&one), sink }
}

const FA_W: i64 = 250;
const FA_PAINTS: i64 = 50000;

fn fnv_field(f: &ChunkField) -> i64 {
    let mut h = fnv(fnv(FNV_OFFSET, &f.xs), &f.ys);
    h = fnv1(h, fnv_index(&f.cidx));
    let bsum = f.buckets.iter().fold(0, |s, (&ck, b)| {
        (s + fnv(fnv1(fnv1(fnv1(FNV_OFFSET, ck), b.cx), b.cy), &b.cell_ixs)) & 0xFFFF_FFFF
    });
    h = fnv1(fnv1(h, f.buckets.len() as i64), bsum);
    let dsum = f.dirty.values().fold(0, |s, d| {
        (s + fnv1(fnv1(fnv1(FNV_OFFSET, d.ck), d.cx), d.cy)) & 0xFFFF_FFFF
    });
    fnv1(fnv1(h, f.dirty.len() as i64), dsum)
}

fn paint_field(ox: i64) -> ChunkField {
    let mut f = field_new(&[], &[], 5, 2);
    for i in 0..FA_PAINTS {
        let j = (i * 7919) % FA_PAINTS;
        field_add_cell(&mut f, j % FA_W + ox, j / FA_W);
    }
    f
}

fn bench_field_add_cell(n: i64) -> Row {
    let (us, sink) = timed(n, |r| paint_field(black_box((r & 1) * 32)).dirty.len() as i64);
    let one = paint_field(0);
    Row { name: "field_add_cell", iters: n, us, px: FA_PAINTS, hash: fnv_field(&one), sink }
}

const CD_SIDE: i64 = 100;
const CD_SHIFT: i64 = 2;
const CD_INNER: i64 = 23;
const CD_DIRTY: i64 = 200;
const CD_HALO: i64 = 3;

fn mark_interior(f: &mut ChunkField, rot: i64) {
    f.dirty.clear();
    for i in 0..CD_DIRTY {
        let m = (i + rot * 7) % (CD_INNER * CD_INNER);
        let (cx, cy) = (1 + m % CD_INNER, 1 + m / CD_INNER);
        field_mark_dirty(f, cx * 4 + 1, cy * 4 + 2);
    }
}

fn fnv_inputs(ins: &[ChunkInput]) -> i64 {
    let sum = ins.iter().fold(0, |s, ci| {
        let mut h = fnv1(fnv1(fnv1(FNV_OFFSET, ci.key.ck), ci.key.cx), ci.key.cy);
        h = fnv(fnv1(h, ci.cell_ixs.len() as i64), &ci.cell_ixs);
        h = fnv(fnv1(h, ci.halo_ixs.len() as i64), &ci.halo_ixs);
        (s + h) & 0xFFFF_FFFF
    });
    fnv1(fnv1(FNV_OFFSET, ins.len() as i64), sum)
}

fn bench_collect_dirty(n: i64) -> Row {
    let mut xs = Vec::new();
    let mut ys = Vec::new();
    for y in 0..CD_SIDE {
        for x in 0..CD_SIDE {
            xs.push(x);
            ys.push(y);
        }
    }
    let mut f = field_new(&xs, &ys, CD_SHIFT, 0);
    let (us, sink) = timed(n, |r| {
        mark_interior(&mut f, black_box(r));
        collect_dirty_inputs(black_box(&f), CD_HALO).len() as i64
    });
    mark_interior(&mut f, 0);
    let one = collect_dirty_inputs(&f, CD_HALO);
    let px = one.iter().map(|ci| ci.cell_ixs.len() as i64).sum();
    Row { name: "collect_dirty_inputs", iters: n, us, px, hash: fnv_inputs(&one), sink }
}

const ES_SEGS: i64 = 500000;

fn emit_mesh(salt: f64) -> SegMesh {
    let mut m = SegMesh::default();
    for i in 0..ES_SEGS {
        let x = (i as f64) * 0.37 + salt;
        let y = (i as f64) * 0.11;
        emit_segment(&mut m, i & 7, x, y, 1.5, x + 0.5, y - 0.25, 2.5, i * 7, i >> 2);
    }
    m
}

fn fnv_mesh(m: &SegMesh) -> i64 {
    let mut h = FNV_OFFSET;
    for i in 0..m.kinds.len() {
        h = fnv1(h, m.kinds[i] as i64);
        for v in [m.x0s[i], m.y0s[i], m.z0s[i], m.x1s[i], m.y1s[i], m.z1s[i]] {
            h = fnv1(h, milli(v as f64));
        }
        h = fnv1(h, m.colors[i] as i64);
        h = fnv1(h, m.cell_ix[i]);
    }
    h
}

fn bench_emit_segment(n: i64) -> Row {
    let (us, sink) = timed(n, |r| {
        let m = emit_mesh(black_box((r & 1) as f64 * 0.5));
        black_box(&m).kinds.len() as i64
    });
    let one = emit_mesh(0.0);
    Row { name: "emit_segment", iters: n, us, px: ES_SEGS, hash: fnv_mesh(&one), sink }
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
    println!("routine\titers\tus\tns_op\tpx\tns_px\thash");
    print_row(&bench_collect_dirty(n));
    print_row(&bench_build_index(n));
    print_row(&bench_field_add_cell(n));
    print_row(&bench_emit_segment(n));
}
