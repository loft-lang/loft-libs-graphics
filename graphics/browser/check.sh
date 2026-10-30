#!/bin/bash
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# @PLN208: graphics 0.10 in a browser page.  Builds browser/check.loft with --html (the Rust
# writer's page, or the C writer's with --native-c), serves it, and loads it in headless Chrome
# through loft's tools/html_render_check.mjs.  Once the page prints READY FOR INPUT the harness
# dispatches a left and a right click, a wheel notch and keys as DOM events; the deliberately
# broken shader's compile log is the one console error allowed; the page prints PASS / FAIL lines
# and BROWSER DONE.  Exit 0 when every line passed.
#   LOFT=… LOFT_SRC=<loft checkout> graphics/browser/check.sh [--native-c]
set -u
here=$(cd "$(dirname "$0")" && pwd)
loft=${LOFT:-loft}
src=${LOFT_SRC:?LOFT_SRC names the loft checkout holding tools/html_render_check.mjs}
out=$(mktemp -d)
trap 'kill $server 2>/dev/null; rm -rf "$out"' EXIT
LOFT_TIMEOUT=1200 "$loft" ${1:-} --html "$out/check.html" "$here/check.loft" > "$out/build.log" 2>&1 \
  || { tail -20 "$out/build.log"; exit 2; }
port=$((20000 + RANDOM % 20000))
(cd "$out" && python3 -m http.server "$port" --bind 127.0.0.1 > /dev/null 2>&1) &
server=$!
sleep 1
inject='(() => {
  const out = document.getElementById("out").textContent;
  if (out.includes("READY FOR INPUT") && !globalThis.__sent) {
    globalThis.__sent = true;
    const c = document.getElementById("c");
    const r = c.getBoundingClientRect();
    const at = { clientX: r.left + 10.5, clientY: r.top + 20.5, bubbles: true };
    c.dispatchEvent(new MouseEvent("mousedown", { ...at, button: 0 }));
    c.dispatchEvent(new MouseEvent("mouseup", { ...at, button: 0 }));
    c.dispatchEvent(new MouseEvent("mousedown", { ...at, button: 2 }));
    c.dispatchEvent(new MouseEvent("mouseup", { ...at, button: 2 }));
    c.dispatchEvent(new WheelEvent("wheel", { ...at, deltaY: -100 }));
    const key = (type, k, shift) => c.dispatchEvent(new KeyboardEvent(type, { key: k, shiftKey: !!shift, bubbles: true }));
    key("keydown", "a"); key("keyup", "a");
    key("keydown", "Shift", true); key("keydown", "B", true); key("keyup", "B", true); key("keyup", "Shift", false);
    key("keydown", "Enter"); key("keyup", "Enter");
    key("keydown", "Escape"); key("keyup", "Escape");
  }
  return out.includes("BROWSER DONE");
})()'
node "$src/tools/html_render_check.mjs" "http://127.0.0.1:$port/check.html" \
  --wait-ms 120000 --port $((port + 1)) --ready "$inject" \
  --allow "does not compile" --allow "ERROR: 0:" \
  --assert '(() => { const t = document.getElementById("out").textContent; console.log(t); return t.includes("BROWSER DONE") && !t.includes("FAIL"); })()'
