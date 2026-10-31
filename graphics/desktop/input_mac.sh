#!/bin/bash
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# graphics' macOS input against the same input.expected as X11's input.sh: input_mac.c posts the
# sequence into its own AppKit queue (no accessibility grant needed) and prints what the shim made
# of it.  Runs of MouseMove are collapsed to their last, as input.sh does.
#   graphics/desktop/input_mac.sh
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c99 -O1 -o "$out/input" "$here/input_mac.c" || exit 2
"$out/input" > "$out/raw" || { cat "$out/raw"; exit 2; }
awk '/^MouseMove/ { pending = $0; next } { if (pending != "") print pending; pending = ""; print }' "$out/raw" > "$out/got"
if diff -u "$here/input.expected" "$out/got"; then echo "as expected"; else exit 1; fi
