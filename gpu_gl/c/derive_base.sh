#!/bin/sh
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# gl_base.h is graphics' c/graphics_gl.c with every name of its own given another prefix
# (gg_ -> gq_), so the two can be linked into one program.  It is derived, never edited:
#   gpu_gl/c/derive_base.sh           write gl_base.h again
#   gpu_gl/c/derive_base.sh --check   exit 1 when gl_base.h is not what graphics' file gives
# A page's imports keep their names: they are the page's, not this file's.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../graphics/c/graphics_gl.c"
out="$here/gl_base.h"
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
{
  printf '/* DERIVED from graphics/c/graphics_gl.c by gpu_gl/c/derive_base.sh: do not edit.\n'
  printf ' * The window hosts, the GL function table and the texture and framebuffer code of the\n'
  printf ' * graphics library, under the prefix gq_ so both link into one program.  graphics keeps\n'
  printf ' * its own copy until its Window is a gpu Device; then this is the only one. */\n'
  sed -e 's/gg_/gq_/g' -e 's/GG_/GQ_/g' -e 's/GQ_IMPORT(gq_/GQ_IMPORT(gg_/' "$src"
} > "$tmp"
if [ "${1:-}" = "--check" ]; then
  cmp -s "$tmp" "$out" || { echo "gl_base.h is not derived from graphics/c/graphics_gl.c as it is now: run gpu_gl/c/derive_base.sh" >&2; exit 1; }
else
  cp "$tmp" "$out"
fi
