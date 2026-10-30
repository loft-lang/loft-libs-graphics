/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * The graphics library's window and GL context, in C99: a `[c] shim` loft builds itself, bound by
 * the `#c` declarations in src/graphics.loft.  A window is a handle (its record's address as an
 * integer, 0 for none) that the loft `Window` holds and closes when it dies.  A shader or a mesh
 * is a handle too, holding a counted reference to its window: released after the window closed it
 * is a no-op (closing the context freed it), and the window's record lives until the last one.
 *
 * Linux: X11 and GLX, opened at run time (`libX11.so.6`, `libGL.so.1`) with the few types and
 * constants this file uses declared here, so it builds with loft's fixed flags and no development
 * package.  Everything a window needs — the display connection, the context, the function pointers
 * — lives in its own record: nothing is shared between windows, and nothing outlives the last one.
 *
 * Other hosts (Windows, macOS, the wasm targets) open no window yet: `gg_open` answers 0, which
 * the loft side reports as a null `Window`. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int64_t gg_open(int64_t width, int64_t height, const char *title, int64_t fullscreen);
int64_t gg_poll(int64_t h);
void gg_swap(int64_t h);
void gg_clear(int64_t h, int64_t color);
void gg_close(int64_t h);
int64_t gg_width(int64_t h);
int64_t gg_height(int64_t h);
int64_t gg_event_count(int64_t h);
void gg_events(int64_t h, int64_t *out, int64_t n);
void gg_read_pixels(int64_t h, int64_t *out, int64_t n);
int64_t gg_shader(int64_t win, const char *vertex, const char *fragment);
void gg_uniform_f(int64_t sh, const char *name, const double *v, int64_t n);
void gg_uniform_i(int64_t sh, const char *name, int64_t v);
int64_t gg_mesh(int64_t win, const float *data, int64_t n, int64_t stride);
void gg_draw(int64_t mesh, int64_t shader);
void gg_release(int64_t res);
void gg_capability(int64_t win, int64_t cap, int64_t on);
void gg_blend(int64_t win, int64_t src, int64_t dst);
void gg_cull(int64_t win, int64_t face);
void gg_depth_write(int64_t win, int64_t on);
void gg_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height);
void gg_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height);
void gg_finish(int64_t win);
void gg_line_width(int64_t win, const double *width, int64_t n);
void gg_point_size(int64_t win, const double *size, int64_t n);

/* One queued input event: an EV_* tag (src/graphics.loft) and up to four integer fields. */
#define GG_EVENT_WORDS 5
#define GG_EV_RESIZE 11

#if defined(__linux__) && !defined(__wasm__)

#include <dlfcn.h>
#include <pthread.h>

/* ── Xlib, as much as this file uses (Xlib.h, X.h) ─────────────────────────────────────────── */
typedef unsigned long gx_xid;
typedef struct {
    void *visual;
    unsigned long visualid;
    int screen, depth, class_;
    unsigned long red_mask, green_mask, blue_mask;
    int colormap_size, bits_per_rgb;
} gx_visual_info;
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
typedef union {
    int type;
    gx_any_event any;
    gx_configure_event configure;
    gx_client_event client;
    long pad[24];
} gx_event;
typedef struct {
    int type;
    void *display;
    gx_xid resourceid;
    unsigned long serial;
    unsigned char error_code, request_code, minor_code;
} gx_error_event;

#define GX_INPUT_OUTPUT 1
#define GX_ALLOC_NONE 0
#define GX_CW_BORDER_PIXEL (1L << 3)
#define GX_CW_EVENT_MASK (1L << 11)
#define GX_CW_COLORMAP (1L << 13)
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
#define GX_PROP_MODE_REPLACE 0

/* ── GLX and GL (glx.h, glxext.h, gl.h) ─────────────────────────────────────────────────────── */
#define GLX_DOUBLEBUFFER 5
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10
#define GLX_ALPHA_SIZE 11
#define GLX_DEPTH_SIZE 12
#define GLX_STENCIL_SIZE 13
#define GLX_X_VISUAL_TYPE 0x22
#define GLX_TRUE_COLOR 0x8002
#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_RENDER_TYPE 0x8011
#define GLX_X_RENDERABLE 0x8012
#define GLX_WINDOW_BIT 0x1
#define GLX_RGBA_BIT 0x1
#define GLX_SAMPLE_BUFFERS 100000
#define GLX_SAMPLES 100001
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x1

#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_TEST 0x0B71
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_BACK 0x0405
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_SCISSOR_TEST 0x0C11
#define GL_FRONT 0x0404
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305

typedef void *gx_fbconfig;
typedef void *gx_context;
typedef int (*gx_error_handler)(void *, gx_error_event *);

/* Every entry point a window calls, resolved when it opens. */
typedef struct {
    void *(*XOpenDisplay)(const char *);
    int (*XCloseDisplay)(void *);
    int (*XDefaultScreen)(void *);
    gx_xid (*XRootWindow)(void *, int);
    int (*XDisplayWidth)(void *, int);
    int (*XDisplayHeight)(void *, int);
    gx_xid (*XCreateColormap)(void *, gx_xid, void *, int);
    int (*XFreeColormap)(void *, gx_xid);
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
    int (*XSync)(void *, int);
    int (*XFree)(void *);
    gx_error_handler (*XSetErrorHandler)(gx_error_handler);

    gx_fbconfig *(*glXChooseFBConfig)(void *, int, const int *, int *);
    int (*glXGetFBConfigAttrib)(void *, gx_fbconfig, int, int *);
    gx_visual_info *(*glXGetVisualFromFBConfig)(void *, gx_fbconfig);
    void *(*glXGetProcAddressARB)(const unsigned char *);
    gx_context (*glXCreateContextAttribsARB)(void *, gx_fbconfig, gx_context, int, const int *);
    int (*glXMakeCurrent)(void *, gx_xid, gx_context);
    void (*glXSwapBuffers)(void *, gx_xid);
    void (*glXDestroyContext)(void *, gx_context);
    void (*glXSwapIntervalEXT)(void *, gx_xid, int);

    void (*glClearColor)(float, float, float, float);
    void (*glClear)(unsigned);
    void (*glColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
    void (*glViewport)(int, int, int, int);
    void (*glEnable)(unsigned);
    void (*glReadBuffer)(unsigned);
    void (*glPixelStorei)(unsigned, int);
    void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *);

    unsigned (*glCreateShader)(unsigned);
    void (*glShaderSource)(unsigned, int, const char *const *, const int *);
    void (*glCompileShader)(unsigned);
    void (*glGetShaderiv)(unsigned, unsigned, int *);
    void (*glGetShaderInfoLog)(unsigned, int, int *, char *);
    void (*glDeleteShader)(unsigned);
    unsigned (*glCreateProgram)(void);
    void (*glAttachShader)(unsigned, unsigned);
    void (*glLinkProgram)(unsigned);
    void (*glGetProgramiv)(unsigned, unsigned, int *);
    void (*glGetProgramInfoLog)(unsigned, int, int *, char *);
    void (*glDeleteProgram)(unsigned);
    void (*glUseProgram)(unsigned);
    int (*glGetUniformLocation)(unsigned, const char *);
    void (*glUniform1f)(int, float);
    void (*glUniform2f)(int, float, float);
    void (*glUniform3f)(int, float, float, float);
    void (*glUniform4f)(int, float, float, float, float);
    void (*glUniform1i)(int, int);
    void (*glUniformMatrix4fv)(int, int, unsigned char, const float *);
    void (*glGenVertexArrays)(int, unsigned *);
    void (*glBindVertexArray)(unsigned);
    void (*glDeleteVertexArrays)(int, const unsigned *);
    void (*glGenBuffers)(int, unsigned *);
    void (*glBindBuffer)(unsigned, unsigned);
    void (*glBufferData)(unsigned, intptr_t, const void *, unsigned);
    void (*glDeleteBuffers)(int, const unsigned *);
    void (*glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
    void (*glEnableVertexAttribArray)(unsigned);
    void (*glDrawArrays)(unsigned, int, int);
    void (*glDisable)(unsigned);
    void (*glBlendFunc)(unsigned, unsigned);
    void (*glCullFace)(unsigned);
    void (*glDepthMask)(unsigned char);
    void (*glScissor)(int, int, int, int);
    void (*glFinish)(void);
    void (*glLineWidth)(float);
    void (*glPointSize)(float);
} gg_api;

typedef struct {
    void *libx11, *libgl;
    gg_api api;
    void *display;
    gx_xid window, colormap, wm_delete;
    gx_context context;
    pthread_t thread;
    int width, height, should_close;
    int closed; /* `gg_close` ran: the context and the X window are gone */
    int64_t refs; /* the shaders and meshes still holding this window */
    int64_t *events;
    int64_t event_count, event_cap;
} gg_window;

/* A shader or a mesh: its GL names and the window whose context made them. */
enum { GG_SHADER = 1, GG_MESH = 2 };
typedef struct {
    gg_window *w;
    int kind;
    unsigned id, buffer; /* program; or vertex array and its buffer */
    int vertices;
} gg_res;

static gg_window *gg_of(int64_t h) {
    return (gg_window *) (intptr_t) h;
}

static gg_res *gg_res_of(int64_t h) {
    return (gg_res *) (intptr_t) h;
}

/* The window whose context is current on this thread, so a call on another window's resource
 * rebinds only when the window changes.  Speed only: every call names its own window. */
static __thread gg_window *gg_current;

/* A GL call from another thread than the window's finds no current context there: it would draw
 * nothing, or into another window.  Refused, and said so. */
static int gg_on_thread(gg_window *w, const char *what) {
    if (pthread_equal(w->thread, pthread_self())) return 1;
    fprintf(stderr, "graphics: %s called from a thread other than the one that opened the "
                    "window; a window is used on its own thread only\n", what);
    return 0;
}

/* Ready `w` for a GL call named `what`: open, on its thread, its context current. */
static int gg_use(gg_window *w, const char *what) {
    if (!w || w->closed || !gg_on_thread(w, what)) return 0;
    if (gg_current != w) {
        w->api.glXMakeCurrent(w->display, w->window, w->context);
        gg_current = w;
    }
    return 1;
}

#define GG_SYM(lib, name)                                                                         \
    (*(void **) (&w->api.name) = dlsym(w->lib, #name), w->api.name != NULL)

/* The libraries stay loaded once a window opened them (`RTLD_NODELETE`): Mesa does not unload
 * cleanly — each unload and reload left 10 MiB of address space behind and cost a second of LLVM
 * start-up, so the 200th window of a program that opens them in turn failed to open. */
static int gg_bind(gg_window *w) {
    w->libx11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    w->libgl = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!w->libx11 || !w->libgl) return 0;
    if (!(GG_SYM(libx11, XOpenDisplay) && GG_SYM(libx11, XCloseDisplay) &&
          GG_SYM(libx11, XDefaultScreen) && GG_SYM(libx11, XRootWindow) &&
          GG_SYM(libx11, XDisplayWidth) && GG_SYM(libx11, XDisplayHeight) &&
          GG_SYM(libx11, XCreateColormap) && GG_SYM(libx11, XFreeColormap) &&
          GG_SYM(libx11, XCreateWindow) && GG_SYM(libx11, XDestroyWindow) &&
          GG_SYM(libx11, XStoreName) && GG_SYM(libx11, XInternAtom) &&
          GG_SYM(libx11, XSetWMProtocols) && GG_SYM(libx11, XChangeProperty) &&
          GG_SYM(libx11, XMapWindow) && GG_SYM(libx11, XPending) &&
          GG_SYM(libx11, XNextEvent) && GG_SYM(libx11, XSync) && GG_SYM(libx11, XFree) &&
          GG_SYM(libx11, XSetErrorHandler)))
        return 0;
    if (!(GG_SYM(libgl, glXChooseFBConfig) && GG_SYM(libgl, glXGetFBConfigAttrib) &&
          GG_SYM(libgl, glXGetVisualFromFBConfig) && GG_SYM(libgl, glXGetProcAddressARB) &&
          GG_SYM(libgl, glXMakeCurrent) && GG_SYM(libgl, glXSwapBuffers) &&
          GG_SYM(libgl, glXDestroyContext)))
        return 0;
    return 1;
}

/* GL entry points through glXGetProcAddressARB, which answers for core and extension functions
 * alike once a context exists. */
#define GG_PROC(name)                                                                             \
    (*(void **) (&w->api.name) = w->api.glXGetProcAddressARB((const unsigned char *) #name),      \
     w->api.name != NULL)

static int gg_bind_gl(gg_window *w) {
    (void) GG_PROC(glXSwapIntervalEXT); /* optional */
    return GG_PROC(glClearColor) && GG_PROC(glClear) && GG_PROC(glColorMask) &&
           GG_PROC(glViewport) && GG_PROC(glEnable) && GG_PROC(glReadBuffer) &&
           GG_PROC(glPixelStorei) && GG_PROC(glReadPixels) && GG_PROC(glCreateShader) &&
           GG_PROC(glShaderSource) && GG_PROC(glCompileShader) && GG_PROC(glGetShaderiv) &&
           GG_PROC(glGetShaderInfoLog) && GG_PROC(glDeleteShader) && GG_PROC(glCreateProgram) &&
           GG_PROC(glAttachShader) && GG_PROC(glLinkProgram) && GG_PROC(glGetProgramiv) &&
           GG_PROC(glGetProgramInfoLog) && GG_PROC(glDeleteProgram) && GG_PROC(glUseProgram) &&
           GG_PROC(glGetUniformLocation) && GG_PROC(glUniform1f) && GG_PROC(glUniform2f) &&
           GG_PROC(glUniform3f) && GG_PROC(glUniform4f) && GG_PROC(glUniform1i) &&
           GG_PROC(glUniformMatrix4fv) && GG_PROC(glGenVertexArrays) &&
           GG_PROC(glBindVertexArray) && GG_PROC(glDeleteVertexArrays) && GG_PROC(glGenBuffers) &&
           GG_PROC(glBindBuffer) && GG_PROC(glBufferData) && GG_PROC(glDeleteBuffers) &&
           GG_PROC(glVertexAttribPointer) && GG_PROC(glEnableVertexAttribArray) &&
           GG_PROC(glDrawArrays) && GG_PROC(glDisable) && GG_PROC(glBlendFunc) &&
           GG_PROC(glCullFace) && GG_PROC(glDepthMask) && GG_PROC(glScissor) &&
           GG_PROC(glFinish) && GG_PROC(glLineWidth) && GG_PROC(glPointSize);
}

/* An X protocol error while the context is made (a config the server refuses, a context the
 * thread cannot bind) ends the process under Xlib's default handler.  This one records it instead,
 * for `gg_open` to answer 0.  Xlib calls it on the thread that made the failing request, so the
 * flag is that thread's; the handler itself is the process's, installed only while `gg_open`
 * makes the context. */
static __thread int gg_x_failed;
static int gg_on_x_error(void *display, gx_error_event *e) {
    (void) display;
    (void) e;
    gg_x_failed = 1;
    return 0;
}

/* The frame-buffer config 0.9 chose through glutin: double-buffered true-colour RGB, an opaque
 * one preferred (an alpha channel makes a compositor treat the window as see-through), then the
 * most samples. */
static gx_fbconfig gg_pick_config(gg_window *w, int screen) {
    static const int attrs[] = {GLX_X_RENDERABLE, 1, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                                GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
                                GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
                                GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, 1, 0};
    int n = 0, i, best = -1, best_opaque = 0, best_samples = -1;
    gx_fbconfig *all = w->api.glXChooseFBConfig(w->display, screen, attrs, &n);
    gx_fbconfig pick = NULL;
    if (!all) return NULL;
    for (i = 0; i < n; i++) {
        int alpha = 0, samples = 0, opaque;
        w->api.glXGetFBConfigAttrib(w->display, all[i], GLX_ALPHA_SIZE, &alpha);
        w->api.glXGetFBConfigAttrib(w->display, all[i], GLX_SAMPLES, &samples);
        opaque = alpha == 0;
        if (best < 0 || (opaque && !best_opaque) ||
            (opaque == best_opaque && samples > best_samples)) {
            best = i;
            best_opaque = opaque;
            best_samples = samples;
        }
    }
    if (best >= 0) pick = all[best];
    w->api.XFree(all);
    return pick;
}

/* Close the window: the context (and every GL object in it), the X window, the connection. */
static void gg_teardown(gg_window *w) {
    if (w->display) {
        if (w->context) {
            w->api.glXMakeCurrent(w->display, 0, NULL);
            w->api.glXDestroyContext(w->display, w->context);
        }
        if (w->window) w->api.XDestroyWindow(w->display, w->window);
        if (w->colormap) w->api.XFreeColormap(w->display, w->colormap);
        w->api.XCloseDisplay(w->display);
    }
    if (gg_current == w) gg_current = NULL;
    if (w->libgl) dlclose(w->libgl);
    if (w->libx11) dlclose(w->libx11);
    w->closed = 1;
}

/* The record goes once the window is closed and no shader or mesh holds it. */
static void gg_forget(gg_window *w) {
    if (!w->closed || w->refs > 0) return;
    free(w->events);
    free(w);
}

static void gg_free(gg_window *w) {
    gg_teardown(w);
    gg_forget(w);
}

static void gg_push(gg_window *w, int64_t tag, int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t *slot;
    if (w->event_count == w->event_cap) {
        int64_t cap = w->event_cap ? w->event_cap * 2 : 16;
        int64_t *grown = (int64_t *) realloc(w->events, (size_t) cap * GG_EVENT_WORDS * 8u);
        if (!grown) return;
        w->events = grown;
        w->event_cap = cap;
    }
    slot = w->events + w->event_count * GG_EVENT_WORDS;
    slot[0] = tag;
    slot[1] = a;
    slot[2] = b;
    slot[3] = c;
    slot[4] = d;
    w->event_count++;
}

/* Present an opaque frame: whatever alpha the shaders wrote is forced to 1 first, as 0.9 did. */
static void gg_present(gg_window *w) {
    w->api.glColorMask(0, 0, 0, 1);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT);
    w->api.glColorMask(1, 1, 1, 1);
    w->api.glXSwapBuffers(w->display, w->window);
}

int64_t gg_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    static const int context_attrs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                                        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                                        GLX_CONTEXT_PROFILE_MASK_ARB,
                                        GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    gg_window *w = (gg_window *) calloc(1, sizeof(gg_window));
    gx_fbconfig config;
    gx_visual_info *visual;
    gx_window_attrs attrs;
    gx_error_handler previous = NULL;
    int trapped = 0;
    gx_xid root;
    int screen;
    if (!w) return 0;
    w->thread = pthread_self();
    if (!gg_bind(w)) goto fail;
    w->display = w->api.XOpenDisplay(NULL);
    if (!w->display) goto fail;
    screen = w->api.XDefaultScreen(w->display);
    root = w->api.XRootWindow(w->display, screen);
    if (fullscreen) {
        width = w->api.XDisplayWidth(w->display, screen);
        height = w->api.XDisplayHeight(w->display, screen);
    }
    if (width <= 0 || height <= 0) goto fail;
    config = gg_pick_config(w, screen);
    if (!config) goto fail;
    visual = w->api.glXGetVisualFromFBConfig(w->display, config);
    if (!visual) goto fail;
    w->colormap = w->api.XCreateColormap(w->display, root, visual->visual, GX_ALLOC_NONE);
    memset(&attrs, 0, sizeof attrs);
    attrs.colormap = w->colormap;
    attrs.event_mask = GX_STRUCTURE_NOTIFY_MASK | GX_EXPOSURE_MASK | GX_KEY_PRESS_MASK |
                       GX_KEY_RELEASE_MASK | GX_BUTTON_PRESS_MASK | GX_BUTTON_RELEASE_MASK |
                       GX_POINTER_MOTION_MASK | GX_FOCUS_CHANGE_MASK;
    w->window = w->api.XCreateWindow(w->display, root, 0, 0, (unsigned) width, (unsigned) height,
                                     0, visual->depth, GX_INPUT_OUTPUT, visual->visual,
                                     GX_CW_BORDER_PIXEL | GX_CW_COLORMAP | GX_CW_EVENT_MASK,
                                     &attrs);
    w->api.XFree(visual);
    if (!w->window) goto fail;
    w->api.XStoreName(w->display, w->window, title);
    w->wm_delete = w->api.XInternAtom(w->display, "WM_DELETE_WINDOW", 0);
    w->api.XSetWMProtocols(w->display, w->window, &w->wm_delete, 1);
    if (fullscreen) {
        gx_xid state = w->api.XInternAtom(w->display, "_NET_WM_STATE", 0);
        gx_xid full = w->api.XInternAtom(w->display, "_NET_WM_STATE_FULLSCREEN", 0);
        w->api.XChangeProperty(w->display, w->window, state, GX_ATOM_ATOM, 32,
                               GX_PROP_MODE_REPLACE, (const unsigned char *) &full, 1);
    }

    *(void **) (&w->api.glXCreateContextAttribsARB) =
        w->api.glXGetProcAddressARB((const unsigned char *) "glXCreateContextAttribsARB");
    if (!w->api.glXCreateContextAttribsARB) goto fail;
    gg_x_failed = 0;
    previous = w->api.XSetErrorHandler(gg_on_x_error);
    trapped = 1;
    w->context = w->api.glXCreateContextAttribsARB(w->display, config, NULL, 1, context_attrs);
    w->api.XSync(w->display, 0);
    if (gg_x_failed || !w->context) goto fail;

    w->api.XMapWindow(w->display, w->window);
    for (;;) { /* until the server says the window is mapped */
        gx_event ev;
        w->api.XNextEvent(w->display, &ev);
        if (ev.type == GX_MAP_NOTIFY && ev.any.window == w->window) break;
        if (ev.type == GX_CONFIGURE_NOTIFY && ev.configure.window == w->window) {
            width = ev.configure.width;
            height = ev.configure.height;
        }
    }
    if (!w->api.glXMakeCurrent(w->display, w->window, w->context)) goto fail;
    w->api.XSync(w->display, 0);
    if (gg_x_failed) goto fail;
    gg_current = w;
    w->api.XSetErrorHandler(previous);
    trapped = 0;
    if (!gg_bind_gl(w)) goto fail;
    if (w->api.glXSwapIntervalEXT) w->api.glXSwapIntervalEXT(w->display, w->window, 1);
    w->width = (int) width;
    w->height = (int) height;

    /* 0.9's first frames: depth test on, both buffers opaque black before anything is shown. */
    w->api.glEnable(GL_DEPTH_TEST);
    w->api.glViewport(0, 0, w->width, w->height);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gg_present(w);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gg_present(w);
    return (int64_t) (intptr_t) w;
fail:
    if (trapped) { /* tear the half-made context down while its errors are still recorded */
        if (w->context) {
            w->api.glXMakeCurrent(w->display, 0, NULL);
            w->api.glXDestroyContext(w->display, w->context);
            w->context = NULL;
        }
        w->api.XSync(w->display, 0);
        w->api.XSetErrorHandler(previous);
    }
    gg_free(w);
    return 0;
}

/* Drain the window's pending events: a resize applies to the viewport after the pump, as 0.9's
 * did, and is queued as a `Resize`; the window manager's close request ends the loop. */
int64_t gg_poll(int64_t h) {
    gg_window *w = gg_of(h);
    int width, height;
    if (!gg_use(w, "Window.poll")) return 0;
    width = w->width;
    height = w->height;
    while (w->api.XPending(w->display) > 0) {
        gx_event ev;
        w->api.XNextEvent(w->display, &ev);
        switch (ev.type) {
        case GX_CONFIGURE_NOTIFY:
            if (ev.configure.width > 0 && ev.configure.height > 0) {
                width = ev.configure.width;
                height = ev.configure.height;
            }
            break;
        case GX_CLIENT_MESSAGE:
            if ((gx_xid) ev.client.data.l[0] == w->wm_delete) w->should_close = 1;
            break;
        case GX_DESTROY_NOTIFY:
            w->should_close = 1;
            break;
        default:
            break;
        }
    }
    if (width != w->width || height != w->height) {
        w->width = width;
        w->height = height;
        w->api.glViewport(0, 0, width, height);
        gg_push(w, GG_EV_RESIZE, width, height, 0, 0);
    }
    return !w->should_close;
}

void gg_swap(int64_t h) {
    gg_window *w = gg_of(h);
    if (gg_use(w, "Window.swap")) gg_present(w);
}

/* `color` is packed 0xAARRGGBB, as `rgba()` packs it. */
void gg_clear(int64_t h, int64_t color) {
    gg_window *w = gg_of(h);
    uint32_t c = (uint32_t) color;
    if (!gg_use(w, "Window.clear")) return;
    w->api.glClearColor((float) ((c >> 16) & 0xFF) / 255.0f, (float) ((c >> 8) & 0xFF) / 255.0f,
                        (float) (c & 0xFF) / 255.0f, (float) ((c >> 24) & 0xFF) / 255.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gg_close(int64_t h) {
    gg_window *w = gg_of(h);
    if (!w || w->closed || !gg_on_thread(w, "Window.close")) return;
    gg_free(w);
}

int64_t gg_width(int64_t h) {
    gg_window *w = gg_of(h);
    return w ? w->width : 0;
}

int64_t gg_height(int64_t h) {
    gg_window *w = gg_of(h);
    return w ? w->height : 0;
}

int64_t gg_event_count(int64_t h) {
    gg_window *w = gg_of(h);
    return w ? w->event_count : 0;
}

/* Copy the queued events out, GG_EVENT_WORDS integers each, oldest first, and empty the queue. */
void gg_events(int64_t h, int64_t *out, int64_t n) {
    gg_window *w = gg_of(h);
    int64_t words;
    if (!w) return;
    words = w->event_count * GG_EVENT_WORDS;
    if (n < words) words = n - n % GG_EVENT_WORDS;
    memcpy(out, w->events, (size_t) words * 8u);
    w->event_count = 0;
}

/* The back buffer as 0xAARRGGBB pixels, top row first: read before `swap`, after the frame's
 * draws.  `n` is width * height. */
void gg_read_pixels(int64_t h, int64_t *out, int64_t n) {
    gg_window *w = gg_of(h);
    unsigned char *rgba;
    int64_t x, y;
    if (!gg_use(w, "Window.capture")) return;
    if (n != (int64_t) w->width * w->height || n <= 0) return;
    rgba = (unsigned char *) malloc((size_t) n * 4u);
    if (!rgba) return;
    w->api.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    w->api.glReadBuffer(GL_BACK);
    w->api.glReadPixels(0, 0, w->width, w->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (y = 0; y < w->height; y++) {
        const unsigned char *row = rgba + (size_t) (w->height - 1 - y) * w->width * 4u;
        for (x = 0; x < w->width; x++) {
            const unsigned char *p = row + x * 4;
            out[y * w->width + x] = (int64_t) (((uint32_t) p[3] << 24) | ((uint32_t) p[0] << 16) |
                                               ((uint32_t) p[1] << 8) | p[2]);
        }
    }
    free(rgba);
}

static gg_res *gg_new_res(gg_window *w, int kind) {
    gg_res *r = (gg_res *) calloc(1, sizeof(gg_res));
    if (!r) return NULL;
    r->w = w;
    r->kind = kind;
    w->refs++;
    return r;
}

static unsigned gg_compile(gg_window *w, unsigned type, const char *source, const char *what) {
    unsigned id = w->api.glCreateShader(type);
    int ok = 0;
    w->api.glShaderSource(id, 1, &source, NULL);
    w->api.glCompileShader(id);
    w->api.glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        log[0] = 0;
        w->api.glGetShaderInfoLog(id, (int) sizeof log, NULL, log);
        fprintf(stderr, "graphics: the %s shader does not compile:\n%s\n", what, log);
        w->api.glDeleteShader(id);
        return 0;
    }
    return id;
}

/* Compile and link a program; 0, with the driver's log on stderr, when either fails. */
int64_t gg_shader(int64_t win, const char *vertex, const char *fragment) {
    gg_window *w = gg_of(win);
    unsigned vs, fs, program;
    int ok = 0;
    gg_res *r;
    if (!gg_use(w, "Window.shader")) return 0;
    vs = gg_compile(w, GL_VERTEX_SHADER, vertex, "vertex");
    if (!vs) return 0;
    fs = gg_compile(w, GL_FRAGMENT_SHADER, fragment, "fragment");
    if (!fs) {
        w->api.glDeleteShader(vs);
        return 0;
    }
    program = w->api.glCreateProgram();
    w->api.glAttachShader(program, vs);
    w->api.glAttachShader(program, fs);
    w->api.glLinkProgram(program);
    w->api.glDeleteShader(vs);
    w->api.glDeleteShader(fs);
    w->api.glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        log[0] = 0;
        w->api.glGetProgramInfoLog(program, (int) sizeof log, NULL, log);
        fprintf(stderr, "graphics: the shader program does not link:\n%s\n", log);
        w->api.glDeleteProgram(program);
        return 0;
    }
    r = gg_new_res(w, GG_SHADER);
    if (!r) {
        w->api.glDeleteProgram(program);
        return 0;
    }
    r->id = program;
    return (int64_t) (intptr_t) r;
}

/* Bind shader `sh` and answer the location of its uniform `name`, -1 for none. */
static int gg_uniform_at(gg_res *r, const char *name, const char *what) {
    if (!r || r->kind != GG_SHADER || !gg_use(r->w, what)) return -1;
    r->w->api.glUseProgram(r->id);
    return r->w->api.glGetUniformLocation(r->id, name);
}

/* A float uniform of `n` components: 1 to 4 a float or vector, 16 a column-major mat4. */
void gg_uniform_f(int64_t sh, const char *name, const double *v, int64_t n) {
    gg_res *r = gg_res_of(sh);
    int at = gg_uniform_at(r, name, "Shader.set");
    gg_api *gl;
    if (at < 0) return;
    gl = &r->w->api;
    if (n == 1) gl->glUniform1f(at, (float) v[0]);
    else if (n == 2) gl->glUniform2f(at, (float) v[0], (float) v[1]);
    else if (n == 3) gl->glUniform3f(at, (float) v[0], (float) v[1], (float) v[2]);
    else if (n == 4) gl->glUniform4f(at, (float) v[0], (float) v[1], (float) v[2], (float) v[3]);
    else if (n >= 16) {
        float m[16];
        int i;
        for (i = 0; i < 16; i++) m[i] = (float) v[i];
        gl->glUniformMatrix4fv(at, 1, 0, m);
    }
}

void gg_uniform_i(int64_t sh, const char *name, int64_t v) {
    gg_res *r = gg_res_of(sh);
    int at = gg_uniform_at(r, name, "Shader.set_int");
    if (at >= 0) r->w->api.glUniform1i(at, (int) v);
}

/* A vertex array over `n` floats, `stride` per vertex, laid out as 0.9 laid it: position at 0;
 * a normal at 1 from 6 floats; a UV at 2 at exactly 8; a colour at 2 from 10; an anchor at 3 and
 * a birth time at 4 from 14. */
int64_t gg_mesh(int64_t win, const float *data, int64_t n, int64_t stride) {
    gg_window *w = gg_of(win);
    gg_api *gl;
    gg_res *r;
    int bytes;
    if (stride < 3 || !gg_use(w, "Window.mesh")) return 0;
    r = gg_new_res(w, GG_MESH);
    if (!r) return 0;
    gl = &w->api;
    bytes = (int) stride * 4;
    r->vertices = (int) (n / stride);
    gl->glGenVertexArrays(1, &r->id);
    gl->glGenBuffers(1, &r->buffer);
    gl->glBindVertexArray(r->id);
    gl->glBindBuffer(GL_ARRAY_BUFFER, r->buffer);
    gl->glBufferData(GL_ARRAY_BUFFER, (intptr_t) r->vertices * bytes, data, GL_STATIC_DRAW);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, 0, bytes, (const void *) 0);
    gl->glEnableVertexAttribArray(0);
    if (stride >= 6) {
        gl->glVertexAttribPointer(1, 3, GL_FLOAT, 0, bytes, (const void *) 12);
        gl->glEnableVertexAttribArray(1);
    }
    if (stride == 8) {
        gl->glVertexAttribPointer(2, 2, GL_FLOAT, 0, bytes, (const void *) 24);
        gl->glEnableVertexAttribArray(2);
    }
    if (stride >= 10) {
        gl->glVertexAttribPointer(2, 4, GL_FLOAT, 0, bytes, (const void *) 24);
        gl->glEnableVertexAttribArray(2);
    }
    if (stride >= 14) {
        gl->glVertexAttribPointer(3, 3, GL_FLOAT, 0, bytes, (const void *) 40);
        gl->glEnableVertexAttribArray(3);
        gl->glVertexAttribPointer(4, 1, GL_FLOAT, 0, bytes, (const void *) 52);
        gl->glEnableVertexAttribArray(4);
    }
    gl->glBindVertexArray(0);
    return (int64_t) (intptr_t) r;
}

/* Draw all of `mesh`'s vertices as triangles with `shader`. */
void gg_draw(int64_t mesh, int64_t shader) {
    gg_res *m = gg_res_of(mesh), *s = gg_res_of(shader);
    if (!m || !s || m->kind != GG_MESH || s->kind != GG_SHADER) return;
    if (m->w != s->w) {
        fprintf(stderr, "graphics: Mesh.draw with a shader of another window\n");
        return;
    }
    if (!gg_use(m->w, "Mesh.draw")) return;
    m->w->api.glUseProgram(s->id);
    m->w->api.glBindVertexArray(m->id);
    m->w->api.glDrawArrays(GL_TRIANGLES, 0, m->vertices);
    m->w->api.glBindVertexArray(0);
}

/* Release a shader or a mesh.  After its window closed there is nothing left to delete. */
void gg_release(int64_t res) {
    gg_res *r = gg_res_of(res);
    gg_window *w;
    if (!r) return;
    w = r->w;
    if (!w->closed) {
        if (!gg_use(w, r->kind == GG_SHADER ? "Shader drop" : "Mesh drop")) return;
        if (r->kind == GG_SHADER) {
            w->api.glDeleteProgram(r->id);
        } else {
            w->api.glDeleteVertexArrays(1, &r->id);
            w->api.glDeleteBuffers(1, &r->buffer);
        }
    }
    w->refs--;
    free(r);
    gg_forget(w);
}

/* ── Render state: 0.9's own codes, so a ported call keeps its arguments ─────────────────── */

/* GL_DEPTH_TEST = 1, GL_BLEND = 2, GL_CULL_FACE = 3, GL_SCISSOR_TEST = 4 (src/graphics.loft). */
void gg_capability(int64_t win, int64_t cap, int64_t on) {
    static const unsigned caps[] = {0, GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST};
    gg_window *w = gg_of(win);
    if (cap < 1 || cap > 4 || !gg_use(w, on ? "Window.enable" : "Window.disable")) return;
    if (on) w->api.glEnable(caps[cap]);
    else w->api.glDisable(caps[cap]);
}

/* BLEND_ZERO = 0 … BLEND_ONE_MINUS_DST_ALPHA = 5; anything else is ONE, as in 0.9. */
static unsigned gg_blend_factor(int64_t f) {
    static const unsigned factors[] = {GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                       GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    return f >= 0 && f <= 5 ? factors[f] : GL_ONE;
}

void gg_blend(int64_t win, int64_t src, int64_t dst) {
    gg_window *w = gg_of(win);
    if (gg_use(w, "Window.blend")) w->api.glBlendFunc(gg_blend_factor(src), gg_blend_factor(dst));
}

/* 0 culls back faces, anything else front faces. */
void gg_cull(int64_t win, int64_t face) {
    gg_window *w = gg_of(win);
    if (gg_use(w, "Window.cull")) w->api.glCullFace(face == 0 ? GL_BACK : GL_FRONT);
}

void gg_depth_write(int64_t win, int64_t on) {
    gg_window *w = gg_of(win);
    if (gg_use(w, "Window.depth_write")) w->api.glDepthMask(on ? 1 : 0);
}

void gg_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    gg_window *w = gg_of(win);
    if (gg_use(w, "Window.viewport"))
        w->api.glViewport((int) x, (int) y, (int) width, (int) height);
}

void gg_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    gg_window *w = gg_of(win);
    if (gg_use(w, "Window.scissor"))
        w->api.glScissor((int) x, (int) y, (int) width, (int) height);
}

void gg_finish(int64_t win) {
    gg_window *w = gg_of(win);
    if (gg_use(w, "Window.finish")) w->api.glFinish();
}

/* A float travels as a one-element vector: `#c` takes no scalar float. */
void gg_line_width(int64_t win, const double *width, int64_t n) {
    gg_window *w = gg_of(win);
    if (n >= 1 && gg_use(w, "Window.line_width")) w->api.glLineWidth((float) width[0]);
}

void gg_point_size(int64_t win, const double *size, int64_t n) {
    gg_window *w = gg_of(win);
    if (n >= 1 && gg_use(w, "Window.point_size")) w->api.glPointSize((float) size[0]);
}

#else /* no window on this host yet */

int64_t gg_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    (void) width;
    (void) height;
    (void) title;
    (void) fullscreen;
    return 0;
}
int64_t gg_poll(int64_t h) {
    (void) h;
    return 0;
}
void gg_swap(int64_t h) {
    (void) h;
}
void gg_clear(int64_t h, int64_t color) {
    (void) h;
    (void) color;
}
void gg_close(int64_t h) {
    (void) h;
}
int64_t gg_width(int64_t h) {
    (void) h;
    return 0;
}
int64_t gg_height(int64_t h) {
    (void) h;
    return 0;
}
int64_t gg_event_count(int64_t h) {
    (void) h;
    return 0;
}
void gg_events(int64_t h, int64_t *out, int64_t n) {
    (void) h;
    (void) out;
    (void) n;
}
void gg_read_pixels(int64_t h, int64_t *out, int64_t n) {
    (void) h;
    (void) out;
    (void) n;
}
int64_t gg_shader(int64_t win, const char *vertex, const char *fragment) {
    (void) win;
    (void) vertex;
    (void) fragment;
    return 0;
}
void gg_uniform_f(int64_t sh, const char *name, const double *v, int64_t n) {
    (void) sh;
    (void) name;
    (void) v;
    (void) n;
}
void gg_uniform_i(int64_t sh, const char *name, int64_t v) {
    (void) sh;
    (void) name;
    (void) v;
}
int64_t gg_mesh(int64_t win, const float *data, int64_t n, int64_t stride) {
    (void) win;
    (void) data;
    (void) n;
    (void) stride;
    return 0;
}
void gg_draw(int64_t mesh, int64_t shader) {
    (void) mesh;
    (void) shader;
}
void gg_release(int64_t res) {
    (void) res;
}
void gg_capability(int64_t win, int64_t cap, int64_t on) {
    (void) win;
    (void) cap;
    (void) on;
}
void gg_blend(int64_t win, int64_t src, int64_t dst) {
    (void) win;
    (void) src;
    (void) dst;
}
void gg_cull(int64_t win, int64_t face) {
    (void) win;
    (void) face;
}
void gg_depth_write(int64_t win, int64_t on) {
    (void) win;
    (void) on;
}
void gg_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    (void) win;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
}
void gg_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    (void) win;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
}
void gg_finish(int64_t win) {
    (void) win;
}
void gg_line_width(int64_t win, const double *width, int64_t n) {
    (void) win;
    (void) width;
    (void) n;
}
void gg_point_size(int64_t win, const double *size, int64_t n) {
    (void) win;
    (void) size;
    (void) n;
}

#endif
