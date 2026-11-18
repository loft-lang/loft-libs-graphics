/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The X11 window of a Vulkan device: the window, its keys, pointer, typed text and queued
 * events, with no GL context.  `libX11.so.6` is opened at run time with the few types this file
 * uses declared here.
 *
 * Every event is reported as the graphics library's X11 host reports it (gpu_gl's gl_base.h,
 * derived from graphics' c/graphics_gl.c): the same key codes, modifiers, buttons, wheel steps,
 * text and resize events, so a game reads the same input through either backend.  When that
 * host changes how it reports input, this file follows. */
#ifndef GV_X11_HOST_H
#define GV_X11_HOST_H
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

/* ── Xlib, as much as this file uses (Xlib.h, X.h) ─────────────────────────────────────────── */
typedef unsigned long gx_xid;
typedef struct {
    gx_xid background_pixmap;
    unsigned long background_pixel;
    gx_xid border_pixmap;
    unsigned long border_pixel;
    int bit_gravity, win_gravity, backing_store;
    unsigned long backing_planes, backing_pixel;
    int save_under;
    long event_mask, do_not_propagate_mask;
    int override_redirect;
    gx_xid colormap, cursor;
} gx_window_attrs;
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    void *display;
    gx_xid window;
} gx_any_event;
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    void *display;
    gx_xid event, window;
    int x, y, width, height, border_width;
    gx_xid above;
    int override_redirect;
} gx_configure_event;
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    void *display;
    gx_xid window;
    gx_xid message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
} gx_client_event;
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    void *display;
    gx_xid window, root, subwindow;
    unsigned long time;
    int x, y, x_root, y_root;
    unsigned state, detail; /* keycode or button */
    int same_screen;
} gx_input_event; /* XKeyEvent and XButtonEvent share this layout */
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    void *display;
    gx_xid window, root, subwindow;
    unsigned long time;
    int x, y, x_root, y_root;
    unsigned state;
    char is_hint;
    int same_screen;
} gx_motion_event;
typedef union {
    int type;
    gx_any_event any;
    gx_configure_event configure;
    gx_client_event client;
    gx_input_event input;
    gx_motion_event motion;
    long pad[24];
} gx_event;

#define GX_INPUT_OUTPUT 1
#define GX_CW_BACK_PIXEL (1L << 1)
#define GX_CW_BORDER_PIXEL (1L << 3)
#define GX_CW_EVENT_MASK (1L << 11)
#define GX_KEY_PRESS_MASK (1L << 0)
#define GX_KEY_RELEASE_MASK (1L << 1)
#define GX_BUTTON_PRESS_MASK (1L << 2)
#define GX_BUTTON_RELEASE_MASK (1L << 3)
#define GX_POINTER_MOTION_MASK (1L << 6)
#define GX_EXPOSURE_MASK (1L << 15)
#define GX_STRUCTURE_NOTIFY_MASK (1L << 17)
#define GX_FOCUS_CHANGE_MASK (1L << 21)
#define GX_MAP_NOTIFY 19
#define GX_CONFIGURE_NOTIFY 22
#define GX_DESTROY_NOTIFY 17
#define GX_CLIENT_MESSAGE 33
#define GX_ATOM_ATOM 4
#define GX_KEY_PRESS 2
#define GX_KEY_RELEASE 3
#define GX_BUTTON_PRESS 4
#define GX_BUTTON_RELEASE 5
#define GX_MOTION_NOTIFY 6
#define GX_SHIFT_MASK (1 << 0)
#define GX_CONTROL_MASK (1 << 2)
#define GX_MOD1_MASK (1 << 3)
#define GX_MOD4_MASK (1 << 6)
#define GX_XIM_PREEDIT_NOTHING 0x0008L
#define GX_XIM_STATUS_NOTHING 0x0400L
#define GX_PROP_MODE_REPLACE 0

/* The X entry points a window calls, resolved when it opens. */
typedef struct {
    void *(*XOpenDisplay)(const char *);
    int (*XCloseDisplay)(void *);
    int (*XDefaultScreen)(void *);
    gx_xid (*XRootWindow)(void *, int);
    int (*XDisplayWidth)(void *, int);
    int (*XDisplayHeight)(void *, int);
    gx_xid (*XCreateWindow)(void *, gx_xid, int, int, unsigned, unsigned, unsigned, int,
                            unsigned, void *, unsigned long, gx_window_attrs *);
    int (*XDestroyWindow)(void *, gx_xid);
    int (*XStoreName)(void *, gx_xid, const char *);
    gx_xid (*XInternAtom)(void *, const char *, int);
    int (*XSetWMProtocols)(void *, gx_xid, gx_xid *, int);
    int (*XChangeProperty)(void *, gx_xid, gx_xid, gx_xid, int, int, const unsigned char *, int);
    int (*XMapWindow)(void *, gx_xid);
    int (*XPending)(void *);
    int (*XNextEvent)(void *, gx_event *);
    int (*XLookupString)(gx_input_event *, char *, int, gx_xid *, void *);
    int (*XkbSetDetectableAutoRepeat)(void *, int, int *);
    void *(*XOpenIM)(void *, void *, char *, char *);
    int (*XCloseIM)(void *);
    void *(*XCreateIC)(void *, ...);
    void (*XDestroyIC)(void *);
    int (*Xutf8LookupString)(void *, gx_input_event *, char *, int, gx_xid *, int *);
    int (*XFilterEvent)(gx_event *, gx_xid);
} gv_x_api;

/* One queued input event: an EV_* tag (screen's and graphics' numbering) and four fields. */
#define GV_EVENT_WORDS 5
#define GV_EV_KEY_DOWN 1
#define GV_EV_KEY_UP 2
#define GV_EV_TEXT 3
#define GV_EV_MOUSE_DOWN 4
#define GV_EV_MOUSE_UP 5
#define GV_EV_MOUSE_MOVE 6
#define GV_EV_WHEEL 7
#define GV_EV_RESIZE 11
#define GV_MOD_SHIFT 1
#define GV_MOD_CTRL 2
#define GV_MOD_ALT 4
#define GV_MOD_SUPER 8

/* A window and what its input holds now. */
typedef struct {
    gv_x_api xa;
    void *libx11;
    void *display;
    gx_xid window, wm_delete;
    void *im, *ic; /* the input method and context, when the server has one */
    int width, height, should_close;
    int64_t *events;
    int64_t event_count, event_cap;
    char *text; /* the UTF-8 of the queued `Text` events, end to end */
    int64_t text_len, text_cap;
    unsigned char keys[256]; /* which key codes are down */
    int mouse_x, mouse_y, buttons;
} gv_x_window;

#define GV_XSYM(name) (*(void **) (&x->xa.name) = dlsym(x->libx11, #name), x->xa.name != NULL)

/* libX11 stays loaded once a window opened it (`RTLD_NODELETE`), as graphics keeps it. */
static int gv_x_bind(gv_x_window *x) {
    x->libx11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!x->libx11) return 0;
    return GV_XSYM(XOpenDisplay) && GV_XSYM(XCloseDisplay) && GV_XSYM(XDefaultScreen) &&
           GV_XSYM(XRootWindow) && GV_XSYM(XDisplayWidth) && GV_XSYM(XDisplayHeight) &&
           GV_XSYM(XCreateWindow) && GV_XSYM(XDestroyWindow) && GV_XSYM(XStoreName) &&
           GV_XSYM(XInternAtom) && GV_XSYM(XSetWMProtocols) && GV_XSYM(XChangeProperty) &&
           GV_XSYM(XMapWindow) && GV_XSYM(XPending) && GV_XSYM(XNextEvent) &&
           GV_XSYM(XLookupString) && GV_XSYM(XkbSetDetectableAutoRepeat) && GV_XSYM(XOpenIM) &&
           GV_XSYM(XCloseIM) && GV_XSYM(XCreateIC) && GV_XSYM(XDestroyIC) &&
           GV_XSYM(Xutf8LookupString) && GV_XSYM(XFilterEvent);
}

static void gv_x_push(gv_x_window *x, int64_t tag, int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t *slot;
    if (x->event_count == x->event_cap) {
        int64_t cap = x->event_cap ? x->event_cap * 2 : 16;
        int64_t *grown = (int64_t *) realloc(x->events, (size_t) cap * GV_EVENT_WORDS * 8u);
        if (!grown) return;
        x->events = grown;
        x->event_cap = cap;
    }
    slot = x->events + x->event_count * GV_EVENT_WORDS;
    slot[0] = tag;
    slot[1] = a;
    slot[2] = b;
    slot[3] = c;
    slot[4] = d;
    x->event_count++;
}

static void gv_x_text_push(gv_x_window *x, const char *bytes, int n) {
    if (x->text_len + n > x->text_cap) {
        int64_t cap = (x->text_cap ? x->text_cap * 2 : 64) + n;
        char *grown = (char *) realloc(x->text, (size_t) cap);
        if (!grown) return;
        x->text = grown;
        x->text_cap = cap;
    }
    gv_x_push(x, GV_EV_TEXT, x->text_len, n, 0, 0);
    memcpy(x->text + x->text_len, bytes, (size_t) n);
    x->text_len += n;
}

/* The key codes of screen::keys(): ASCII lower-cased for a character, their own codes for the
 * named keys, -1 for a key that has none. */
static int64_t gv_x_key_code(gx_xid sym) {
    if (sym >= 0x20 && sym <= 0x7e) return sym >= 'A' && sym <= 'Z' ? (int64_t) sym + 32 : (int64_t) sym;
    if (sym >= 0xffbe && sym <= 0xffc9) return 135 + (int64_t) (sym - 0xffbe); /* F1-F12 */
    if (sym >= 0xffb0 && sym <= 0xffb9) return '0' + (int64_t) (sym - 0xffb0); /* keypad digits */
    switch (sym) {
    case 0xff0d: case 0xff8d: return 13; /* Return, KP_Enter */
    case 0xff1b: return 27;  /* Escape */
    case 0xff09: return 9;   /* Tab */
    case 0xff08: return 8;   /* BackSpace */
    case 0xff52: return 128; /* Up */
    case 0xff54: return 129; /* Down */
    case 0xff51: return 130; /* Left */
    case 0xff53: return 131; /* Right */
    case 0xffe1: case 0xffe2: return 132; /* Shift */
    case 0xffe3: case 0xffe4: return 133; /* Control */
    case 0xffff: return 147; /* Delete */
    case 0xff63: return 148; /* Insert */
    case 0xff50: return 149; /* Home */
    case 0xff57: return 150; /* End */
    case 0xff55: return 151; /* Page_Up */
    case 0xff56: return 152; /* Page_Down */
    default: return -1;
    }
}

static int64_t gv_x_mods(unsigned state) {
    return ((state & GX_SHIFT_MASK) ? GV_MOD_SHIFT : 0) | ((state & GX_CONTROL_MASK) ? GV_MOD_CTRL : 0) |
           ((state & GX_MOD1_MASK) ? GV_MOD_ALT : 0) | ((state & GX_MOD4_MASK) ? GV_MOD_SUPER : 0);
}

/* A press: KeyDown (a repeat when the key is already down) and, when it types anything, a Text;
 * a release: KeyUp.  The modifiers are the state before the event, as X reports them. */
static void gv_x_key(gv_x_window *x, gx_input_event *e, int press) {
    char buf[64];
    gx_xid sym = 0;
    int n, status = 0;
    int64_t code;
    unsigned state = e->state;
    if (press && x->ic)
        n = x->xa.Xutf8LookupString(x->ic, e, buf, (int) sizeof buf - 1, &sym, &status);
    else
        n = x->xa.XLookupString(e, buf, (int) sizeof buf - 1, &sym, NULL);
    if (n < 0) n = 0;
    code = gv_x_key_code(sym);
    if (press) {
        int repeat = code >= 0 && code < 256 && x->keys[code];
        if (code >= 0 && code < 256) x->keys[code] = 1;
        gv_x_push(x, GV_EV_KEY_DOWN, code, gv_x_mods(state), repeat, 0);
        if (n > 0) gv_x_text_push(x, buf, n);
    } else {
        if (code >= 0 && code < 256) x->keys[code] = 0;
        gv_x_push(x, GV_EV_KEY_UP, code, gv_x_mods(state), 0, 0);
    }
}

/* X buttons 1-3 are left, middle, right — 1, 4 and 2 here; 4 and 5 are a wheel step up and down. */
static void gv_x_button(gv_x_window *x, gx_input_event *e, int press) {
    int bit = e->detail == 1 ? 1 : e->detail == 2 ? 4 : e->detail == 3 ? 2 : 0;
    x->mouse_x = e->x;
    x->mouse_y = e->y;
    if (e->detail == 4 || e->detail == 5) {
        if (press) gv_x_push(x, GV_EV_WHEEL, e->detail == 4 ? 1 : -1, e->x, e->y, 0);
        return;
    }
    if (!bit) return;
    if (press) x->buttons |= bit;
    else x->buttons &= ~bit;
    gv_x_push(x, press ? GV_EV_MOUSE_DOWN : GV_EV_MOUSE_UP, bit, e->x, e->y, 0);
}

static void gv_x_close(gv_x_window *x) {
    if (x->ic) x->xa.XDestroyIC(x->ic);
    if (x->im) x->xa.XCloseIM(x->im);
    x->ic = x->im = NULL;
    if (x->display) {
        if (x->window) x->xa.XDestroyWindow(x->display, x->window);
        x->xa.XCloseDisplay(x->display);
    }
    x->display = NULL;
    x->window = 0;
    free(x->events);
    free(x->text);
    x->events = NULL;
    x->text = NULL;
    x->event_count = x->event_cap = x->text_len = x->text_cap = 0;
}

/* Open the display.  A server that still counts the connections just closed resets a new one:
 * under Xvfb one open in a thousand, more when several programs open at once.  It is asked
 * again for up to half a second.  A display that is not there refuses, and is not asked twice. */
static void *gv_x_open_display(gv_x_window *x) {
    struct timeval wait;
    int tries;
    for (tries = 0; tries < 50; tries++) {
        void *display;
        errno = 0;
        display = x->xa.XOpenDisplay(NULL);
        if (display || (errno != ECONNRESET && errno != EAGAIN)) return display;
        wait.tv_sec = 0;
        wait.tv_usec = 10000;
        select(0, NULL, NULL, NULL, &wait);
    }
    return NULL;
}

/* Open a window of `width` x `height` (the screen's size when `fullscreen`), mapped, on the
 * server's default visual; 0 when there is no X server. */
static int gv_x_open(gv_x_window *x, int64_t width, int64_t height, const char *title,
                     int64_t fullscreen) {
    gx_window_attrs attrs;
    gx_xid root;
    int screen;
    if (!gv_x_bind(x)) return 0;
    x->display = gv_x_open_display(x);
    if (!x->display) return 0;
    screen = x->xa.XDefaultScreen(x->display);
    root = x->xa.XRootWindow(x->display, screen);
    if (fullscreen) {
        width = x->xa.XDisplayWidth(x->display, screen);
        height = x->xa.XDisplayHeight(x->display, screen);
    }
    if (width <= 0 || height <= 0) return 0;
    memset(&attrs, 0, sizeof attrs);
    attrs.event_mask = GX_STRUCTURE_NOTIFY_MASK | GX_EXPOSURE_MASK | GX_KEY_PRESS_MASK |
                       GX_KEY_RELEASE_MASK | GX_BUTTON_PRESS_MASK | GX_BUTTON_RELEASE_MASK |
                       GX_POINTER_MOTION_MASK | GX_FOCUS_CHANGE_MASK;
    /* depth and visual copied from the root (0, NULL); black until the first frame is shown */
    x->window = x->xa.XCreateWindow(x->display, root, 0, 0, (unsigned) width, (unsigned) height,
                                    0, 0, GX_INPUT_OUTPUT, NULL,
                                    GX_CW_BACK_PIXEL | GX_CW_BORDER_PIXEL | GX_CW_EVENT_MASK,
                                    &attrs);
    if (!x->window) return 0;
    x->xa.XStoreName(x->display, x->window, title);
    x->wm_delete = x->xa.XInternAtom(x->display, "WM_DELETE_WINDOW", 0);
    x->xa.XSetWMProtocols(x->display, x->window, &x->wm_delete, 1);
    /* auto-repeat as press after press, no release between */
    x->xa.XkbSetDetectableAutoRepeat(x->display, 1, NULL);
    x->im = x->xa.XOpenIM(x->display, NULL, NULL, NULL);
    if (x->im)
        x->ic = x->xa.XCreateIC(x->im, "inputStyle", GX_XIM_PREEDIT_NOTHING | GX_XIM_STATUS_NOTHING,
                                "clientWindow", x->window, "focusWindow", x->window, NULL);
    if (fullscreen) {
        gx_xid state = x->xa.XInternAtom(x->display, "_NET_WM_STATE", 0);
        gx_xid full = x->xa.XInternAtom(x->display, "_NET_WM_STATE_FULLSCREEN", 0);
        x->xa.XChangeProperty(x->display, x->window, state, GX_ATOM_ATOM, 32,
                              GX_PROP_MODE_REPLACE, (const unsigned char *) &full, 1);
    }
    x->xa.XMapWindow(x->display, x->window);
    for (;;) { /* until the server says the window is mapped */
        gx_event ev;
        x->xa.XNextEvent(x->display, &ev);
        if (ev.type == GX_MAP_NOTIFY && ev.any.window == x->window) break;
        if (ev.type == GX_CONFIGURE_NOTIFY && ev.configure.window == x->window) {
            width = ev.configure.width;
            height = ev.configure.height;
        }
    }
    x->width = (int) width;
    x->height = (int) height;
    return 1;
}

/* Drain the window's pending events.  A new size is queued as a `Resize` and kept; the window
 * manager's close request ends the loop.  Answers whether the size changed. */
static int gv_x_poll(gv_x_window *x) {
    int width = x->width, height = x->height;
    while (x->xa.XPending(x->display) > 0) {
        gx_event ev;
        x->xa.XNextEvent(x->display, &ev);
        if (x->ic && x->xa.XFilterEvent(&ev, 0)) continue; /* the input method took it */
        switch (ev.type) {
        case GX_KEY_PRESS:
        case GX_KEY_RELEASE:
            gv_x_key(x, &ev.input, ev.type == GX_KEY_PRESS);
            break;
        case GX_BUTTON_PRESS:
        case GX_BUTTON_RELEASE:
            gv_x_button(x, &ev.input, ev.type == GX_BUTTON_PRESS);
            break;
        case GX_MOTION_NOTIFY:
            x->mouse_x = ev.motion.x;
            x->mouse_y = ev.motion.y;
            gv_x_push(x, GV_EV_MOUSE_MOVE, ev.motion.x, ev.motion.y, 0, 0);
            break;
        case GX_CONFIGURE_NOTIFY:
            if (ev.configure.width > 0 && ev.configure.height > 0) {
                width = ev.configure.width;
                height = ev.configure.height;
            }
            break;
        case GX_CLIENT_MESSAGE:
            if ((gx_xid) ev.client.data.l[0] == x->wm_delete) x->should_close = 1;
            break;
        case GX_DESTROY_NOTIFY:
            x->should_close = 1;
            break;
        default:
            break;
        }
    }
    if (width != x->width || height != x->height) {
        x->width = width;
        x->height = height;
        gv_x_push(x, GV_EV_RESIZE, width, height, 0, 0);
        return 1;
    }
    return 0;
}

/* Copy the queued events out, GV_EVENT_WORDS integers each, oldest first, and empty the queue. */
static void gv_x_events(gv_x_window *x, int64_t *out, int64_t n) {
    int64_t words = x->event_count * GV_EVENT_WORDS;
    if (n < words) words = n - n % GV_EVENT_WORDS;
    if (words > 0) memcpy(out, x->events, (size_t) words * 8u);
    x->event_count = 0;
    x->text_len = 0;
}

#endif
