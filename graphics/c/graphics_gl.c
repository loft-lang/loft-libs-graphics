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
 * A browser page (`--html`, where loft defines `LOFT_BROWSER`): the page's canvas and WebGL2,
 * through its `loft_gl` imports (loft's doc/loft-gl-wasm.js) — the GL function table is filled
 * from them, and everything above it is the same code as on Linux.
 *
 * macOS: AppKit and an `NSOpenGLContext` (a 3.2 core profile, which macOS serves as GL 4.1),
 * reached through the Objective-C runtime — libobjc, AppKit and OpenGL.framework opened at run
 * time, each message sent through `objc_msgSend` cast to the method's own type — so it too builds
 * with loft's fixed flags and no SDK.  A Cocoa window lives on the main thread.
 *
 * Other hosts (Windows, a WASI module) open no window yet: `gg_open` answers 0, which the loft
 * side reports as a null `Window`. */
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
int64_t gg_text_size(int64_t h);
void gg_text_copy(int64_t h, uint8_t *out, int64_t n);
int64_t gg_key_down(int64_t h, int64_t key);
int64_t gg_mouse(int64_t h, int64_t which);
void gg_read_pixels(int64_t h, int64_t *out, int64_t n);
int64_t gg_shader(int64_t win, const char *vertex, const char *fragment);
void gg_uniform_f(int64_t sh, const char *name, const double *v, int64_t n);
void gg_uniform_i(int64_t sh, const char *name, int64_t v);
int64_t gg_mesh(int64_t win, const float *data, int64_t n, int64_t stride);
void gg_draw(int64_t mesh, int64_t shader);
void gg_release(int64_t res);
void gg_capability(int64_t win, int64_t cap, int64_t on);
int64_t gg_capability_on(int64_t win, int64_t cap);
void gg_draw_over(int64_t mesh, int64_t shader);
void gg_blend(int64_t win, int64_t src, int64_t dst);
void gg_cull(int64_t win, int64_t face);
void gg_depth_write(int64_t win, int64_t on);
void gg_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height);
void gg_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height);
void gg_finish(int64_t win);
void gg_line_width(int64_t win, const double *width, int64_t n);
void gg_point_size(int64_t win, const double *size, int64_t n);
int64_t gg_texture(int64_t win, int64_t width, int64_t height, int64_t depth);
int64_t gg_texture_from(int64_t win, const int64_t *px, int64_t n, int64_t width, int64_t height);
void gg_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                    const int64_t *px, int64_t n);
void gg_texture_filter(int64_t tex, int64_t nearest);
void gg_texture_mipmaps(int64_t tex);
void gg_texture_bind(int64_t tex, int64_t unit);
int64_t gg_texture_size(int64_t tex, int64_t which);
int64_t gg_framebuffer(int64_t win);
void gg_framebuffer_attach(int64_t fb, int64_t tex);
void gg_render_to(int64_t win, int64_t fb);
int64_t gg_quad(int64_t win);
void gg_mesh_update(int64_t mesh, const float *data, int64_t n);
void gg_mesh_indices(int64_t mesh, const int64_t *idx, int64_t n);
void gg_draw_as(int64_t mesh, int64_t shader, int64_t mode, int64_t indexed, int64_t instances);
int64_t gg_instances(int64_t win, const float *data, int64_t n);
void gg_buffer_update(int64_t buf, const float *data, int64_t n);
void gg_instance_attrib(int64_t mesh, int64_t buf, int64_t location, int64_t components,
                        int64_t stride, int64_t offset);

/* One queued input event: an EV_* tag (src/graphics.loft) and up to four integer fields. */
#define GG_EVENT_WORDS 5
#define GG_EV_KEY_DOWN 1
#define GG_EV_KEY_UP 2
#define GG_EV_TEXT 3
#define GG_EV_MOUSE_DOWN 4
#define GG_EV_MOUSE_UP 5
#define GG_EV_MOUSE_MOVE 6
#define GG_EV_WHEEL 7
#define GG_EV_RESIZE 11
#define GG_MOD_SHIFT 1
#define GG_MOD_CTRL 2
#define GG_MOD_ALT 4
#define GG_MOD_SUPER 8

#if defined(__linux__) && !defined(__wasm__)
#define GG_X11 1 /* a window through X11 and GLX, opened at run time */
#elif defined(__APPLE__) && !defined(__wasm__)
#define GG_COCOA 1 /* a window through AppKit and NSOpenGL, by the Objective-C runtime */
#elif defined(__wasm__) && defined(LOFT_BROWSER)
#define GG_BROWSER 1 /* the page's canvas, through its `loft_gl` imports (WebGL2) */
#endif

#if defined(GG_X11) || defined(GG_COCOA) || defined(GG_BROWSER)

#if defined(GG_X11)
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
#define GX_XLOOKUP_CHARS 2
#define GX_XLOOKUP_BOTH 4
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

#endif /* GG_X11 */

#if defined(GG_COCOA)
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>

/* ── The Objective-C runtime and AppKit, as much as this file uses ───────────────────────────── */
typedef void *gc_id;
typedef void *gc_sel;
typedef struct {
    double x, y;
} gc_point;
typedef struct {
    double x, y, width, height;
} gc_rect;

#define GC_STYLE_TITLED 1ul
#define GC_STYLE_CLOSABLE 2ul
#define GC_STYLE_MINIATURIZABLE 4ul
#define GC_STYLE_RESIZABLE 8ul
#define GC_BACKING_BUFFERED 2ul
#define GC_POLICY_REGULAR 0l
#define GC_MAIN_MENU_LEVEL 24l
#define GC_HIDE_DOCK (1ul << 1)
#define GC_HIDE_MENU_BAR (1ul << 3)

#define GC_PFA_DOUBLE_BUFFER 5u
#define GC_PFA_COLOR_SIZE 8u
#define GC_PFA_DEPTH_SIZE 12u
#define GC_PFA_ACCELERATED 73u
#define GC_PFA_PROFILE 99u
#define GC_PROFILE_3_2_CORE 0x3200u
#define GC_SWAP_INTERVAL 222l
#define GC_SURFACE_OPACITY 236l

#define GC_LEFT_DOWN 1ul
#define GC_LEFT_UP 2ul
#define GC_RIGHT_DOWN 3ul
#define GC_RIGHT_UP 4ul
#define GC_MOVED 5ul
#define GC_LEFT_DRAGGED 6ul
#define GC_RIGHT_DRAGGED 7ul
#define GC_KEY_DOWN 10ul
#define GC_KEY_UP 11ul
#define GC_FLAGS_CHANGED 12ul
#define GC_SCROLL 22ul
#define GC_OTHER_DOWN 25ul
#define GC_OTHER_UP 26ul
#define GC_OTHER_DRAGGED 27ul

#define GC_FLAG_SHIFT (1ul << 17)
#define GC_FLAG_CONTROL (1ul << 18)
#define GC_FLAG_OPTION (1ul << 19)
#define GC_FLAG_COMMAND (1ul << 20)
#endif /* GG_COCOA */

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
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT 0x1902
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_NONE 0
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_UNSIGNED_INT 0x1405

#if defined(GG_X11)
typedef void *gx_fbconfig;
typedef void *gx_context;
typedef int (*gx_error_handler)(void *, gx_error_event *);

/* The X and GLX entry points a window calls, resolved when it opens. */
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
    int (*XLookupString)(gx_input_event *, char *, int, gx_xid *, void *);
    int (*XkbSetDetectableAutoRepeat)(void *, int, int *);
    void *(*XOpenIM)(void *, void *, char *, char *);
    int (*XCloseIM)(void *);
    void *(*XCreateIC)(void *, ...);
    void (*XDestroyIC)(void *);
    int (*Xutf8LookupString)(void *, gx_input_event *, char *, int, gx_xid *, int *);
    int (*XFilterEvent)(gx_event *, gx_xid);

    gx_fbconfig *(*glXChooseFBConfig)(void *, int, const int *, int *);
    int (*glXGetFBConfigAttrib)(void *, gx_fbconfig, int, int *);
    gx_visual_info *(*glXGetVisualFromFBConfig)(void *, gx_fbconfig);
    void *(*glXGetProcAddressARB)(const unsigned char *);
    gx_context (*glXCreateContextAttribsARB)(void *, gx_fbconfig, gx_context, int, const int *);
    int (*glXMakeCurrent)(void *, gx_xid, gx_context);
    void (*glXSwapBuffers)(void *, gx_xid);
    void (*glXDestroyContext)(void *, gx_context);
    void (*glXSwapIntervalEXT)(void *, gx_xid, int);
} gg_x_api;
#endif /* GG_X11 */

/* Every GL entry point a window calls: resolved through GLX when it opens, or the page's
 * `loft_gl` imports in a browser. */
typedef struct {

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
    void (*glGenTextures)(int, unsigned *);
    void (*glBindTexture)(unsigned, unsigned);
    void (*glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
    void (*glTexSubImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
    void (*glTexParameteri)(unsigned, unsigned, int);
    void (*glDeleteTextures)(int, const unsigned *);
    void (*glGenerateMipmap)(unsigned);
    void (*glActiveTexture)(unsigned);
    void (*glGenFramebuffers)(int, unsigned *);
    void (*glBindFramebuffer)(unsigned, unsigned);
    void (*glFramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
    void (*glDeleteFramebuffers)(int, const unsigned *);
    void (*glDrawBuffer)(unsigned);
    void (*glDrawElements)(unsigned, int, unsigned, const void *);
    void (*glDrawArraysInstanced)(unsigned, int, int, int);
    void (*glDrawElementsInstanced)(unsigned, int, unsigned, const void *, int);
    void (*glVertexAttribDivisor)(unsigned, unsigned);
} gg_api;

/* Every entry of `gg_api`, for a platform that looks each one up by name: `P(name)` answers
 * whether `name` was found. */
#define GG_GL_ALL(P)                                                                              \
    (P(glClearColor) && P(glClear) && P(glColorMask) && P(glViewport) && P(glEnable) &&           \
     P(glReadBuffer) && P(glPixelStorei) && P(glReadPixels) && P(glCreateShader) &&               \
     P(glShaderSource) && P(glCompileShader) && P(glGetShaderiv) && P(glGetShaderInfoLog) &&      \
     P(glDeleteShader) && P(glCreateProgram) && P(glAttachShader) && P(glLinkProgram) &&          \
     P(glGetProgramiv) && P(glGetProgramInfoLog) && P(glDeleteProgram) && P(glUseProgram) &&      \
     P(glGetUniformLocation) && P(glUniform1f) && P(glUniform2f) && P(glUniform3f) &&             \
     P(glUniform4f) && P(glUniform1i) && P(glUniformMatrix4fv) && P(glGenVertexArrays) &&         \
     P(glBindVertexArray) && P(glDeleteVertexArrays) && P(glGenBuffers) && P(glBindBuffer) &&     \
     P(glBufferData) && P(glDeleteBuffers) && P(glVertexAttribPointer) &&                         \
     P(glEnableVertexAttribArray) && P(glDrawArrays) && P(glDisable) && P(glBlendFunc) &&         \
     P(glCullFace) && P(glDepthMask) && P(glScissor) && P(glFinish) && P(glLineWidth) &&          \
     P(glPointSize) && P(glGenTextures) && P(glBindTexture) && P(glTexImage2D) &&                 \
     P(glTexSubImage2D) && P(glTexParameteri) && P(glDeleteTextures) && P(glGenerateMipmap) &&    \
     P(glActiveTexture) && P(glGenFramebuffers) && P(glBindFramebuffer) &&                        \
     P(glFramebufferTexture2D) && P(glDeleteFramebuffers) && P(glDrawBuffer) &&                   \
     P(glDrawElements) && P(glDrawArraysInstanced) && P(glDrawElementsInstanced) &&               \
     P(glVertexAttribDivisor))

typedef struct {
    gg_api api;
#if defined(GG_X11)
    gg_x_api xa;
    void *libx11, *libgl;
    void *display;
    gx_xid window, colormap, wm_delete;
    gx_context context;
    pthread_t thread;
    void *im, *ic; /* the input method and context, when the server has one */
#elif defined(GG_COCOA)
    gc_id window, view, context, format;
    int fullscreen;
    pthread_t thread;
    void *next_open; /* the next open window: AppKit has one event queue for all of them */
#endif
    int width, height, should_close;
    int closed; /* `gg_close` ran: the context and the X window are gone */
    unsigned target; /* the framebuffer draws go to, 0 for the window */
    unsigned caps; /* which of GL_DEPTH_TEST … GL_SCISSOR_TEST (bits 1-4) are on: every change
                    * comes through this file, so the record is the context's state */
    int64_t refs; /* the shaders and meshes still holding this window */
    int64_t *events;
    int64_t event_count, event_cap;
    char *text; /* the UTF-8 of the queued `Text` events, end to end */
    int64_t text_len, text_cap;
    unsigned char keys[256]; /* which KEY_* codes are down */
    int mouse_x, mouse_y, buttons;
} gg_window;

/* A shader or a mesh: its GL names and the window whose context made them. */
enum { GG_SHADER = 1, GG_MESH = 2, GG_TEXTURE = 3, GG_FRAMEBUFFER = 4, GG_BUFFER = 5 };
typedef struct {
    gg_window *w;
    int kind;
    unsigned id, buffer; /* program; vertex array and its buffer; texture; framebuffer; buffer */
    unsigned indices_id; /* a mesh's index buffer, 0 for none */
    int vertices, indices, stride;
    int width, height, depth; /* a texture */
    int has_colour; /* a framebuffer: a colour attachment, else it draws to no colour buffer */
} gg_res;

static gg_window *gg_of(int64_t h) {
    return (gg_window *) (intptr_t) h;
}

static gg_res *gg_res_of(int64_t h) {
    return (gg_res *) (intptr_t) h;
}

#if defined(GG_X11)
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
        w->xa.glXMakeCurrent(w->display, w->window, w->context);
        gg_current = w;
    }
    return 1;
}

#define GG_SYM(lib, name)                                                                         \
    (*(void **) (&w->xa.name) = dlsym(w->lib, #name), w->xa.name != NULL)

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
          GG_SYM(libx11, XSetErrorHandler) && GG_SYM(libx11, XLookupString) &&
          GG_SYM(libx11, XkbSetDetectableAutoRepeat) && GG_SYM(libx11, XOpenIM) &&
          GG_SYM(libx11, XCloseIM) && GG_SYM(libx11, XCreateIC) && GG_SYM(libx11, XDestroyIC) &&
          GG_SYM(libx11, Xutf8LookupString) && GG_SYM(libx11, XFilterEvent)))
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
    (*(void **) (&w->api.name) = w->xa.glXGetProcAddressARB((const unsigned char *) #name),      \
     w->api.name != NULL)

static int gg_bind_gl(gg_window *w) {
    *(void **) (&w->xa.glXSwapIntervalEXT) = /* optional */
        w->xa.glXGetProcAddressARB((const unsigned char *) "glXSwapIntervalEXT");
    return GG_GL_ALL(GG_PROC);
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
    gx_fbconfig *all = w->xa.glXChooseFBConfig(w->display, screen, attrs, &n);
    gx_fbconfig pick = NULL;
    if (!all) return NULL;
    for (i = 0; i < n; i++) {
        int alpha = 0, samples = 0, opaque;
        w->xa.glXGetFBConfigAttrib(w->display, all[i], GLX_ALPHA_SIZE, &alpha);
        w->xa.glXGetFBConfigAttrib(w->display, all[i], GLX_SAMPLES, &samples);
        opaque = alpha == 0;
        if (best < 0 || (opaque && !best_opaque) ||
            (opaque == best_opaque && samples > best_samples)) {
            best = i;
            best_opaque = opaque;
            best_samples = samples;
        }
    }
    if (best >= 0) pick = all[best];
    w->xa.XFree(all);
    return pick;
}

/* Close the window: the context (and every GL object in it), the X window, the connection. */
static void gg_teardown(gg_window *w) {
    if (w->display) {
        if (w->context) {
            w->xa.glXMakeCurrent(w->display, 0, NULL);
            w->xa.glXDestroyContext(w->display, w->context);
        }
        if (w->window) w->xa.XDestroyWindow(w->display, w->window);
        if (w->colormap) w->xa.XFreeColormap(w->display, w->colormap);
        w->xa.XCloseDisplay(w->display);
    }
    if (gg_current == w) gg_current = NULL;
    if (w->ic) w->xa.XDestroyIC(w->ic);
    if (w->im) w->xa.XCloseIM(w->im);
    w->ic = w->im = NULL;
    if (w->libgl) dlclose(w->libgl);
    if (w->libx11) dlclose(w->libx11);
    w->closed = 1;
}

#elif defined(GG_COCOA)

/* The window whose context is current on this thread (speed only, as on X11). */
static __thread gg_window *gg_current;

/* The runtime, bound once for the process: system libraries are never unloaded. */
static struct {
    int loaded; /* 1 bound, -1 could not be */
    void *objc, *appkit, *gl;
    gc_id (*get_class)(const char *);
    gc_sel (*selector)(const char *);
    gc_id (*allocate_class)(gc_id, const char *, size_t);
    void (*register_class)(gc_id);
    signed char (*add_method)(gc_id, gc_sel, void (*)(void), const char *);
    void (*msg)(void);       /* objc_msgSend, cast at each call to the method's own type */
    void (*msg_stret)(void); /* objc_msgSend_stret: a returned rect on x86-64; none on arm64 */
    void *(*pool_push)(void);
    void (*pool_pop)(void *);
    gc_id app, run_loop_mode, full_class;
    gg_window *open; /* the open windows, each event routed to its own */
} gc;

static gc_sel gc_s(const char *name) {
    return gc.selector(name);
}
static gc_id gc_class(const char *name) {
    return gc.get_class(name);
}
static gc_id gc_o(gc_id o, const char *s) {
    return ((gc_id (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s));
}
static gc_id gc_o_p(gc_id o, const char *s, const void *a) {
    return ((gc_id (*)(gc_id, gc_sel, const void *)) gc.msg)(o, gc_s(s), a);
}
static void gc_v(gc_id o, const char *s) {
    ((void (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s));
}
static void gc_v_o(gc_id o, const char *s, gc_id a) {
    ((void (*)(gc_id, gc_sel, gc_id)) gc.msg)(o, gc_s(s), a);
}
static void gc_v_l(gc_id o, const char *s, long a) { /* an integer, enum or BOOL argument */
    ((void (*)(gc_id, gc_sel, long)) gc.msg)(o, gc_s(s), a);
}
static unsigned long gc_l(gc_id o, const char *s) { /* an NSInteger, NSUInteger or enum */
    return ((unsigned long (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s));
}
static int gc_b(gc_id o, const char *s) { /* a BOOL: only its low byte is defined */
    return ((signed char (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s)) != 0;
}
static double gc_d(gc_id o, const char *s) {
    return ((double (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s));
}
static gc_point gc_pt(gc_id o, const char *s) {
    return ((gc_point (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s));
}
static gc_rect gc_rc(gc_id o, const char *s) {
#if defined(__x86_64__)
    gc_rect r;
    ((void (*)(gc_rect *, gc_id, gc_sel)) gc.msg_stret)(&r, o, gc_s(s));
    return r;
#else
    return ((gc_rect (*)(gc_id, gc_sel)) gc.msg)(o, gc_s(s));
#endif
}

/* A borderless window takes no keys unless its class says it can: the fullscreen window's class,
 * made at run time, answers yes. */
static signed char gc_yes(gc_id self, gc_sel cmd) {
    (void) self;
    (void) cmd;
    return 1;
}

#define GC_SYM(lib, field, name) (*(void **) (&gc.field) = dlsym(gc.lib, name), gc.field != NULL)

static int gc_load(void) {
    void **mode;
    if (gc.loaded) return gc.loaded > 0;
    gc.loaded = -1;
    gc.objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW | RTLD_GLOBAL);
    gc.appkit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW | RTLD_GLOBAL);
    gc.gl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW | RTLD_LOCAL);
    if (!gc.objc || !gc.appkit || !gc.gl) return 0;
    if (!(GC_SYM(objc, get_class, "objc_getClass") && GC_SYM(objc, selector, "sel_registerName") &&
          GC_SYM(objc, allocate_class, "objc_allocateClassPair") &&
          GC_SYM(objc, register_class, "objc_registerClassPair") &&
          GC_SYM(objc, add_method, "class_addMethod") && GC_SYM(objc, msg, "objc_msgSend") &&
          GC_SYM(objc, pool_push, "objc_autoreleasePoolPush") &&
          GC_SYM(objc, pool_pop, "objc_autoreleasePoolPop")))
        return 0;
#if defined(__x86_64__)
    if (!GC_SYM(objc, msg_stret, "objc_msgSend_stret")) return 0;
#endif
    mode = (void **) dlsym(RTLD_DEFAULT, "NSDefaultRunLoopMode");
    if (!mode || !*mode) return 0;
    gc.run_loop_mode = *mode;
    gc.app = gc_o(gc_class("NSApplication"), "sharedApplication");
    if (!gc.app) return 0;
    gc_v_l(gc.app, "setActivationPolicy:", GC_POLICY_REGULAR);
    gc_v(gc.app, "finishLaunching");
    gc.full_class = gc.allocate_class(gc_class("NSWindow"), "GGFullscreenWindow", 0);
    if (gc.full_class) {
#if defined(__x86_64__)
        const char *types = "c@:";
#else
        const char *types = "B@:";
#endif
        gc.add_method(gc.full_class, gc_s("canBecomeKeyWindow"), (void (*)(void)) gc_yes, types);
        gc.add_method(gc.full_class, gc_s("canBecomeMainWindow"), (void (*)(void)) gc_yes, types);
        gc.register_class(gc.full_class);
    } else {
        gc.full_class = gc_class("GGFullscreenWindow"); /* registered by an earlier load */
    }
    gc.loaded = 1;
    return 1;
}

static int gg_on_thread(gg_window *w, const char *what) {
    if (pthread_equal(w->thread, pthread_self())) return 1;
    fprintf(stderr, "graphics: %s called from a thread other than the one that opened the "
                    "window; a window is used on its own thread only\n", what);
    return 0;
}

static int gg_use(gg_window *w, const char *what) {
    if (!w || w->closed || !gg_on_thread(w, what)) return 0;
    if (gg_current != w) {
        gc_v(w->context, "makeCurrentContext");
        gg_current = w;
    }
    return 1;
}

/* OpenGL.framework exports every core entry point by name. */
#define GG_PROC(name) (*(void **) (&w->api.name) = dlsym(gc.gl, #name), w->api.name != NULL)

static int gg_bind_gl(gg_window *w) {
    return GG_GL_ALL(GG_PROC);
}

/* Close the window: the context (and every GL object in it) and the NSWindow. */
static void gg_teardown(gg_window *w) {
    gg_window **link;
    void *pool;
    w->closed = 1;
    if (gc.loaded <= 0) return;
    for (link = &gc.open; *link; link = (gg_window **) &(*link)->next_open)
        if (*link == w) {
            *link = (gg_window *) w->next_open;
            break;
        }
    pool = gc.pool_push();
    if (w->context) {
        if (gg_current == w) gc_v(gc_class("NSOpenGLContext"), "clearCurrentContext");
        gc_v(w->context, "clearDrawable");
        gc_v(w->context, "release");
    }
    if (w->format) gc_v(w->format, "release");
    if (w->window) {
        gc_v(w->window, "close");
        gc_v(w->window, "release");
    }
    if (w->fullscreen) gc_v_l(gc.app, "setPresentationOptions:", 0);
    gc.pool_pop(pool);
    if (gg_current == w) gg_current = NULL;
}

#else /* GG_BROWSER: the page's canvas and WebGL2 context, through its `loft_gl` imports */

#define GG_IMPORT(name) __attribute__((import_module("loft_gl"), import_name(#name)))
GG_IMPORT(gg_js_open) int gg_js_open(int width, int height, int fullscreen);
GG_IMPORT(gg_js_size) int gg_js_size(int which);
GG_IMPORT(gg_present) void gg_js_present(void);
GG_IMPORT(gg_js_event) int gg_js_event(int *out, char *text, int cap);
/* One import per GL entry point, of the GL signature: doc/loft-gl-wasm.js maps each onto WebGL2. */
GG_IMPORT(gg_glClearColor) void gg_glClearColor(float, float, float, float);
GG_IMPORT(gg_glClear) void gg_glClear(unsigned);
GG_IMPORT(gg_glColorMask) void gg_glColorMask(unsigned char, unsigned char, unsigned char, unsigned char);
GG_IMPORT(gg_glViewport) void gg_glViewport(int, int, int, int);
GG_IMPORT(gg_glEnable) void gg_glEnable(unsigned);
GG_IMPORT(gg_glReadBuffer) void gg_glReadBuffer(unsigned);
GG_IMPORT(gg_glPixelStorei) void gg_glPixelStorei(unsigned, int);
GG_IMPORT(gg_glReadPixels) void gg_glReadPixels(int, int, int, int, unsigned, unsigned, void *);
GG_IMPORT(gg_glCreateShader) unsigned gg_glCreateShader(unsigned);
GG_IMPORT(gg_glShaderSource) void gg_glShaderSource(unsigned, int, const char *const *, const int *);
GG_IMPORT(gg_glCompileShader) void gg_glCompileShader(unsigned);
GG_IMPORT(gg_glGetShaderiv) void gg_glGetShaderiv(unsigned, unsigned, int *);
GG_IMPORT(gg_glGetShaderInfoLog) void gg_glGetShaderInfoLog(unsigned, int, int *, char *);
GG_IMPORT(gg_glDeleteShader) void gg_glDeleteShader(unsigned);
GG_IMPORT(gg_glCreateProgram) unsigned gg_glCreateProgram(void);
GG_IMPORT(gg_glAttachShader) void gg_glAttachShader(unsigned, unsigned);
GG_IMPORT(gg_glLinkProgram) void gg_glLinkProgram(unsigned);
GG_IMPORT(gg_glGetProgramiv) void gg_glGetProgramiv(unsigned, unsigned, int *);
GG_IMPORT(gg_glGetProgramInfoLog) void gg_glGetProgramInfoLog(unsigned, int, int *, char *);
GG_IMPORT(gg_glDeleteProgram) void gg_glDeleteProgram(unsigned);
GG_IMPORT(gg_glUseProgram) void gg_glUseProgram(unsigned);
GG_IMPORT(gg_glGetUniformLocation) int gg_glGetUniformLocation(unsigned, const char *);
GG_IMPORT(gg_glUniform1f) void gg_glUniform1f(int, float);
GG_IMPORT(gg_glUniform2f) void gg_glUniform2f(int, float, float);
GG_IMPORT(gg_glUniform3f) void gg_glUniform3f(int, float, float, float);
GG_IMPORT(gg_glUniform4f) void gg_glUniform4f(int, float, float, float, float);
GG_IMPORT(gg_glUniform1i) void gg_glUniform1i(int, int);
GG_IMPORT(gg_glUniformMatrix4fv) void gg_glUniformMatrix4fv(int, int, unsigned char, const float *);
GG_IMPORT(gg_glGenVertexArrays) void gg_glGenVertexArrays(int, unsigned *);
GG_IMPORT(gg_glBindVertexArray) void gg_glBindVertexArray(unsigned);
GG_IMPORT(gg_glDeleteVertexArrays) void gg_glDeleteVertexArrays(int, const unsigned *);
GG_IMPORT(gg_glGenBuffers) void gg_glGenBuffers(int, unsigned *);
GG_IMPORT(gg_glBindBuffer) void gg_glBindBuffer(unsigned, unsigned);
GG_IMPORT(gg_glBufferData) void gg_glBufferData(unsigned, intptr_t, const void *, unsigned);
GG_IMPORT(gg_glDeleteBuffers) void gg_glDeleteBuffers(int, const unsigned *);
GG_IMPORT(gg_glVertexAttribPointer) void gg_glVertexAttribPointer(unsigned, int, unsigned, unsigned char, int, const void *);
GG_IMPORT(gg_glEnableVertexAttribArray) void gg_glEnableVertexAttribArray(unsigned);
GG_IMPORT(gg_glDrawArrays) void gg_glDrawArrays(unsigned, int, int);
GG_IMPORT(gg_glDisable) void gg_glDisable(unsigned);
GG_IMPORT(gg_glBlendFunc) void gg_glBlendFunc(unsigned, unsigned);
GG_IMPORT(gg_glCullFace) void gg_glCullFace(unsigned);
GG_IMPORT(gg_glDepthMask) void gg_glDepthMask(unsigned char);
GG_IMPORT(gg_glScissor) void gg_glScissor(int, int, int, int);
GG_IMPORT(gg_glFinish) void gg_glFinish(void);
GG_IMPORT(gg_glLineWidth) void gg_glLineWidth(float);
GG_IMPORT(gg_glPointSize) void gg_glPointSize(float);
GG_IMPORT(gg_glGenTextures) void gg_glGenTextures(int, unsigned *);
GG_IMPORT(gg_glBindTexture) void gg_glBindTexture(unsigned, unsigned);
GG_IMPORT(gg_glTexImage2D) void gg_glTexImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
GG_IMPORT(gg_glTexSubImage2D) void gg_glTexSubImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
GG_IMPORT(gg_glTexParameteri) void gg_glTexParameteri(unsigned, unsigned, int);
GG_IMPORT(gg_glDeleteTextures) void gg_glDeleteTextures(int, const unsigned *);
GG_IMPORT(gg_glGenerateMipmap) void gg_glGenerateMipmap(unsigned);
GG_IMPORT(gg_glActiveTexture) void gg_glActiveTexture(unsigned);
GG_IMPORT(gg_glGenFramebuffers) void gg_glGenFramebuffers(int, unsigned *);
GG_IMPORT(gg_glBindFramebuffer) void gg_glBindFramebuffer(unsigned, unsigned);
GG_IMPORT(gg_glFramebufferTexture2D) void gg_glFramebufferTexture2D(unsigned, unsigned, unsigned, unsigned, int);
GG_IMPORT(gg_glDeleteFramebuffers) void gg_glDeleteFramebuffers(int, const unsigned *);
GG_IMPORT(gg_glDrawBuffer) void gg_glDrawBuffer(unsigned);
GG_IMPORT(gg_glDrawElements) void gg_glDrawElements(unsigned, int, unsigned, const void *);
GG_IMPORT(gg_glDrawArraysInstanced) void gg_glDrawArraysInstanced(unsigned, int, int, int);
GG_IMPORT(gg_glDrawElementsInstanced) void gg_glDrawElementsInstanced(unsigned, int, unsigned, const void *, int);
GG_IMPORT(gg_glVertexAttribDivisor) void gg_glVertexAttribDivisor(unsigned, unsigned);

/* A page is one thread, and its context is current from the first window on. */
static int gg_use(gg_window *w, const char *what) {
    (void) what;
    return w && !w->closed;
}

static int gg_bind_gl(gg_window *w) {
    w->api.glClearColor = gg_glClearColor;
    w->api.glClear = gg_glClear;
    w->api.glColorMask = gg_glColorMask;
    w->api.glViewport = gg_glViewport;
    w->api.glEnable = gg_glEnable;
    w->api.glReadBuffer = gg_glReadBuffer;
    w->api.glPixelStorei = gg_glPixelStorei;
    w->api.glReadPixels = gg_glReadPixels;
    w->api.glCreateShader = gg_glCreateShader;
    w->api.glShaderSource = gg_glShaderSource;
    w->api.glCompileShader = gg_glCompileShader;
    w->api.glGetShaderiv = gg_glGetShaderiv;
    w->api.glGetShaderInfoLog = gg_glGetShaderInfoLog;
    w->api.glDeleteShader = gg_glDeleteShader;
    w->api.glCreateProgram = gg_glCreateProgram;
    w->api.glAttachShader = gg_glAttachShader;
    w->api.glLinkProgram = gg_glLinkProgram;
    w->api.glGetProgramiv = gg_glGetProgramiv;
    w->api.glGetProgramInfoLog = gg_glGetProgramInfoLog;
    w->api.glDeleteProgram = gg_glDeleteProgram;
    w->api.glUseProgram = gg_glUseProgram;
    w->api.glGetUniformLocation = gg_glGetUniformLocation;
    w->api.glUniform1f = gg_glUniform1f;
    w->api.glUniform2f = gg_glUniform2f;
    w->api.glUniform3f = gg_glUniform3f;
    w->api.glUniform4f = gg_glUniform4f;
    w->api.glUniform1i = gg_glUniform1i;
    w->api.glUniformMatrix4fv = gg_glUniformMatrix4fv;
    w->api.glGenVertexArrays = gg_glGenVertexArrays;
    w->api.glBindVertexArray = gg_glBindVertexArray;
    w->api.glDeleteVertexArrays = gg_glDeleteVertexArrays;
    w->api.glGenBuffers = gg_glGenBuffers;
    w->api.glBindBuffer = gg_glBindBuffer;
    w->api.glBufferData = gg_glBufferData;
    w->api.glDeleteBuffers = gg_glDeleteBuffers;
    w->api.glVertexAttribPointer = gg_glVertexAttribPointer;
    w->api.glEnableVertexAttribArray = gg_glEnableVertexAttribArray;
    w->api.glDrawArrays = gg_glDrawArrays;
    w->api.glDisable = gg_glDisable;
    w->api.glBlendFunc = gg_glBlendFunc;
    w->api.glCullFace = gg_glCullFace;
    w->api.glDepthMask = gg_glDepthMask;
    w->api.glScissor = gg_glScissor;
    w->api.glFinish = gg_glFinish;
    w->api.glLineWidth = gg_glLineWidth;
    w->api.glPointSize = gg_glPointSize;
    w->api.glGenTextures = gg_glGenTextures;
    w->api.glBindTexture = gg_glBindTexture;
    w->api.glTexImage2D = gg_glTexImage2D;
    w->api.glTexSubImage2D = gg_glTexSubImage2D;
    w->api.glTexParameteri = gg_glTexParameteri;
    w->api.glDeleteTextures = gg_glDeleteTextures;
    w->api.glGenerateMipmap = gg_glGenerateMipmap;
    w->api.glActiveTexture = gg_glActiveTexture;
    w->api.glGenFramebuffers = gg_glGenFramebuffers;
    w->api.glBindFramebuffer = gg_glBindFramebuffer;
    w->api.glFramebufferTexture2D = gg_glFramebufferTexture2D;
    w->api.glDeleteFramebuffers = gg_glDeleteFramebuffers;
    w->api.glDrawBuffer = gg_glDrawBuffer;
    w->api.glDrawElements = gg_glDrawElements;
    w->api.glDrawArraysInstanced = gg_glDrawArraysInstanced;
    w->api.glDrawElementsInstanced = gg_glDrawElementsInstanced;
    w->api.glVertexAttribDivisor = gg_glVertexAttribDivisor;
    return 1;
}

/* The page's context outlives a window: only the record is closed. */
static void gg_teardown(gg_window *w) {
    w->closed = 1;
}
#endif

/* The record goes once the window is closed and no shader or mesh holds it. */
static void gg_forget(gg_window *w) {
    if (!w->closed || w->refs > 0) return;
    free(w->events);
    free(w->text);
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

static void gg_text_push(gg_window *w, const char *bytes, int n) {
    if (w->text_len + n > w->text_cap) {
        int64_t cap = (w->text_cap ? w->text_cap * 2 : 64) + n;
        char *grown = (char *) realloc(w->text, (size_t) cap);
        if (!grown) return;
        w->text = grown;
        w->text_cap = cap;
    }
    gg_push(w, GG_EV_TEXT, w->text_len, n, 0, 0);
    memcpy(w->text + w->text_len, bytes, (size_t) n);
    w->text_len += n;
}

#if defined(GG_X11)
/* 0.9's key codes (src/graphics.loft KEY_*): ASCII lower-cased for a character, its own codes for
 * the named keys, -1 for a key it has none for. */
static int64_t gg_key_code(gx_xid sym) {
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

/* The MOD_* bits of an X modifier state. */
static int64_t gg_mods(unsigned state) {
    return ((state & GX_SHIFT_MASK) ? GG_MOD_SHIFT : 0) | ((state & GX_CONTROL_MASK) ? GG_MOD_CTRL : 0) |
           ((state & GX_MOD1_MASK) ? GG_MOD_ALT : 0) | ((state & GX_MOD4_MASK) ? GG_MOD_SUPER : 0);
}


/* A key press: KeyDown (a repeat when the key is already down) and, when it types anything, a
 * Text with what it typed (Enter types "\r", Backspace "\b", as 0.9 reported them); a release:
 * KeyUp.  The modifiers are the state before the event, as X and 0.9 report them: pressing Shift
 * reports no Shift, releasing it reports Shift. */
static void gg_key(gg_window *w, gx_input_event *e, int press) {
    char buf[64];
    gx_xid sym = 0;
    int n, status = 0;
    int64_t code;
    unsigned state = e->state;
    if (press && w->ic)
        n = w->xa.Xutf8LookupString(w->ic, e, buf, (int) sizeof buf - 1, &sym, &status);
    else
        n = w->xa.XLookupString(e, buf, (int) sizeof buf - 1, &sym, NULL);
    if (n < 0) n = 0;
    code = gg_key_code(sym);
    if (press) {
        int repeat = code >= 0 && code < 256 && w->keys[code];
        if (code >= 0 && code < 256) w->keys[code] = 1;
        gg_push(w, GG_EV_KEY_DOWN, code, gg_mods(state), repeat, 0);
        if (n > 0) gg_text_push(w, buf, n);
    } else {
        if (code >= 0 && code < 256) w->keys[code] = 0;
        gg_push(w, GG_EV_KEY_UP, code, gg_mods(state), 0, 0);
    }
}

/* X buttons 1-3 are left, middle, right — 1, 4 and 2 here, as 0.9 numbered them; 4 and 5 are
 * the wheel, a step up and a step down. */
static void gg_button(gg_window *w, gx_input_event *e, int press) {
    int bit = e->detail == 1 ? 1 : e->detail == 2 ? 4 : e->detail == 3 ? 2 : 0;
    w->mouse_x = e->x;
    w->mouse_y = e->y;
    if (e->detail == 4 || e->detail == 5) {
        if (press) gg_push(w, GG_EV_WHEEL, e->detail == 4 ? 1 : -1, e->x, e->y, 0);
        return;
    }
    if (!bit) return;
    if (press) w->buttons |= bit;
    else w->buttons &= ~bit;
    gg_push(w, press ? GG_EV_MOUSE_DOWN : GG_EV_MOUSE_UP, bit, e->x, e->y, 0);
}

/* Present an opaque frame: whatever alpha the shaders wrote is forced to 1 first, as 0.9 did. */
static void gg_present(gg_window *w) {
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glColorMask(0, 0, 0, 1);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT);
    w->api.glColorMask(1, 1, 1, 1);
    w->xa.glXSwapBuffers(w->display, w->window);
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
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
    w->display = w->xa.XOpenDisplay(NULL);
    if (!w->display) goto fail;
    screen = w->xa.XDefaultScreen(w->display);
    root = w->xa.XRootWindow(w->display, screen);
    if (fullscreen) {
        width = w->xa.XDisplayWidth(w->display, screen);
        height = w->xa.XDisplayHeight(w->display, screen);
    }
    if (width <= 0 || height <= 0) goto fail;
    config = gg_pick_config(w, screen);
    if (!config) goto fail;
    visual = w->xa.glXGetVisualFromFBConfig(w->display, config);
    if (!visual) goto fail;
    w->colormap = w->xa.XCreateColormap(w->display, root, visual->visual, GX_ALLOC_NONE);
    memset(&attrs, 0, sizeof attrs);
    attrs.colormap = w->colormap;
    attrs.event_mask = GX_STRUCTURE_NOTIFY_MASK | GX_EXPOSURE_MASK | GX_KEY_PRESS_MASK |
                       GX_KEY_RELEASE_MASK | GX_BUTTON_PRESS_MASK | GX_BUTTON_RELEASE_MASK |
                       GX_POINTER_MOTION_MASK | GX_FOCUS_CHANGE_MASK;
    w->window = w->xa.XCreateWindow(w->display, root, 0, 0, (unsigned) width, (unsigned) height,
                                     0, visual->depth, GX_INPUT_OUTPUT, visual->visual,
                                     GX_CW_BORDER_PIXEL | GX_CW_COLORMAP | GX_CW_EVENT_MASK,
                                     &attrs);
    w->xa.XFree(visual);
    if (!w->window) goto fail;
    w->xa.XStoreName(w->display, w->window, title);
    w->wm_delete = w->xa.XInternAtom(w->display, "WM_DELETE_WINDOW", 0);
    w->xa.XSetWMProtocols(w->display, w->window, &w->wm_delete, 1);
    /* Auto-repeat as press after press, no release between: a key held down is then one
     * KeyDown, its repeats, and one KeyUp, as 0.9 reported it. */
    w->xa.XkbSetDetectableAutoRepeat(w->display, 1, NULL);
    w->im = w->xa.XOpenIM(w->display, NULL, NULL, NULL);
    if (w->im)
        w->ic = w->xa.XCreateIC(w->im, "inputStyle", GX_XIM_PREEDIT_NOTHING | GX_XIM_STATUS_NOTHING,
                                 "clientWindow", w->window, "focusWindow", w->window, NULL);
    if (fullscreen) {
        gx_xid state = w->xa.XInternAtom(w->display, "_NET_WM_STATE", 0);
        gx_xid full = w->xa.XInternAtom(w->display, "_NET_WM_STATE_FULLSCREEN", 0);
        w->xa.XChangeProperty(w->display, w->window, state, GX_ATOM_ATOM, 32,
                               GX_PROP_MODE_REPLACE, (const unsigned char *) &full, 1);
    }

    *(void **) (&w->xa.glXCreateContextAttribsARB) =
        w->xa.glXGetProcAddressARB((const unsigned char *) "glXCreateContextAttribsARB");
    if (!w->xa.glXCreateContextAttribsARB) goto fail;
    gg_x_failed = 0;
    previous = w->xa.XSetErrorHandler(gg_on_x_error);
    trapped = 1;
    w->context = w->xa.glXCreateContextAttribsARB(w->display, config, NULL, 1, context_attrs);
    w->xa.XSync(w->display, 0);
    if (gg_x_failed || !w->context) goto fail;

    w->xa.XMapWindow(w->display, w->window);
    for (;;) { /* until the server says the window is mapped */
        gx_event ev;
        w->xa.XNextEvent(w->display, &ev);
        if (ev.type == GX_MAP_NOTIFY && ev.any.window == w->window) break;
        if (ev.type == GX_CONFIGURE_NOTIFY && ev.configure.window == w->window) {
            width = ev.configure.width;
            height = ev.configure.height;
        }
    }
    if (!w->xa.glXMakeCurrent(w->display, w->window, w->context)) goto fail;
    w->xa.XSync(w->display, 0);
    if (gg_x_failed) goto fail;
    gg_current = w;
    w->xa.XSetErrorHandler(previous);
    trapped = 0;
    if (!gg_bind_gl(w)) goto fail;
    if (w->xa.glXSwapIntervalEXT) w->xa.glXSwapIntervalEXT(w->display, w->window, 1);
    w->width = (int) width;
    w->height = (int) height;

    /* 0.9's first frames: depth test on, both buffers opaque black before anything is shown. */
    w->api.glEnable(GL_DEPTH_TEST);
    w->caps = 1u << 1;
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
            w->xa.glXMakeCurrent(w->display, 0, NULL);
            w->xa.glXDestroyContext(w->display, w->context);
            w->context = NULL;
        }
        w->xa.XSync(w->display, 0);
        w->xa.XSetErrorHandler(previous);
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
    while (w->xa.XPending(w->display) > 0) {
        gx_event ev;
        w->xa.XNextEvent(w->display, &ev);
        if (w->ic && w->xa.XFilterEvent(&ev, 0)) continue; /* the input method took it */
        switch (ev.type) {
        case GX_KEY_PRESS:
        case GX_KEY_RELEASE:
            gg_key(w, &ev.input, ev.type == GX_KEY_PRESS);
            break;
        case GX_BUTTON_PRESS:
        case GX_BUTTON_RELEASE:
            gg_button(w, &ev.input, ev.type == GX_BUTTON_PRESS);
            break;
        case GX_MOTION_NOTIFY:
            w->mouse_x = ev.motion.x;
            w->mouse_y = ev.motion.y;
            gg_push(w, GG_EV_MOUSE_MOVE, ev.motion.x, ev.motion.y, 0, 0);
            break;
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

#elif defined(GG_COCOA)

/* 0.9's key codes from a virtual key code: the named keys by their place on the keyboard, a
 * character key by what it types with Shift alone (`charactersIgnoringModifiers`), lower-cased —
 * so Shift+1 is `!`, as X11 reports it — and -1 for a key there is no code for. */
static int64_t gc_key_code(unsigned kc, gc_id ev) {
    static const unsigned char fkeys[12] = {0x7A, 0x78, 0x63, 0x76, 0x60, 0x61,
                                            0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F};
    const char *chars;
    int i;
    for (i = 0; i < 12; i++)
        if (kc == fkeys[i]) return 135 + i; /* F1-F12 */
    if (kc >= 0x52 && kc <= 0x59) return '0' + (int64_t) (kc - 0x52); /* keypad 0-7 */
    switch (kc) {
    case 0x5B: return '8';
    case 0x5C: return '9';
    case 0x24: case 0x4C: return 13; /* Return, keypad Enter */
    case 0x35: return 27;  /* Escape */
    case 0x30: return 9;   /* Tab */
    case 0x33: return 8;   /* Delete, which types a backspace */
    case 0x7E: return 128; /* Up */
    case 0x7D: return 129; /* Down */
    case 0x7B: return 130; /* Left */
    case 0x7C: return 131; /* Right */
    case 0x38: case 0x3C: return 132; /* Shift */
    case 0x3B: case 0x3E: return 133; /* Control */
    case 0x75: return 147; /* forward delete */
    case 0x72: return 148; /* Help, where Insert is */
    case 0x73: return 149; /* Home */
    case 0x77: return 150; /* End */
    case 0x74: return 151; /* Page Up */
    case 0x79: return 152; /* Page Down */
    case 0x3A: case 0x3D: case 0x37: case 0x36: case 0x39: return -1; /* Option, Command, Caps */
    default: break;
    }
    if (!ev) return -1;
    chars = (const char *) gc_o(gc_o(ev, "charactersIgnoringModifiers"), "UTF8String");
    if (chars && chars[0] >= 0x20 && chars[0] <= 0x7e && chars[1] == 0)
        return chars[0] >= 'A' && chars[0] <= 'Z' ? chars[0] + 32 : chars[0];
    return -1;
}

static int64_t gc_mods(unsigned long flags) {
    return ((flags & GC_FLAG_SHIFT) ? GG_MOD_SHIFT : 0) | ((flags & GC_FLAG_CONTROL) ? GG_MOD_CTRL : 0) |
           ((flags & GC_FLAG_OPTION) ? GG_MOD_ALT : 0) | ((flags & GC_FLAG_COMMAND) ? GG_MOD_SUPER : 0);
}

/* A key: KeyDown (a repeat when it is already down) and the Text it typed, or KeyUp — the
 * modifiers as they stood before the event, as on X11.  What a key types is AppKit's, except
 * where X11 types something else: Delete types a backspace "\b", forward delete "\x7f", and the
 * keys that type nothing (arrows, F-keys: U+F700-U+F8FF here) no Text. */
static void gc_key(gg_window *w, gc_id ev, int press) {
    unsigned kc = (unsigned) gc_l(ev, "keyCode");
    int64_t code = gc_key_code(kc, ev);
    int64_t mods = gc_mods(gc_l(ev, "modifierFlags"));
    if (!press) {
        if (code >= 0 && code < 256) w->keys[code] = 0;
        gg_push(w, GG_EV_KEY_UP, code, mods, 0, 0);
        return;
    }
    {
        int repeat = code >= 0 && code < 256 && w->keys[code];
        const char *text = (const char *) gc_o(gc_o(ev, "characters"), "UTF8String");
        size_t n = text ? strlen(text) : 0;
        if (code >= 0 && code < 256) w->keys[code] = 1;
        gg_push(w, GG_EV_KEY_DOWN, code, mods, repeat, 0);
        if (kc == 0x33) text = "\b", n = 1;
        else if (kc == 0x75) text = "\x7f", n = 1;
        else if (n == 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] >= 0x9C &&
                 (unsigned char) text[1] <= 0xA3)
            n = 0; /* U+F700-U+F8FF: AppKit's function-key characters */
        if (n > 0) gg_text_push(w, text, (int) n);
    }
}

/* Shift, Control, Option, Command: AppKit reports them as the modifier state changing.  The
 * key's bit in the new state says press or release; the state before is the new one without it
 * (a press) or with it (a release). */
static void gc_flags(gg_window *w, gc_id ev) {
    unsigned kc = (unsigned) gc_l(ev, "keyCode");
    unsigned long flags = gc_l(ev, "modifierFlags"), bit;
    int64_t code = gc_key_code(kc, NULL);
    int press;
    switch (kc) {
    case 0x38: case 0x3C: bit = GC_FLAG_SHIFT; break;
    case 0x3B: case 0x3E: bit = GC_FLAG_CONTROL; break;
    case 0x3A: case 0x3D: bit = GC_FLAG_OPTION; break;
    case 0x37: case 0x36: bit = GC_FLAG_COMMAND; break;
    default: return; /* Caps Lock, Fn */
    }
    press = (flags & bit) != 0;
    if (code >= 0 && code < 256) w->keys[code] = (unsigned char) press;
    gg_push(w, press ? GG_EV_KEY_DOWN : GG_EV_KEY_UP, code,
            gc_mods(press ? flags & ~bit : flags | bit), 0, 0);
}

/* The event's place in pixels from the content's top left (AppKit's y runs up from the bottom). */
static void gc_where(gg_window *w, gc_id ev, int *x, int *y) {
    gc_point p = gc_pt(ev, "locationInWindow");
    *x = (int) floor(p.x);
    *y = (int) floor((double) w->height - p.y);
}

static void gc_event(gg_window *w, gc_id ev, unsigned long type) {
    int x, y, bit = 0, inside;
    double dy;
    switch (type) {
    case GC_KEY_DOWN:
    case GC_KEY_UP:
        gc_key(w, ev, type == GC_KEY_DOWN);
        return;
    case GC_FLAGS_CHANGED:
        gc_flags(w, ev);
        return;
    default:
        break;
    }
    gc_where(w, ev, &x, &y);
    inside = x >= 0 && y >= 0 && x < w->width && y < w->height;
    switch (type) {
    case GC_LEFT_DOWN: case GC_LEFT_UP: bit = 1; break;
    case GC_RIGHT_DOWN: case GC_RIGHT_UP: bit = 2; break;
    case GC_OTHER_DOWN: case GC_OTHER_UP: bit = gc_l(ev, "buttonNumber") == 2 ? 4 : 0; break;
    case GC_MOVED: case GC_LEFT_DRAGGED: case GC_RIGHT_DRAGGED: case GC_OTHER_DRAGGED:
        if (!inside && !w->buttons) return; /* over the title bar */
        w->mouse_x = x;
        w->mouse_y = y;
        gg_push(w, GG_EV_MOUSE_MOVE, x, y, 0, 0);
        return;
    case GC_SCROLL:
        dy = gc_d(ev, "scrollingDeltaY");
        if (!inside || dy == 0.0) return;
        w->mouse_x = x;
        w->mouse_y = y;
        gg_push(w, GG_EV_WHEEL, dy > 0.0 ? 1 : -1, x, y, 0);
        return;
    default:
        return;
    }
    if (!bit) return;
    if (type == GC_LEFT_DOWN || type == GC_RIGHT_DOWN || type == GC_OTHER_DOWN) {
        if (!inside) return; /* the title bar or a border: the window's, not the program's */
        w->buttons |= bit;
        w->mouse_x = x;
        w->mouse_y = y;
        gg_push(w, GG_EV_MOUSE_DOWN, bit, x, y, 0);
    } else if (w->buttons & bit) { /* a release ends a press, wherever it lands */
        w->buttons &= ~bit;
        w->mouse_x = x;
        w->mouse_y = y;
        gg_push(w, GG_EV_MOUSE_UP, bit, x, y, 0);
    }
}

/* Take every queued event, each to the window it belongs to.  Keys are answered here and not
 * passed on: AppKit beeps at a key no view takes.  Everything else goes on to AppKit, which moves,
 * resizes and closes the window. */
static void gc_pump(void) {
    gc_id past = gc_o(gc_class("NSDate"), "distantPast");
    for (;;) {
        gc_id ev = ((gc_id (*)(gc_id, gc_sel, unsigned long, gc_id, gc_id, signed char)) gc.msg)(
            gc.app, gc_s("nextEventMatchingMask:untilDate:inMode:dequeue:"), ~0ul, past,
            gc.run_loop_mode, 1);
        unsigned long type;
        gc_id target;
        gg_window *w;
        if (!ev) break;
        type = gc_l(ev, "type");
        target = gc_o(ev, "window");
        for (w = gc.open; w; w = (gg_window *) w->next_open)
            if (w->window == target) {
                gc_event(w, ev, type);
                break;
            }
        if (type == GC_KEY_DOWN || type == GC_KEY_UP) continue;
        gc_v_o(gc.app, "sendEvent:", ev);
    }
    gc_v(gc.app, "updateWindows");
}

/* Present an opaque frame: whatever alpha the shaders wrote is forced to 1 first, as 0.9 did. */
static void gg_present(gg_window *w) {
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glColorMask(0, 0, 0, 1);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT);
    w->api.glColorMask(1, 1, 1, 1);
    gc_v(w->context, "flushBuffer");
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
}

/* A titled window centred on the main screen, or for `fullscreen` a borderless one over the whole
 * screen, the dock and the menu bar hidden.  The pixel format is 0.9's: double-buffered RGB, no
 * alpha, 24-bit depth.  The drawable is in points (no Retina backing), as the window's size is. */
int64_t gg_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    static const uint32_t attrs[] = {GC_PFA_ACCELERATED, GC_PFA_DOUBLE_BUFFER, GC_PFA_COLOR_SIZE, 24,
                                     GC_PFA_DEPTH_SIZE, 24, GC_PFA_PROFILE, GC_PROFILE_3_2_CORE, 0};
    gg_window *w;
    void *pool;
    gc_rect content, bounds;
    unsigned long style = GC_STYLE_TITLED | GC_STYLE_CLOSABLE | GC_STYLE_MINIATURIZABLE |
                          GC_STYLE_RESIZABLE;
    gc_id cls;
    int one = 1;
    if (!pthread_main_np()) {
        fprintf(stderr, "graphics: on macOS a window opens on the main thread only\n");
        return 0;
    }
    if (!gc_load()) return 0;
    w = (gg_window *) calloc(1, sizeof(gg_window));
    if (!w) return 0;
    w->thread = pthread_self();
    pool = gc.pool_push();
    content.x = content.y = 0.0;
    content.width = (double) width;
    content.height = (double) height;
    cls = gc_class("NSWindow");
    if (fullscreen) {
        gc_id screen = gc_o(gc_class("NSScreen"), "mainScreen");
        if (!screen || !gc.full_class) goto fail;
        content = gc_rc(screen, "frame");
        style = 0;
        cls = gc.full_class;
        w->fullscreen = 1;
    }
    if (content.width <= 0.0 || content.height <= 0.0) goto fail;
    w->window = ((gc_id (*)(gc_id, gc_sel, gc_rect, unsigned long, unsigned long, signed char)) gc.msg)(
        gc_o(cls, "alloc"), gc_s("initWithContentRect:styleMask:backing:defer:"), content, style,
        GC_BACKING_BUFFERED, 0);
    if (!w->window) goto fail;
    gc_v_l(w->window, "setReleasedWhenClosed:", 0);
    gc_v_l(w->window, "setAcceptsMouseMovedEvents:", 1);
    gc_v_o(w->window, "setTitle:", gc_o_p(gc_class("NSString"), "stringWithUTF8String:", title));
    if (fullscreen) {
        gc_v_l(w->window, "setLevel:", GC_MAIN_MENU_LEVEL + 1);
        gc_v_l(gc.app, "setPresentationOptions:", (long) (GC_HIDE_DOCK | GC_HIDE_MENU_BAR));
    } else {
        gc_v(w->window, "center");
    }
    w->view = gc_o(w->window, "contentView");
    gc_v_l(w->view, "setWantsBestResolutionOpenGLSurface:", 0);
    w->format = gc_o_p(gc_o(gc_class("NSOpenGLPixelFormat"), "alloc"), "initWithAttributes:", attrs);
    if (!w->format) goto fail;
    w->context = ((gc_id (*)(gc_id, gc_sel, gc_id, gc_id)) gc.msg)(
        gc_o(gc_class("NSOpenGLContext"), "alloc"), gc_s("initWithFormat:shareContext:"), w->format,
        NULL);
    if (!w->context) goto fail;
    ((void (*)(gc_id, gc_sel, const int *, long)) gc.msg)(w->context, gc_s("setValues:forParameter:"),
                                                           &one, GC_SWAP_INTERVAL);
    ((void (*)(gc_id, gc_sel, const int *, long)) gc.msg)(w->context, gc_s("setValues:forParameter:"),
                                                           &one, GC_SURFACE_OPACITY);
    gc_v_o(w->window, "makeKeyAndOrderFront:", NULL);
    gc_v_l(gc.app, "activateIgnoringOtherApps:", 1);
    w->next_open = gc.open;
    gc.open = w;
    gc_pump(); /* the window on screen before its view is drawn into */
    gc_v_o(w->context, "setView:", w->view);
    gc_v(w->context, "makeCurrentContext");
    gg_current = w;
    if (!gg_bind_gl(w)) goto fail;
    bounds = gc_rc(w->view, "bounds");
    w->width = (int) bounds.width;
    w->height = (int) bounds.height;
    if (w->width <= 0 || w->height <= 0) goto fail;

    /* 0.9's first frames: depth test on, both buffers opaque black before anything is shown. */
    w->api.glEnable(GL_DEPTH_TEST);
    w->caps = 1u << 1;
    w->api.glViewport(0, 0, w->width, w->height);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gg_present(w);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gg_present(w);
    gc.pool_pop(pool);
    return (int64_t) (intptr_t) w;
fail:
    gc.pool_pop(pool);
    gg_free(w);
    return 0;
}

/* Take the queued events; a resize applies to the context and the viewport and is queued as a
 * `Resize`; the window's close button ends the loop (a minimised window is still open). */
int64_t gg_poll(int64_t h) {
    gg_window *w = gg_of(h);
    void *pool;
    gc_rect bounds;
    int width, height;
    if (!gg_use(w, "Window.poll")) return 0;
    pool = gc.pool_push();
    gc_pump();
    if (!w->closed) {
        if (!gc_b(w->window, "isVisible") && !gc_b(w->window, "isMiniaturized")) w->should_close = 1;
        bounds = gc_rc(w->view, "bounds");
        width = (int) bounds.width;
        height = (int) bounds.height;
        if (width > 0 && height > 0 && (width != w->width || height != w->height)) {
            gg_use(w, "Window.poll");
            gc_v(w->context, "update");
            w->width = width;
            w->height = height;
            w->api.glViewport(0, 0, width, height);
            gg_push(w, GG_EV_RESIZE, width, height, 0, 0);
        }
    }
    gc.pool_pop(pool);
    return !w->should_close;
}

#else /* GG_BROWSER */

/* Present the frame and give the page its turn: the browser draws it, and its events arrive. */
static void gg_present(gg_window *w) {
    (void) w;
    gg_js_present();
}

/* Size the page's canvas (or the whole page, for `fullscreen`) and take its WebGL2 context. */
int64_t gg_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    gg_window *w;
    (void) title;
    if (!fullscreen && (width <= 0 || height <= 0)) return 0;
    if (!gg_js_open((int) width, (int) height, (int) fullscreen)) return 0;
    w = (gg_window *) calloc(1, sizeof(gg_window));
    if (!w) return 0;
    gg_bind_gl(w);
    w->width = gg_js_size(0);
    w->height = gg_js_size(1);
    /* 0.9's first state: depth test on, the frame opaque black */
    w->api.glEnable(GL_DEPTH_TEST);
    w->caps = 1u << 1;
    w->api.glViewport(0, 0, w->width, w->height);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return (int64_t) (intptr_t) w;
}

/* Take the page's queued input — already in 0.9's codes (doc/loft-gl-wasm.js) — into the
 * window's queue and its input state.  A page never asks to close. */
int64_t gg_poll(int64_t h) {
    gg_window *w = gg_of(h);
    int ev[5];
    char text[64];
    int tag;
    if (!gg_use(w, "Window.poll")) return 0;
    while ((tag = gg_js_event(ev, text, (int) sizeof text)) != 0) {
        if (tag == GG_EV_KEY_DOWN || tag == GG_EV_KEY_UP) {
            if (ev[0] >= 0 && ev[0] < 256) w->keys[ev[0]] = tag == GG_EV_KEY_DOWN;
            gg_push(w, tag, ev[0], ev[1], tag == GG_EV_KEY_DOWN ? ev[2] : 0, 0);
        } else if (tag == GG_EV_TEXT) {
            gg_text_push(w, text, ev[0]);
        } else if (tag == GG_EV_MOUSE_DOWN || tag == GG_EV_MOUSE_UP) {
            w->mouse_x = ev[1];
            w->mouse_y = ev[2];
            if (tag == GG_EV_MOUSE_DOWN) w->buttons |= ev[0];
            else w->buttons &= ~ev[0];
            gg_push(w, tag, ev[0], ev[1], ev[2], 0);
        } else if (tag == GG_EV_MOUSE_MOVE) {
            w->mouse_x = ev[0];
            w->mouse_y = ev[1];
            gg_push(w, tag, ev[0], ev[1], 0, 0);
        } else if (tag == GG_EV_WHEEL) {
            gg_push(w, tag, ev[0], ev[1], ev[2], 0);
        }
    }
    return !w->should_close;
}
#endif

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
    if (!w || w->closed) return;
#if defined(GG_X11) || defined(GG_COCOA)
    if (!gg_on_thread(w, "Window.close")) return;
#endif
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
    w->text_len = 0;
}

/* The bytes of the queued `Text` events — copy them before `gg_events` empties the queue. */
int64_t gg_text_size(int64_t h) {
    gg_window *w = gg_of(h);
    return w ? w->text_len : 0;
}

void gg_text_copy(int64_t h, uint8_t *out, int64_t n) {
    gg_window *w = gg_of(h);
    if (!w || n < w->text_len) return;
    memcpy(out, w->text, (size_t) w->text_len);
}

int64_t gg_key_down(int64_t h, int64_t key) {
    gg_window *w = gg_of(h);
    return w && key >= 0 && key < 256 && w->keys[key];
}

/* 0: x, 1: y, 2: the buttons held (1 left, 2 right, 4 middle). */
int64_t gg_mouse(int64_t h, int64_t which) {
    gg_window *w = gg_of(h);
    if (!w) return 0;
    return which == 0 ? w->mouse_x : which == 1 ? w->mouse_y : w->buttons;
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
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    w->api.glReadBuffer(GL_BACK);
    w->api.glReadPixels(0, 0, w->width, w->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
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

/* A driver's log without its trailing newlines, so the message ends where it says it ends. */
static void gg_trim(char *log) {
    size_t n = strlen(log);
    while (n > 0 && (log[n - 1] == '\n' || log[n - 1] == '\r' || log[n - 1] == ' ')) log[--n] = 0;
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
        gg_trim(log);
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
        gg_trim(log);
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
    r->stride = (int) stride;
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

/* Draw `mesh` with `shader`: DRAW_TRIANGLES 0, DRAW_LINES 1, DRAW_POINTS 2 (0.9's codes); all
 * its vertices, or all its indices; `instances` times when that is above 0. */
void gg_draw_as(int64_t mesh, int64_t shader, int64_t mode, int64_t indexed, int64_t instances) {
    gg_res *m = gg_res_of(mesh), *s = gg_res_of(shader);
    unsigned prim = mode == 1 ? GL_LINES : mode == 2 ? GL_POINTS : GL_TRIANGLES;
    gg_api *gl;
    if (!m || !s || m->kind != GG_MESH || s->kind != GG_SHADER) return;
    if (m->w != s->w) {
        fprintf(stderr, "graphics: GpuMesh.draw with a shader of another window\n");
        return;
    }
    if (!gg_use(m->w, "GpuMesh.draw")) return;
    if (indexed && !m->indices_id) return;
    gl = &m->w->api;
    gl->glUseProgram(s->id);
    gl->glBindVertexArray(m->id);
    if (indexed && instances > 0)
        gl->glDrawElementsInstanced(prim, m->indices, GL_UNSIGNED_INT, NULL, (int) instances);
    else if (indexed)
        gl->glDrawElements(prim, m->indices, GL_UNSIGNED_INT, NULL);
    else if (instances > 0)
        gl->glDrawArraysInstanced(prim, 0, m->vertices, (int) instances);
    else
        gl->glDrawArrays(prim, 0, m->vertices);
    gl->glBindVertexArray(0);
}

void gg_draw(int64_t mesh, int64_t shader) {
    gg_draw_as(mesh, shader, 0, 0, 0);
}

/* A 2-D draw: over whatever the frame holds, in call order — the depth test is off for this one
 * draw and back as it was after it, so a HUD neither hides behind a 3-D scene nor loses to an
 * earlier rectangle at the same depth. */
void gg_draw_over(int64_t mesh, int64_t shader) {
    gg_res *m = gg_res_of(mesh);
    unsigned depth;
    if (!m || m->kind != GG_MESH || !gg_use(m->w, "Painter")) return;
    depth = (m->w->caps >> 1) & 1u;
    if (depth) m->w->api.glDisable(GL_DEPTH_TEST);
    gg_draw_as(mesh, shader, 0, 0, 0);
    if (depth) m->w->api.glEnable(GL_DEPTH_TEST);
}

static gg_res *gg_mesh_of(int64_t h, const char *what) {
    gg_res *r = gg_res_of(h);
    if (!r || r->kind != GG_MESH || !gg_use(r->w, what)) return NULL;
    return r;
}

/* New vertices for the mesh, in the same layout; uploaded for change (DYNAMIC_DRAW). */
void gg_mesh_update(int64_t mesh, const float *data, int64_t n) {
    gg_res *r = gg_mesh_of(mesh, "GpuMesh.update");
    if (!r || r->stride < 3) return;
    r->vertices = (int) (n / r->stride);
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, r->buffer);
    r->w->api.glBufferData(GL_ARRAY_BUFFER, (intptr_t) r->vertices * r->stride * 4, data,
                           GL_DYNAMIC_DRAW);
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* The mesh's index list, 32 bits each; a second call replaces the first. */
void gg_mesh_indices(int64_t mesh, const int64_t *idx, int64_t n) {
    gg_res *r = gg_mesh_of(mesh, "GpuMesh.indices");
    uint32_t *u;
    int64_t i;
    if (!r || n < 0) return;
    u = (uint32_t *) malloc((size_t) (n > 0 ? n : 1) * 4u);
    if (!u) return;
    for (i = 0; i < n; i++) u[i] = (uint32_t) idx[i];
    r->w->api.glBindVertexArray(r->id);
    if (!r->indices_id) r->w->api.glGenBuffers(1, &r->indices_id);
    r->w->api.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->indices_id);
    r->w->api.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (intptr_t) n * 4, u, GL_STATIC_DRAW);
    r->w->api.glBindVertexArray(0);
    r->indices = (int) n;
    free(u);
}

/* Per-instance floats, uploaded for change (DYNAMIC_DRAW). */
int64_t gg_instances(int64_t win, const float *data, int64_t n) {
    gg_window *w = gg_of(win);
    gg_res *r;
    if (!gg_use(w, "Window.instances")) return 0;
    r = gg_new_res(w, GG_BUFFER);
    if (!r) return 0;
    w->api.glGenBuffers(1, &r->id);
    w->api.glBindBuffer(GL_ARRAY_BUFFER, r->id);
    w->api.glBufferData(GL_ARRAY_BUFFER, (intptr_t) n * 4, data, GL_DYNAMIC_DRAW);
    w->api.glBindBuffer(GL_ARRAY_BUFFER, 0);
    return (int64_t) (intptr_t) r;
}

void gg_buffer_update(int64_t buf, const float *data, int64_t n) {
    gg_res *r = gg_res_of(buf);
    if (!r || r->kind != GG_BUFFER || !gg_use(r->w, "InstanceBuffer.update")) return;
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, r->id);
    r->w->api.glBufferData(GL_ARRAY_BUFFER, (intptr_t) n * 4, data, GL_DYNAMIC_DRAW);
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* Feed attribute `location` of `mesh` from `buf`, once per instance: `components` floats at
 * `offset` floats into each record of `stride` floats. */
void gg_instance_attrib(int64_t mesh, int64_t buf, int64_t location, int64_t components,
                        int64_t stride, int64_t offset) {
    gg_res *m = gg_mesh_of(mesh, "GpuMesh.instance_attrib"), *b = gg_res_of(buf);
    gg_api *gl;
    if (!m || !b || b->kind != GG_BUFFER || b->w != m->w || location < 0) return;
    gl = &m->w->api;
    gl->glBindVertexArray(m->id);
    gl->glBindBuffer(GL_ARRAY_BUFFER, b->id);
    gl->glVertexAttribPointer((unsigned) location, (int) components, GL_FLOAT, 0,
                              (int) stride * 4, (const void *) (intptr_t) (offset * 4));
    gl->glEnableVertexAttribArray((unsigned) location);
    gl->glVertexAttribDivisor((unsigned) location, 1);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glBindVertexArray(0);
}

/* Release a shader or a mesh.  After its window closed there is nothing left to delete. */
void gg_release(int64_t res) {
    gg_res *r = gg_res_of(res);
    gg_window *w;
    if (!r) return;
    w = r->w;
#if defined(GG_BROWSER)
    if (1) { /* the page's context outlives the window: delete in it whenever */
#else
    if (!w->closed) {
        if (!gg_use(w, "a GL object's drop")) return;
#endif
        if (r->kind == GG_SHADER) {
            w->api.glDeleteProgram(r->id);
        } else if (r->kind == GG_MESH) {
            w->api.glDeleteVertexArrays(1, &r->id);
            w->api.glDeleteBuffers(1, &r->buffer);
            if (r->indices_id) w->api.glDeleteBuffers(1, &r->indices_id);
        } else if (r->kind == GG_BUFFER) {
            w->api.glDeleteBuffers(1, &r->id);
        } else if (r->kind == GG_TEXTURE) {
            w->api.glDeleteTextures(1, &r->id);
        } else {
            if (w->target == r->id) { /* drawing returns to the window */
                w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
                w->target = 0;
            }
            w->api.glDeleteFramebuffers(1, &r->id);
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
    if (on) w->caps |= 1u << cap;
    else w->caps &= ~(1u << cap);
}

/* Whether capability `cap` is on: read from the record, so asking costs no GL call. */
int64_t gg_capability_on(int64_t win, int64_t cap) {
    gg_window *w = gg_of(win);
    if (cap < 1 || cap > 4 || !w || w->closed) return 0;
    return (w->caps >> cap) & 1u;
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

/* ── Textures and framebuffers, made as 0.9 made them ──────────────────────────────────────── */

static gg_res *gg_texture_of(int64_t h, const char *what) {
    gg_res *r = gg_res_of(h);
    if (!r || r->kind != GG_TEXTURE || !gg_use(r->w, what)) return NULL;
    return r;
}

/* An empty colour texture (RGBA8, linear) or depth texture (32-bit float, nearest, clamped). */
int64_t gg_texture(int64_t win, int64_t width, int64_t height, int64_t depth) {
    gg_window *w = gg_of(win);
    gg_res *r;
    gg_api *gl;
    if (width <= 0 || height <= 0 || !gg_use(w, "Window.texture")) return 0;
    r = gg_new_res(w, GG_TEXTURE);
    if (!r) return 0;
    gl = &w->api;
    r->width = (int) width;
    r->height = (int) height;
    r->depth = depth != 0;
    gl->glGenTextures(1, &r->id);
    gl->glBindTexture(GL_TEXTURE_2D, r->id);
    if (r->depth) {
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, r->width, r->height, 0,
                         GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r->width, r->height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, NULL);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return (int64_t) (intptr_t) r;
}

/* 0xAARRGGBB pixels as RGBA bytes, row 0 first (it lands at the texture's bottom row). */
static unsigned char *gg_rgba(const int64_t *px, int64_t n) {
    unsigned char *out = (unsigned char *) malloc((size_t) n * 4u);
    int64_t i;
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        uint32_t p = (uint32_t) px[i];
        out[4 * i] = (unsigned char) (p >> 16);
        out[4 * i + 1] = (unsigned char) (p >> 8);
        out[4 * i + 2] = (unsigned char) p;
        out[4 * i + 3] = (unsigned char) (p >> 24);
    }
    return out;
}

/* A colour texture holding a canvas's pixels: linear, clamped. */
int64_t gg_texture_from(int64_t win, const int64_t *px, int64_t n, int64_t width, int64_t height) {
    gg_window *w = gg_of(win);
    unsigned char *rgba;
    gg_res *r;
    gg_api *gl;
    if (width <= 0 || height <= 0 || n < width * height || !gg_use(w, "Window.texture_from"))
        return 0;
    rgba = gg_rgba(px, width * height);
    if (!rgba) return 0;
    r = gg_new_res(w, GG_TEXTURE);
    if (!r) {
        free(rgba);
        return 0;
    }
    gl = &w->api;
    r->width = (int) width;
    r->height = (int) height;
    gl->glGenTextures(1, &r->id);
    gl->glBindTexture(GL_TEXTURE_2D, r->id);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r->width, r->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    free(rgba);
    return (int64_t) (intptr_t) r;
}

/* Replace a `width` x `height` region at (x, y) of a colour texture. */
void gg_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                    const int64_t *px, int64_t n) {
    gg_res *r = gg_texture_of(tex, "Texture.put");
    unsigned char *rgba;
    if (!r || r->depth || width <= 0 || height <= 0 || n < width * height) return;
    rgba = gg_rgba(px, width * height);
    if (!rgba) return;
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
    r->w->api.glTexSubImage2D(GL_TEXTURE_2D, 0, (int) x, (int) y, (int) width, (int) height,
                              GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    r->w->api.glBindTexture(GL_TEXTURE_2D, 0);
    free(rgba);
}

void gg_texture_filter(int64_t tex, int64_t nearest) {
    gg_res *r = gg_texture_of(tex, "Texture.filter");
    int f = nearest ? GL_NEAREST : GL_LINEAR;
    if (!r) return;
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
    r->w->api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    r->w->api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    r->w->api.glBindTexture(GL_TEXTURE_2D, 0);
}

/* Mipmaps from the current pixels, minified trilinearly. */
void gg_texture_mipmaps(int64_t tex) {
    gg_res *r = gg_texture_of(tex, "Texture.mipmaps");
    if (!r) return;
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
    r->w->api.glGenerateMipmap(GL_TEXTURE_2D);
    r->w->api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    r->w->api.glBindTexture(GL_TEXTURE_2D, 0);
}

/* Bind to texture unit `unit`, where a sampler uniform set to `unit` reads it. */
void gg_texture_bind(int64_t tex, int64_t unit) {
    gg_res *r = gg_texture_of(tex, "Texture.bind");
    if (!r || unit < 0) return;
    r->w->api.glActiveTexture(GL_TEXTURE0 + (unsigned) unit);
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
}

/* 0: the width, 1: the height. */
int64_t gg_texture_size(int64_t tex, int64_t which) {
    gg_res *r = gg_res_of(tex);
    if (!r || r->kind != GG_TEXTURE) return 0;
    return which == 0 ? r->width : r->height;
}

int64_t gg_framebuffer(int64_t win) {
    gg_window *w = gg_of(win);
    gg_res *r;
    if (!gg_use(w, "Window.framebuffer")) return 0;
    r = gg_new_res(w, GG_FRAMEBUFFER);
    if (!r) return 0;
    w->api.glGenFramebuffers(1, &r->id);
    return (int64_t) (intptr_t) r;
}

/* Attach a texture: a depth texture as the depth buffer, any other as colour 0.  Without a
 * colour attachment the framebuffer draws and reads no colour (a depth-only pass). */
void gg_framebuffer_attach(int64_t fb, int64_t tex) {
    gg_res *f = gg_res_of(fb), *t = gg_res_of(tex);
    gg_api *gl;
    if (!f || !t || f->kind != GG_FRAMEBUFFER || t->kind != GG_TEXTURE) return;
    if (f->w != t->w) {
        fprintf(stderr, "graphics: Framebuffer.attach with a texture of another window\n");
        return;
    }
    if (!gg_use(f->w, "Framebuffer.attach")) return;
    gl = &f->w->api;
    gl->glBindFramebuffer(GL_FRAMEBUFFER, f->id);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, t->depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, t->id, 0);
    if (!t->depth) f->has_colour = 1;
    gl->glDrawBuffer(f->has_colour ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    gl->glReadBuffer(f->has_colour ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, f->w->target);
}

/* Draw into framebuffer `fb` from now on; 0 draws into the window again. */
void gg_render_to(int64_t win, int64_t fb) {
    gg_window *w = gg_of(win);
    gg_res *f = gg_res_of(fb);
    unsigned id = 0;
    if (f) {
        if (f->kind != GG_FRAMEBUFFER || f->w != w) {
            fprintf(stderr, "graphics: Window.render_to with a framebuffer of another window\n");
            return;
        }
        id = f->id;
    }
    if (!gg_use(w, "Window.render_to")) return;
    w->api.glBindFramebuffer(GL_FRAMEBUFFER, id);
    w->target = id;
}

/* Two triangles over the whole target: a vec2 position at location 0, a vec2 UV at 1. */
int64_t gg_quad(int64_t win) {
    static const float verts[24] = {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
                                    -1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
                                    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f};
    gg_window *w = gg_of(win);
    gg_res *r;
    gg_api *gl;
    if (!gg_use(w, "Window.quad")) return 0;
    r = gg_new_res(w, GG_MESH);
    if (!r) return 0;
    gl = &w->api;
    r->vertices = 6;
    r->stride = 4;
    gl->glGenVertexArrays(1, &r->id);
    gl->glGenBuffers(1, &r->buffer);
    gl->glBindVertexArray(r->id);
    gl->glBindBuffer(GL_ARRAY_BUFFER, r->buffer);
    gl->glBufferData(GL_ARRAY_BUFFER, (intptr_t) sizeof verts, verts, GL_STATIC_DRAW);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, 0, 16, (const void *) 0);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(1, 2, GL_FLOAT, 0, 16, (const void *) 8);
    gl->glEnableVertexAttribArray(1);
    gl->glBindVertexArray(0);
    return (int64_t) (intptr_t) r;
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
void gg_draw_over(int64_t mesh, int64_t shader) {
    (void) mesh;
    (void) shader;
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
int64_t gg_capability_on(int64_t win, int64_t cap) {
    (void) win;
    (void) cap;
    return 0;
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

int64_t gg_texture(int64_t win, int64_t width, int64_t height, int64_t depth) {
    (void) win;
    (void) width;
    (void) height;
    (void) depth;
    return 0;
}
int64_t gg_texture_from(int64_t win, const int64_t *px, int64_t n, int64_t width, int64_t height) {
    (void) win;
    (void) px;
    (void) n;
    (void) width;
    (void) height;
    return 0;
}
void gg_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                    const int64_t *px, int64_t n) {
    (void) tex;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    (void) px;
    (void) n;
}
void gg_texture_filter(int64_t tex, int64_t nearest) {
    (void) tex;
    (void) nearest;
}
void gg_texture_mipmaps(int64_t tex) {
    (void) tex;
}
void gg_texture_bind(int64_t tex, int64_t unit) {
    (void) tex;
    (void) unit;
}
int64_t gg_texture_size(int64_t tex, int64_t which) {
    (void) tex;
    (void) which;
    return 0;
}
int64_t gg_framebuffer(int64_t win) {
    (void) win;
    return 0;
}
void gg_framebuffer_attach(int64_t fb, int64_t tex) {
    (void) fb;
    (void) tex;
}
void gg_render_to(int64_t win, int64_t fb) {
    (void) win;
    (void) fb;
}
int64_t gg_quad(int64_t win) {
    (void) win;
    return 0;
}

void gg_mesh_update(int64_t mesh, const float *data, int64_t n) {
    (void) mesh;
    (void) data;
    (void) n;
}
void gg_mesh_indices(int64_t mesh, const int64_t *idx, int64_t n) {
    (void) mesh;
    (void) idx;
    (void) n;
}
void gg_draw_as(int64_t mesh, int64_t shader, int64_t mode, int64_t indexed, int64_t instances) {
    (void) mesh;
    (void) shader;
    (void) mode;
    (void) indexed;
    (void) instances;
}
int64_t gg_instances(int64_t win, const float *data, int64_t n) {
    (void) win;
    (void) data;
    (void) n;
    return 0;
}
void gg_buffer_update(int64_t buf, const float *data, int64_t n) {
    (void) buf;
    (void) data;
    (void) n;
}
void gg_instance_attrib(int64_t mesh, int64_t buf, int64_t location, int64_t components,
                        int64_t stride, int64_t offset) {
    (void) mesh;
    (void) buf;
    (void) location;
    (void) components;
    (void) stride;
    (void) offset;
}

int64_t gg_text_size(int64_t h) {
    (void) h;
    return 0;
}
void gg_text_copy(int64_t h, uint8_t *out, int64_t n) {
    (void) h;
    (void) out;
    (void) n;
}
int64_t gg_key_down(int64_t h, int64_t key) {
    (void) h;
    (void) key;
    return 0;
}
int64_t gg_mouse(int64_t h, int64_t which) {
    (void) h;
    (void) which;
    return 0;
}

#endif
