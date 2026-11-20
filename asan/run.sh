#!/bin/sh
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Run packages' tests with their C built under AddressSanitizer.
#
#   asan/run.sh <package>…        e.g.  asan/run.sh gpu_gl gpu_vulkan
#
# The tests run on the interpreter, which loads a library's C as a shared object: `asan/cc`
# builds that object sanitized, and the sanitizer's runtime is loaded ahead of loft itself
# (`LD_PRELOAD`), since loft is not built with it.  A read or write of freed memory, or past
# the end of a block, in a library's C then stops the test with the sanitizer's report.
#
# Before any package, `asan/selftest` runs: a package whose C reads a freed block on purpose.
# The run goes on only when the sanitizer reported exactly that — a sanitizer that is not
# live would let every package pass.  Linux; needs a display for the packages that draw.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
[ $# -ge 1 ] || { sed -n '4,16p' "$0" >&2; exit 2; }
real="${ASAN_REAL_CC:-cc}"
runtime=$("$real" -print-file-name=libasan.so)
case "$runtime" in
/*) [ -e "$runtime" ] || { echo "asan/run.sh: $runtime is not there" >&2; exit 1; } ;;
*) echo "asan/run.sh: $real has no AddressSanitizer runtime (libasan.so)" >&2; exit 1 ;;
esac
export CC="$here/cc" ASAN_REAL_CC="$real"
# Programs loft starts inherit the preload; `bin/` holds the stand-in for the one that hangs.
export PATH="$here/bin:$PATH"
# Leaks are not this run's question: loft is not built sanitized and ends without freeing.
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:${ASAN_OPTIONS:-}"
# loft keeps a compiled program beside its source; that copy names the library built for it,
# so without this a run after an ordinary one would load the ordinary library again.
export LOFT_NO_CACHE=1
run() { LD_PRELOAD="$runtime" "$@"; }

# Whether a library built by `asan/cc` is among a package's built ones.
sanitized() {
    for so in "$1"/native-auto/*.so; do
        [ -e "$so" ] && nm -D "$so" 2>/dev/null | grep -q '__asan_init' && return 0
    done
    return 1
}

# 1. The sanitizer is live in a library loft loads: the self-test's freed read is reported.
said=$(cd "$here/selftest" && LOFT_TIMEOUT=120 run loft --interpret src/main.loft 2>&1) && status=0 || status=$?
if [ "$status" -eq 0 ] || ! printf '%s\n' "$said" | grep -q 'AddressSanitizer: heap-use-after-free'; then
    echo "asan/run.sh: the self-test read freed memory and the sanitizer did not report it (exit $status):" >&2
    printf '%s\n' "$said" | tail -15 >&2
    exit 1
fi
echo "asan: live — the self-test's read of freed memory was reported"

# 2. Each package's tests, its C sanitized.
failed=0
for pkg in "$@"; do
    out=$(cd "$here/../$pkg" && LOFT_TIMEOUT="${LOFT_TIMEOUT:-600}" run loft --interpret --tests tests 2>&1) && status=0 || status=$?
    line=$(printf '%s\n' "$out" | grep 'test result' | tail -1 | cut -c1-60)
    if [ "$status" -ne 0 ] || printf '%s\n' "$out" | grep -q 'AddressSanitizer'; then
        echo "asan: $pkg FAILED (exit $status) $line"
        printf '%s\n' "$out" | grep -A25 'AddressSanitizer' | head -40
        printf '%s\n' "$out" | grep -E 'FAIL|error' | head -10
        failed=1
    elif grep -q '^shim' "$here/../$pkg/loft.toml" && ! sanitized "$here/../$pkg"; then
        echo "asan: $pkg FAILED: its tests passed, but no sanitized build of its C is in $pkg/native-auto"
        failed=1
    else
        echo "asan: $pkg $line"
    fi
done
exit "$failed"
