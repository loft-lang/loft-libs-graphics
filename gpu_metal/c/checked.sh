#!/bin/sh
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Build gpu_metal checked for a test run, and unchecked again after.
#
#   gpu_metal/c/checked.sh on        the next build of this library looks up every handle a call uses
#   gpu_metal/c/checked.sh off       as committed: no check compiled in
#   gpu_metal/c/checked.sh --check   exit 1 when checked.h has the define (never commit it)
#   gpu_metal/c/checked.sh run CMD…  on, run CMD with GPU_METAL_DEBUG=abort, then off whatever CMD did
#
# loft compiles a library's C with flags of its own and takes none from a package, so the
# switch is this header and not a -D.
set -eu
h="$(dirname "$0")/checked.h"
set_to() {
    if [ "$1" = on ]; then
        sed -i 's|^/\* #define GPU_METAL_CHECKED 1 \*/$|#define GPU_METAL_CHECKED 1|' "$h"
    else
        sed -i 's|^#define GPU_METAL_CHECKED 1$|/* #define GPU_METAL_CHECKED 1 */|' "$h"
    fi
}
case "${1:-}" in
on | off) set_to "$1" ;;
--check)
    if grep -q '^#define GPU_METAL_CHECKED' "$h"; then
        echo "checked.sh: $h builds the library checked; run 'checked.sh off' before committing" >&2
        exit 1
    fi
    ;;
run)
    shift
    set_to on
    trap 'set_to off' EXIT INT TERM
    GPU_METAL_DEBUG="${GPU_METAL_DEBUG:-abort}" "$@"
    ;;
*)
    sed -n '4,12p' "$0" >&2
    exit 2
    ;;
esac
