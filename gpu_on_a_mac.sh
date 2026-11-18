#!/bin/bash
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# The gpu packages checked on a Mac with a GPU of its own: what the library CI cannot measure.
# Its macOS runner is a virtual machine, where Metal is a paravirtual GPU and OpenGL is Apple's
# software renderer, so there `gpu_metal`'s comparison with OpenGL allows 2 of 255 in every
# pixel.  On a real Mac the two agree to the bit, and this is the run that says so.  Run it
# before a release of `gpu`, `gpu_gl`, `gpu_metal`, `gpu_host`, `gpu_paint` or `scene3d`:
#
#   ./gpu_on_a_mac.sh            the tests, Metal's validation, the thread check
#   ./gpu_on_a_mac.sh --timed    the same, then a frame's cost on each backend
#
# It opens windows.  Every test must draw (`GRAPHICS_TEST_MUST_DRAW=1`): one that finds no
# window fails.  It prints a line a run and exits non-zero when one failed; the logs stay in
# the directory it names.  `loft` is the one on the PATH.
set -u
cd "$(dirname "$0")" || exit 1
root=$PWD
logs=$(mktemp -d "${TMPDIR:-/tmp}/gpu_on_a_mac.XXXXXX")
failed=0

if [ "$(uname)" != "Darwin" ]; then
  echo "this check is for a Mac"; exit 1
fi

run() {  # run <name> <directory> <command...>
  local name=$1 dir=$2; shift 2
  if ( cd "$dir" && "$@" ) > "$logs/$name.log" 2>&1; then
    echo "ok    $name  $(grep -E '^test result' "$logs/$name.log" | sed 's/ \[.*//' | tail -1)"
  else
    echo "FAIL  $name  see $logs/$name.log"
    failed=1
  fi
}

export GRAPHICS_TEST_WINDOW=1 GRAPHICS_TEST_MUST_DRAW=1 LOFT_TIMEOUT=300
for package in gpu gpu_host gpu_gl gpu_metal gpu_paint gpu_record scene3d; do
  for mode in interpret native-c native; do
    run "$package-$mode" "$package" loft "--$mode" --tests tests --lib ..
  done
done

# Metal's own validation: it stops the program at a call Metal's rules refuse.
run "gpu_metal-validation" gpu_metal env MTL_DEBUG_LAYER=1 loft --interpret --tests tests --lib ..

# Two devices on two threads under the thread sanitizer, and a hidden application.
mkdir -p "$logs/include/gpu" && cp gpu/c/gpu_backend.h "$logs/include/gpu/"
run "gpu_metal-threads" gpu_metal sh -c \
  "cc -g -fsanitize=thread -I'$logs/include' tests/host_checks.c -framework CoreFoundation -o '$logs/host_checks' && '$logs/host_checks'"

if [ "${1:-}" = "--timed" ]; then
  loft --lib "$root" gpu_host/bench/backends.loft 2>&1 | tee "$logs/timed.log"
fi

[ $failed = 0 ] && echo "all passed; logs in $logs"
exit $failed
