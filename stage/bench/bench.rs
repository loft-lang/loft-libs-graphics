// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// stage-reference — the pure-Rust twin of the `stage` package's performance pass
// (bench/bench.loft), one file built with `rustc -O`.  It computes the SAME workloads with
// the SAME arithmetic, in the same order, and prints the same rows, hash included: a row
// whose hash matches the loft build's is a like-for-like comparison, and only then is a
// routine's loft time judged against it (@FR-Perf-Weight).  No dependencies and no
// cleverness — plain idiomatic Rust, the speed an industry implementation reaches without
// effort, which is exactly what the bar should be.
//
//     rustc -O --edition=2021 bench/bench.rs -o bench/.build/stats_rs && bench/.build/stats_rs --n 2
//
// Each routine is a port of its loft original in src/stage.loft (and graphics' canvas):
// integers are i64 like loft's, a colour is an i64 in a Vec, and every branch the library
// takes is taken here.  Where loft reads a pixel through `get_pixel` / `set_pixel` with a
// bounds test per pixel, this twin clamps the span to the canvas once — off-canvas pixels
// are no-ops in the library, so the pictures are identical.  `black_box` guards each op's
// INPUT (the repetition number) and the sink — never anything inside a kernel.
use std::hint::black_box;
use std::time::Instant;

const FNV_OFFSET: i64 = 2166136261;
const FNV_PRIME: i64 = 16777619;

const SCENE_NODES: i64 = 5000;
const RENDER_NODES: i64 = 200;
const RENDER_W: i64 = 960;
const RENDER_H: i64 = 540;
const LIGHT_W: i64 = 640;
const LIGHT_H: i64 = 360;
const BLUR_W: i64 = 320;
const BLUR_H: i64 = 240;
const BLUR_R: i64 = 3;

const LOOP: i64 = 0;
const ONCE: i64 = 1;
const PINGPONG: i64 = 2;
const HUD_NONE: i64 = 1000000;
const LIGHT_FALLOFF: f64 = 1.0;

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

// ── graphics::Canvas ────────────────────────────────────────────────

struct Canvas {
    width: i64,
    height: i64,
    data: Vec<i64>,
}

fn canvas(w: i64, h: i64, fill: i64) -> Canvas {
    Canvas { width: w, height: h, data: vec![fill; (w * h) as usize] }
}

fn rgba(r: i64, g: i64, b: i64, a: i64) -> i64 {
    ((a & 255) << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255)
}

impl Canvas {
    fn get_pixel(&self, x: i64, y: i64) -> i64 {
        if x < 0 || x >= self.width || y < 0 || y >= self.height {
            0
        } else {
            self.data[(y * self.width + x) as usize]
        }
    }
}

// ── The stage ───────────────────────────────────────────────────────

#[derive(Clone, Default)]
struct Node {
    parent: i64,
    x: f64,
    y: f64,
    rot: f64,
    sx: f64,
    sy: f64,
    ox: f64,
    oy: f64,
    w: f64,
    h: f64,
    colour: i64,
    visible: bool,
    alpha: f64,
    clips: bool,
    clipped: bool,
    cx0: f64,
    cy0: f64,
    cx1: f64,
    cy1: f64,
    mirror: bool,
    seq: i64,
    elapsed: i64,
    cols: i64,
    rows: i64,
    sway: f64,
    phase: f64,
    cue: f64,
    layer: i64,
    depth: f64,
    a: f64,
    b: f64,
    c: f64,
    d: f64,
    tx: f64,
    ty: f64,
}

struct Place {
    x: f64,
    y: f64,
    rot: f64,
    sx: f64,
    sy: f64,
    ox: f64,
    oy: f64,
    w: f64,
    h: f64,
    colour: i64,
    visible: bool,
    alpha: f64,
    clips: bool,
    seq: i64,
    cols: i64,
    rows: i64,
    sway: f64,
    layer: i64,
    depth: f64,
}

impl Default for Place {
    fn default() -> Place {
        Place { x: 0.0, y: 0.0, rot: 0.0, sx: 1.0, sy: 1.0, ox: 0.0, oy: 0.0, w: 0.0, h: 0.0,
                colour: 0, visible: true, alpha: 1.0, clips: false, seq: -1, cols: 1, rows: 1,
                sway: 0.0, layer: 0, depth: 0.0 }
    }
}

struct Sequence {
    first: i64,
    count: i64,
    fps: f64,
    mode: i64,
}

struct Light {
    x: f64,
    y: f64,
    r: f64,
    colour: i64,
    power: f64,
}

struct Stage {
    nodes: Vec<Node>,
    order: Vec<i64>,
    cue: bool,
    cue_near: f64,
    cue_far: f64,
    cue_scale: f64,
    cue_haze: f64,
    haze_colour: i64,
    lights: Vec<Light>,
    ambient: f64,
    light_map: bool,
    cam_x: f64,
    cam_y: f64,
    time: f64,
    seqs: Vec<Sequence>,
}

struct P2 {
    x: f64,
    y: f64,
}

struct Bounds {
    x0: f64,
    y0: f64,
    x1: f64,
    y1: f64,
}

fn stage_new() -> Stage {
    Stage { nodes: Vec::new(), order: Vec::new(), cue: false, cue_near: 0.0, cue_far: 1.0,
            cue_scale: 1.0, cue_haze: 0.0, haze_colour: 0, lights: Vec::new(), ambient: 1.0,
            light_map: false, cam_x: 0.0, cam_y: 0.0, time: 0.0,
            seqs: Vec::new() }
}

impl Stage {
    fn node_add(&mut self, parent: i64, p: &Place) -> i64 {
        let idx = self.nodes.len() as i64;
        if parent >= idx || parent < -1 {
            return -1;
        }
        self.nodes.push(Node {
            parent, x: p.x, y: p.y, rot: p.rot, sx: p.sx, sy: p.sy, ox: p.ox, oy: p.oy,
            w: p.w, h: p.h, colour: p.colour, visible: p.visible, alpha: p.alpha,
            clips: p.clips, seq: p.seq, cols: p.cols, rows: p.rows, sway: p.sway,
            layer: p.layer, depth: p.depth, a: 1.0, d: 1.0, ..Default::default()
        });
        idx
    }

    fn depth_cue(&mut self, near: f64, far: f64, far_scale: f64, far_haze: f64, haze: i64) {
        self.cue = true;
        self.cue_near = near;
        self.cue_far = far;
        self.cue_scale = far_scale;
        self.cue_haze = far_haze;
        self.haze_colour = haze;
    }

    fn add_sequence(&mut self, first: i64, count: i64, fps: f64, mode: i64) -> i64 {
        self.seqs.push(Sequence { first, count: if count < 1 { 1 } else { count }, fps, mode });
        self.seqs.len() as i64 - 1
    }

    fn advance(&mut self, dt_us: i64) {
        for n in self.nodes.iter_mut() {
            if n.seq >= 0 {
                n.elapsed += dt_us;
            }
        }
    }

    fn cue_of(&self, depth: f64) -> f64 {
        if !self.cue {
            return 0.0;
        }
        let span = self.cue_far - self.cue_near;
        if span > -0.000001 && span < 0.000001 {
            return 0.0;
        }
        let t = (depth - self.cue_near) / span;
        if t < 0.0 {
            return 0.0;
        }
        if t > 1.0 {
            return 1.0;
        }
        t
    }

    fn world_point(&self, idx: usize, lx: f64, ly: f64) -> P2 {
        let n = &self.nodes[idx];
        P2 { x: n.a * lx + n.c * ly + n.tx, y: n.b * lx + n.d * ly + n.ty }
    }

    fn world_origin(&self, idx: usize) -> P2 {
        let n = &self.nodes[idx];
        self.world_point(idx, n.ox, n.oy)
    }

    fn bounds_of(&self, idx: usize) -> Bounds {
        let n = &self.nodes[idx];
        let p0 = self.world_point(idx, 0.0, 0.0);
        let p1 = self.world_point(idx, n.w, 0.0);
        let p2 = self.world_point(idx, 0.0, n.h);
        let p3 = self.world_point(idx, n.w, n.h);
        let (mut x0, mut x1, mut y0, mut y1) = (p0.x, p0.x, p0.y, p0.y);
        for p in [&p1, &p2, &p3] {
            if p.x < x0 { x0 = p.x; }
            if p.x > x1 { x1 = p.x; }
        }
        for p in [&p1, &p2, &p3] {
            if p.y < y0 { y0 = p.y; }
            if p.y > y1 { y1 = p.y; }
        }
        Bounds { x0, y0, x1, y1 }
    }

    fn compose(&mut self) {
        for i in 0..self.nodes.len() {
            let (x, y, rot, sx, sy, ox, oy, mirror, depth, parent, clips) = {
                let n = &self.nodes[i];
                (n.x, n.y, n.rot, n.sx, n.sy, n.ox, n.oy, n.mirror, n.depth, n.parent, n.clips)
            };
            let cs = rot.cos();
            let sn = rot.sin();
            let cue = self.cue_of(depth);
            let k = 1.0 + (self.cue_scale - 1.0) * cue;
            let mx = if mirror { -1.0 } else { 1.0 };
            let la = cs * sx * k * mx;
            let lb = sn * sx * k * mx;
            let lc = -sn * sy * k;
            let ld = cs * sy * k;
            let ltx = x - (la * ox + lc * oy);
            let lty = y - (lb * ox + ld * oy);
            let (mut cl, mut cx0, mut cy0, mut cx1, mut cy1) = (false, 0.0, 0.0, 0.0, 0.0);
            let world = if parent < 0 {
                [la, lb, lc, ld, ltx, lty]
            } else {
                let p = &self.nodes[parent as usize];
                cl = p.clipped;
                cx0 = p.cx0;
                cy0 = p.cy0;
                cx1 = p.cx1;
                cy1 = p.cy1;
                [p.a * la + p.c * lb, p.b * la + p.d * lb, p.a * lc + p.c * ld,
                 p.b * lc + p.d * ld, p.a * ltx + p.c * lty + p.tx, p.b * ltx + p.d * lty + p.ty]
            };
            {
                let cur = &mut self.nodes[i];
                cur.phase = x * 0.37 + y * 0.11;
                cur.cue = cue;
                cur.a = world[0];
                cur.b = world[1];
                cur.c = world[2];
                cur.d = world[3];
                cur.tx = world[4];
                cur.ty = world[5];
            }
            if clips {
                let b = self.bounds_of(i);
                if !cl {
                    cl = true;
                    cx0 = b.x0;
                    cy0 = b.y0;
                    cx1 = b.x1;
                    cy1 = b.y1;
                } else {
                    if b.x0 > cx0 { cx0 = b.x0; }
                    if b.y0 > cy0 { cy0 = b.y0; }
                    if b.x1 < cx1 { cx1 = b.x1; }
                    if b.y1 < cy1 { cy1 = b.y1; }
                }
            }
            let cur = &mut self.nodes[i];
            cur.clipped = cl;
            cur.cx0 = cx0;
            cur.cy0 = cy0;
            cur.cx1 = cx1;
            cur.cy1 = cy1;
        }
        self.order_nodes();
    }

    fn after(&self, x: i64, y: i64) -> bool {
        let a = &self.nodes[x as usize];
        let b = &self.nodes[y as usize];
        if a.layer != b.layer {
            return a.layer > b.layer;
        }
        if a.depth != b.depth {
            return a.depth < b.depth;
        }
        x > y
    }

    // The library's bottom-up merge sort, stable by construction.
    fn order_nodes(&mut self) {
        let n = self.nodes.len();
        let mut src: Vec<i64> = (0..n as i64).collect();
        let mut width = 1;
        while width < n {
            let mut dst: Vec<i64> = Vec::new();
            let mut lo = 0;
            while lo < n {
                let mid = (lo + width).min(n);
                let hi = (lo + 2 * width).min(n);
                let (mut a, mut b) = (lo, mid);
                while a < mid || b < hi {
                    let take_a = if a >= mid {
                        false
                    } else if b < hi {
                        !self.after(src[a], src[b])
                    } else {
                        true
                    };
                    if take_a {
                        dst.push(src[a]);
                        a += 1;
                    } else {
                        dst.push(src[b]);
                        b += 1;
                    }
                }
                lo = hi;
            }
            src = dst;
            width *= 2;
        }
        self.order = src;
    }

    fn frame_of(&self, idx: usize) -> i64 {
        let n = &self.nodes[idx];
        if n.seq < 0 || n.seq >= self.seqs.len() as i64 {
            return 0;
        }
        let q = &self.seqs[n.seq as usize];
        let mut raw = ((n.elapsed as f64) * q.fps / 1000000.0) as i64;
        if raw < 0 {
            raw = 0;
        }
        if q.count <= 1 {
            return q.first;
        }
        if q.mode == ONCE {
            return q.first + raw.min(q.count - 1);
        }
        if q.mode == PINGPONG {
            let period = q.count * 2 - 2;
            let pp = raw.checked_rem(period).unwrap_or(0);
            if pp < q.count {
                return q.first + pp;
            }
            return q.first + period - pp;
        }
        q.first + raw.checked_rem(q.count).unwrap_or(0)
    }

    fn sway_of(&self, idx: usize) -> f64 {
        let n = &self.nodes[idx];
        if n.sway == 0.0 {
            return 0.0;
        }
        n.sway * (self.time + n.phase).sin()
    }

    fn light_rgb_at(&self, wx: f64, wy: f64) -> (f64, f64, f64) {
        let (mut lr, mut lg, mut lb) = (self.ambient, self.ambient, self.ambient);
        for l in &self.lights {
            let a = light_reach(l, wx, wy);
            if a <= 0.0 {
                continue;
            }
            lr += (((l.colour >> 16) & 255) as f64 / 255.0) * a;
            lg += (((l.colour >> 8) & 255) as f64 / 255.0) * a;
            lb += ((l.colour & 255) as f64 / 255.0) * a;
        }
        (clamp01(lr), clamp01(lg), clamp01(lb))
    }

    fn lit_colour(&self, idx: usize) -> i64 {
        let n = &self.nodes[idx];
        if self.light_map {
            return n.colour;
        }
        if self.lights.is_empty() && self.ambient >= 1.0 {
            return n.colour;
        }
        let o = self.world_origin(idx);
        let (lr, lg, lb) = self.light_rgb_at(o.x, o.y);
        pack_channel(n.colour >> 16, lr) << 16 | pack_channel(n.colour >> 8, lg) << 8
            | pack_channel(n.colour, lb)
    }

    // A missing layer is the plain one: parallax 1.0, no fog, no blur.
    fn view_offset(&self) -> P2 {
        let f = 1.0;
        P2 { x: -self.cam_x * f + 0.0, y: -self.cam_y * f + 0.0 }
    }

    fn render_stage(&self, cv: &mut Canvas) {
        // The light map is off, so one band: every layer, blurring (radius 0) as each ends.
        let (lo, hi) = (-HUD_NONE, HUD_NONE);
        let mut prev = -HUD_NONE;
        let mut started = false;
        for &oi in &self.order {
            let ln = self.nodes[oi as usize].layer;
            if ln < lo || ln >= hi {
                continue;
            }
            if started && ln == prev {
                continue;
            }
            self.paint_band(cv, ln, ln + 1);
            prev = ln;
            started = true;
        }
    }

    fn paint_band(&self, cv: &mut Canvas, lo: i64, hi: i64) {
        for &oi in &self.order {
            let i = oi as usize;
            let n = &self.nodes[i];
            if n.layer < lo || n.layer >= hi {
                continue;
            }
            if !n.visible || n.w <= 0.0 || n.h <= 0.0 {
                continue;
            }
            if n.alpha <= 0.0 {
                continue;
            }
            let ia = ((n.alpha * 255.0 + 0.5) as i64).clamp(0, 255);
            let hz = ((n.cue * self.cue_haze * 255.0 + 0.5) as i64).clamp(0, 255);
            let lc = self.lit_colour(i);
            let hc = self.haze_colour;
            let cr = mix((lc >> 16) & 255, (hc >> 16) & 255, hz);
            let cg = mix((lc >> 8) & 255, (hc >> 8) & 255, hz);
            let cb = mix(lc & 255, hc & 255, hz);
            // Layer fog density is 0 on a layer nobody configured, so `fg` is 0.
            let sr = (cr * ia + 127) / 255;
            let sg = (cg * ia + 127) / 255;
            let sb = (cb * ia + 127) / 255;
            let o = self.world_point(i, 0.0, 0.0);
            let far = self.world_point(i, n.w, n.h);
            let off = self.view_offset();
            let sw = self.sway_of(i);
            let mut x0 = (o.x + off.x + sw) as i64;
            let mut y0 = (o.y + off.y) as i64;
            let mut x1 = (far.x + off.x + sw) as i64;
            let mut y1 = (far.y + off.y) as i64;
            if x1 < x0 { std::mem::swap(&mut x0, &mut x1); }
            if y1 < y0 { std::mem::swap(&mut y0, &mut y1); }
            if n.clipped {
                x0 = x0.max((n.cx0 + off.x) as i64);
                y0 = y0.max((n.cy0 + off.y) as i64);
                x1 = x1.min((n.cx1 + off.x) as i64);
                y1 = y1.min((n.cy1 + off.y) as i64);
            }
            // Off-canvas pixels are no-ops in the library; clamp the span once.
            let (x0, x1) = (x0.max(0), x1.min(cv.width));
            let (y0, y1) = (y0.max(0), y1.min(cv.height));
            if x0 >= x1 {
                continue;
            }
            let w = cv.width;
            for py in y0..y1 {
                let row = &mut cv.data[(py * w + x0) as usize..(py * w + x1) as usize];
                for d in row.iter_mut() {
                    let dr = (*d >> 16) & 255;
                    let dg = (*d >> 8) & 255;
                    let db = *d & 255;
                    *d = 0xff000000 | (over(sr, dr, ia) << 16) | (over(sg, dg, ia) << 8)
                        | over(sb, db, ia);
                }
            }
        }
    }

    fn composite_light(&self, cv: &mut Canvas) {
        if !self.light_map {
            return;
        }
        let off = self.view_offset();
        let w = cv.width;
        for py in 0..cv.height {
            for px in 0..w {
                let (lr, lg, lb) = self.light_rgb_at((px as f64) - off.x, (py as f64) - off.y);
                let d = &mut cv.data[(py * w + px) as usize];
                *d = (*d & 0xff000000) | (scale_channel((*d >> 16) & 255, lr) << 16)
                    | (scale_channel((*d >> 8) & 255, lg) << 8) | scale_channel(*d & 255, lb);
            }
        }
    }

    fn pack_instances(&self) -> Vec<f32> {
        let mut out: Vec<f32> = Vec::new();
        for &oi in &self.order {
            let i = oi as usize;
            let n = &self.nodes[i];
            if !n.visible || n.w <= 0.0 || n.h <= 0.0 {
                continue;
            }
            let o = self.world_point(i, 0.0, 0.0);
            let lc = self.lit_colour(i);
            out.extend_from_slice(&[
                (n.a * n.w) as f32, (n.b * n.w) as f32, (n.c * n.h) as f32, (n.d * n.h) as f32,
                o.x as f32, o.y as f32,
                self.frame_of(i) as f32, n.cols as f32, n.rows as f32, 0.0f32,
                ((((lc >> 16) & 255) as f64 / 255.0) * n.alpha) as f32,
                ((((lc >> 8) & 255) as f64 / 255.0) * n.alpha) as f32,
                (((lc & 255) as f64 / 255.0) * n.alpha) as f32,
                n.alpha as f32, n.sway as f32, n.phase as f32,
            ]);
        }
        out
    }

    fn draw_list(&self) -> Vec<DrawRect> {
        let mut out: Vec<DrawRect> = Vec::new();
        for &oi in &self.order {
            let i = oi as usize;
            let n = &self.nodes[i];
            if !n.visible {
                continue;
            }
            if n.w <= 0.0 || n.h <= 0.0 {
                continue;
            }
            let o = self.world_point(i, 0.0, 0.0);
            let far = self.world_point(i, n.w, n.h);
            let (mut x0, mut y0) = (o.x, o.y);
            let mut w = far.x - x0;
            let mut h = far.y - y0;
            if w < 0.0 { x0 = far.x; w = -w; }
            if h < 0.0 { y0 = far.y; h = -h; }
            out.push(DrawRect {
                rect: UiRect { x: x0 as i64, y: y0 as i64, w: w as i64, h: h as i64 },
                colour: n.colour,
            });
        }
        out
    }
}

struct UiRect {
    x: i64,
    y: i64,
    w: i64,
    h: i64,
}

struct DrawRect {
    rect: UiRect,
    colour: i64,
}

fn light_reach(l: &Light, px: f64, py: f64) -> f64 {
    if l.r <= 0.0 {
        return 0.0;
    }
    let dx = px - l.x;
    let dy = py - l.y;
    let d = (dx * dx + dy * dy).sqrt();
    let mut a = 1.0 - d / l.r;
    if a <= 0.0 {
        return 0.0;
    }
    if a > 1.0 {
        return 1.0;
    }
    if LIGHT_FALLOFF != 1.0 {
        a = a.powf(LIGHT_FALLOFF);
    }
    a * l.power
}

fn clamp01(v: f64) -> f64 {
    if v < 0.0 {
        return 0.0;
    }
    if v > 1.0 {
        return 1.0;
    }
    v
}

fn pack_channel(material: i64, level: f64) -> i64 {
    let k = level.clamp(0.0, 1.0);
    let v = (((material & 255) as f64) * k + 0.5) as i64;
    v.clamp(0, 255)
}

fn scale_channel(c: i64, level: f64) -> i64 {
    let v = ((c as f64) * level + 0.5) as i64;
    v.clamp(0, 255)
}

fn mix(a: i64, b: i64, t: i64) -> i64 {
    (a * (255 - t) + b * t + 127) / 255
}

fn over(src: i64, dst: i64, ia: i64) -> i64 {
    (src * 255 + dst * (255 - ia) + 127) / 255
}

fn blur_region(cv: &mut Canvas, x0: i64, y0: i64, x1: i64, y1: i64, radius: i64) {
    if radius <= 0 {
        return;
    }
    let w = x1 - x0;
    let h = y1 - y0;
    if w <= 0 || h <= 0 {
        return;
    }
    let mut src: Vec<i64> = Vec::with_capacity((w * h) as usize);
    for py in y0..y1 {
        for px in x0..x1 {
            src.push(cv.get_pixel(px, py));
        }
    }
    let cw = cv.width;
    for py in 0..h {
        for px in 0..w {
            let (mut sr, mut sg, mut sb, mut n) = (0i64, 0i64, 0i64, 0i64);
            for ky in (py - radius)..(py + radius + 1) {
                let jy = ky.clamp(0, h - 1);
                for kx in (px - radius)..(px + radius + 1) {
                    let jx = kx.clamp(0, w - 1);
                    let v = src[(jy * w + jx) as usize];
                    sr += (v >> 16) & 255;
                    sg += (v >> 8) & 255;
                    sb += v & 255;
                    n += 1;
                }
            }
            cv.data[((y0 + py) * cw + x0 + px) as usize] = 0xff000000
                | (((sr + n / 2) / n) << 16) | (((sg + n / 2) / n) << 8) | ((sb + n / 2) / n);
        }
    }
}

// ── The scenes ──────────────────────────────────────────────────────

fn big_scene() -> Stage {
    let mut st = stage_new();
    st.depth_cue(0.0, 400.0, 0.6, 0.3, 0x405060);
    st.add_sequence(0, 8, 12.0, LOOP);
    st.add_sequence(8, 5, 10.0, PINGPONG);
    for i in 0..SCENE_NODES {
        let g = i % 5;
        let parent = if g == 0 { -1 } else { i - 1 };
        st.node_add(parent, &Place {
            x: if g == 0 { ((i * 37) % 1900) as f64 } else { 6.0 + g as f64 },
            y: if g == 0 { ((i * 53) % 1000) as f64 } else { 4.0 },
            rot: ((i % 7) as f64) * 0.05,
            sx: 1.0 + ((i % 3) as f64) * 0.25,
            sy: 1.0 - ((i % 4) as f64) * 0.1,
            ox: ((i % 4) as f64) * 2.0,
            oy: ((i % 3) as f64) * 3.0,
            w: if i % 23 == 0 { 0.0 } else { 16.0 + ((i % 5) * 4) as f64 },
            h: 20.0 + ((i % 3) * 6) as f64,
            colour: (i * 2654435761) & 0xFFFFFF,
            visible: i % 17 != 0,
            alpha: 1.0 - ((i % 4) as f64) * 0.2,
            clips: g == 1,
            seq: if i % 6 == 0 { (i / 6) % 2 } else { -1 },
            cols: 4,
            rows: 4,
            sway: ((i % 3) as f64) * 0.5,
            layer: i % 3,
            depth: ((i * 31) % 400) as f64,
        });
    }
    st.advance(123457);
    st.compose();
    st
}

fn render_scene() -> Stage {
    let mut st = stage_new();
    st.depth_cue(0.0, 50.0, 1.0, 0.4, 0x304050);
    for i in 0..RENDER_NODES {
        let root = i % 2 == 0;
        st.node_add(if root { -1 } else { i - 1 }, &Place {
            x: if root { (8 + (i * 37) % 850) as f64 } else { 12.0 },
            y: if root { (8 + (i * 53) % 440) as f64 } else { 10.0 },
            w: 64.0,
            h: 64.0,
            colour: (i * 2654435761) & 0xFFFFFF,
            alpha: 1.0 - ((i % 4) as f64) * 0.15,
            layer: if i % 10 == 9 { 1 } else { 0 },
            depth: ((i * 13) % 50) as f64,
            ..Default::default()
        });
    }
    st.compose();
    st
}

fn pattern_canvas(w: i64, h: i64) -> Canvas {
    let mut cv = canvas(w, h, 0);
    for y in 0..h {
        for x in 0..w {
            cv.data[(y * w + x) as usize] = rgba((x * 3) & 255, (y * 5) & 255, (x ^ y) & 255, 255);
        }
    }
    cv
}

fn timed<F: FnMut(i64) -> i64>(n: i64, mut f: F) -> (i64, i64) {
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        sink = sink.wrapping_add(f(black_box(r)));
    }
    (t0.elapsed().as_micros() as i64, black_box(sink))
}

// ── The rows ────────────────────────────────────────────────────────

fn bench_render(n: i64) -> Row {
    let mut st = render_scene();
    let mut cv = canvas(RENDER_W, RENDER_H, rgba(20, 30, 40, 255));
    let (us, sink) = timed(n, |r| {
        st.cam_x = (r & 1) as f64;
        st.render_stage(&mut cv);
        cv.get_pixel(100, 100) & 255
    });
    st.cam_x = 0.0;
    let mut one = canvas(RENDER_W, RENDER_H, rgba(20, 30, 40, 255));
    st.render_stage(&mut one);
    Row { name: "render_stage", iters: n, us, px: RENDER_NODES * 64 * 64,
          hash: fnv(FNV_OFFSET, &one.data), sink }
}

fn fnv_composed(st: &Stage) -> i64 {
    let mut ints: Vec<i64> = Vec::new();
    for n in &st.nodes {
        ints.extend_from_slice(&[micro(n.a), micro(n.b), micro(n.c), micro(n.d), micro(n.tx),
                                 micro(n.ty), micro(n.cue), micro(n.phase),
                                 if n.clipped { 1 } else { 0 }, micro(n.cx0), micro(n.cy0),
                                 micro(n.cx1), micro(n.cy1)]);
    }
    ints.extend_from_slice(&st.order);
    fnv(FNV_OFFSET, &ints)
}

fn bench_compose(n: i64) -> Row {
    let mut st = big_scene();
    let (us, sink) = timed(n, |r| {
        st.nodes[0].x = (r & 1) as f64;
        st.compose();
        st.order[7]
    });
    st.nodes[0].x = 0.0;
    st.compose();
    Row { name: "compose", iters: n, us, px: SCENE_NODES, hash: fnv_composed(&st), sink }
}

fn bench_pack(n: i64) -> Row {
    let mut st = big_scene();
    let (us, sink) = timed(n, |r| {
        st.nodes[1].alpha = 0.5 + ((r & 1) as f64) * 0.25;
        st.pack_instances().len() as i64
    });
    st.nodes[1].alpha = 0.5;
    let one = st.pack_instances();
    let ints: Vec<i64> = one.iter().map(|&x| micro(x as f64)).collect();
    Row { name: "pack_instances", iters: n, us, px: one.len() as i64 / 16,
          hash: fnv(FNV_OFFSET, &ints), sink }
}

fn bench_blur(n: i64) -> Row {
    let mut cv = pattern_canvas(BLUR_W, BLUR_H);
    let (us, sink) = timed(n, |_r| {
        blur_region(&mut cv, 0, 0, BLUR_W, BLUR_H, black_box(BLUR_R));
        cv.get_pixel(100, 100) & 255
    });
    let mut one = pattern_canvas(BLUR_W, BLUR_H);
    blur_region(&mut one, 0, 0, BLUR_W, BLUR_H, BLUR_R);
    Row { name: "blur_region", iters: n, us, px: BLUR_W * BLUR_H,
          hash: fnv(FNV_OFFSET, &one.data), sink }
}

fn bench_draw_list(n: i64) -> Row {
    let mut st = big_scene();
    let base = st.nodes[1].colour;
    let (us, sink) = timed(n, |r| {
        st.nodes[1].colour = base ^ (r & 1);
        st.draw_list().len() as i64
    });
    st.nodes[1].colour = base;
    let one = st.draw_list();
    let mut ints: Vec<i64> = Vec::new();
    for d in &one {
        ints.extend_from_slice(&[d.rect.x, d.rect.y, d.rect.w, d.rect.h, d.colour]);
    }
    Row { name: "draw_list", iters: n, us, px: one.len() as i64, hash: fnv(FNV_OFFSET, &ints),
          sink }
}

fn light_scene() -> Stage {
    let mut st = stage_new();
    st.light_map = true;
    st.ambient = 0.2;
    for k in 0..8i64 {
        st.lights.push(Light { x: ((k * 83) % 640) as f64, y: ((k * 47) % 360) as f64,
                               r: 150.0 + ((k % 3) * 50) as f64,
                               colour: (k * 2654435761) & 0xFFFFFF,
                               power: 0.8 + ((k % 4) as f64) * 0.2 });
    }
    st
}

fn bench_light(n: i64) -> Row {
    let mut st = light_scene();
    let mut cv = pattern_canvas(LIGHT_W, LIGHT_H);
    let (us, sink) = timed(n, |r| {
        st.lights[0].x = (r & 1) as f64;
        st.lights[0].y = 0.0;
        st.composite_light(&mut cv);
        cv.get_pixel(100, 100) & 255
    });
    st.lights[0].x = 0.0;
    st.lights[0].y = 0.0;
    let mut one = pattern_canvas(LIGHT_W, LIGHT_H);
    st.composite_light(&mut one);
    Row { name: "composite_light", iters: n, us, px: LIGHT_W * LIGHT_H,
          hash: fnv(FNV_OFFSET, &one.data), sink }
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
    let rows = [bench_render(n), bench_compose(n), bench_pack(n), bench_blur(n),
                bench_draw_list(n), bench_light(n)];
    let mut sink = 0i64;
    for row in &rows {
        print_row(row);
        sink = sink.wrapping_add(row.sink);
    }
    println!("time: {}ms sink={}", t0.elapsed().as_millis(), sink);
}
