// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// text2d-reference — the pure-Rust twin of the `text2d` package's performance pass
// (bench/bench.loft), one file built with `rustc -O`.  It computes the SAME workloads with
// the SAME algorithms, in the same order, and prints the same rows, hash included: a row
// whose hash matches the loft build's is a like-for-like comparison, and only then is a
// routine's loft time judged against it.  No dependencies and no cleverness — plain
// idiomatic Rust, the speed an industry implementation reaches without effort.
//
//     rustc -O --edition=2021 bench/bench.rs -o bench/.build/stats_rs && bench/.build/stats_rs --n 50
//
// A port of src/text2d.loft and src/face.loft, with the graphics package's `Canvas`
// (`set_pixel` stores when in range, `get_pixel` answers 0 out of range).  The glyph
// search is the library's bisection, the atlas lookup its LINEAR scan of the cell codes,
// `wrap` its greedy per-character walk measured in characters.  The face is a pair of
// static tables rather than a vector built per call.  `black_box` guards the op's INPUT
// and the sink, never anything inside the kernel.
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

// ── graphics: the canvas ────────────────────────────────────────────

struct Canvas {
    w: i64,
    h: i64,
    data: Vec<i64>,
}

fn canvas(w: i64, h: i64, fill: i64) -> Canvas {
    Canvas { w, h, data: vec![fill; (w * h) as usize] }
}

impl Canvas {
    fn get_pixel(&self, x: i64, y: i64) -> i64 {
        if x < 0 || x >= self.w || y < 0 || y >= self.h {
            0
        } else {
            self.data[(y * self.w + x) as usize]
        }
    }

    fn set_pixel(&mut self, x: i64, y: i64, colour: i64) {
        if x >= 0 && x < self.w && y >= 0 && y < self.h {
            self.data[(y * self.w + x) as usize] = colour;
        }
    }
}

// ── face.loft ───────────────────────────────────────────────────────

const GLYPH_W: i64 = 5;
const GLYPH_H: i64 = 7;

const FACE_CODES: [i64; 56] = [
    32, 33, 35, 37, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 65, 66, 67,
    68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
    84, 85, 86, 87, 88, 89, 90, 95,
];

const FACE_ROWS: [i64; 392] = [
    0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 0, 4, 10, 31,
    10, 10, 31, 10, 0, 17, 18, 4, 4, 4, 9, 17, 4, 4, 0, 0,
    0, 0, 0, 2, 4, 8, 8, 8, 4, 2, 8, 4, 2, 2, 2, 4,
    8, 0, 21, 14, 31, 14, 21, 0, 0, 4, 4, 31, 4, 4, 0, 0,
    0, 0, 0, 6, 6, 8, 0, 0, 0, 31, 0, 0, 0, 0, 0, 0,
    0, 0, 6, 6, 1, 1, 2, 4, 8, 16, 16, 14, 17, 19, 21, 25,
    17, 14, 4, 12, 4, 4, 4, 4, 14, 14, 17, 1, 2, 4, 8, 31,
    31, 2, 4, 2, 1, 17, 14, 2, 6, 10, 18, 31, 2, 2, 31, 16,
    30, 1, 1, 17, 14, 6, 8, 16, 30, 17, 17, 14, 31, 1, 2, 4,
    8, 8, 8, 14, 17, 17, 14, 17, 17, 14, 14, 17, 17, 15, 1, 2,
    12, 0, 6, 6, 0, 6, 6, 0, 0, 6, 6, 0, 6, 6, 8, 2,
    4, 8, 16, 8, 4, 2, 0, 0, 31, 0, 31, 0, 0, 8, 4, 2,
    1, 2, 4, 8, 14, 17, 1, 2, 4, 0, 4, 14, 17, 17, 31, 17,
    17, 17, 30, 17, 17, 30, 17, 17, 30, 14, 17, 16, 16, 16, 17, 14,
    28, 18, 17, 17, 17, 18, 28, 31, 16, 16, 30, 16, 16, 31, 31, 16,
    16, 30, 16, 16, 16, 14, 17, 16, 19, 17, 17, 15, 17, 17, 17, 31,
    17, 17, 17, 14, 4, 4, 4, 4, 4, 14, 1, 1, 1, 1, 17, 17,
    14, 17, 18, 20, 24, 20, 18, 17, 16, 16, 16, 16, 16, 16, 31, 17,
    27, 21, 17, 17, 17, 17, 17, 25, 21, 19, 17, 17, 17, 14, 17, 17,
    17, 17, 17, 14, 30, 17, 17, 30, 16, 16, 16, 14, 17, 17, 17, 21,
    18, 13, 30, 17, 17, 30, 20, 18, 17, 15, 16, 16, 14, 1, 1, 30,
    31, 4, 4, 4, 4, 4, 4, 17, 17, 17, 17, 17, 17, 14, 17, 17,
    17, 17, 17, 10, 4, 17, 17, 17, 17, 21, 27, 17, 17, 17, 10, 4,
    10, 17, 17, 17, 17, 10, 4, 4, 4, 4, 31, 1, 2, 4, 8, 16,
    31, 0, 0, 0, 0, 0, 0, 31,
];

// ── text2d.loft: the blitter ────────────────────────────────────────

fn fold(code: i64) -> i64 {
    if (97..=122).contains(&code) { code - 32 } else { code }
}

/// Where the glyph for `code` starts in FACE_ROWS, or -1 — the library's bisection.
fn glyph_at(code: i64) -> i64 {
    let c = fold(code);
    let (mut lo, mut hi) = (0i64, FACE_CODES.len() as i64 - 1);
    while lo <= hi {
        let mid = (lo + hi) / 2;
        let at = FACE_CODES[mid as usize];
        if at == c {
            return mid * GLYPH_H;
        }
        if at < c { lo = mid + 1 } else { hi = mid - 1 }
    }
    -1
}

fn blit_glyph(cv: &mut Canvas, code: i64, x: i64, y: i64, colour: i64, scale: i64) {
    let at = glyph_at(code);
    if at < 0 {
        return;
    }
    for ry in 0..GLYPH_H {
        let bits = FACE_ROWS[(at + ry) as usize];
        for rx in 0..GLYPH_W {
            if bits & (1 << (GLYPH_W - 1 - rx)) != 0 {
                for sy in 0..scale {
                    for sx in 0..scale {
                        cv.set_pixel(x + rx * scale + sx, y + ry * scale + sy, colour);
                    }
                }
            }
        }
    }
}

fn write_text(cv: &mut Canvas, s: &str, x: i64, y: i64, colour: i64, scale: i64) -> i64 {
    let sc = scale.max(1);
    let mut pen = x;
    for ch in s.chars() {
        blit_glyph(cv, ch as i64, pen, y, colour, sc);
        pen += (GLYPH_W + 1) * sc;
    }
    pen - x - sc
}

// ── text2d.loft: the atlas ──────────────────────────────────────────

struct Atlas {
    cv: Canvas,
    codes: Vec<i64>,
    scale: i64,
    cols: i64,
    next: i64,
    writes: i64,
}

#[derive(Clone, Copy)]
struct Quad {
    x: i64,
    y: i64,
    cell: i64,
}

fn atlas_new(scale: i64, capacity: i64) -> Atlas {
    let sc = scale.max(1);
    let cap = capacity.max(1);
    let cols = cap.min(16);
    let rows = ((cap + cols - 1) / cols).max(1);
    Atlas {
        cv: canvas(cols * GLYPH_W * sc, rows * GLYPH_H * sc, 0),
        codes: Vec::new(),
        scale: sc,
        cols,
        next: 0,
        writes: 0,
    }
}

impl Atlas {
    /// The cell holding `code` — a linear scan of the cell codes, as the library's is —
    /// rasterising it on first use; -1 when the sheet is full.
    fn cell(&mut self, code: i64) -> i64 {
        let c = fold(code);
        if let Some(i) = self.codes.iter().position(|&x| x == c) {
            return i as i64;
        }
        let cap = (self.cv.w / (GLYPH_W * self.scale)) * (self.cv.h / (GLYPH_H * self.scale));
        if self.next >= cap {
            return -1;
        }
        let cell = self.next;
        let cx = (cell % self.cols) * GLYPH_W * self.scale;
        let cy = (cell / self.cols) * GLYPH_H * self.scale;
        blit_glyph(&mut self.cv, c, cx, cy, 0xffffffff, self.scale);
        self.codes.push(c);
        self.next = cell + 1;
        self.writes += 1;
        cell
    }

    fn layout(&mut self, s: &str, x: i64, y: i64) -> Vec<Quad> {
        let mut out = Vec::new();
        let mut pen = x;
        for ch in s.chars() {
            let cell = self.cell(ch as i64);
            out.push(Quad { x: pen, y, cell });
            pen += (GLYPH_W + 1) * self.scale;
        }
        out
    }
}

/// Blit laid-out quads from the sheet: per quad, per row, the sheet's row slice against
/// the target's, clipped to both, writing every non-zero texel.
fn draw_quads(cv: &mut Canvas, atlas: &Atlas, quads: &[Quad], colour: i64) {
    let gw = GLYPH_W * atlas.scale;
    let gh = GLYPH_H * atlas.scale;
    let sheet = &atlas.cv;
    for q in quads {
        if q.cell < 0 {
            continue;
        }
        let sx = (q.cell % atlas.cols) * gw;
        let sy = (q.cell / atlas.cols) * gh;
        // Columns where both the sheet texel and the target pixel are in range.
        let lo = 0.max(-sx).max(-q.x);
        let hi = gw.min(sheet.w - sx).min(cv.w - q.x);
        if lo >= hi {
            continue;
        }
        for py in 0..gh {
            let (srow, trow) = (sy + py, q.y + py);
            if srow < 0 || srow >= sheet.h || trow < 0 || trow >= cv.h {
                continue;
            }
            let s0 = (srow * sheet.w + sx + lo) as usize;
            let t0 = (trow * cv.w + q.x + lo) as usize;
            let n = (hi - lo) as usize;
            let src = &sheet.data[s0..s0 + n];
            let dst = &mut cv.data[t0..t0 + n];
            for (d, &s) in dst.iter_mut().zip(src) {
                if s != 0 {
                    *d = colour;
                }
            }
        }
    }
}

// ── text2d.loft: metrics and wrapping ───────────────────────────────

struct Metrics {
    adv64: i64,
}

fn metrics_builtin(scale: i64) -> Metrics {
    Metrics { adv64: (GLYPH_W + 1) * scale * 64 }
}

fn char_len(s: &str) -> i64 {
    s.chars().count() as i64
}

impl Metrics {
    fn width(&self, s: &str) -> i64 {
        (char_len(s) * self.adv64 + 63) / 64
    }

    fn chars_fitting(&self, s: &str, px: i64) -> i64 {
        let n = char_len(s);
        if self.adv64 <= 0 {
            return n;
        }
        ((px * 64) / self.adv64).max(1).min(n)
    }

    fn wrap(&self, s: &str, px: i64) -> Vec<String> {
        let mut lines = Vec::new();
        let mut line = String::new();
        let mut word = String::new();
        for c in s.chars() {
            if c == '\n' {
                let open = self.place(std::mem::take(&mut line), &word, px, &mut lines);
                lines.push(open);
                word.clear();
                continue;
            }
            if c == ' ' {
                line = self.place(line, &word, px, &mut lines);
                word.clear();
                continue;
            }
            word.push(c);
        }
        line = self.place(line, &word, px, &mut lines);
        if !line.is_empty() || lines.is_empty() {
            lines.push(line);
        }
        lines
    }

    /// Put `word` onto `line`, pushing completed lines, and answer the line still open.
    fn place(&self, line: String, word: &str, px: i64, lines: &mut Vec<String>) -> String {
        if word.is_empty() {
            return line;
        }
        let cand = if line.is_empty() { word.to_string() } else { format!("{line} {word}") };
        if self.width(&cand) <= px {
            return cand;
        }
        if !line.is_empty() {
            lines.push(line);
        }
        let mut rest = word.to_string();
        while self.width(&rest) > px {
            let k = self.chars_fitting(&rest, px) as usize;
            lines.push(rest.chars().take(k).collect());
            rest = rest.chars().skip(k).collect();
        }
        rest
    }
}

// ── The workload's text ─────────────────────────────────────────────

fn gen_code(l: i64, k: i64) -> i64 {
    let raw = 32 + (l * 131 + k * 29) % 64;
    if (65..=90).contains(&raw) && (k & 1) == 1 { raw + 32 } else { raw }
}

fn gen_line(l: i64, cols: i64) -> String {
    (0..cols).map(|k| char::from_u32(gen_code(l, k) as u32).unwrap()).collect()
}

fn gen_lines(count: i64, cols: i64) -> Vec<String> {
    (0..count).map(|l| gen_line(l, cols)).collect()
}

fn warm_atlas(scale: i64) -> Atlas {
    let mut at = atlas_new(scale, 64);
    for c in 32..96 {
        at.cell(c);
    }
    at
}

// ── write_text ──────────────────────────────────────────────────────

const WT_LINES: i64 = 500;
const WT_COLS: i64 = 40;
const WT_SCALE: i64 = 2;
const WT_W: i64 = 480;
const WT_H: i64 = 800;

fn write_all(cv: &mut Canvas, lines: &[String], colour: i64) -> i64 {
    let mut sum = 0;
    for (l, s) in lines.iter().enumerate() {
        sum += write_text(cv, s, 0, (l as i64 % 50) * 16, colour, WT_SCALE);
    }
    sum
}

fn lit_bits(code: i64) -> i64 {
    let at = glyph_at(code);
    if at < 0 {
        return 0;
    }
    (0..GLYPH_H).map(|ry| (FACE_ROWS[(at + ry) as usize] & 31).count_ones() as i64).sum()
}

fn bench_write_text(n: i64) -> Row {
    let lines = gen_lines(WT_LINES, WT_COLS);
    let mut cv = canvas(WT_W, WT_H, 0);
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        sink += write_all(&mut cv, black_box(&lines), black_box(0xFF203040 + (r & 1)));
    }
    let us = t0.elapsed().as_micros() as i64;
    let mut one = canvas(WT_W, WT_H, 0);
    write_all(&mut one, &lines, 0xFF203040);
    let calls: i64 =
        lines.iter().flat_map(|s| s.chars()).map(|c| lit_bits(c as i64) * WT_SCALE * WT_SCALE).sum();
    Row { name: "write_text", iters: n, us, px: calls, hash: fnv(FNV_OFFSET, &one.data),
          sink: black_box(sink + cv.get_pixel(3, 3)) }
}

// ── atlas_cell ──────────────────────────────────────────────────────

const AC_KEYS: i64 = 64000;

fn sum_cells(at: &mut Atlas, keys: &[i64]) -> i64 {
    keys.iter().map(|&k| at.cell(k)).sum()
}

fn bench_atlas_cell(n: i64) -> Row {
    let mut at = warm_atlas(1);
    let a: Vec<i64> = (0..AC_KEYS).map(|i| gen_code(i >> 6, i & 63)).collect();
    let b: Vec<i64> = (0..AC_KEYS).map(|i| a[((i + 1) % AC_KEYS) as usize]).collect();
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        let keys = if r & 1 == 0 { &a } else { &b };
        sink += sum_cells(&mut at, black_box(keys));
    }
    let us = t0.elapsed().as_micros() as i64;
    let one: Vec<i64> = a.iter().map(|&k| at.cell(k)).collect();
    Row { name: "atlas_cell", iters: n, us, px: AC_KEYS, hash: fnv(FNV_OFFSET, &one),
          sink: black_box(sink + at.writes) }
}

// ── layout ──────────────────────────────────────────────────────────

const LY_STRINGS: i64 = 2000;
const LY_COLS: i64 = 32;

fn layout_all(at: &mut Atlas, strs: &[String], x0: i64) -> i64 {
    let mut sum = 0;
    for (i, s) in strs.iter().enumerate() {
        let qs = at.layout(s, x0, i as i64 * 8);
        sum += qs.len() as i64 + qs.first().map_or(0, |q| q.cell);
    }
    sum
}

fn bench_layout(n: i64) -> Row {
    let mut at = warm_atlas(1);
    let strs = gen_lines(LY_STRINGS, LY_COLS);
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        sink += layout_all(&mut at, black_box(&strs), black_box(r & 1));
    }
    let us = t0.elapsed().as_micros() as i64;
    let mut ints = Vec::new();
    for (i, s) in strs.iter().enumerate() {
        for q in at.layout(s, 0, i as i64 * 8) {
            ints.extend([q.x, q.y, q.cell]);
        }
    }
    Row { name: "layout", iters: n, us, px: LY_STRINGS * LY_COLS, hash: fnv(FNV_OFFSET, &ints),
          sink: black_box(sink + at.writes) }
}

// ── wrap ────────────────────────────────────────────────────────────

const WR_WORDS: i64 = 3400;
const WR_PX: i64 = 240;

fn gen_paragraph(shift: i64) -> String {
    let mut s = String::new();
    for w in 0..WR_WORDS {
        let len = if w % 53 == 52 { 45 } else { 1 + (w * 7 + w / 3) % 9 };
        for j in 0..len {
            if (w + j) % 17 == 0 {
                s.push('é');
            } else {
                s.push(char::from_u32((97 + (w * 5 + j * 3 + shift) % 26) as u32).unwrap());
            }
        }
        s.push_str(if w % 211 == 210 { "\n" } else if w % 97 == 96 { "  " } else { " " });
    }
    s
}

fn bench_wrap(n: i64) -> Row {
    let m = metrics_builtin(1);
    let a = gen_paragraph(0);
    let b = gen_paragraph(1);
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        let src = if r & 1 == 0 { &a } else { &b };
        sink += m.wrap(black_box(src), WR_PX).len() as i64;
    }
    let us = t0.elapsed().as_micros() as i64;
    let mut ints = Vec::new();
    for ln in m.wrap(&a, WR_PX) {
        ints.extend(ln.chars().map(|c| c as i64));
        ints.push(-1);
    }
    Row { name: "wrap", iters: n, us, px: char_len(&a), hash: fnv(FNV_OFFSET, &ints),
          sink: black_box(sink) }
}

// ── draw_quads ──────────────────────────────────────────────────────

const DQ_LINES: i64 = 50;
const DQ_COLS: i64 = 40;
const DQ_SCALE: i64 = 2;

fn bench_draw_quads(n: i64) -> Row {
    let mut at = warm_atlas(DQ_SCALE);
    let mut quads = Vec::new();
    for l in 0..DQ_LINES {
        quads.extend(at.layout(&gen_line(l, DQ_COLS), 0, l * 16));
    }
    let mut cv = canvas(WT_W, WT_H, 0);
    let t0 = Instant::now();
    let mut sink = 0i64;
    for r in 0..n {
        draw_quads(&mut cv, &at, black_box(&quads), black_box(0xFF405060 + (r & 1)));
        sink += cv.get_pixel(3, 3);
    }
    let us = t0.elapsed().as_micros() as i64;
    let mut one = canvas(WT_W, WT_H, 0);
    draw_quads(&mut one, &at, &quads, 0xFF405060);
    let texels = quads.len() as i64 * 5 * DQ_SCALE * 7 * DQ_SCALE;
    Row { name: "draw_quads", iters: n, us, px: texels, hash: fnv(FNV_OFFSET, &one.data),
          sink: black_box(sink) }
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
    let mut sink = 0i64;
    // `wrap` is held out until the loft lanes agree with it — see the note in bench.loft.
    if std::env::args().any(|a| a == "--wrap") {
        print_row(&bench_wrap(n));
    }
    for r in [bench_write_text(n), bench_atlas_cell(n), bench_layout(n), bench_draw_quads(n)] {
        print_row(&r);
        sink = sink.wrapping_add(r.sink);
    }
    println!("time: {}ms sink={}", t0.elapsed().as_millis(), sink);
}
