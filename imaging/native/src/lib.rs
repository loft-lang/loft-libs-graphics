// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later

//! Native PNG decoder using loft-ffi for direct store access.

#![allow(clippy::missing_safety_doc)]

use loft_ffi::{LoftRef, LoftStore};
use loft_ffi_macros::loft_native;
use png::Decoder;
use std::fs::File;
use std::io::{BufReader, BufWriter};

/// Field offsets for the Image struct in the loft store.
// loft reorders struct fields to place 8-byte members first for alignment, so
// the source order (name, width, height, data) lays out in the store as:
//   width  @ 0  (integer → i64, 8 bytes)
//   height @ 8  (integer → i64, 8 bytes)
//   name   @ 16 (text    → u32 record ref, 4 bytes)
//   data   @ 20 (vector  → u32 record ref, 4 bytes)
// Verified against the interpreter/native read offsets (`OpGetInt(img,0)`,
// `OpGetInt(img,8)`, `OpGetText(img,16)`, `OpGetField(img,20)`).  @P321c.
mod image_fields {
    pub const WIDTH: u16 = 0; // integer (i64)
    pub const HEIGHT: u16 = 8; // integer (i64)
    pub const NAME: u16 = 16; // text (record ref)
    pub const DATA: u16 = 20; // vector ref (Pixel elements, 4 bytes each)
}

/// Decode `path` into exactly `width * height` RGBA quadruples.
///
/// A loft `Image` is `width * height` four-byte `Pixel` records, so every PNG
/// colour type is expanded to 8-bit RGBA here — there is no way to say "this row
/// is 1 byte per pixel".  Anything left un-expanded is not a smaller image, it is
/// the raw byte stream re-cut into fours.  A source with **no alpha channel is
/// filled 255**, not 0: zero alpha is invisible, so the fallback has to be opaque
/// or every RGB PNG would decode to nothing.  Returns `None` rather than a
/// mismatched buffer, so the caller's failure path is the one that runs.
fn decode_png(path: &str) -> Option<(u32, u32, Vec<u8>)> {
    let file = File::open(path).ok()?;
    let mut decoder = Decoder::new(BufReader::new(file));
    // Expand palette indices and sub-byte greyscale, and drop 16-bit samples to
    // 8, so the only output colour types left are the four folded below.
    decoder.set_transformations(png::Transformations::normalize_to_color8());
    let mut reader = decoder.read_info().ok()?;
    let buf_size = reader.output_buffer_size();
    let mut buf = vec![0u8; buf_size];
    let info = reader.next_frame(&mut buf).ok()?;
    buf.truncate(info.buffer_size());
    if info.bit_depth != png::BitDepth::Eight {
        return None;
    }
    let rgba: Vec<u8> = match info.color_type {
        png::ColorType::Rgb => buf.chunks_exact(3).flat_map(|p| [p[0], p[1], p[2], 255]).collect(),
        png::ColorType::Rgba => buf,
        png::ColorType::Grayscale => buf.iter().flat_map(|&g| [g, g, g, 255]).collect(),
        png::ColorType::GrayscaleAlpha => buf
            .chunks_exact(2)
            .flat_map(|p| [p[0], p[0], p[0], p[1]])
            .collect(),
        // normalize_to_color8 expands these; reaching here means it did not.
        png::ColorType::Indexed => return None,
    };
    let expected = (info.width as usize)
        .checked_mul(info.height as usize)?
        .checked_mul(4)?;
    if rgba.len() != expected {
        return None;
    }
    Some((info.width, info.height, rgba))
}

/// Decode a PNG file and write the result directly into an Image struct.
/// The Image fields (name, width, height, data) are written via LoftStore.
#[loft_native]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn n_load_png(
    mut store: LoftStore,
    path_ptr: *const u8,
    path_len: usize,
    image: LoftRef,
) -> bool {
    let path = unsafe { loft_ffi::text(path_ptr, path_len) };
    let (w, h, pixels) = match decode_png(path) {
        Some(data) => data,
        None => return false,
    };
    let name = std::path::Path::new(path)
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("");
    unsafe {
        // Write Image struct fields.  width/height are plain `integer` →
        // 8-byte i64 (set_long); name/data are 4-byte record refs (set_int).
        store.set_text(image.rec, image.pos, image_fields::NAME, name);
        store.set_long(image.rec, image.pos, image_fields::WIDTH, i64::from(w));
        store.set_long(image.rec, image.pos, image_fields::HEIGHT, i64::from(h));
        // Create pixel vector and bulk-copy RGBA data (4 bytes per Pixel).
        let vec = store.alloc_vector_from_bytes(
            4,
            pixels.len() as u32 / 4,
            pixels.as_ptr(),
            pixels.len(),
        );
        store.set_int(image.rec, image.pos, image_fields::DATA, vec.rec as i32);
    }
    true
}

/// Write RGBA texels out, choosing the colour type from the PIXELS.
///
/// An all-opaque image is written as RGB, exactly as it was before alpha existed,
/// so no consumer's output file changes for gaining a channel it does not use —
/// and one texel below 255 switches the whole file to RGBA.  The same rule
/// `graphics::save_png` already follows: the choice is read off the pixels rather
/// than declared, because a flag nobody sets is a flag that is always wrong.
fn encode_png(path: &str, width: u32, height: u32, rgba_data: &[u8]) -> bool {
    let file = match File::create(path) {
        Ok(f) => f,
        Err(_) => return false,
    };
    let has_alpha = rgba_data.chunks_exact(4).any(|p| p[3] != 255);
    let mut encoder = png::Encoder::new(BufWriter::new(file), width, height);
    encoder.set_color(if has_alpha {
        png::ColorType::Rgba
    } else {
        png::ColorType::Rgb
    });
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = match encoder.write_header() {
        Ok(w) => w,
        Err(_) => return false,
    };
    if has_alpha {
        writer.write_image_data(rgba_data).is_ok()
    } else {
        let rgb: Vec<u8> = rgba_data
            .chunks_exact(4)
            .flat_map(|p| [p[0], p[1], p[2]])
            .collect();
        writer.write_image_data(&rgb).is_ok()
    }
}

/// Encode an Image struct as a PNG file.
/// Reads width, height, and pixel data (4 bytes per Pixel: r, g, b, a) from the store.
#[loft_native]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn n_save_png(
    store: LoftStore,
    image: LoftRef,
    path_ptr: *const u8,
    path_len: usize,
) -> bool {
    let path = unsafe { loft_ffi::text(path_ptr, path_len) };
    // width/height are plain `integer` → 8-byte i64 (get_long); data is a
    // 4-byte record ref (get_int).
    let w = unsafe { store.get_long(image.rec, image.pos, image_fields::WIDTH) } as u32;
    let h = unsafe { store.get_long(image.rec, image.pos, image_fields::HEIGHT) } as u32;
    let data_rec = unsafe { store.get_int(image.rec, image.pos, image_fields::DATA) } as u32;
    if w == 0 || h == 0 || data_rec == 0 {
        return false;
    }
    let data_ref = LoftRef {
        store_nr: image.store_nr,
        rec: data_rec,
        pos: 0,
    };
    let count = unsafe { store.vector_len(&data_ref) };
    let expected = w * h;
    if count < expected {
        return false;
    }
    // Each Pixel is 4 bytes (r, g, b, a) stored contiguously in the vector.
    let ptr = unsafe { store.vector_data_ptr(&data_ref) };
    let rgba_data = unsafe { std::slice::from_raw_parts(ptr, (expected * 4) as usize) };
    encode_png(path, w, h, rgba_data)
}

// @PLAN12 phase 2 — the `loft_ffi::loft_register! { … }` symbol list is
// generated from `../loft.toml::[native.functions]` by `build.rs` (via
// `loft-ffi-build`) and `include!`d here, so the cdylib's exported symbol
// set is never hand-maintained.
include!(concat!(env!("OUT_DIR"), "/loft_register_gen.rs"));
