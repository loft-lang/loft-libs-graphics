#!/bin/bash
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# @PLN208 G6: inject the same input into the 0.10 window and 0.9's, under one Xvfb, and compare
# what each delivers with what 0.9's key table says (input.expected).  Runs of MouseMove are
# collapsed to their last — winit may coalesce motion, X reports each step.
#   graphics/compare/input.sh [--interpret|--native|--native-c]
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
status=0
for which in new 09; do
  m=$mode
  [ "$which" = 09 ] && [ "$mode" = --native-c ] && m=--interpret   # 0.9's crate does not load on the C
  DISPLAY=$disp LOFT_TIMEOUT=300 "$loft" "$m" "$here/input_$which.loft" > "$out/$which.raw" 2> "$out/$which.err" &
  prog=$!
  DISPLAY=$disp "$out/inject" g6-input || echo "injector failed ($?) for $which"
  wait $prog
  grep -v '^ready$' "$out/$which.raw" | awk '/^MouseMove/ { pending = $0; next } { if (pending != "") print pending; pending = ""; print }' > "$out/$which.txt"
  [ "$which" = 09 ] && awk '/^Wheel/ && $0 == last { next } { print; last = $0 }' "$out/$which.txt" > "$out/09.dedup" && mv "$out/09.dedup" "$out/$which.txt"
  if diff -u "$here/input.expected" "$out/$which.txt"; then echo "$which: as expected"; else status=1; fi
done
exit $status
