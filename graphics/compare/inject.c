/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Drives a window titled argv[1] through XTest (libXtst, opened at run time): pointer moves,
 * clicks, the wheel, and keys with Shift — the input half of compare/input.sh (@PLN208 G6). */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

static int (*fake_key)(Display *, unsigned, int, unsigned long);
static int (*fake_button)(Display *, unsigned, int, unsigned long);
static int (*fake_motion)(Display *, int, int, int, unsigned long);

static Window find(Display *d, Window w, const char *title) {
    char *name = NULL;
    Window root, parent, *kids = NULL, hit = 0;
    unsigned n = 0, i;
    if (XFetchName(d, w, &name) && name) {
        int same = strcmp(name, title) == 0;
        XFree(name);
        if (same) return w;
    }
    if (XQueryTree(d, w, &root, &parent, &kids, &n)) {
        for (i = 0; i < n && !hit; i++) hit = find(d, kids[i], title);
        if (kids) XFree(kids);
    }
    return hit;
}

static void key(Display *d, KeySym s, int press) {
    fake_key(d, XKeysymToKeycode(d, s), press, 0);
    XSync(d, False);
    usleep(30000);
}

static void tap(Display *d, KeySym s) {
    key(d, s, 1);
    key(d, s, 0);
}

static void button(Display *d, unsigned b, int press) {
    fake_button(d, b, press, 0);
    XSync(d, False);
    usleep(30000);
}

int main(int argc, char **argv) {
    Display *d = XOpenDisplay(NULL);
    void *xtst = dlopen("libXtst.so.6", RTLD_NOW);
    Window w = 0;
    int tries, x0, y0;
    Window child;
    if (!d || !xtst || argc < 2) return 2;
    *(void **) &fake_key = dlsym(xtst, "XTestFakeKeyEvent");
    *(void **) &fake_button = dlsym(xtst, "XTestFakeButtonEvent");
    *(void **) &fake_motion = dlsym(xtst, "XTestFakeMotionEvent");
    for (tries = 0; tries < 200 && !w; tries++) {
        w = find(d, DefaultRootWindow(d), argv[1]);
        if (!w) usleep(50000);
    }
    if (!w) return 3;
    usleep(300000);
    XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &x0, &y0, &child);
    fake_motion(d, -1, x0 + 10, y0 + 20, 0);
    XSync(d, False);
    usleep(50000);
    XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XSync(d, False);
    usleep(50000);
    fake_motion(d, -1, x0 + 50, y0 + 60, 0);
    XSync(d, False);
    usleep(50000);
    button(d, 1, 1);
    button(d, 1, 0);
    button(d, 3, 1);
    button(d, 3, 0);
    button(d, 4, 1);
    button(d, 4, 0);
    button(d, 5, 1);
    button(d, 5, 0);
    tap(d, XK_a);
    key(d, XK_Shift_L, 1);
    tap(d, XK_b);
    tap(d, XK_1);
    key(d, XK_Shift_L, 0);
    tap(d, XK_Return);
    tap(d, XK_Left);
    tap(d, XK_F5);
    tap(d, XK_BackSpace);
    tap(d, XK_space);
    tap(d, XK_Escape);
    XCloseDisplay(d);
    return 0;
}
