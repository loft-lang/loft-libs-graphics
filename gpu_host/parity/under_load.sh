#!/bin/bash
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# A test file run several times at once on a loaded machine, each under an X server of its own.
# An X server starts over when its last client leaves and drops a connection that arrives while
# it does (under Xvfb one open in three with every core busy), so this is where a backend that
# gives up on a dropped connection is seen: its `many_devices.loft` fails here in about half
# the runs and passes on an idle machine.
#
#   gpu_host/parity/under_load.sh <package> <test file> [runs=8] [--native]
#   gpu_host/parity/under_load.sh gpu_gl tests/many_devices.loft
#   VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json MESA_VK_WSI_DEBUG=sw \
#     gpu_host/parity/under_load.sh gpu_vulkan tests/many_devices.loft
#
# Prints one line per run and "N of M passed"; exits 1 unless all did.  Needs xvfb-run.
set -u
here=$(cd "$(dirname "$0")/../.." && pwd)
pkg=${1:?package}; file=${2:?test file}; runs=${3:-8}; mode=${4:---interpret}
cd "$here/$pkg" || exit 2
out=$(mktemp -d "${TMPDIR:-/tmp}/under_load.XXXXXX")
burners=""
trap 'kill $burners 2>/dev/null; rm -rf "$out"' EXIT
for _ in $(seq 1 $(( $(nproc) * 2 ))); do
  sh -c 'while :; do :; done' & burners="$burners $!"
done
base=$(( 3000 + $$ % 2000 ))
runners=""
for r in $(seq 1 "$runs"); do
  ( LOFT_TIMEOUT=600 GRAPHICS_TEST_MUST_DRAW=1 xvfb-run -n $(( base + r )) \
      loft "$mode" --tests "$file" > "$out/$r.log" 2>&1
    echo $? > "$out/$r.exit" ) &
  runners="$runners $!"
done
wait $runners
passed=0
for r in $(seq 1 "$runs"); do
  code=$(cat "$out/$r.exit" 2>/dev/null || echo "?")
  [ "$code" = 0 ] && passed=$(( passed + 1 ))
  echo "run $r: exit $code  $(grep -E '^ *FAIL .*::|test result' "$out/$r.log" | head -1 | cut -c1-140)"
done
echo "$passed of $runs passed"
[ "$passed" = "$runs" ]
