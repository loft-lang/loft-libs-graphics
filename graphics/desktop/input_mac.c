/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * graphics' macOS input, in one process: opens a window through the shim, posts input.sh's
 * sequence into the application's own AppKit queue — a move, left and right clicks, a wheel step
 * each way, `a`, Shift+`b`, Shift+`1`, Return, Left, F5, Delete, Space, Escape — and prints what
 * `gg_poll` made of it in desktop/input.loft's format, for input_mac.sh to compare with
 * input.expected.  Posting into the process's own queue needs no accessibility grant (a CI runner
 * has none) and still takes every event through AppKit and the shim's pump.
 *
 * Keys are built as CoreGraphics events (the modifier keys as FlagsChanged, as the keyboard sends
 * them), the clicks as NSEvents at a point in the window.  The wheel is the exception: an
 * in-process scroll event has no window (the window server sets that on a real one), so AppKit
 * has nothing to route it by — it goes straight to the shim's translation instead, placed where
 * a window's own event would be. */
#include "../c/graphics_gl.c"

#if defined(GG_COCOA)
typedef void *cg_ref;
typedef struct {
    double x, y;
} cg_point;
static cg_ref (*cg_key)(cg_ref, unsigned short, int);
static cg_ref (*cg_scroll)(cg_ref, uint32_t, uint32_t, int32_t, ...);
static void (*cg_set_type)(cg_ref, uint32_t);
static void (*cg_set_flags)(cg_ref, uint64_t);
static void (*cg_set_location)(cg_ref, cg_point);
static void (*cg_release)(cg_ref);
static gg_window *win;
static long win_number;

static void post(gc_id ev) {
    ((void (*)(gc_id, gc_sel, gc_id, signed char)) gc.msg)(gc.app, gc_s("postEvent:atStart:"), ev, 0);
}

static void post_cg(cg_ref e) {
    post(gc_o_p(gc_class("NSEvent"), "eventWithCGEvent:", e));
    cg_release(e);
}

/* A key down or up, with the modifier state `flags` (CoreGraphics' bits are AppKit's). */
static void key(unsigned short code, int press, uint64_t flags) {
    cg_ref e = cg_key(NULL, code, press);
    cg_set_flags(e, flags);
    post_cg(e);
}

static void tap(unsigned short code, uint64_t flags) {
    key(code, 1, flags);
    key(code, 0, flags);
}

static void modifier(unsigned short code, uint64_t flags) {
    cg_ref e = cg_key(NULL, code, 1);
    cg_set_type(e, 12); /* kCGEventFlagsChanged */
    cg_set_flags(e, flags);
    post_cg(e);
}

/* A mouse event at (x, y) from the content's top left. */
static void mouse(unsigned long type, int x, int y) {
    gc_point p;
    p.x = (double) x;
    p.y = (double) (win->height - y);
    post(((gc_id (*)(gc_id, gc_sel, unsigned long, gc_point, unsigned long, double, long, gc_id, long,
                     long, float)) gc.msg)(
        gc_class("NSEvent"),
        gc_s("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:"
             "clickCount:pressure:"),
        type, p, 0ul, 0.0, win_number, NULL, 0l, 1l, 1.0f));
}

/* A wheel step at (x, y) in the content.  A windowless event's `locationInWindow` is the screen
 * point (from the bottom), so the CoreGraphics location (from the top) is chosen to make that the
 * point a window's event would carry: (x, height - y). */
static void wheel(int delta, int x, int y) {
    gc_rect screen = gc_rc(gc_o(gc_class("NSScreen"), "mainScreen"), "frame");
    cg_point at;
    cg_ref e = cg_scroll(NULL, 1, 1, delta); /* kCGScrollEventUnitLine */
    at.x = (double) x;
    at.y = screen.height - (double) (win->height - y);
    cg_set_location(e, at);
    gc_event(win, gc_o_p(gc_class("NSEvent"), "eventWithCGEvent:", e), GC_SCROLL);
    cg_release(e);
}

static void print_events(int64_t h) {
    int64_t n = gg_event_count(h), size = gg_text_size(h), i;
    int64_t *ev = (int64_t *) calloc((size_t) (n > 0 ? n : 1) * GG_EVENT_WORDS, 8u);
    uint8_t *text = (uint8_t *) malloc((size_t) (size > 0 ? size : 1));
    gg_text_copy(h, text, size);
    gg_events(h, ev, n * GG_EVENT_WORDS);
    for (i = 0; i < n; i++) {
        int64_t *e = ev + i * GG_EVENT_WORDS;
        switch (e[0]) {
        case GG_EV_KEY_DOWN: printf("KeyDown %lld %lld %s\n", (long long) e[1], (long long) e[2], e[3] ? "true" : "false"); break;
        case GG_EV_KEY_UP: printf("KeyUp %lld %lld\n", (long long) e[1], (long long) e[2]); break;
        case GG_EV_TEXT: printf("Text %.*s\n", (int) e[2], (const char *) text + e[1]); break;
        case GG_EV_MOUSE_DOWN: printf("MouseDown %lld %lld %lld\n", (long long) e[1], (long long) e[2], (long long) e[3]); break;
        case GG_EV_MOUSE_UP: printf("MouseUp %lld %lld %lld\n", (long long) e[1], (long long) e[2], (long long) e[3]); break;
        case GG_EV_MOUSE_MOVE: printf("MouseMove %lld %lld\n", (long long) e[1], (long long) e[2]); break;
        case GG_EV_WHEEL: printf("Wheel %lld %lld %lld\n", (long long) e[1], (long long) e[2], (long long) e[3]); break;
        case GG_EV_RESIZE: printf("Resize %lld %lld\n", (long long) e[1], (long long) e[2]); break;
        default: break;
        }
    }
    free(ev);
    free(text);
}

#define CG_SYM(var, name) (*(void **) (&var) = dlsym(cg, name), var != NULL)

int main(void) {
    void *cg;
    int64_t h = gg_open(200, 150, "g6-input", 0);
    void *pool;
    int i;
    const uint64_t shift = 1ull << 17;
    if (!h) {
        puts("no window");
        return 2;
    }
    win = gg_of(h);
    win_number = (long) gc_l(win->window, "windowNumber");
    cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_NOW);
    if (!cg || !(CG_SYM(cg_key, "CGEventCreateKeyboardEvent") &&
                 CG_SYM(cg_scroll, "CGEventCreateScrollWheelEvent") &&
                 CG_SYM(cg_set_type, "CGEventSetType") && CG_SYM(cg_set_flags, "CGEventSetFlags") &&
                 CG_SYM(cg_set_location, "CGEventSetLocation") &&
                 CG_SYM(cg_release, "CFRelease"))) {
        puts("no CoreGraphics");
        return 2;
    }
    for (i = 0; i < 10; i++) gg_poll(h); /* the window settled, and key */
    print_events(h);                     /* (its first frames' events are not the sequence's) */
    pool = gc.pool_push();
    mouse(GC_MOVED, 10, 20);
    mouse(GC_MOVED, 50, 60);
    mouse(GC_LEFT_DOWN, 50, 60);
    mouse(GC_LEFT_UP, 50, 60);
    mouse(GC_RIGHT_DOWN, 50, 60);
    mouse(GC_RIGHT_UP, 50, 60);
    for (i = 0; i < 5; i++) gg_poll(h); /* the clicks taken before the wheel joins the queue */
    wheel(1, 50, 60);
    wheel(-1, 50, 60);
    tap(0x00, 0);         /* a */
    modifier(0x38, shift); /* Shift down */
    tap(0x0B, shift);      /* b */
    tap(0x12, shift);      /* 1 */
    modifier(0x38, 0);     /* Shift up */
    tap(0x24, 0);          /* Return */
    tap(0x7B, 0);          /* Left */
    tap(0x60, 0);          /* F5 */
    tap(0x33, 0);          /* Delete */
    tap(0x31, 0);          /* Space */
    tap(0x35, 0);          /* Escape */
    gc.pool_pop(pool);
    for (i = 0; i < 20; i++) {
        gg_poll(h);
        print_events(h);
    }
    printf("held %s mouse %lld %lld buttons %lld\n", gg_key_down(h, 132) ? "true" : "false",
           (long long) gg_mouse(h, 0), (long long) gg_mouse(h, 1), (long long) gg_mouse(h, 2));
    gg_close(h);
    return 0;
}
#else
int main(void) {
    puts("not macOS");
    return 2;
}
#endif
