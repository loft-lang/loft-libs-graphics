// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// drawing-reference — the pure-Rust reference for the `drawing` package's performance
// pass (bench/bench.loft), one file built with `rustc -O` as loft's own bench/ builds its
// references.  It computes the SAME workloads with the SAME arithmetic, in
// the same order, and prints the same rows, hash included: a row whose hash matches the
// loft build's is a like-for-like comparison, and only then is a routine's loft time
// judged against it (@FR-Perf-Weight).  No dependencies and no cleverness — plain
// idiomatic Rust, the speed an industry implementation reaches without effort, which is
// exactly what the bar should be.
//
//     rustc -O --edition=2021 bench/bench.rs -o bench/.build/bench_rs && bench/.build/bench_rs --n 50
//
// Each routine is a port of its loft original (src/brush.loft, src/raster.loft,
// src/drawing.loft): the pixel values are i64 like loft's integers, casts truncate
// toward zero as loft's do, and Pillow's rasteriser keeps its crossings in f32.
use std::time::Instant;

const FNV_OFFSET: i64 = 2166136261;
const FNV_PRIME: i64 = 16777619;
const PI: f64 = 3.141592653589793;

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

fn timed<F: FnMut() -> i64>(n: i64, mut f: F) -> (i64, i64) {
    let t0 = Instant::now();
    let mut sink = 0i64;
    for _ in 0..n {
        sink = sink.wrapping_add(f());
    }
    (t0.elapsed().as_micros() as i64, sink)
}

// ── noise.loft ──────────────────────────────────────────────────────

fn seed_hash(seed: i64, idx: i64, salt: i64) -> f64 {
    let mut hx = ((seed * 73856093) ^ (idx * 19349663) ^ (salt * 83492791)) & 0xFFFF_FFFF;
    hx = (hx ^ (hx >> 13)) & 0xFFFF_FFFF;
    hx = (hx * 1274126177) & 0xFFFF_FFFF;
    (hx as f64 / 4294967295.0) * 2.0 - 1.0
}

fn seed_wave(seed: i64, u: f64) -> f64 {
    let p1 = seed_hash(seed, 0, 11) * PI;
    let p2 = seed_hash(seed, 0, 22) * PI;
    0.6 * (2.0 * PI * u + p1).sin() + 0.4 * (4.0 * PI * u + p2).sin()
}

// ── brush.loft: the footprint ───────────────────────────────────────

fn clampf(v: f64, lo: f64, hi: f64) -> f64 {
    if v < lo { lo } else if v > hi { hi } else { v }
}

fn clampi(v: i64, lo: i64, hi: i64) -> i64 {
    if v < lo { lo } else if v > hi { hi } else { v }
}

fn floor_i(v: f64) -> i64 {
    let i = v as i64;
    if (i as f64) > v { i - 1 } else { i }
}

fn hair_brush(bw: i64, bh: i64, seed: i64, gap: f64) -> Vec<i64> {
    let mut img = vec![0i64; (bw * bh) as usize];
    let mut x = 0i64;
    let mut ci = 0i64;
    while x < bw {
        let cw = 1 + ((2.99 * (seed_hash(seed, ci, 7) + 1.0) * 0.5) as i64);
        let val = 0.72 + 0.28 * (seed_hash(seed, ci, 8) + 1.0) * 0.5;
        let split = (seed_hash(seed, ci, 9) + 1.0) * 0.5 < gap;
        for k in 0..cw {
            let xx = x + k;
            if xx >= bw {
                break;
            }
            let edge = if xx == 0 || xx == bw - 1 { 0.6 } else { 1.0 };
            for y in 0..bh {
                let u = y as f64 / bh as f64;
                let v = val * (1.0 - 0.12 * (1.0 + seed_wave(seed * 3 + ci, u)) * 0.5);
                let mut a = edge;
                if split {
                    a = 0.35 * edge * clampf((seed_wave(seed * 7 + ci, u) + 0.6) / 0.6, 0.0, 1.0);
                }
                let g = (255.0 * v + 0.5) as i64;
                let ai = (255.0 * a + 0.5) as i64;
                img[(y * bw + xx) as usize] = (ai << 24) | (g << 16) | (g << 8) | g;
            }
        }
        x += cw;
        ci += 1;
    }
    img
}

// ── drawing.loft: smoothing ─────────────────────────────────────────

#[derive(Clone, Copy)]
struct Pt {
    x: f64,
    y: f64,
}

fn pt(x: f64, y: f64) -> Pt {
    Pt { x, y }
}

fn ctrl(pts: &[Pt], i: i64, closed: bool) -> Pt {
    let n = pts.len() as i64;
    if closed {
        return pts[i.rem_euclid(n) as usize];
    }
    pts[clampi(i, 0, n - 1) as usize]
}

fn half_chord(pts: &[Pt], i: i64, closed: bool) -> Pt {
    let a = ctrl(pts, i - 1, closed);
    let b = ctrl(pts, i + 1, closed);
    pt((b.x - a.x) * 0.5, (b.y - a.y) * 0.5)
}

const SMOOTH_SAMPLES: i64 = 10;

fn smooth_pts(pts: &[Pt], flags: &[bool], closed: bool) -> Vec<Pt> {
    let n = pts.len() as i64;
    if n < 3 || !flags.iter().any(|&f| f) {
        return pts.to_vec();
    }
    let mut out = vec![pts[0]];
    let segs = if closed { n } else { n - 1 };
    for i in 0..segs {
        let ia = (i % n) as usize;
        let ib = ((i + 1) % n) as usize;
        let a = pts[ia];
        let b = pts[ib];
        let chx = b.x - a.x;
        let chy = b.y - a.y;
        let ta = if flags[ia] { half_chord(pts, ia as i64, closed) } else { pt(chx, chy) };
        let tb = if flags[ib] { half_chord(pts, ib as i64, closed) } else { pt(chx, chy) };
        for k in 1..=SMOOTH_SAMPLES {
            let t = k as f64 / SMOOTH_SAMPLES as f64;
            let t2 = t * t;
            let t3 = t2 * t;
            let h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
            let h10 = t3 - 2.0 * t2 + t;
            let h01 = -2.0 * t3 + 3.0 * t2;
            let h11 = t3 - t2;
            out.push(pt(
                h00 * a.x + h10 * ta.x + h01 * b.x + h11 * tb.x,
                h00 * a.y + h10 * ta.y + h01 * b.y + h11 * tb.y,
            ));
        }
    }
    out
}

// ── drawing.loft: the frond array ───────────────────────────────────

struct Frond {
    pts: Vec<Pt>,
    wid: Vec<f64>,
}

#[derive(Clone)]
struct FrondSpec {
    count: i64,
    len1: f64,
    len2: f64,
    wid1: f64,
    wid2: f64,
    ang1: f64,
    ang2: f64,
    mirror: bool,
    jitter: f64,
    clump: f64,
    fray: f64,
    bow: f64,
    seed: i64,
    depth: i64,
    sub: f64,
}

fn round_half_even(v: f64) -> f64 {
    let f = v.floor();
    let d = v - f;
    if d > 0.5 {
        return f + 1.0;
    }
    if d < 0.5 {
        return f;
    }
    if (f as i64) % 2 == 0 { f } else { f + 1.0 }
}

fn fronds(x1: f64, y1: f64, x2: f64, y2: f64, sp: &FrondSpec, pw: i64, ph: i64) -> Vec<Frond> {
    let mut out: Vec<Frond> = Vec::new();
    let pwf = pw as f64;
    let phf = ph as f64;
    let p1x = x1 * pwf;
    let p1y = y1 * phf;
    let dx = x2 * pwf - p1x;
    let dy = y2 * phf - p1y;
    let mut ln = (dx * dx + dy * dy).sqrt();
    if ln == 0.0 {
        ln = 0.000000001;
    }
    let ux = dx / ln;
    let uy = dy / ln;
    let nx = -uy;
    let ny = ux;
    let n = sp.count;
    for i in 0..n {
        let ub = (i as f64 + 0.5) / n as f64;
        let mut u = ub + sp.clump * seed_wave(sp.seed, ub) * (0.5 / n as f64);
        if u < 0.0 {
            u = 0.0;
        }
        if u > 1.0 {
            u = 1.0;
        }
        let ltr = sp.len1 + (sp.len2 - sp.len1) * u;
        let dte = if u < 1.0 - u { u } else { 1.0 - u };
        let mut edge = 1.0 - dte / 0.25;
        if edge < 0.0 {
            edge = 0.0;
        }
        let lpx = ltr * (1.0 - sp.fray * edge) * (1.0 + sp.jitter * 0.4 * seed_hash(sp.seed, i, 1)) * pwf;
        let an = (sp.ang1 + (sp.ang2 - sp.ang1) * u) + sp.jitter * 15.0 * seed_hash(sp.seed, i, 2);
        let phi = an * PI / 180.0;
        let wi = (sp.wid1 + (sp.wid2 - sp.wid1) * u) * (1.0 + sp.jitter * 0.3 * seed_hash(sp.seed, i, 3));
        let rx = p1x + u * dx;
        let ry = p1y + u * dy;
        let sides: Vec<f64> = if sp.mirror { vec![1.0, -1.0] } else { vec![1.0] };
        for &s in &sides {
            let fx = nx * s * phi.cos() + ux * phi.sin();
            let fy = ny * s * phi.cos() + uy * phi.sin();
            let tx = rx + lpx * fx;
            let ty = ry + lpx * fy;
            let mut tip = wi * 0.15;
            if tip < 0.5 {
                tip = 0.5;
            }
            if sp.bow != 0.0 {
                let mx = (rx + tx) / 2.0 + sp.bow * lpx * (-fy);
                let my = (ry + ty) / 2.0 + sp.bow * lpx * fx;
                out.push(Frond {
                    pts: vec![pt(rx / pwf, ry / phf), pt(mx / pwf, my / phf), pt(tx / pwf, ty / phf)],
                    wid: vec![wi, (wi + tip) / 2.0, tip],
                });
            } else {
                out.push(Frond { pts: vec![pt(rx / pwf, ry / phf), pt(tx / pwf, ty / phf)], wid: vec![wi, tip] });
            }
        }
    }
    if sp.depth <= 1 {
        return out;
    }
    let base = out.len();
    let mut cn = round_half_even(n as f64 * 0.55) as i64;
    if cn < 2 {
        cn = 2;
    }
    for k in 0..base {
        let a = out[k].pts[0];
        let b = *out[k].pts.last().unwrap();
        let sub = FrondSpec {
            count: cn,
            len1: sp.len1 * sp.sub,
            len2: sp.len1 * sp.sub * 0.5,
            wid1: sp.wid1 * 0.5,
            wid2: if sp.wid2 * 0.5 > 0.6 { sp.wid2 * 0.5 } else { 0.6 },
            ang1: sp.ang1,
            ang2: sp.ang2,
            mirror: true,
            jitter: sp.jitter,
            clump: sp.clump,
            fray: sp.fray,
            bow: sp.bow,
            seed: sp.seed * 31 + k as i64 + 1,
            depth: sp.depth - 1,
            sub: sp.sub,
        };
        out.extend(fronds(a.x, a.y, b.x, b.y, &sub, pw, ph));
    }
    out
}

// ── brush.loft: the lock ────────────────────────────────────────────

struct Brush {
    bw: i64,
    bh: i64,
    img: Vec<i64>,
}

#[derive(Clone)]
struct LockStyle {
    w0: f64,
    w: f64,
    swell: f64,
    body: f64,
    tips: i64,
    tipvar: f64,
    spread: f64,
    seed: i64,
    period: f64,
    dark: i64,
    base: i64,
    lit: i64,
    lx: f64,
    ly: f64,
    lz: f64,
    alpha: f64,
    flip: bool,
}

struct Layer {
    x0: i64,
    y0: i64,
    lw: i64,
    lh: i64,
    px: Vec<i64>,
}

fn rgba(r: i64, g: i64, b: i64, a: i64) -> i64 {
    (a << 24) | (r << 16) | (g << 8) | b
}
fn color_r(c: i64) -> i64 { (c >> 16) & 255 }
fn color_g(c: i64) -> i64 { (c >> 8) & 255 }
fn color_b(c: i64) -> i64 { c & 255 }
fn color_a(c: i64) -> i64 { (c >> 24) & 255 }

struct PathPt {
    x: f64,
    y: f64,
    tx: f64,
    ty: f64,
}

fn path_at(px: &[f64], py: &[f64], cum: &[f64], dist: f64) -> PathPt {
    let n = px.len();
    let mut k = n - 2;
    for i in 0..(n - 1) {
        if dist <= cum[i + 1] {
            k = i;
            break;
        }
    }
    let seg = cum[k + 1] - cum[k];
    let u = (dist - cum[k]) / seg;
    let dx = px[k + 1] - px[k];
    let dy = py[k + 1] - py[k];
    PathPt { x: px[k] + u * dx, y: py[k] + u * dy, tx: dx / seg, ty: dy / seg }
}

fn lock_width(t: f64, w0: f64, w: f64, swell: f64) -> f64 {
    if swell <= 0.000000001 || t >= swell {
        return w;
    }
    w0 + (w - w0) * (0.5 * PI * t / swell).sin()
}

struct Ribbon {
    rx: Vec<f64>,
    ry: Vec<f64>,
    rhw: Vec<f64>,
    ral: Vec<f64>,
    rmx: Vec<f64>,
    slo: f64,
    shi: f64,
}

fn lock_ribbons(xs: &[f64], ys: &[f64], st: &LockStyle) -> Vec<Ribbon> {
    let mut px = vec![xs[0]];
    let mut py = vec![ys[0]];
    let mut cum = vec![0.0];
    for i in 1..xs.len() {
        let ddx = xs[i] - px[px.len() - 1];
        let ddy = ys[i] - py[py.len() - 1];
        let d = (ddx * ddx + ddy * ddy).sqrt();
        if d > 0.000001 {
            px.push(xs[i]);
            py.push(ys[i]);
            cum.push(cum[cum.len() - 1] + d);
        }
    }
    let mut out: Vec<Ribbon> = Vec::new();
    if px.len() < 2 {
        return out;
    }
    let len = cum[cum.len() - 1];
    let body = clampf(st.body, 0.05, 1.0);
    let swell = clampf(st.swell, 0.0, body);
    let ds = clampf(0.5 * st.w, 2.0, 6.0);
    let mut nb = -floor_i(-(body * len / ds));
    if nb < 2 {
        nb = 2;
    }
    nb += 1;
    let mut bx = Vec::new();
    let mut by = Vec::new();
    let mut bhw = Vec::new();
    let mut bal = Vec::new();
    let mut bmx = Vec::new();
    for j in 0..nb {
        let t = body * j as f64 / (nb - 1) as f64;
        let dist = t * len;
        let q = path_at(&px, &py, &cum, dist);
        bx.push(q.x);
        by.push(q.y);
        bhw.push(0.5 * lock_width(t, st.w0, st.w, swell));
        bal.push(dist);
        bmx.push(0.0);
    }
    out.push(Ribbon { rx: bx, ry: by, rhw: bhw, ral: bal, rmx: bmx, slo: -1.0, shi: 1.0 });
    let hwb = 0.5 * lock_width(body, st.w0, st.w, swell);
    let tail = (1.0 - body) * len;
    if st.tips <= 0 || tail < 1.0 || hwb < 0.3 {
        return out;
    }
    let mut raw = Vec::new();
    let mut tot = 0.0;
    for i in 0..st.tips {
        let r = 1.0 + 0.5 * seed_hash(st.seed, i, 41);
        raw.push(r);
        tot += r;
    }
    let mut a1 = -1.0;
    for i in 0..st.tips {
        let a0 = a1;
        a1 = a0 + (2.0 * raw[i as usize] / tot);
        let c = 0.5 * (a0 + a1);
        let hs = 0.5 * (a1 - a0);
        let mut ell = tail * (1.0 + st.tipvar * seed_hash(st.seed, i, 42));
        if ell < 0.15 * tail {
            ell = 0.15 * tail;
        }
        let th = st.spread * seed_hash(st.seed, i, 43) * (PI / 180.0);
        let mut m = -floor_i(-(ell / ds));
        if m < 2 {
            m = 2;
        }
        m += 1;
        let mut sx = Vec::new();
        let mut sy = Vec::new();
        let mut shw = Vec::new();
        let mut sal = Vec::new();
        let mut smx = Vec::new();
        for j in 0..m {
            let u = j as f64 / (m - 1) as f64;
            let sd = body * len + u * ell * th.cos();
            let sq = path_at(&px, &py, &cum, sd);
            let off = c * hwb + u * ell * th.sin();
            sx.push(sq.x + sq.ty * off);
            sy.push(sq.y - sq.tx * off);
            shw.push(hs * hwb * (1.0 - u * u));
            sal.push(sd);
            smx.push(u);
        }
        out.push(Ribbon { rx: sx, ry: sy, rhw: shw, ral: sal, rmx: smx, slo: a0, shi: a1 });
    }
    out
}

struct Lay {
    x0: i64,
    y0: i64,
    lw: i64,
    lh: i64,
    best: Vec<f64>,
    sb: Vec<f64>,
    sl: Vec<f64>,
    al: Vec<f64>,
    mx: Vec<f64>,
    nx: Vec<f64>,
    ny: Vec<f64>,
}

#[allow(clippy::too_many_arguments)]
fn raster_segment(lay: &mut Lay, ax: f64, ay: f64, bx: f64, by: f64, hwa: f64, hwb: f64,
                  ala: f64, alb: f64, mxa: f64, mxb: f64, slo: f64, shi: f64) {
    let dx = bx - ax;
    let dy = by - ay;
    let l2 = dx * dx + dy * dy;
    if l2 < 0.000000001 {
        return;
    }
    let ln = l2.sqrt();
    let nx = dy / ln;
    let ny = -dx / ln;
    let hm = if hwa > hwb { hwa } else { hwb };
    let mut x0 = floor_i((if ax < bx { ax } else { bx }) - hm);
    if x0 < lay.x0 {
        x0 = lay.x0;
    }
    let mut x1 = -floor_i(-((if ax > bx { ax } else { bx }) + hm));
    if x1 > lay.x0 + lay.lw - 1 {
        x1 = lay.x0 + lay.lw - 1;
    }
    let mut y0 = floor_i((if ay < by { ay } else { by }) - hm);
    if y0 < lay.y0 {
        y0 = lay.y0;
    }
    let mut y1 = -floor_i(-((if ay > by { ay } else { by }) + hm));
    if y1 > lay.y0 + lay.lh - 1 {
        y1 = lay.y0 + lay.lh - 1;
    }
    if x1 < x0 || y1 < y0 {
        return;
    }
    for yy in y0..=y1 {
        let cy = yy as f64 + 0.5;
        for xx in x0..=x1 {
            let cx = xx as f64 + 0.5;
            let u = clampf(((cx - ax) * dx + (cy - ay) * dy) / l2, 0.0, 1.0);
            let hw = hwa + (hwb - hwa) * u;
            if hw <= 0.01 {
                continue;
            }
            let dist = (cx - ax - u * dx) * nx + (cy - ay - u * dy) * ny;
            let s = dist / hw;
            let a = if s >= 0.0 { s } else { -s };
            if a > 1.0 {
                continue;
            }
            let idx = ((yy - lay.y0) * lay.lw + (xx - lay.x0)) as usize;
            if a < lay.best[idx] {
                lay.best[idx] = a;
                lay.sl[idx] = s;
                lay.sb[idx] = slo + (s + 1.0) * 0.5 * (shi - slo);
                lay.al[idx] = ala + (alb - ala) * u;
                lay.mx[idx] = mxa + (mxb - mxa) * u;
                lay.nx[idx] = nx;
                lay.ny[idx] = ny;
            }
        }
    }
}

struct Smp {
    a: f64,
    r: f64,
    g: f64,
    b: f64,
}

fn chan(c: i64, sh: i64) -> f64 {
    ((c >> sh) & 255) as f64
}

fn brush_sample(img: &[i64], bw: i64, bh: i64, fu: f64, fv: f64) -> Smp {
    let iu = floor_i(fu);
    let tu = fu - iu as f64;
    let iv = fv as i64;
    let tv = fv - iv as f64;
    let u0 = clampi(iu, 0, bw - 1);
    let u1 = clampi(iu + 1, 0, bw - 1);
    let v0 = iv % bh;
    let v1 = (iv + 1) % bh;
    let c00 = img[(v0 * bw + u0) as usize];
    let c10 = img[(v0 * bw + u1) as usize];
    let c01 = img[(v1 * bw + u0) as usize];
    let c11 = img[(v1 * bw + u1) as usize];
    let w00 = (1.0 - tu) * (1.0 - tv);
    let w10 = tu * (1.0 - tv);
    let w01 = (1.0 - tu) * tv;
    let w11 = tu * tv;
    let mix = |sh: i64| chan(c00, sh) * w00 + chan(c10, sh) * w10 + chan(c01, sh) * w01 + chan(c11, sh) * w11;
    Smp { a: mix(24), r: mix(16), g: mix(8), b: mix(0) }
}

fn ramp(c0: i64, c1: i64, f: f64, v: f64) -> i64 {
    ((c0 as f64 + (c1 - c0) as f64 * f) * v / 255.0 + 0.5) as i64
}

fn lock_layer(xs: &[f64], ys: &[f64], cw: i64, ch: i64, br: &Brush, st: &LockStyle) -> Layer {
    let none = Layer { x0: 0, y0: 0, lw: 0, lh: 0, px: Vec::new() };
    let rib = lock_ribbons(xs, ys, st);
    if rib.is_empty() {
        return none;
    }
    let mut minx = 1000000000000000000.0f64;
    let mut miny = 1000000000000000000.0f64;
    let mut maxx = -1000000000000000000.0f64;
    let mut maxy = -1000000000000000000.0f64;
    for r in &rib {
        for i in 0..r.rx.len() {
            let x = r.rx[i];
            let y = r.ry[i];
            let h = r.rhw[i];
            if x - h < minx { minx = x - h; }
            if x + h > maxx { maxx = x + h; }
            if y - h < miny { miny = y - h; }
            if y + h > maxy { maxy = y + h; }
        }
    }
    let mut x0 = floor_i(minx) - 1;
    if x0 < 0 { x0 = 0; }
    let mut y0 = floor_i(miny) - 1;
    if y0 < 0 { y0 = 0; }
    let mut x1 = floor_i(maxx) + 1;
    if x1 > cw - 1 { x1 = cw - 1; }
    let mut y1 = floor_i(maxy) + 1;
    if y1 > ch - 1 { y1 = ch - 1; }
    if x1 < x0 || y1 < y0 {
        return none;
    }
    let lw = x1 - x0 + 1;
    let lh = y1 - y0 + 1;
    let n = (lw * lh) as usize;
    let mut lay = Lay {
        x0, y0, lw, lh,
        best: vec![2.0; n], sb: vec![0.0; n], sl: vec![0.0; n], al: vec![0.0; n],
        mx: vec![0.0; n], nx: vec![0.0; n], ny: vec![0.0; n],
    };
    for r in &rib {
        for i in 0..(r.rx.len() - 1) {
            raster_segment(&mut lay, r.rx[i], r.ry[i], r.rx[i + 1], r.ry[i + 1],
                           r.rhw[i], r.rhw[i + 1], r.ral[i], r.ral[i + 1],
                           r.rmx[i], r.rmx[i + 1], r.slo, r.shi);
        }
    }
    let mut lx = st.lx;
    let mut ly = st.ly;
    let mut lz = st.lz;
    let mut ll = (lx * lx + ly * ly + lz * lz).sqrt();
    if ll < 0.000000001 {
        lx = 0.0;
        ly = 0.0;
        lz = 1.0;
        ll = 1.0;
    }
    lx /= ll;
    ly /= ll;
    lz /= ll;
    let crest = clampf(lz, 0.05, 0.95);
    let period = if st.period > 0.000001 { st.period } else { 1.0 };
    let phase = (seed_hash(st.seed, 0, 44) + 1.0) * 0.5 * period;
    let bwf = br.bw as f64;
    let bhf = br.bh as f64;
    let mut out = vec![0i64; n];
    for idx in 0..n {
        if lay.best[idx] > 1.5 {
            continue;
        }
        let mut s = lay.sb[idx];
        let mut uu = (s + 1.0) * 0.5;
        if st.flip {
            uu = 1.0 - uu;
        }
        let smp = brush_sample(&br.img, br.bw, br.bh, uu * bwf - 0.5,
                               ((lay.al[idx] + phase) / period) * bhf - 0.5 + bhf * 4096.0);
        let ia = smp.a * st.alpha;
        if ia < 0.5 {
            continue;
        }
        s = s + (lay.sl[idx] - s) * lay.mx[idx];
        let nz2 = 1.0 - s * s;
        let nz = (if nz2 > 0.0 { nz2 } else { 0.0 }).sqrt();
        let lit = clampf(s * lay.nx[idx] * lx + s * lay.ny[idx] * ly + nz * lz, 0.0, 1.0);
        let (c0, c1, f) = if lit < crest {
            (st.dark, st.base, lit / crest)
        } else {
            (st.base, st.lit, (lit - crest) / (1.0 - crest))
        };
        let r = ramp(color_r(c0), color_r(c1), f, smp.r);
        let g = ramp(color_g(c0), color_g(c1), f, smp.g);
        let b = ramp(color_b(c0), color_b(c1), f, smp.b);
        out[idx] = (((ia + 0.5) as i64) << 24) | (clampi(r, 0, 255) << 16) | (clampi(g, 0, 255) << 8) | clampi(b, 0, 255);
    }
    Layer { x0, y0, lw, lh, px: out }
}

struct Canvas {
    w: i64,
    h: i64,
    data: Vec<i64>,
}

fn canvas(w: i64, h: i64, fill: i64) -> Canvas {
    Canvas { w, h, data: vec![fill; (w * h) as usize] }
}

fn composite_layer(cv: &mut Canvas, lay: &Layer) {
    for j in 0..lay.lh {
        for i in 0..lay.lw {
            let c = lay.px[(j * lay.lw + i) as usize];
            let sa = (c >> 24) & 255;
            if sa == 0 {
                continue;
            }
            let di = ((lay.y0 + j) * cv.w + (lay.x0 + i)) as usize;
            let d = cv.data[di];
            let t = color_a(d) * (255 - sa);
            let oa = sa * 255 + t;
            let r = (((c >> 16) & 255) * sa * 255 + color_r(d) * t + oa / 2) / oa;
            let g = (((c >> 8) & 255) * sa * 255 + color_g(d) * t + oa / 2) / oa;
            let b = ((c & 255) * sa * 255 + color_b(d) * t + oa / 2) / oa;
            cv.data[di] = rgba(r, g, b, (oa + 127) / 255);
        }
    }
}

// ── raster.loft: Pillow's rasteriser ────────────────────────────────

#[derive(Clone, Copy)]
struct Edge {
    xmin: i64,
    xmax: i64,
    ymin: i64,
    ymax: i64,
    dx: f32,
    x0: i64,
    y0: i64,
}

fn round_up(v: f64) -> i64 {
    if v >= 0.0 { (v + 0.5).floor() as i64 } else { -((v.abs() + 0.5).floor() as i64) }
}

fn round_down(v: f64) -> i64 {
    if v >= 0.0 { (v - 0.5).ceil() as i64 } else { -((v.abs() - 0.5).ceil() as i64) }
}

fn roundf(v: f32) -> f32 {
    let d = v as f64;
    if d >= 0.0 { (d + 0.5).floor() as f32 } else { (d - 0.5).ceil() as f32 }
}

fn make_edge(x0: i64, y0: i64, x1: i64, y1: i64) -> Edge {
    Edge {
        xmin: if x0 <= x1 { x0 } else { x1 },
        xmax: if x0 <= x1 { x1 } else { x0 },
        ymin: if y0 <= y1 { y0 } else { y1 },
        ymax: if y0 <= y1 { y1 } else { y0 },
        dx: if y0 == y1 { 0.0f32 } else { (x1 - x0) as f32 / (y1 - y0) as f32 },
        x0,
        y0,
    }
}

fn edge_x(e: &Edge, y: i64) -> f32 {
    (y - e.y0) as f32 * e.dx + e.x0 as f32
}

fn pil_hline(cv: &mut Canvas, x0: i64, y: i64, x1: i64, ink: i64) {
    if y < 0 || y >= cv.h {
        return;
    }
    let mut lo = x0;
    let mut hi = x1;
    if lo < 0 { lo = 0; } else if lo >= cv.w { return; }
    if hi < 0 { return; } else if hi >= cv.w { hi = cv.w - 1; }
    if lo > hi {
        return;
    }
    let base = (y * cv.w) as usize;
    for x in lo..=hi {
        cv.data[base + x as usize] = ink;
    }
}

fn polygon_generic(cv: &mut Canvas, edges: &[Edge], ink: i64) {
    if edges.is_empty() {
        return;
    }
    let mut table: Vec<Edge> = Vec::new();
    let mut ymin = cv.h - 1;
    let mut ymax = 0;
    for e in edges {
        if ymin > e.ymin { ymin = e.ymin; }
        if ymax < e.ymax { ymax = e.ymax; }
        if e.ymin == e.ymax {
            pil_hline(cv, e.xmin, e.ymin, e.xmax, ink);
            continue;
        }
        table.push(*e);
    }
    if ymin < 0 { ymin = 0; }
    if ymax > cv.h { ymax = cv.h; }
    let ec = table.len();
    if ec == 0 {
        return;
    }
    let mut xx = vec![0.0f32; ec * 2];
    let mut y = ymin;
    while y <= ymax {
        let mut j = 0usize;
        for i in 0..ec {
            let cur = table[i];
            if y >= cur.ymin && y <= cur.ymax {
                xx[j] = edge_x(&cur, y);
                j += 1;
                if y == cur.ymax && y < ymax {
                    xx[j] = xx[j - 1];
                    j += 1;
                } else if cur.dx != 0.0f32 && roundf(xx[j - 1]) == xx[j - 1] {
                    for k in 0..i {
                        let oth = table[k];
                        if (cur.dx > 0.0f32 && oth.dx <= 0.0f32) || (cur.dx < 0.0f32 && oth.dx >= 0.0f32) {
                            continue;
                        }
                        if ((y == cur.ymin && y == oth.ymin) || (y == cur.ymax && y == oth.ymax))
                            && xx[j - 1] == edge_x(&oth, y)
                        {
                            let off = if y == ymax { -1 } else { 1 };
                            let ac = edge_x(&cur, y + off);
                            let ao = edge_x(&oth, y + off);
                            if y == cur.ymax {
                                if cur.dx > 0.0f32 {
                                    xx[k] = (if ac > ao { ac } else { ao }) + 1.0f32;
                                } else {
                                    xx[k] = (if ac < ao { ac } else { ao }) - 1.0f32;
                                }
                            } else if cur.dx > 0.0f32 {
                                xx[k] = if ac < ao { ac } else { ao };
                            } else {
                                xx[k] = (if ac > ao { ac } else { ao }) + 1.0f32;
                            }
                            break;
                        }
                    }
                }
            }
        }
        let mut s = 1usize;
        while s < j {
            let v = xx[s];
            let mut t = s;
            while t > 0 && xx[t - 1] > v {
                xx[t] = xx[t - 1];
                t -= 1;
            }
            xx[t] = v;
            s += 1;
        }
        let mut p = 1usize;
        while p < j {
            pil_hline(cv, round_up(xx[p - 1] as f64), y, round_down(xx[p] as f64), ink);
            p += 2;
        }
        y += 1;
    }
}

fn fill_poly(cv: &mut Canvas, pts: &[Pt], ink: i64) {
    let count = pts.len();
    if count == 0 {
        return;
    }
    let xs: Vec<i64> = pts.iter().map(|p| p.x as i64).collect();
    let ys: Vec<i64> = pts.iter().map(|p| p.y as i64).collect();
    let mut edges: Vec<Edge> = Vec::new();
    let mut i = 0usize;
    while i < count - 1 {
        let x0 = xs[i];
        let y0 = ys[i];
        let x1 = xs[i + 1];
        let y1 = ys[i + 1];
        let mut merged = false;
        if y0 == y1 && i != 0 && y0 == ys[i - 1] && !edges.is_empty() {
            let prev = xs[i - 1];
            let last = edges.len() - 1;
            if x1 > x0 && x0 > prev {
                edges[last].xmax = x1;
                merged = true;
            } else if x1 < x0 && x0 < prev {
                edges[last].xmin = x1;
                merged = true;
            }
        }
        if !merged {
            edges.push(make_edge(x0, y0, x1, y1));
        }
        i += 1;
    }
    let end = count - 1;
    if xs[end] != xs[0] || ys[end] != ys[0] {
        edges.push(make_edge(xs[end], ys[end], xs[0], ys[0]));
    }
    polygon_generic(cv, &edges, ink);
}

fn wide_line(cv: &mut Canvas, wx0: f64, wy0: f64, wx1: f64, wy1: f64, ink: i64, width: i64) {
    let x0 = wx0 as i64;
    let y0 = wy0 as i64;
    let x1 = wx1 as i64;
    let y1 = wy1 as i64;
    let dx = x1 - x0;
    let dy = y1 - y0;
    if dx == 0 && dy == 0 {
        if x0 >= 0 && x0 < cv.w && y0 >= 0 && y0 < cv.h {
            cv.data[(y0 * cv.w + x0) as usize] = ink;
        }
        return;
    }
    let big = ((dx * dx + dy * dy) as f64).sqrt();
    let small = (width - 1) as f64 / 2.0;
    let rmax = round_up(small) as f64 / big;
    let rmin = round_down(small) as f64 / big;
    let dxmin = round_down(rmin * dy as f64);
    let dxmax = round_down(rmax * dy as f64);
    let dymin = round_down(rmin * dx as f64);
    let dymax = round_down(rmax * dx as f64);
    let vx = [x0 - dxmin, x1 - dxmin, x1 + dxmax, x0 + dxmax];
    let vy = [y0 + dymax, y1 + dymax, y1 - dymin, y0 - dymin];
    let mut edges = Vec::new();
    for k in 0..4 {
        edges.push(make_edge(vx[k], vy[k], vx[(k + 1) % 4], vy[(k + 1) % 4]));
    }
    polygon_generic(cv, &edges, ink);
}

// ── The workloads (bench.loft's, number for number) ─────────────────

fn count_ink(cv: &Canvas, ink: i64) -> i64 {
    cv.data.iter().filter(|&&c| c == ink).count() as i64
}

fn bench_hash(n: i64) -> Row {
    let (us, sink) = timed(n, || {
        let mut acc = 0.0;
        for i in 0..100000 {
            acc += seed_hash(1, i, 7);
        }
        (acc * 1000.0) as i64
    });
    let mut one = 0.0;
    for i in 0..100000 {
        one += seed_hash(1, i, 7);
    }
    Row { name: "hash", iters: n, us, px: 100000, hash: fnv(FNV_OFFSET, &[(one * 1000000.0) as i64]), sink }
}

fn bench_hair(n: i64) -> Row {
    let (us, sink) = timed(n, || hair_brush(12, 48, 1, 0.35)[0]);
    let one = hair_brush(12, 48, 1, 0.35);
    Row { name: "hair", iters: n, us, px: 576, hash: fnv(FNV_OFFSET, &one), sink }
}

fn petal_outline() -> Vec<Pt> {
    vec![pt(100.0, 100.0), pt(80.0, 140.0), pt(88.0, 193.0), pt(100.0, 200.0), pt(112.0, 193.0), pt(120.0, 140.0)]
}

fn fnv_pts(pts: &[Pt]) -> i64 {
    let mut ints = Vec::new();
    for p in pts {
        ints.push(milli(p.x));
        ints.push(milli(p.y));
    }
    fnv(FNV_OFFSET, &ints)
}

fn bench_smooth(n: i64) -> Row {
    let pts = petal_outline();
    let flags = [true; 6];
    let (us, sink) = timed(n, || smooth_pts(&pts, &flags, true).len() as i64);
    let one = smooth_pts(&pts, &flags, true);
    Row { name: "smooth", iters: n, us, px: one.len() as i64, hash: fnv_pts(&one), sink }
}

fn frond_spec() -> FrondSpec {
    FrondSpec {
        count: 24, len1: 0.1, len2: 0.1, wid1: 3.0, wid2: 3.0, ang1: 30.0, ang2: 30.0,
        mirror: false, jitter: 0.5, clump: 0.6, fray: 0.4, bow: 0.0, seed: 1, depth: 2, sub: 0.32,
    }
}

fn fnv_fronds(fs: &[Frond]) -> i64 {
    let mut ints = Vec::new();
    for f in fs {
        for p in &f.pts {
            ints.push(milli(p.x));
            ints.push(milli(p.y));
        }
        for &w in &f.wid {
            ints.push(milli(w));
        }
    }
    fnv(FNV_OFFSET, &ints)
}

fn bench_fronds(n: i64) -> Row {
    let sp = frond_spec();
    let (us, sink) = timed(n, || fronds(0.1, 0.5, 0.9, 0.5, &sp, 256, 256).len() as i64);
    let one = fronds(0.1, 0.5, 0.9, 0.5, &sp, 256, 256);
    let px: i64 = one.iter().map(|f| f.pts.len() as i64).sum();
    Row { name: "fronds", iters: n, us, px, hash: fnv_fronds(&one), sink }
}

fn hair() -> Brush {
    Brush { bw: 12, bh: 48, img: hair_brush(12, 48, 1, 0.35) }
}

fn lock_style(w: f64, tips: i64, seed: i64) -> LockStyle {
    LockStyle {
        w0: 6.0, w, swell: 0.3, body: 0.8, tips, tipvar: 0.35, spread: 8.0, seed, period: 144.0,
        dark: rgba(60, 40, 20, 255), base: rgba(120, 80, 40, 255), lit: rgba(167, 141, 115, 255),
        lx: -0.5, ly: -0.8, lz: 0.6, alpha: 1.0, flip: false,
    }
}

fn bench_lock(n: i64) -> Row {
    let br = hair();
    let st = lock_style(60.0, 3, 1);
    let xs = [18.0, 342.0];
    let ys = [90.0, 90.0];
    let (us, sink) = timed(n, || lock_layer(&xs, &ys, 360, 180, &br, &st).lw);
    let one = lock_layer(&xs, &ys, 360, 180, &br, &st);
    Row { name: "lock", iters: n, us, px: one.lw * one.lh, hash: fnv(FNV_OFFSET, &one.px), sink }
}

fn curved_path() -> Vec<Pt> {
    smooth_pts(&[pt(36.0, 54.0), pt(216.0, 90.0), pt(324.0, 162.0)], &[false, true, false], false)
}

fn bench_lock_curved(n: i64) -> Row {
    let br = hair();
    let st = lock_style(40.0, 4, 2);
    let pts = curved_path();
    let xs: Vec<f64> = pts.iter().map(|p| p.x).collect();
    let ys: Vec<f64> = pts.iter().map(|p| p.y).collect();
    let (us, sink) = timed(n, || lock_layer(&xs, &ys, 360, 180, &br, &st).lw);
    let one = lock_layer(&xs, &ys, 360, 180, &br, &st);
    Row { name: "lock_curved", iters: n, us, px: one.lw * one.lh, hash: fnv(FNV_OFFSET, &one.px), sink }
}

fn bench_composite(n: i64) -> Row {
    let br = hair();
    let st = lock_style(60.0, 3, 1);
    let lay = lock_layer(&[18.0, 342.0], &[90.0, 90.0], 360, 180, &br, &st);
    let mut cv = canvas(360, 180, rgba(128, 128, 128, 255));
    let (us, sink) = timed(n, || {
        composite_layer(&mut cv, &lay);
        cv.data[(90 * 360 + 100) as usize] & 255
    });
    let mut one = canvas(360, 180, rgba(128, 128, 128, 255));
    composite_layer(&mut one, &lay);
    Row { name: "composite", iters: n, us, px: lay.lw * lay.lh, hash: fnv(FNV_OFFSET, &one.data), sink }
}

fn circle_px(cx: f64, cy: f64, r: f64, n: i64) -> Vec<Pt> {
    let mut out = Vec::new();
    for i in 0..=n {
        let a = (2.0 * PI * i as f64) / n as f64;
        out.push(pt(cx + r * a.cos(), cy + r * a.sin()));
    }
    out
}

fn pentagram_px() -> Vec<Pt> {
    let mut out = Vec::new();
    for k in 0..=5i64 {
        let a = (-90.0 + ((k * 2) % 5) as f64 * 72.0) * PI / 180.0;
        out.push(pt(128.0 + 110.0 * a.cos(), 128.0 + 110.0 * a.sin()));
    }
    out
}

fn bench_fill(n: i64, name: &'static str, pts: &[Pt]) -> Row {
    let ink = rgba(200, 40, 40, 255);
    let mut cv = canvas(256, 256, 0);
    let (us, sink) = timed(n, || {
        fill_poly(&mut cv, pts, ink);
        cv.data[(40 * 256 + 128) as usize] & 255
    });
    Row { name, iters: n, us, px: count_ink(&cv, ink), hash: fnv(FNV_OFFSET, &cv.data), sink }
}

fn bench_wide_line(n: i64) -> Row {
    let ink = rgba(200, 40, 40, 255);
    let mut cv = canvas(256, 256, 0);
    let (us, sink) = timed(n, || {
        wide_line(&mut cv, 20.0, 30.0, 236.0, 200.0, ink, 9);
        cv.data[(115 * 256 + 128) as usize] & 255
    });
    Row { name: "wide_line", iters: n, us, px: count_ink(&cv, ink), hash: fnv(FNV_OFFSET, &cv.data), sink }
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
    print_row(&bench_hash(n));
    print_row(&bench_hair(n));
    print_row(&bench_smooth(n));
    print_row(&bench_fronds(n));
    print_row(&bench_lock(n));
    print_row(&bench_lock_curved(n));
    print_row(&bench_composite(n));
    print_row(&bench_fill(n, "fill_circle", &circle_px(128.0, 128.0, 100.0, 28)));
    print_row(&bench_fill(n, "fill_star", &pentagram_px()));
    print_row(&bench_wide_line(n));
}
