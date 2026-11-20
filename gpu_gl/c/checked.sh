#!/bin/sh
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Build gpu_gl checked for a test run, and unchecked again after.
#
#   gpu_gl/c/checked.sh on        the next build of this library checks every OpenGL call
#   gpu_gl/c/checked.sh off       as committed: no check compiled in
#   gpu_gl/c/checked.sh --check   exit 1 when checked.h has the define (never commit it)
#   gpu_gl/c/checked.sh run CMD…  on, run CMD with GPU_GL_DEBUG=abort, then off whatever CMD did
#
# loft compiles a library's C with flags of its own and takes none from a package, so the
# switch is this header and not a -D.
set -eu
h="$(dirname "$0")/checked.h"
# Through a file beside it and not `sed -i`: GNU sed and the BSD sed of macOS take that flag
# differently.
set_to() {
    if [ "$1" = on ]; then
        sed 's|^/\* #define GPU_GL_CHECKED 1 \*/$|#define GPU_GL_CHECKED 1|' "$h" > "$h.new"
    else
        sed 's|^#define GPU_GL_CHECKED 1$|/* #define GPU_GL_CHECKED 1 */|' "$h" > "$h.new"
    fi
    mv "$h.new" "$h"
}
case "${1:-}" in
on | off) set_to "$1" ;;
--check)
    if grep -q '^#define GPU_GL_CHECKED' "$h"; then
        echo "checked.sh: $h builds the library checked; run 'checked.sh off' before committing" >&2
        exit 1
    fi
    ;;
run)
    shift
    set_to on
    trap 'set_to off' EXIT INT TERM
    GPU_GL_DEBUG="${GPU_GL_DEBUG:-abort}" "$@"
    ;;
*)
    sed -n '4,12p' "$0" >&2
    exit 2
    ;;
esac
