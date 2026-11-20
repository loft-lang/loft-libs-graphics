#!/usr/bin/env python3
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
"""The same input sent to a window of each backend gives the same events.

    xvfb-run -a gpu_host/parity/events.py [backend ...]        default: gpu_gl gpu_vulkan

For each backend it starts `events.loft`, finds its window, and sends through the X server's
test extension: the pointer moved in, a key, a shifted key, a click, the wheel both ways, a
second move, a resize and Escape.  The lines the programs print are compared; exit 1 and both
listings when they differ.  Needs a display (X11) and libXtst; `LOFT` names the loft to run.
"""
import ctypes
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(HERE)
X = ctypes.CDLL("libX11.so.6")
T = ctypes.CDLL("libXtst.so.6")
X.XOpenDisplay.restype = ctypes.c_void_p
X.XDefaultRootWindow.restype = ctypes.c_ulong
X.XStringToKeysym.restype = ctypes.c_ulong
for f in (X.XDefaultRootWindow, X.XFlush, X.XSync):
    f.argtypes = [ctypes.c_void_p] + ([ctypes.c_int] if f is X.XSync else [])
X.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
X.XKeysymToKeycode.restype = ctypes.c_ubyte


def window_named(dpy, name):
    root = X.XDefaultRootWindow(dpy)
    r, p = ctypes.c_ulong(), ctypes.c_ulong()
    kids, n = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
    X.XQueryTree(ctypes.c_void_p(dpy), ctypes.c_ulong(root), ctypes.byref(r), ctypes.byref(p),
                 ctypes.byref(kids), ctypes.byref(n))
    found = 0
    for i in range(n.value):
        got = ctypes.c_char_p()
        if X.XFetchName(ctypes.c_void_p(dpy), ctypes.c_ulong(kids[i]), ctypes.byref(got)) and got.value:
            if got.value.decode(errors="replace") == name:
                found = kids[i]
    return found


def origin(dpy, win):
    x, y, child = ctypes.c_int(), ctypes.c_int(), ctypes.c_ulong()
    X.XTranslateCoordinates(ctypes.c_void_p(dpy), ctypes.c_ulong(win),
                            ctypes.c_ulong(X.XDefaultRootWindow(dpy)), 0, 0,
                            ctypes.byref(x), ctypes.byref(y), ctypes.byref(child))
    return x.value, y.value


def heard(backend):
    loft = os.environ.get("LOFT", "loft")
    proc = subprocess.Popen([loft, "--interpret", "--lib", os.path.dirname(PKG), "parity/events.loft", backend],
                            cwd=PKG, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    dpy = X.XOpenDisplay(None)
    if not dpy:
        raise SystemExit("events.py: no display")
    win = 0
    for _ in range(200):
        win = window_named(dpy, "events-" + backend)
        if win or proc.poll() is not None:
            break
        time.sleep(0.05)
    if not win:
        out = proc.communicate()[0]
        raise SystemExit(f"events.py: no window of {backend}: {out.strip()}")
    time.sleep(0.5)  # mapped and first frames shown
    wx, wy = origin(dpy, win)
    d = ctypes.c_void_p(dpy)

    def step():
        X.XSync(d, 0)
        time.sleep(0.15)

    def key(sym, down):
        code = X.XKeysymToKeycode(d, X.XStringToKeysym(sym.encode()))
        T.XTestFakeKeyEvent(d, ctypes.c_uint(code), ctypes.c_int(1 if down else 0), ctypes.c_ulong(0))
        step()

    def button(n, down):
        T.XTestFakeButtonEvent(d, ctypes.c_uint(n), ctypes.c_int(1 if down else 0), ctypes.c_ulong(0))
        step()

    def move(x, y):
        T.XTestFakeMotionEvent(d, ctypes.c_int(0), ctypes.c_int(wx + x), ctypes.c_int(wy + y), ctypes.c_ulong(0))
        step()

    move(30, 40)
    key("a", True); key("a", False)
    key("Shift_L", True); key("a", True); key("a", False); key("Shift_L", False)
    key("Home", True); key("Home", False)
    button(1, True); button(1, False)
    button(4, True); button(4, False)
    button(5, True); button(5, False)
    move(50, 60)
    X.XResizeWindow(d, ctypes.c_ulong(win), ctypes.c_uint(240), ctypes.c_uint(180))
    step(); step()
    key("Escape", True); key("Escape", False)
    try:
        out = proc.communicate(timeout=20)[0]
    except subprocess.TimeoutExpired:
        proc.kill()
        out = proc.communicate()[0]
    X.XCloseDisplay(d)
    return [line for line in out.splitlines() if line.startswith(("event", "typed", "size", "no window"))]


def main():
    backends = sys.argv[1:] or ["gpu_gl", "gpu_vulkan"]
    listings = [heard(b) for b in backends]
    same = all(l == listings[0] for l in listings[1:])
    if not same or "-v" in os.environ.get("EVENTS_PARITY", ""):
        for b, l in zip(backends, listings):
            print(f"== {b}: {len(l)} lines")
            print("\n".join(l))
    if not listings[0] or len(listings[0]) < 10:
        print(f"events.py: {backends[0]} heard too little to compare ({len(listings[0])} lines)")
        return 1
    print(f"events.py: {' and '.join(backends)} heard {'the same' if same else 'DIFFERENT'} {len(listings[0])} lines")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
