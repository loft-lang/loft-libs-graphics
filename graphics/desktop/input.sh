#!/bin/bash
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# graphics' X11 input, end to end: inject a sequence through XTest (inject.c, libXtst opened at run
# time) into the window desktop/input.loft opens under its own Xvfb, and compare the events it
# prints with input.expected — written from the key table (KEY_*): a move, left and right clicks,
# a wheel step each way, `a`, Shift+`b`, Shift+`1`, Enter, Left, F5, Backspace, Space, Escape.
# Runs of MouseMove are collapsed to their last.
#   graphics/desktop/input.sh [--interpret|--native|--native-c]
set -u
here=$(cd "$(dirname "$0")" && pwd)
mode=${1:---interpret}
loft=${LOFT:-loft}
out=$(mktemp -d)
cc -O1 -o "$out/inject" "$here/inject.c" -lX11 -ldl || exit 2
disp=:$((90 + RANDOM % 9))
Xvfb "$disp" -noreset -screen 0 800x600x24 > /dev/null 2>&1 &
xvfb=$!
trap 'kill $xvfb 2>/dev/null; rm -rf "$out"' EXIT
sleep 1
DISPLAY=$disp LOFT_TIMEOUT=300 "$loft" "$mode" "$here/input.loft" > "$out/raw" 2> "$out/err" &
prog=$!
DISPLAY=$disp "$out/inject" g6-input || echo "injector failed ($?)"
wait $prog
grep -v '^ready$' "$out/raw" | awk '/^MouseMove/ { pending = $0; next } { if (pending != "") print pending; pending = ""; print }' > "$out/got"
if diff -u "$here/input.expected" "$out/got"; then echo "as expected"; else exit 1; fi
