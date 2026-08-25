// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later

//! P130: GL functions must not crash when called without a valid GL context.
//! In headless environments `loft_gl_create_window` fails; subsequent GL
//! calls should no-op and return safe defaults instead of calling through
//! null function pointers.

use loft_graphics_native::*;

/// Call a representative cross-section of GL functions without ever
/// creating a window.  Every one must return its default (0, false, or
/// void) without panicking or aborting.
#[test]
fn p130_gl_functions_noop_without_context() {
    // Window/lifecycle — poll and swap should be safe
    assert!(!loft_gl_poll_events());
    loft_gl_swap_buffers();

    // Drawing
    loft_gl_clear(0xFF000000);
    loft_gl_draw(0, 0);
    loft_gl_draw_elements(0, 0, 0);
    loft_gl_draw_mode(0, 0, 0);
    loft_gl_draw_fullscreen_quad();

    // Shaders
    let shader = loft_gl_create_shader(
        c"#version 330\nvoid main(){}".as_ptr().cast(),
        26,
        c"#version 330\nvoid main(){}".as_ptr().cast(),
        26,
    );
    assert_eq!(shader, 0);
    loft_gl_use_shader(0);
    loft_gl_delete_shader(0);

    // State management
    loft_gl_enable(1);
    loft_gl_disable(2);
    loft_gl_blend_func(2, 3);
    loft_gl_cull_face(0);
    loft_gl_depth_mask(true);
    loft_gl_viewport(0, 0, 800, 600);
    loft_gl_line_width(1.0);
    loft_gl_point_size(1.0);

    // Framebuffers
    assert_eq!(loft_gl_create_framebuffer(), 0);
    loft_gl_bind_framebuffer(0);
    loft_gl_framebuffer_texture(0, 0, 0);
    loft_gl_delete_framebuffer(0);

    // Textures
    assert_eq!(loft_gl_create_depth_texture(256, 256), 0);
    assert_eq!(loft_gl_create_color_texture(256, 256), 0);
    loft_gl_bind_texture(0, 0);
    loft_gl_delete_texture(0);
    assert_eq!(loft_gl_upload_alpha_texture(std::ptr::null(), 0, 0), 0);

    // Uniform setters
    loft_gl_set_uniform_float(0, c"u".as_ptr().cast(), 1, 1.0);
    loft_gl_set_uniform_int(0, c"u".as_ptr().cast(), 1, 1);
    loft_gl_set_uniform_vec3(0, c"u".as_ptr().cast(), 1, 0.0, 0.0, 0.0);

    // Cleanup
    loft_gl_delete_vao(0);

    // Destroy (should be safe even without create)
    loft_gl_destroy_window();
}

// ── The same contract on a target that can never have a context ──────────
//
// The test above holds `GL_READY` false by not creating a window.  On wasm32
// nothing can set it: there is no display server, so the window layer is not
// compiled in at all and the guard can never open.  That is what lets the ~90
// GL entry points be shared source across both targets rather than a desktop
// set and a hand-written wasm set that drift.
//
// It only works while the crate still COMPILES for wasm32, and that is the
// property that broke: `winit` and `glutin` were unconditional dependencies,
// neither builds for wasm32, and so the whole package — including the pure
// software canvas and the PNG encoder, which need no window — was off
// `--native-wasm` entirely.  Nothing measured it, because the shared library
// CI builds no wasm target and `loft test` has no `--native-wasm` mode.

/// Every crate in the unconditional `[dependencies]` table must be one that
/// cross-builds for wasm32.
///
/// An ALLOW-list, not a deny-list, and deliberately so: a deny-list of
/// "crates known to need a display" is silent about the next one added, while
/// this fails on ANY new unconditional dependency until someone says which
/// side of the line it is on.  Being wrong costs a red test with a message
/// naming the crate; being wrong the other way costs the wasm target again,
/// silently.
///
/// To add a dependency: cross-build it (`cargo build --target wasm32-wasip2`)
/// and, if it is clean, add it here; if it needs a display server or a host
/// audio/window API, put it under
/// `[target.'cfg(not(target_arch = "wasm32"))'.dependencies]` instead and give
/// its entry points a wasm32 twin the way `loft_gl_create_window` has one.
#[test]
fn unconditional_dependencies_are_wasm_clean() {
    // Verified by cross-building each one on its own for wasm32-wasip2.
    const WASM_CLEAN: &[&str] = &[
        "loft-ffi",
        "loft-ffi-macros",
        "gl",
        "fontdue",
        "png",
        "image",
        "rodio",
    ];

    let manifest = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/Cargo.toml"))
        .expect("the crate's own Cargo.toml is readable");

    // Read only the unconditional `[dependencies]` table: the next `[`-headed
    // line ends it, which is where the target-gated table begins.
    let body = manifest
        .split_once("\n[dependencies]\n")
        .expect("crate has a [dependencies] table")
        .1;
    let body = body.split_once("\n[").map_or(body, |(before, _)| before);

    let mut offenders = Vec::new();
    for line in body.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let name = line.split('=').next().unwrap_or("").trim();
        if !name.is_empty() && !WASM_CLEAN.contains(&name) {
            offenders.push(name.to_string());
        }
    }

    assert!(
        offenders.is_empty(),
        "unconditional dependencies not known to build for wasm32: {offenders:?}.\n\
         Cross-build each one (cargo build --target wasm32-wasip2).  If it is clean, \
         add it to WASM_CLEAN above.  If it needs a display server or a host \
         window/audio API, move it to \
         [target.'cfg(not(target_arch = \"wasm32\"))'.dependencies] and give its entry \
         points a wasm32 twin — see loft_gl_create_window."
    );
}

/// The cross-build itself, when the toolchain has the target.
///
/// The check above is the one that always runs and always means something;
/// this one proves that what it asserts is actually SUFFICIENT — that the
/// allow-list plus the `cfg` split really do produce a wasm32 build, rather
/// than a manifest that merely looks right.  It skips where the target is not
/// installed, which is why it is the second line of defence and not the only
/// one.
#[test]
fn cross_builds_for_wasm32_wasip2() {
    use std::process::Command;

    let installed = Command::new("rustc")
        .args(["--print", "target-list"])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).contains("wasm32-wasip2"))
        .unwrap_or(false);
    if !installed {
        println!(
            "skip: rustc does not know wasm32-wasip2 \
             (rustup target add wasm32-wasip2) — \
             unconditional_dependencies_are_wasm_clean still covers the regression"
        );
        return;
    }

    let out = Command::new(std::env::var("CARGO").as_deref().unwrap_or("cargo"))
        .args(["build", "--target", "wasm32-wasip2"])
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output();

    let out = match out {
        Ok(o) => o,
        Err(e) => {
            println!("skip: cargo is not invocable from the test ({e})");
            return;
        }
    };

    let err = String::from_utf8_lossy(&out.stderr);
    // A missing std for the target is an unusable toolchain, not a defect in
    // this crate — say so rather than reporting the crate broken.
    if !out.status.success() && err.contains("can't find crate for `std`") {
        println!("skip: no std for wasm32-wasip2 (rustup target add wasm32-wasip2)");
        return;
    }
    assert!(
        out.status.success(),
        "the crate no longer cross-builds for wasm32-wasip2:\n{err}"
    );
}
