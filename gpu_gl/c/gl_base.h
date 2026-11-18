/* DERIVED from graphics/c/graphics_gl.c by gpu_gl/c/derive_base.sh: do not edit.
 * The window hosts, the GL function table and the texture and framebuffer code of the
 * graphics library, under the prefix gq_ so both link into one program.  graphics keeps
 * its own copy until its Window is a gpu Device; then this is the only one. */
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
 * Android (`loft --native-c --native-android`): the activity's window, through EGL and GLES 3.0
 * (`libEGL.so`, `libGLESv3.so`, opened at run time), reached through the activity loft's Android
 * entry hands over (`loft_android_app()`).  GLES 3.0 is WebGL2, so the shaders take the browser's
 * rewrite of their `#version 330 core` line.  The window is the screen; the program runs on the
 * thread that owns the activity's looper, and `Window.poll` drains it: each pointer is a Touch
 * event with its id, and the first is also the left mouse button; a key is its 0.9 code; the
 * window's surface follows the activity (gone while it is in the background, made again when it
 * returns).
 *
 * Other hosts (Windows, a WASI module) open no window yet: `gq_open` answers 0, which the loft
 * side reports as a null `Window`. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int64_t gq_open(int64_t width, int64_t height, const char *title, int64_t fullscreen);
int64_t gq_poll(int64_t h);
void gq_swap(int64_t h);
void gq_clear(int64_t h, int64_t color);
void gq_close(int64_t h);
int64_t gq_width(int64_t h);
int64_t gq_height(int64_t h);
int64_t gq_event_count(int64_t h);
void gq_events(int64_t h, int64_t *out, int64_t n);
int64_t gq_text_size(int64_t h);
void gq_text_copy(int64_t h, uint8_t *out, int64_t n);
int64_t gq_key_down(int64_t h, int64_t key);
int64_t gq_mouse(int64_t h, int64_t which);
void gq_read_pixels(int64_t h, uint32_t *out, int64_t n);
int64_t gq_shader(int64_t win, const char *vertex, const char *fragment);
void gq_uniform_f(int64_t sh, const char *name, const double *v, int64_t n);
void gq_uniform_i(int64_t sh, const char *name, int64_t v);
int64_t gq_mesh(int64_t win, const float *data, int64_t n, int64_t stride);
void gq_draw(int64_t mesh, int64_t shader);
void gq_release(int64_t res);
void gq_capability(int64_t win, int64_t cap, int64_t on);
int64_t gq_capability_on(int64_t win, int64_t cap);
void gq_draw_over(int64_t mesh, int64_t shader);
void gq_blend(int64_t win, int64_t src, int64_t dst);
void gq_cull(int64_t win, int64_t face);
void gq_depth_write(int64_t win, int64_t on);
void gq_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height);
void gq_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height);
void gq_finish(int64_t win);
void gq_line_width(int64_t win, const double *width, int64_t n);
void gq_point_size(int64_t win, const double *size, int64_t n);
int64_t gq_texture(int64_t win, int64_t width, int64_t height, int64_t depth);
int64_t gq_texture_from(int64_t win, const uint32_t *px, int64_t n, int64_t width, int64_t height);
void gq_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                    const uint32_t *px, int64_t n);
void gq_texture_filter(int64_t tex, int64_t nearest);
void gq_texture_mipmaps(int64_t tex);
void gq_texture_bind(int64_t tex, int64_t unit);
int64_t gq_texture_size(int64_t tex, int64_t which);
int64_t gq_framebuffer(int64_t win);
void gq_framebuffer_attach(int64_t fb, int64_t tex);
void gq_render_to(int64_t win, int64_t fb);
int64_t gq_quad(int64_t win);
void gq_mesh_update(int64_t mesh, const float *data, int64_t n);
void gq_mesh_indices(int64_t mesh, const int64_t *idx, int64_t n);
void gq_draw_as(int64_t mesh, int64_t shader, int64_t mode, int64_t indexed, int64_t instances);
int64_t gq_instances(int64_t win, const float *data, int64_t n);
void gq_buffer_update(int64_t buf, const float *data, int64_t n);
void gq_instance_attrib(int64_t mesh, int64_t buf, int64_t location, int64_t components,
                        int64_t stride, int64_t offset);

/* One queued input event: an EV_* tag (src/graphics.loft) and up to four integer fields. */
#define GQ_EVENT_WORDS 5
#define GQ_EV_KEY_DOWN 1
#define GQ_EV_KEY_UP 2
#define GQ_EV_TEXT 3
#define GQ_EV_MOUSE_DOWN 4
#define GQ_EV_MOUSE_UP 5
#define GQ_EV_MOUSE_MOVE 6
#define GQ_EV_WHEEL 7
#define GQ_EV_TOUCH_BEGIN 8
#define GQ_EV_TOUCH_MOVE 9
#define GQ_EV_TOUCH_END 10
#define GQ_EV_RESIZE 11
#define GQ_MOD_SHIFT 1
#define GQ_MOD_CTRL 2
#define GQ_MOD_ALT 4
#define GQ_MOD_SUPER 8

#if defined(__ANDROID__)
#define GQ_ANDROID 1 /* the activity's window, through EGL and GLES 3, opened at run time */
#elif defined(__linux__) && !defined(__wasm__)
#define GQ_X11 1 /* a window through X11 and GLX, opened at run time */
#elif defined(__APPLE__) && !defined(__wasm__)
#define GQ_COCOA 1 /* a window through AppKit and NSOpenGL, by the Objective-C runtime */
#elif defined(__wasm__) && defined(LOFT_BROWSER)
#define GQ_BROWSER 1 /* the page's canvas, through its `loft_gl` imports (WebGL2) */
#endif

#if defined(GQ_X11) || defined(GQ_COCOA) || defined(GQ_BROWSER) || defined(GQ_ANDROID)

#if defined(GQ_X11)
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/select.h>

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

#endif /* GQ_X11 */

#if defined(GQ_COCOA)
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

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
#endif /* GQ_COCOA */

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

#if defined(GQ_X11)
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
} gq_x_api;
#elif defined(GQ_ANDROID)
#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <EGL/egl.h>

/* The EGL entry points a window calls, looked up in `libEGL.so` when it opens: the app links no
 * graphics library, as the X11 build links none. */
typedef struct {
    EGLDisplay (*eglGetDisplay)(EGLNativeDisplayType);
    EGLBoolean (*eglInitialize)(EGLDisplay, EGLint *, EGLint *);
    EGLBoolean (*eglBindAPI)(EGLenum);
    EGLBoolean (*eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
    EGLContext (*eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
    EGLSurface (*eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *);
    EGLBoolean (*eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    EGLBoolean (*eglSwapBuffers)(EGLDisplay, EGLSurface);
    EGLBoolean (*eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint *);
    EGLBoolean (*eglDestroySurface)(EGLDisplay, EGLSurface);
    EGLBoolean (*eglDestroyContext)(EGLDisplay, EGLContext);
} gq_egl_api;

/* The activity loft's Android entry runs the program for (src/c/android/loft_android_main.c). */
struct android_app *loft_android_app(void);
#endif

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
} gq_api;

/* Every entry of `gq_api`, for a platform that looks each one up by name: `P(name)` answers
 * whether `name` was found. */
#define GQ_GL_ALL(P)                                                                              \
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
    gq_api api;
#if defined(GQ_X11)
    gq_x_api xa;
    void *libx11, *libgl;
    void *display;
    gx_xid window, colormap, wm_delete;
    gx_context context;
    pthread_t thread;
    void *im, *ic; /* the input method and context, when the server has one */
#elif defined(GQ_COCOA)
    gc_id window, view, context, format;
    int fullscreen;
    pthread_t thread;
    void *next_open; /* the next open window: AppKit has one event queue for all of them */
#elif defined(GQ_ANDROID)
    gq_egl_api ea;
    void *libegl, *libgles;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface; /* EGL_NO_SURFACE while the activity has no window */
    struct android_app *app;
#endif
    int width, height, should_close;
    int closed; /* `gq_close` ran: the context and the X window are gone */
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
} gq_window;

/* A shader or a mesh: its GL names and the window whose context made them. */
enum { GQ_SHADER = 1, GQ_MESH = 2, GQ_TEXTURE = 3, GQ_FRAMEBUFFER = 4, GQ_BUFFER = 5 };
typedef struct {
    gq_window *w;
    int kind;
    unsigned id, buffer; /* program; vertex array and its buffer; texture; framebuffer; buffer */
    unsigned indices_id; /* a mesh's index buffer, 0 for none */
    int vertices, indices, stride;
    int width, height, depth; /* a texture */
    int has_colour; /* a framebuffer: a colour attachment, else it draws to no colour buffer */
} gq_res;

static gq_window *gq_of(int64_t h) {
    return (gq_window *) (intptr_t) h;
}

static gq_res *gq_res_of(int64_t h) {
    return (gq_res *) (intptr_t) h;
}

#if defined(GQ_X11)
/* The window whose context is current on this thread, so a call on another window's resource
 * rebinds only when the window changes.  Speed only: every call names its own window. */
static __thread gq_window *gq_current;

/* A GL call from another thread than the window's finds no current context there: it would draw
 * nothing, or into another window.  Refused, and said so. */
static int gq_on_thread(gq_window *w, const char *what) {
    if (pthread_equal(w->thread, pthread_self())) return 1;
    fprintf(stderr, "graphics: %s called from a thread other than the one that opened the "
                    "window; a window is used on its own thread only\n", what);
    return 0;
}

/* Ready `w` for a GL call named `what`: open, on its thread, its context current. */
static int gq_use(gq_window *w, const char *what) {
    if (!w || w->closed || !gq_on_thread(w, what)) return 0;
    if (gq_current != w) {
        w->xa.glXMakeCurrent(w->display, w->window, w->context);
        gq_current = w;
    }
    return 1;
}

#define GQ_SYM(lib, name)                                                                         \
    (*(void **) (&w->xa.name) = dlsym(w->lib, #name), w->xa.name != NULL)

/* The libraries stay loaded once a window opened them (`RTLD_NODELETE`): Mesa does not unload
 * cleanly — each unload and reload left 10 MiB of address space behind and cost a second of LLVM
 * start-up, so the 200th window of a program that opens them in turn failed to open. */
static int gq_bind(gq_window *w) {
    w->libx11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    w->libgl = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!w->libx11 || !w->libgl) return 0;
    if (!(GQ_SYM(libx11, XOpenDisplay) && GQ_SYM(libx11, XCloseDisplay) &&
          GQ_SYM(libx11, XDefaultScreen) && GQ_SYM(libx11, XRootWindow) &&
          GQ_SYM(libx11, XDisplayWidth) && GQ_SYM(libx11, XDisplayHeight) &&
          GQ_SYM(libx11, XCreateColormap) && GQ_SYM(libx11, XFreeColormap) &&
          GQ_SYM(libx11, XCreateWindow) && GQ_SYM(libx11, XDestroyWindow) &&
          GQ_SYM(libx11, XStoreName) && GQ_SYM(libx11, XInternAtom) &&
          GQ_SYM(libx11, XSetWMProtocols) && GQ_SYM(libx11, XChangeProperty) &&
          GQ_SYM(libx11, XMapWindow) && GQ_SYM(libx11, XPending) &&
          GQ_SYM(libx11, XNextEvent) && GQ_SYM(libx11, XSync) && GQ_SYM(libx11, XFree) &&
          GQ_SYM(libx11, XSetErrorHandler) && GQ_SYM(libx11, XLookupString) &&
          GQ_SYM(libx11, XkbSetDetectableAutoRepeat) && GQ_SYM(libx11, XOpenIM) &&
          GQ_SYM(libx11, XCloseIM) && GQ_SYM(libx11, XCreateIC) && GQ_SYM(libx11, XDestroyIC) &&
          GQ_SYM(libx11, Xutf8LookupString) && GQ_SYM(libx11, XFilterEvent)))
        return 0;
    if (!(GQ_SYM(libgl, glXChooseFBConfig) && GQ_SYM(libgl, glXGetFBConfigAttrib) &&
          GQ_SYM(libgl, glXGetVisualFromFBConfig) && GQ_SYM(libgl, glXGetProcAddressARB) &&
          GQ_SYM(libgl, glXMakeCurrent) && GQ_SYM(libgl, glXSwapBuffers) &&
          GQ_SYM(libgl, glXDestroyContext)))
        return 0;
    return 1;
}

/* GL entry points through glXGetProcAddressARB, which answers for core and extension functions
 * alike once a context exists. */
#define GQ_PROC(name)                                                                             \
    (*(void **) (&w->api.name) = w->xa.glXGetProcAddressARB((const unsigned char *) #name),      \
     w->api.name != NULL)

static int gq_bind_gl(gq_window *w) {
    *(void **) (&w->xa.glXSwapIntervalEXT) = /* optional */
        w->xa.glXGetProcAddressARB((const unsigned char *) "glXSwapIntervalEXT");
    return GQ_GL_ALL(GQ_PROC);
}

/* An X protocol error while the context is made (a config the server refuses, a context the
 * thread cannot bind) ends the process under Xlib's default handler.  This one records it instead,
 * for `gq_open` to answer 0.  Xlib calls it on the thread that made the failing request, so the
 * flag is that thread's; the handler itself is the process's, installed only while `gq_open`
 * makes the context. */
static __thread int gq_x_failed;
static int gq_on_x_error(void *display, gx_error_event *e) {
    (void) display;
    (void) e;
    gq_x_failed = 1;
    return 0;
}

/* Open the display.  A server that still counts the connections just closed resets a new one:
 * under Xvfb one open in a thousand, more when several programs open at once.  It is asked
 * again for up to half a second.  A display that is not there refuses, and is not asked twice. */
static void *gq_open_display(gq_window *w) {
    struct timeval wait;
    int tries;
    for (tries = 0; tries < 50; tries++) {
        void *display;
        errno = 0;
        display = w->xa.XOpenDisplay(NULL);
        if (display || (errno != ECONNRESET && errno != EAGAIN)) return display;
        wait.tv_sec = 0;
        wait.tv_usec = 10000;
        select(0, NULL, NULL, NULL, &wait);
    }
    return NULL;
}

/* The frame-buffer config 0.9 chose through glutin: double-buffered true-colour RGB, an opaque
 * one preferred (an alpha channel makes a compositor treat the window as see-through), then the
 * most samples. */
static gx_fbconfig gq_pick_config(gq_window *w, int screen) {
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

/* Close the window: the input context and method (they belong to the connection, so they go
 * before it), the context (and every GL object in it), the X window, the connection. */
static void gq_teardown(gq_window *w) {
    if (w->ic) w->xa.XDestroyIC(w->ic);
    if (w->im) w->xa.XCloseIM(w->im);
    w->ic = w->im = NULL;
    if (w->display) {
        if (w->context) {
            w->xa.glXMakeCurrent(w->display, 0, NULL);
            w->xa.glXDestroyContext(w->display, w->context);
        }
        if (w->window) w->xa.XDestroyWindow(w->display, w->window);
        if (w->colormap) w->xa.XFreeColormap(w->display, w->colormap);
        w->xa.XCloseDisplay(w->display);
    }
    if (gq_current == w) gq_current = NULL;
    if (w->libgl) dlclose(w->libgl);
    if (w->libx11) dlclose(w->libx11);
    w->closed = 1;
}

#elif defined(GQ_COCOA)

/* The window whose context is current on this thread (speed only, as on X11). */
static __thread gq_window *gq_current;

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
    void (*dispatch_sync_f)(void *, void *, void (*)(void *));
    void *main_queue; /* dispatch_get_main_queue(): `_dispatch_main_q`'s address */
    void *(*run_loop_main)(void);
    unsigned char (*run_loop_waiting)(void *);
    int main_served; /* 1 once the main thread was seen in its run loop */
    gc_id app, run_loop_mode, full_class;
    gq_window *open; /* the open windows, each event routed to its own */
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

/* Why the last window could not open: said once per process, since a Mac with a window server
 * that opens none is something to act on (a program with no display answers null quietly). */
static const char *gc_why;
static void gc_say_why(void) {
    static int said;
    if (said || !gc_why) return;
    said = 1;
    fprintf(stderr, "graphics: no window — %s\n", gc_why);
}

static int gc_load(void) {
    void **mode;
    if (gc.loaded) return gc.loaded > 0;
    gc.loaded = -1;
    gc_why = "the Objective-C runtime, AppKit or OpenGL.framework did not load";
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
    *(void **) (&gc.dispatch_sync_f) = dlsym(RTLD_DEFAULT, "dispatch_sync_f");
    gc.main_queue = dlsym(RTLD_DEFAULT, "_dispatch_main_q");
    *(void **) (&gc.run_loop_main) = dlsym(RTLD_DEFAULT, "CFRunLoopGetMain");
    *(void **) (&gc.run_loop_waiting) = dlsym(RTLD_DEFAULT, "CFRunLoopIsWaiting");
    mode = (void **) dlsym(RTLD_DEFAULT, "NSDefaultRunLoopMode");
    if (!gc.dispatch_sync_f || !gc.main_queue || !gc.run_loop_main || !gc.run_loop_waiting ||
        !mode || !*mode)
        return 0;
    gc.run_loop_mode = *mode;
    gc.loaded = 1;
    return 1;
}

/* Whether the main thread runs its run loop, which drains the main queue there: seen waiting in
 * it once within two seconds (a program's first call can come before `main` reaches the loop),
 * then remembered.  A main thread that never waits there — blocked joining the program, as a loft
 * from before 2026-10-31 leaves it — would never run a job sent to it. */
static int gc_main_served(void) {
    int i;
    for (i = 0; !gc.main_served && i < 200; i++) {
        if (gc.run_loop_waiting(gc.run_loop_main())) gc.main_served = 1;
        else usleep(10000);
    }
    return gc.main_served;
}

/* Run `job` on the main thread, where AppKit makes and drives windows, and wait for it: here when
 * this is the main thread (the interpreter, the C backend), else through the main queue, which
 * loft's native `main` drains on macOS (`run_beside_main_queue`).  0 when no thread would run it
 * — said, never waited on forever. */
static int gc_on_main(void (*job)(void *), void *arg) {
    if (pthread_main_np()) {
        job(arg);
        return 1;
    }
    if (!gc_main_served()) {
        static int said;
        if (!said)
            fprintf(stderr, "graphics: a window off the main thread needs the main thread in its run "
                            "loop, which this program's main does not run (a --native program needs "
                            "a loft from 2026-10-31 on)\n");
        said = 1;
        return 0;
    }
    gc.dispatch_sync_f(gc.main_queue, arg, (void (*)(void *)) job);
    return 1;
}

/* The application, once, on the main thread: a regular app (a Dock icon, key windows), launched,
 * and the fullscreen window's class. */
static int gc_app(void) {
    if (gc.app) return 1;
    gc_why = "no NSApplication";
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
    return 1;
}

static int gq_on_thread(gq_window *w, const char *what) {
    if (pthread_equal(w->thread, pthread_self())) return 1;
    fprintf(stderr, "graphics: %s called from a thread other than the one that opened the "
                    "window; a window is used on its own thread only\n", what);
    return 0;
}

static int gq_use(gq_window *w, const char *what) {
    if (!w || w->closed || !gq_on_thread(w, what)) return 0;
    if (gq_current != w) {
        gc_v(w->context, "makeCurrentContext");
        gq_current = w;
    }
    return 1;
}

/* OpenGL.framework exports every core entry point by name. */
#define GQ_PROC(name)                                                                             \
    (*(void **) (&w->api.name) = dlsym(gc.gl, #name),                                             \
     w->api.name != NULL || (gc_why = "OpenGL.framework has no " #name, 0))

static int gq_bind_gl(gq_window *w) {
    return GQ_GL_ALL(GQ_PROC);
}

/* The main thread's half of closing: out of the routing list, the context's drawable, the
 * NSWindow. */
static void gc_teardown_main(void *arg) {
    gq_window *w = (gq_window *) arg, **link;
    void *pool = gc.pool_push();
    for (link = &gc.open; *link; link = (gq_window **) &(*link)->next_open)
        if (*link == w) {
            *link = (gq_window *) w->next_open;
            break;
        }
    if (w->context) {
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
}

/* Close the window: the context (and every GL object in it) and the NSWindow. */
static void gq_teardown(gq_window *w) {
    w->closed = 1;
    if (gc.loaded <= 0) return;
    if (gq_current == w) { /* the context is current on this thread, not the main one */
        gc_v(gc_class("NSOpenGLContext"), "clearCurrentContext");
        gq_current = NULL;
    }
    gc_on_main(gc_teardown_main, w); /* unserved: the window stays on screen until the exit */
}

#elif defined(GQ_ANDROID)

/* The activity shows one window: the one `gq_open` made, which its callbacks reach. */
static gq_window *gq_android_window;

/* A window draws while it has a surface: none while the activity is in the background. */
static int gq_use(gq_window *w, const char *what) {
    (void) what;
    return w && !w->closed && w->surface != EGL_NO_SURFACE;
}

/* GLES 3.0 has no `glDrawBuffer` (only `glDrawBuffers`) and no `glPointSize` (a vertex shader
 * writes `gl_PointSize`): the two entries the window calls answer through these. */
static void (*gq_gles_draw_buffers)(int, const unsigned *);
static void gq_gles_draw_buffer(unsigned b) {
    gq_gles_draw_buffers(1, &b);
}
static void gq_gles_point_size(float size) {
    (void) size;
}

static int gq_gles_sym(gq_window *w, void **slot, const char *name) {
    if (strcmp(name, "glDrawBuffer") == 0) {
        *slot = (void *) gq_gles_draw_buffer;
        return gq_gles_draw_buffers != NULL;
    }
    if (strcmp(name, "glPointSize") == 0) {
        *slot = (void *) gq_gles_point_size;
        return 1;
    }
    *slot = dlsym(w->libgles, name);
    return *slot != NULL;
}

#define GQ_GLES(name) gq_gles_sym(w, (void **) (&w->api.name), #name)

static int gq_bind_gl(gq_window *w) {
    *(void **) (&gq_gles_draw_buffers) = dlsym(w->libgles, "glDrawBuffers");
    return GQ_GL_ALL(GQ_GLES);
}

#define GQ_EGL(name) (*(void **) (&w->ea.name) = dlsym(w->libegl, #name), w->ea.name != NULL)

/* The libraries stay loaded once a window opened them, as the X11 build keeps its own. */
static int gq_bind_egl(gq_window *w) {
    w->libegl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    w->libgles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!w->libegl || !w->libgles) return 0;
    return GQ_EGL(eglGetDisplay) && GQ_EGL(eglInitialize) && GQ_EGL(eglBindAPI) &&
           GQ_EGL(eglChooseConfig) && GQ_EGL(eglCreateContext) && GQ_EGL(eglCreateWindowSurface) &&
           GQ_EGL(eglMakeCurrent) && GQ_EGL(eglSwapBuffers) && GQ_EGL(eglQuerySurface) &&
           GQ_EGL(eglDestroySurface) && GQ_EGL(eglDestroyContext);
}

/* Let go of the surface (the activity's window is going): the context stays, without one. */
static void gq_android_drop_surface(gq_window *w) {
    if (w->surface == EGL_NO_SURFACE) return;
    w->ea.eglMakeCurrent(w->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    w->ea.eglDestroySurface(w->display, w->surface);
    w->surface = EGL_NO_SURFACE;
}

static void gq_teardown(gq_window *w) {
    gq_android_drop_surface(w);
    if (w->context != EGL_NO_CONTEXT) w->ea.eglDestroyContext(w->display, w->context);
    w->context = EGL_NO_CONTEXT;
    if (w->app) {
        w->app->onAppCmd = NULL;
        w->app->onInputEvent = NULL;
    }
    if (gq_android_window == w) gq_android_window = NULL;
    w->closed = 1;
}

#else /* GQ_BROWSER: the page's canvas and WebGL2 context, through its `loft_gl` imports */

#define GQ_IMPORT(name) __attribute__((import_module("loft_gl"), import_name(#name)))
GQ_IMPORT(gg_js_open) int gq_js_open(int width, int height, int fullscreen);
GQ_IMPORT(gg_js_size) int gq_js_size(int which);
GQ_IMPORT(gg_present) void gq_js_present(void);
GQ_IMPORT(gg_js_event) int gq_js_event(int *out, char *text, int cap);
/* One import per GL entry point, of the GL signature: doc/loft-gl-wasm.js maps each onto WebGL2. */
GQ_IMPORT(gg_glClearColor) void gq_glClearColor(float, float, float, float);
GQ_IMPORT(gg_glClear) void gq_glClear(unsigned);
GQ_IMPORT(gg_glColorMask) void gq_glColorMask(unsigned char, unsigned char, unsigned char, unsigned char);
GQ_IMPORT(gg_glViewport) void gq_glViewport(int, int, int, int);
GQ_IMPORT(gg_glEnable) void gq_glEnable(unsigned);
GQ_IMPORT(gg_glReadBuffer) void gq_glReadBuffer(unsigned);
GQ_IMPORT(gg_glPixelStorei) void gq_glPixelStorei(unsigned, int);
GQ_IMPORT(gg_glReadPixels) void gq_glReadPixels(int, int, int, int, unsigned, unsigned, void *);
GQ_IMPORT(gg_glCreateShader) unsigned gq_glCreateShader(unsigned);
GQ_IMPORT(gg_glShaderSource) void gq_glShaderSource(unsigned, int, const char *const *, const int *);
GQ_IMPORT(gg_glCompileShader) void gq_glCompileShader(unsigned);
GQ_IMPORT(gg_glGetShaderiv) void gq_glGetShaderiv(unsigned, unsigned, int *);
GQ_IMPORT(gg_glGetShaderInfoLog) void gq_glGetShaderInfoLog(unsigned, int, int *, char *);
GQ_IMPORT(gg_glDeleteShader) void gq_glDeleteShader(unsigned);
GQ_IMPORT(gg_glCreateProgram) unsigned gq_glCreateProgram(void);
GQ_IMPORT(gg_glAttachShader) void gq_glAttachShader(unsigned, unsigned);
GQ_IMPORT(gg_glLinkProgram) void gq_glLinkProgram(unsigned);
GQ_IMPORT(gg_glGetProgramiv) void gq_glGetProgramiv(unsigned, unsigned, int *);
GQ_IMPORT(gg_glGetProgramInfoLog) void gq_glGetProgramInfoLog(unsigned, int, int *, char *);
GQ_IMPORT(gg_glDeleteProgram) void gq_glDeleteProgram(unsigned);
GQ_IMPORT(gg_glUseProgram) void gq_glUseProgram(unsigned);
GQ_IMPORT(gg_glGetUniformLocation) int gq_glGetUniformLocation(unsigned, const char *);
GQ_IMPORT(gg_glUniform1f) void gq_glUniform1f(int, float);
GQ_IMPORT(gg_glUniform2f) void gq_glUniform2f(int, float, float);
GQ_IMPORT(gg_glUniform3f) void gq_glUniform3f(int, float, float, float);
GQ_IMPORT(gg_glUniform4f) void gq_glUniform4f(int, float, float, float, float);
GQ_IMPORT(gg_glUniform1i) void gq_glUniform1i(int, int);
GQ_IMPORT(gg_glUniformMatrix4fv) void gq_glUniformMatrix4fv(int, int, unsigned char, const float *);
GQ_IMPORT(gg_glGenVertexArrays) void gq_glGenVertexArrays(int, unsigned *);
GQ_IMPORT(gg_glBindVertexArray) void gq_glBindVertexArray(unsigned);
GQ_IMPORT(gg_glDeleteVertexArrays) void gq_glDeleteVertexArrays(int, const unsigned *);
GQ_IMPORT(gg_glGenBuffers) void gq_glGenBuffers(int, unsigned *);
GQ_IMPORT(gg_glBindBuffer) void gq_glBindBuffer(unsigned, unsigned);
GQ_IMPORT(gg_glBufferData) void gq_glBufferData(unsigned, intptr_t, const void *, unsigned);
GQ_IMPORT(gg_glDeleteBuffers) void gq_glDeleteBuffers(int, const unsigned *);
GQ_IMPORT(gg_glVertexAttribPointer) void gq_glVertexAttribPointer(unsigned, int, unsigned, unsigned char, int, const void *);
GQ_IMPORT(gg_glEnableVertexAttribArray) void gq_glEnableVertexAttribArray(unsigned);
GQ_IMPORT(gg_glDrawArrays) void gq_glDrawArrays(unsigned, int, int);
GQ_IMPORT(gg_glDisable) void gq_glDisable(unsigned);
GQ_IMPORT(gg_glBlendFunc) void gq_glBlendFunc(unsigned, unsigned);
GQ_IMPORT(gg_glCullFace) void gq_glCullFace(unsigned);
GQ_IMPORT(gg_glDepthMask) void gq_glDepthMask(unsigned char);
GQ_IMPORT(gg_glScissor) void gq_glScissor(int, int, int, int);
GQ_IMPORT(gg_glFinish) void gq_glFinish(void);
GQ_IMPORT(gg_glLineWidth) void gq_glLineWidth(float);
GQ_IMPORT(gg_glPointSize) void gq_glPointSize(float);
GQ_IMPORT(gg_glGenTextures) void gq_glGenTextures(int, unsigned *);
GQ_IMPORT(gg_glBindTexture) void gq_glBindTexture(unsigned, unsigned);
GQ_IMPORT(gg_glTexImage2D) void gq_glTexImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
GQ_IMPORT(gg_glTexSubImage2D) void gq_glTexSubImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
GQ_IMPORT(gg_glTexParameteri) void gq_glTexParameteri(unsigned, unsigned, int);
GQ_IMPORT(gg_glDeleteTextures) void gq_glDeleteTextures(int, const unsigned *);
GQ_IMPORT(gg_glGenerateMipmap) void gq_glGenerateMipmap(unsigned);
GQ_IMPORT(gg_glActiveTexture) void gq_glActiveTexture(unsigned);
GQ_IMPORT(gg_glGenFramebuffers) void gq_glGenFramebuffers(int, unsigned *);
GQ_IMPORT(gg_glBindFramebuffer) void gq_glBindFramebuffer(unsigned, unsigned);
GQ_IMPORT(gg_glFramebufferTexture2D) void gq_glFramebufferTexture2D(unsigned, unsigned, unsigned, unsigned, int);
GQ_IMPORT(gg_glDeleteFramebuffers) void gq_glDeleteFramebuffers(int, const unsigned *);
GQ_IMPORT(gg_glDrawBuffer) void gq_glDrawBuffer(unsigned);
GQ_IMPORT(gg_glDrawElements) void gq_glDrawElements(unsigned, int, unsigned, const void *);
GQ_IMPORT(gg_glDrawArraysInstanced) void gq_glDrawArraysInstanced(unsigned, int, int, int);
GQ_IMPORT(gg_glDrawElementsInstanced) void gq_glDrawElementsInstanced(unsigned, int, unsigned, const void *, int);
GQ_IMPORT(gg_glVertexAttribDivisor) void gq_glVertexAttribDivisor(unsigned, unsigned);

/* A page is one thread, and its context is current from the first window on. */
static int gq_use(gq_window *w, const char *what) {
    (void) what;
    return w && !w->closed;
}

static int gq_bind_gl(gq_window *w) {
    w->api.glClearColor = gq_glClearColor;
    w->api.glClear = gq_glClear;
    w->api.glColorMask = gq_glColorMask;
    w->api.glViewport = gq_glViewport;
    w->api.glEnable = gq_glEnable;
    w->api.glReadBuffer = gq_glReadBuffer;
    w->api.glPixelStorei = gq_glPixelStorei;
    w->api.glReadPixels = gq_glReadPixels;
    w->api.glCreateShader = gq_glCreateShader;
    w->api.glShaderSource = gq_glShaderSource;
    w->api.glCompileShader = gq_glCompileShader;
    w->api.glGetShaderiv = gq_glGetShaderiv;
    w->api.glGetShaderInfoLog = gq_glGetShaderInfoLog;
    w->api.glDeleteShader = gq_glDeleteShader;
    w->api.glCreateProgram = gq_glCreateProgram;
    w->api.glAttachShader = gq_glAttachShader;
    w->api.glLinkProgram = gq_glLinkProgram;
    w->api.glGetProgramiv = gq_glGetProgramiv;
    w->api.glGetProgramInfoLog = gq_glGetProgramInfoLog;
    w->api.glDeleteProgram = gq_glDeleteProgram;
    w->api.glUseProgram = gq_glUseProgram;
    w->api.glGetUniformLocation = gq_glGetUniformLocation;
    w->api.glUniform1f = gq_glUniform1f;
    w->api.glUniform2f = gq_glUniform2f;
    w->api.glUniform3f = gq_glUniform3f;
    w->api.glUniform4f = gq_glUniform4f;
    w->api.glUniform1i = gq_glUniform1i;
    w->api.glUniformMatrix4fv = gq_glUniformMatrix4fv;
    w->api.glGenVertexArrays = gq_glGenVertexArrays;
    w->api.glBindVertexArray = gq_glBindVertexArray;
    w->api.glDeleteVertexArrays = gq_glDeleteVertexArrays;
    w->api.glGenBuffers = gq_glGenBuffers;
    w->api.glBindBuffer = gq_glBindBuffer;
    w->api.glBufferData = gq_glBufferData;
    w->api.glDeleteBuffers = gq_glDeleteBuffers;
    w->api.glVertexAttribPointer = gq_glVertexAttribPointer;
    w->api.glEnableVertexAttribArray = gq_glEnableVertexAttribArray;
    w->api.glDrawArrays = gq_glDrawArrays;
    w->api.glDisable = gq_glDisable;
    w->api.glBlendFunc = gq_glBlendFunc;
    w->api.glCullFace = gq_glCullFace;
    w->api.glDepthMask = gq_glDepthMask;
    w->api.glScissor = gq_glScissor;
    w->api.glFinish = gq_glFinish;
    w->api.glLineWidth = gq_glLineWidth;
    w->api.glPointSize = gq_glPointSize;
    w->api.glGenTextures = gq_glGenTextures;
    w->api.glBindTexture = gq_glBindTexture;
    w->api.glTexImage2D = gq_glTexImage2D;
    w->api.glTexSubImage2D = gq_glTexSubImage2D;
    w->api.glTexParameteri = gq_glTexParameteri;
    w->api.glDeleteTextures = gq_glDeleteTextures;
    w->api.glGenerateMipmap = gq_glGenerateMipmap;
    w->api.glActiveTexture = gq_glActiveTexture;
    w->api.glGenFramebuffers = gq_glGenFramebuffers;
    w->api.glBindFramebuffer = gq_glBindFramebuffer;
    w->api.glFramebufferTexture2D = gq_glFramebufferTexture2D;
    w->api.glDeleteFramebuffers = gq_glDeleteFramebuffers;
    w->api.glDrawBuffer = gq_glDrawBuffer;
    w->api.glDrawElements = gq_glDrawElements;
    w->api.glDrawArraysInstanced = gq_glDrawArraysInstanced;
    w->api.glDrawElementsInstanced = gq_glDrawElementsInstanced;
    w->api.glVertexAttribDivisor = gq_glVertexAttribDivisor;
    return 1;
}

/* The page's context outlives a window: only the record is closed. */
static void gq_teardown(gq_window *w) {
    w->closed = 1;
}
#endif

/* The record goes once the window is closed and no shader or mesh holds it. */
static void gq_forget(gq_window *w) {
    if (!w->closed || w->refs > 0) return;
    free(w->events);
    free(w->text);
    free(w);
}

static void gq_free(gq_window *w) {
    gq_teardown(w);
    gq_forget(w);
}

static void gq_push(gq_window *w, int64_t tag, int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t *slot;
    if (w->event_count == w->event_cap) {
        int64_t cap = w->event_cap ? w->event_cap * 2 : 16;
        int64_t *grown = (int64_t *) realloc(w->events, (size_t) cap * GQ_EVENT_WORDS * 8u);
        if (!grown) return;
        w->events = grown;
        w->event_cap = cap;
    }
    slot = w->events + w->event_count * GQ_EVENT_WORDS;
    slot[0] = tag;
    slot[1] = a;
    slot[2] = b;
    slot[3] = c;
    slot[4] = d;
    w->event_count++;
}

static void gq_text_push(gq_window *w, const char *bytes, int n) {
    if (w->text_len + n > w->text_cap) {
        int64_t cap = (w->text_cap ? w->text_cap * 2 : 64) + n;
        char *grown = (char *) realloc(w->text, (size_t) cap);
        if (!grown) return;
        w->text = grown;
        w->text_cap = cap;
    }
    gq_push(w, GQ_EV_TEXT, w->text_len, n, 0, 0);
    memcpy(w->text + w->text_len, bytes, (size_t) n);
    w->text_len += n;
}

#if defined(GQ_X11)
/* 0.9's key codes (src/graphics.loft KEY_*): ASCII lower-cased for a character, its own codes for
 * the named keys, -1 for a key it has none for. */
static int64_t gq_key_code(gx_xid sym) {
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
static int64_t gq_mods(unsigned state) {
    return ((state & GX_SHIFT_MASK) ? GQ_MOD_SHIFT : 0) | ((state & GX_CONTROL_MASK) ? GQ_MOD_CTRL : 0) |
           ((state & GX_MOD1_MASK) ? GQ_MOD_ALT : 0) | ((state & GX_MOD4_MASK) ? GQ_MOD_SUPER : 0);
}


/* A key press: KeyDown (a repeat when the key is already down) and, when it types anything, a
 * Text with what it typed (Enter types "\r", Backspace "\b", as 0.9 reported them); a release:
 * KeyUp.  The modifiers are the state before the event, as X and 0.9 report them: pressing Shift
 * reports no Shift, releasing it reports Shift. */
static void gq_key(gq_window *w, gx_input_event *e, int press) {
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
    code = gq_key_code(sym);
    if (press) {
        int repeat = code >= 0 && code < 256 && w->keys[code];
        if (code >= 0 && code < 256) w->keys[code] = 1;
        gq_push(w, GQ_EV_KEY_DOWN, code, gq_mods(state), repeat, 0);
        if (n > 0) gq_text_push(w, buf, n);
    } else {
        if (code >= 0 && code < 256) w->keys[code] = 0;
        gq_push(w, GQ_EV_KEY_UP, code, gq_mods(state), 0, 0);
    }
}

/* X buttons 1-3 are left, middle, right — 1, 4 and 2 here, as 0.9 numbered them; 4 and 5 are
 * the wheel, a step up and a step down. */
static void gq_button(gq_window *w, gx_input_event *e, int press) {
    int bit = e->detail == 1 ? 1 : e->detail == 2 ? 4 : e->detail == 3 ? 2 : 0;
    w->mouse_x = e->x;
    w->mouse_y = e->y;
    if (e->detail == 4 || e->detail == 5) {
        if (press) gq_push(w, GQ_EV_WHEEL, e->detail == 4 ? 1 : -1, e->x, e->y, 0);
        return;
    }
    if (!bit) return;
    if (press) w->buttons |= bit;
    else w->buttons &= ~bit;
    gq_push(w, press ? GQ_EV_MOUSE_DOWN : GQ_EV_MOUSE_UP, bit, e->x, e->y, 0);
}

/* Present an opaque frame: whatever alpha the shaders wrote is forced to 1 first, as 0.9 did. */
static void gq_present(gq_window *w) {
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glColorMask(0, 0, 0, 1);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT);
    w->api.glColorMask(1, 1, 1, 1);
    w->xa.glXSwapBuffers(w->display, w->window);
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
}

int64_t gq_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    static const int context_attrs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                                        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                                        GLX_CONTEXT_PROFILE_MASK_ARB,
                                        GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    gq_window *w = (gq_window *) calloc(1, sizeof(gq_window));
    gx_fbconfig config;
    gx_visual_info *visual;
    gx_window_attrs attrs;
    gx_error_handler previous = NULL;
    int trapped = 0;
    gx_xid root;
    int screen;
    if (!w) return 0;
    w->thread = pthread_self();
    if (!gq_bind(w)) goto fail;
    w->display = gq_open_display(w);
    if (!w->display) goto fail;
    screen = w->xa.XDefaultScreen(w->display);
    root = w->xa.XRootWindow(w->display, screen);
    if (fullscreen) {
        width = w->xa.XDisplayWidth(w->display, screen);
        height = w->xa.XDisplayHeight(w->display, screen);
    }
    if (width <= 0 || height <= 0) goto fail;
    config = gq_pick_config(w, screen);
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
    gq_x_failed = 0;
    previous = w->xa.XSetErrorHandler(gq_on_x_error);
    trapped = 1;
    w->context = w->xa.glXCreateContextAttribsARB(w->display, config, NULL, 1, context_attrs);
    w->xa.XSync(w->display, 0);
    if (gq_x_failed || !w->context) goto fail;

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
    if (gq_x_failed) goto fail;
    gq_current = w;
    w->xa.XSetErrorHandler(previous);
    trapped = 0;
    if (!gq_bind_gl(w)) goto fail;
    if (w->xa.glXSwapIntervalEXT) w->xa.glXSwapIntervalEXT(w->display, w->window, 1);
    w->width = (int) width;
    w->height = (int) height;

    /* 0.9's first frames: depth test on, both buffers opaque black before anything is shown. */
    w->api.glEnable(GL_DEPTH_TEST);
    w->caps = 1u << 1;
    w->api.glViewport(0, 0, w->width, w->height);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gq_present(w);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gq_present(w);
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
    gq_free(w);
    return 0;
}

/* Drain the window's pending events: a resize applies to the viewport after the pump, as 0.9's
 * did, and is queued as a `Resize`; the window manager's close request ends the loop. */
int64_t gq_poll(int64_t h) {
    gq_window *w = gq_of(h);
    int width, height;
    if (!gq_use(w, "Window.poll")) return 0;
    width = w->width;
    height = w->height;
    while (w->xa.XPending(w->display) > 0) {
        gx_event ev;
        w->xa.XNextEvent(w->display, &ev);
        if (w->ic && w->xa.XFilterEvent(&ev, 0)) continue; /* the input method took it */
        switch (ev.type) {
        case GX_KEY_PRESS:
        case GX_KEY_RELEASE:
            gq_key(w, &ev.input, ev.type == GX_KEY_PRESS);
            break;
        case GX_BUTTON_PRESS:
        case GX_BUTTON_RELEASE:
            gq_button(w, &ev.input, ev.type == GX_BUTTON_PRESS);
            break;
        case GX_MOTION_NOTIFY:
            w->mouse_x = ev.motion.x;
            w->mouse_y = ev.motion.y;
            gq_push(w, GQ_EV_MOUSE_MOVE, ev.motion.x, ev.motion.y, 0, 0);
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
        gq_push(w, GQ_EV_RESIZE, width, height, 0, 0);
    }
    return !w->should_close;
}

#elif defined(GQ_COCOA)

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
    return ((flags & GC_FLAG_SHIFT) ? GQ_MOD_SHIFT : 0) | ((flags & GC_FLAG_CONTROL) ? GQ_MOD_CTRL : 0) |
           ((flags & GC_FLAG_OPTION) ? GQ_MOD_ALT : 0) | ((flags & GC_FLAG_COMMAND) ? GQ_MOD_SUPER : 0);
}

/* A key: KeyDown (a repeat when it is already down) and the Text it typed, or KeyUp — the
 * modifiers as they stood before the event, as on X11.  What a key types is AppKit's, except
 * where X11 types something else: Delete types a backspace "\b", forward delete "\x7f", and the
 * keys that type nothing (arrows, F-keys: U+F700-U+F8FF here) no Text. */
static void gc_key(gq_window *w, gc_id ev, int press) {
    unsigned kc = (unsigned) gc_l(ev, "keyCode");
    int64_t code = gc_key_code(kc, ev);
    int64_t mods = gc_mods(gc_l(ev, "modifierFlags"));
    if (!press) {
        if (code >= 0 && code < 256) w->keys[code] = 0;
        gq_push(w, GQ_EV_KEY_UP, code, mods, 0, 0);
        return;
    }
    {
        int repeat = code >= 0 && code < 256 && w->keys[code];
        const char *text = (const char *) gc_o(gc_o(ev, "characters"), "UTF8String");
        size_t n = text ? strlen(text) : 0;
        if (code >= 0 && code < 256) w->keys[code] = 1;
        gq_push(w, GQ_EV_KEY_DOWN, code, mods, repeat, 0);
        if (kc == 0x33) text = "\b", n = 1;
        else if (kc == 0x75) text = "\x7f", n = 1;
        else if (n == 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] >= 0x9C &&
                 (unsigned char) text[1] <= 0xA3)
            n = 0; /* U+F700-U+F8FF: AppKit's function-key characters */
        if (n > 0) gq_text_push(w, text, (int) n);
    }
}

/* Shift, Control, Option, Command: AppKit reports them as the modifier state changing.  The
 * key's bit in the new state says press or release; the state before is the new one without it
 * (a press) or with it (a release). */
static void gc_flags(gq_window *w, gc_id ev) {
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
    gq_push(w, press ? GQ_EV_KEY_DOWN : GQ_EV_KEY_UP, code,
            gc_mods(press ? flags & ~bit : flags | bit), 0, 0);
}

/* The event's place in pixels from the content's top left (AppKit's y runs up from the bottom). */
static void gc_where(gq_window *w, gc_id ev, int *x, int *y) {
    gc_point p = gc_pt(ev, "locationInWindow");
    *x = (int) floor(p.x);
    *y = (int) floor((double) w->height - p.y);
}

static void gc_event(gq_window *w, gc_id ev, unsigned long type) {
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
        gq_push(w, GQ_EV_MOUSE_MOVE, x, y, 0, 0);
        return;
    case GC_SCROLL:
        dy = gc_d(ev, "scrollingDeltaY");
        if (!inside || dy == 0.0) return;
        w->mouse_x = x;
        w->mouse_y = y;
        gq_push(w, GQ_EV_WHEEL, dy > 0.0 ? 1 : -1, x, y, 0);
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
        gq_push(w, GQ_EV_MOUSE_DOWN, bit, x, y, 0);
    } else if (w->buttons & bit) { /* a release ends a press, wherever it lands */
        w->buttons &= ~bit;
        w->mouse_x = x;
        w->mouse_y = y;
        gq_push(w, GQ_EV_MOUSE_UP, bit, x, y, 0);
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
        gq_window *w;
        if (!ev) break;
        type = gc_l(ev, "type");
        target = gc_o(ev, "window");
        if (!target && (type == GC_KEY_DOWN || type == GC_KEY_UP || type == GC_FLAGS_CHANGED))
            target = gc_o(gc.app, "keyWindow"); /* keys go to the key window, as AppKit sends them */
        for (w = gc.open; w; w = (gq_window *) w->next_open)
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
static void gq_present(gq_window *w) {
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glColorMask(0, 0, 0, 1);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT);
    w->api.glColorMask(1, 1, 1, 1);
    gc_v(w->context, "flushBuffer");
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
}

/* What opening asks of the main thread, and what it answers. */
typedef struct {
    gq_window *w;
    double width, height;
    const char *title;
    int fullscreen, ok;
} gc_open_job;

/* The main thread's half of opening: a titled window centred on the main screen, or for
 * `fullscreen` a borderless one over the whole screen, the dock and the menu bar hidden; its
 * context, with 0.9's pixel format (double-buffered RGB, no alpha, 24-bit depth) and the view as
 * its drawable, in points (no Retina backing), as the window's size is. */
static void gc_open_main(void *arg) {
    static const uint32_t attrs[] = {GC_PFA_DOUBLE_BUFFER, GC_PFA_COLOR_SIZE, 24, GC_PFA_DEPTH_SIZE,
                                     24, GC_PFA_PROFILE, GC_PROFILE_3_2_CORE, 0};
    gc_open_job *job = (gc_open_job *) arg;
    gq_window *w = job->w;
    void *pool;
    gc_rect content, bounds;
    unsigned long style = GC_STYLE_TITLED | GC_STYLE_CLOSABLE | GC_STYLE_MINIATURIZABLE |
                          GC_STYLE_RESIZABLE;
    gc_id cls;
    int one = 1;
    if (!gc_app()) return;
    pool = gc.pool_push();
    content.x = content.y = 0.0;
    content.width = job->width;
    content.height = job->height;
    cls = gc_class("NSWindow");
    if (job->fullscreen) {
        gc_id screen = gc_o(gc_class("NSScreen"), "mainScreen");
        gc_why = "no main screen for a fullscreen window";
        if (!screen || !gc.full_class) goto done;
        content = gc_rc(screen, "frame");
        style = 0;
        cls = gc.full_class;
        w->fullscreen = 1;
    }
    gc_why = "a window needs a positive size";
    if (content.width <= 0.0 || content.height <= 0.0) goto done;
    gc_why = "NSWindow did not initialise";
    w->window = ((gc_id (*)(gc_id, gc_sel, gc_rect, unsigned long, unsigned long, signed char)) gc.msg)(
        gc_o(cls, "alloc"), gc_s("initWithContentRect:styleMask:backing:defer:"), content, style,
        GC_BACKING_BUFFERED, 0);
    if (!w->window) goto done;
    gc_v_l(w->window, "setReleasedWhenClosed:", 0);
    gc_v_l(w->window, "setAcceptsMouseMovedEvents:", 1);
    gc_v_o(w->window, "setTitle:", gc_o_p(gc_class("NSString"), "stringWithUTF8String:", job->title));
    if (job->fullscreen) {
        gc_v_l(w->window, "setLevel:", GC_MAIN_MENU_LEVEL + 1);
        gc_v_l(gc.app, "setPresentationOptions:", (long) (GC_HIDE_DOCK | GC_HIDE_MENU_BAR));
    } else {
        gc_v(w->window, "center");
    }
    w->view = gc_o(w->window, "contentView");
    gc_v_l(w->view, "setWantsBestResolutionOpenGLSurface:", 0);
    gc_why = "no pixel format for a 3.2 core, double-buffered, 24-bit depth context";
    w->format = gc_o_p(gc_o(gc_class("NSOpenGLPixelFormat"), "alloc"), "initWithAttributes:", attrs);
    if (!w->format) goto done;
    gc_why = "NSOpenGLContext did not initialise";
    w->context = ((gc_id (*)(gc_id, gc_sel, gc_id, gc_id)) gc.msg)(
        gc_o(gc_class("NSOpenGLContext"), "alloc"), gc_s("initWithFormat:shareContext:"), w->format,
        NULL);
    if (!w->context) goto done;
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
    bounds = gc_rc(w->view, "bounds");
    w->width = (int) bounds.width;
    w->height = (int) bounds.height;
    gc_why = "the window's content view has no size";
    job->ok = w->width > 0 && w->height > 0;
done:
    gc.pool_pop(pool);
}

int64_t gq_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    gq_window *w;
    gc_open_job job;
    if (!gc_load()) {
        gc_say_why();
        return 0;
    }
    w = (gq_window *) calloc(1, sizeof(gq_window));
    if (!w) return 0;
    w->thread = pthread_self();
    job.w = w;
    job.width = (double) width;
    job.height = (double) height;
    job.title = title;
    job.fullscreen = (int) fullscreen;
    job.ok = 0;
    if (!gc_on_main(gc_open_main, &job)) {
        free(w);
        return 0;
    }
    if (!job.ok) goto fail;
    gc_v(w->context, "makeCurrentContext"); /* on the thread that draws */
    gq_current = w;
    gc_why = "an OpenGL entry point is missing from OpenGL.framework";
    if (!gq_bind_gl(w)) goto fail;

    /* 0.9's first frames: depth test on, both buffers opaque black before anything is shown. */
    w->api.glEnable(GL_DEPTH_TEST);
    w->caps = 1u << 1;
    w->api.glViewport(0, 0, w->width, w->height);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gq_present(w);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gq_present(w);
    return (int64_t) (intptr_t) w;
fail:
    gc_say_why();
    gq_free(w);
    return 0;
}

/* The main thread's half of a poll: every queued event taken (each to its own window), whether
 * the close button was pressed (a minimised window is still open), and the view's size — a
 * changed one is applied to the context here, where AppKit wants that done. */
static void gc_poll_main(void *arg) {
    gq_window *w = (gq_window *) arg;
    void *pool = gc.pool_push();
    gc_rect bounds;
    int width, height;
    gc_pump();
    if (!gc_b(w->window, "isVisible") && !gc_b(w->window, "isMiniaturized")) w->should_close = 1;
    bounds = gc_rc(w->view, "bounds");
    width = (int) bounds.width;
    height = (int) bounds.height;
    if (width > 0 && height > 0 && (width != w->width || height != w->height)) {
        gc_v(w->context, "update");
        w->width = width;
        w->height = height;
        gq_push(w, GQ_EV_RESIZE, width, height, 0, 0);
    }
    gc.pool_pop(pool);
}

/* Take the queued events; a resize reaches the viewport and is queued as a `Resize`; the window's
 * close button ends the loop. */
int64_t gq_poll(int64_t h) {
    gq_window *w = gq_of(h);
    int width, height;
    if (!gq_use(w, "Window.poll")) return 0;
    width = w->width;
    height = w->height;
    if (!gc_on_main(gc_poll_main, w)) return 0;
    if (w->width != width || w->height != height)
        w->api.glViewport(0, 0, w->width, w->height);
    return !w->should_close;
}

#elif defined(GQ_ANDROID)

/* The 0.9 code of an Android key code (src/graphics.loft KEY_*): ASCII for a character, its own
 * codes for the named keys, -1 for a key it has none for. */
static int64_t gq_android_key(int32_t kc) {
    if (kc >= AKEYCODE_A && kc <= AKEYCODE_Z) return 'a' + (kc - AKEYCODE_A);
    if (kc >= AKEYCODE_0 && kc <= AKEYCODE_9) return '0' + (kc - AKEYCODE_0);
    if (kc >= AKEYCODE_F1 && kc <= AKEYCODE_F12) return 135 + (kc - AKEYCODE_F1);
    switch (kc) {
    case AKEYCODE_SPACE: return ' ';
    case AKEYCODE_COMMA: return ',';
    case AKEYCODE_PERIOD: return '.';
    case AKEYCODE_MINUS: return '-';
    case AKEYCODE_EQUALS: return '=';
    case AKEYCODE_LEFT_BRACKET: return '[';
    case AKEYCODE_RIGHT_BRACKET: return ']';
    case AKEYCODE_BACKSLASH: return '\\';
    case AKEYCODE_SEMICOLON: return ';';
    case AKEYCODE_APOSTROPHE: return '\'';
    case AKEYCODE_SLASH: return '/';
    case AKEYCODE_GRAVE: return '`';
    case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: return 13;
    case AKEYCODE_ESCAPE: return 27;
    case AKEYCODE_TAB: return 9;
    case AKEYCODE_DEL: return 8; /* Android's DEL is the backspace key */
    case AKEYCODE_DPAD_UP: return 128;
    case AKEYCODE_DPAD_DOWN: return 129;
    case AKEYCODE_DPAD_LEFT: return 130;
    case AKEYCODE_DPAD_RIGHT: return 131;
    case AKEYCODE_SHIFT_LEFT: case AKEYCODE_SHIFT_RIGHT: return 132;
    case AKEYCODE_CTRL_LEFT: case AKEYCODE_CTRL_RIGHT: return 133;
    case AKEYCODE_FORWARD_DEL: return 147;
    case AKEYCODE_INSERT: return 148;
    case AKEYCODE_MOVE_HOME: return 149;
    case AKEYCODE_MOVE_END: return 150;
    case AKEYCODE_PAGE_UP: return 151;
    case AKEYCODE_PAGE_DOWN: return 152;
    default: return -1;
    }
}

/* Each pointer's touch is TouchBegin / TouchMove / TouchEnd with its pointer id, and the first
 * pointer's is also the left mouse button, as a browser makes mouse events of a touch: a
 * program written for the mouse works on the screen.  A key is KeyDown (a repeat when it is
 * already down) and, when it types anything, a Text with what it typed (Enter types "\r",
 * Backspace "\b", as 0.9 reported them), and KeyUp.  Back is left to the system, which finishes
 * the activity, and the next `Window.poll` answers false. */
static int32_t gq_android_input(struct android_app *app, AInputEvent *e) {
    gq_window *w = gq_android_window;
    (void) app;
    if (!w) return 0;
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t raw = AMotionEvent_getAction(e);
        int32_t action = raw & AMOTION_EVENT_ACTION_MASK;
        size_t index = (size_t) ((raw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                 AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        size_t count = AMotionEvent_getPointerCount(e);
        int x = (int) AMotionEvent_getX(e, 0), y = (int) AMotionEvent_getY(e, 0);
        if (action == AMOTION_EVENT_ACTION_MOVE) {
            for (size_t k = 0; k < count; k++)
                gq_push(w, GQ_EV_TOUCH_MOVE, AMotionEvent_getPointerId(e, k),
                        (int64_t) AMotionEvent_getX(e, k), (int64_t) AMotionEvent_getY(e, k), 0);
        } else if (index < count) {
            int64_t tx = (int64_t) AMotionEvent_getX(e, index), ty = (int64_t) AMotionEvent_getY(e, index);
            int64_t id = AMotionEvent_getPointerId(e, index);
            if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)
                gq_push(w, GQ_EV_TOUCH_BEGIN, id, tx, ty, 0);
            else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP ||
                     action == AMOTION_EVENT_ACTION_CANCEL)
                gq_push(w, GQ_EV_TOUCH_END, id, tx, ty, 0);
        }
        w->mouse_x = x;
        w->mouse_y = y;
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            w->buttons |= 1;
            gq_push(w, GQ_EV_MOUSE_DOWN, 1, x, y, 0);
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            w->buttons &= ~1;
            gq_push(w, GQ_EV_MOUSE_UP, 1, x, y, 0);
        } else if (action == AMOTION_EVENT_ACTION_MOVE) {
            gq_push(w, GQ_EV_MOUSE_MOVE, x, y, 0, 0);
        }
        return 1;
    }
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_KEY) {
        int32_t kc = AKeyEvent_getKeyCode(e), meta = AKeyEvent_getMetaState(e);
        int64_t code = gq_android_key(kc);
        int64_t mods = ((meta & AMETA_SHIFT_ON) ? GQ_MOD_SHIFT : 0) | ((meta & AMETA_CTRL_ON) ? GQ_MOD_CTRL : 0) |
                       ((meta & AMETA_ALT_ON) ? GQ_MOD_ALT : 0) | ((meta & AMETA_META_ON) ? GQ_MOD_SUPER : 0);
        if (kc == AKEYCODE_BACK) return 0;
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN) {
            int repeat = code >= 0 && code < 256 && w->keys[code];
            char typed = 0;
            if (code >= 0 && code < 256) w->keys[code] = 1;
            gq_push(w, GQ_EV_KEY_DOWN, code, mods, repeat, 0);
            if (code >= 'a' && code <= 'z') typed = (char) ((meta & AMETA_SHIFT_ON) ? code - 32 : code);
            else if (code >= ' ' && code < 127) typed = (char) code;
            else if (code == 13) typed = '\r';
            else if (code == 8) typed = '\b';
            if (typed) gq_text_push(w, &typed, 1);
        } else if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_UP) {
            if (code >= 0 && code < 256) w->keys[code] = 0;
            gq_push(w, GQ_EV_KEY_UP, code, mods, 0, 0);
        }
        return 1;
    }
    return 0;
}

/* Make the surface for the activity's window, and take its size: a new window after the activity
 * came back can be another size (a rotation), which the program reads as a Resize. */
static void gq_android_surface(gq_window *w) {
    EGLint width = 0, height = 0;
    if (w->surface != EGL_NO_SURFACE || w->context == EGL_NO_CONTEXT || !w->app->window) return;
    w->surface = w->ea.eglCreateWindowSurface(w->display, w->config, w->app->window, NULL);
    if (w->surface == EGL_NO_SURFACE) return;
    w->ea.eglMakeCurrent(w->display, w->surface, w->surface, w->context);
    w->ea.eglQuerySurface(w->display, w->surface, EGL_WIDTH, &width);
    w->ea.eglQuerySurface(w->display, w->surface, EGL_HEIGHT, &height);
    if (w->width && (width != w->width || height != w->height)) gq_push(w, GQ_EV_RESIZE, width, height, 0, 0);
    w->width = width;
    w->height = height;
}

static void gq_android_cmd(struct android_app *app, int32_t cmd) {
    gq_window *w = gq_android_window;
    (void) app;
    if (!w) return;
    if (cmd == APP_CMD_TERM_WINDOW) gq_android_drop_surface(w);
    else if (cmd == APP_CMD_INIT_WINDOW) gq_android_surface(w);
    else if (cmd == APP_CMD_DESTROY) w->should_close = 1;
}

/* Run the activity's queued work: `timeout` -1 waits for the first, 0 takes what is there. */
static void gq_android_pump(struct android_app *app, int timeout) {
    int events;
    struct android_poll_source *source;
    while (ALooper_pollOnce(timeout, NULL, &events, (void **) &source) >= 0) {
        if (source) source->process(app, source);
        if (app->destroyRequested) return;
        timeout = 0;
    }
}

/* Present an opaque frame: whatever alpha the shaders wrote is forced to 1 first, as 0.9 did. */
static void gq_present(gq_window *w) {
    if (w->surface == EGL_NO_SURFACE) return;
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    w->api.glColorMask(0, 0, 0, 1);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT);
    w->api.glColorMask(1, 1, 1, 1);
    w->ea.eglSwapBuffers(w->display, w->surface);
    if (w->target) w->api.glBindFramebuffer(GL_FRAMEBUFFER, w->target);
}

/* The activity's window, whatever size is asked: it is the screen.  The context is made first
 * and waits, without a surface, for the window the activity is given. */
int64_t gq_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    static const EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, 0x40,
                                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                          EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE};
    static const EGLint context_attrs[] = {0x3098 /* EGL_CONTEXT_MAJOR_VERSION */, 3, EGL_NONE};
    struct android_app *app = loft_android_app();
    gq_window *w;
    EGLint n = 0;
    (void) width;
    (void) height;
    (void) title;
    (void) fullscreen;
    if (!app || gq_android_window) return 0;
    w = (gq_window *) calloc(1, sizeof(gq_window));
    if (!w) return 0;
    w->app = app;
    w->surface = EGL_NO_SURFACE;
    w->context = EGL_NO_CONTEXT;
    if (!gq_bind_egl(w) || !gq_bind_gl(w)) {
        free(w);
        return 0;
    }
    w->display = w->ea.eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (w->display == EGL_NO_DISPLAY || !w->ea.eglInitialize(w->display, NULL, NULL) ||
        !w->ea.eglBindAPI(EGL_OPENGL_ES_API) ||
        !w->ea.eglChooseConfig(w->display, config_attrs, &w->config, 1, &n) || n < 1) {
        free(w);
        return 0;
    }
    w->context = w->ea.eglCreateContext(w->display, w->config, EGL_NO_CONTEXT, context_attrs);
    if (w->context == EGL_NO_CONTEXT) {
        free(w);
        return 0;
    }
    gq_android_window = w;
    app->onAppCmd = gq_android_cmd;
    app->onInputEvent = gq_android_input;
    while (!app->window && !app->destroyRequested) gq_android_pump(app, -1);
    gq_android_surface(w);
    if (w->surface == EGL_NO_SURFACE) {
        gq_teardown(w);
        free(w);
        return 0;
    }
    /* 0.9's first state: depth test on, the frame opaque black */
    w->api.glEnable(GL_DEPTH_TEST);
    w->caps = 1u << 1;
    w->api.glViewport(0, 0, w->width, w->height);
    w->api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return (int64_t) (intptr_t) w;
}

/* Run the activity's queued input and lifecycle; while it is in the background (no surface), wait
 * for it to come back rather than spin.  False once the activity is finishing. */
int64_t gq_poll(int64_t h) {
    gq_window *w = gq_of(h);
    if (!w || w->closed) return 0;
    gq_android_pump(w->app, 0);
    while (w->surface == EGL_NO_SURFACE && !w->should_close && !w->app->destroyRequested)
        gq_android_pump(w->app, -1);
    if (w->app->destroyRequested) w->should_close = 1;
    return !w->should_close;
}

#else /* GQ_BROWSER */

/* Present the frame and give the page its turn: the browser draws it, and its events arrive. */
static void gq_present(gq_window *w) {
    (void) w;
    gq_js_present();
}

/* Size the page's canvas (or the whole page, for `fullscreen`) and take its WebGL2 context. */
int64_t gq_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    gq_window *w;
    (void) title;
    if (!fullscreen && (width <= 0 || height <= 0)) return 0;
    if (!gq_js_open((int) width, (int) height, (int) fullscreen)) return 0;
    w = (gq_window *) calloc(1, sizeof(gq_window));
    if (!w) return 0;
    gq_bind_gl(w);
    w->width = gq_js_size(0);
    w->height = gq_js_size(1);
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
int64_t gq_poll(int64_t h) {
    gq_window *w = gq_of(h);
    int ev[5];
    char text[64];
    int tag;
    if (!gq_use(w, "Window.poll")) return 0;
    while ((tag = gq_js_event(ev, text, (int) sizeof text)) != 0) {
        if (tag == GQ_EV_KEY_DOWN || tag == GQ_EV_KEY_UP) {
            if (ev[0] >= 0 && ev[0] < 256) w->keys[ev[0]] = tag == GQ_EV_KEY_DOWN;
            gq_push(w, tag, ev[0], ev[1], tag == GQ_EV_KEY_DOWN ? ev[2] : 0, 0);
        } else if (tag == GQ_EV_TEXT) {
            gq_text_push(w, text, ev[0]);
        } else if (tag == GQ_EV_MOUSE_DOWN || tag == GQ_EV_MOUSE_UP) {
            w->mouse_x = ev[1];
            w->mouse_y = ev[2];
            if (tag == GQ_EV_MOUSE_DOWN) w->buttons |= ev[0];
            else w->buttons &= ~ev[0];
            gq_push(w, tag, ev[0], ev[1], ev[2], 0);
        } else if (tag == GQ_EV_MOUSE_MOVE) {
            w->mouse_x = ev[0];
            w->mouse_y = ev[1];
            gq_push(w, tag, ev[0], ev[1], 0, 0);
        } else if (tag == GQ_EV_WHEEL) {
            gq_push(w, tag, ev[0], ev[1], ev[2], 0);
        }
    }
    return !w->should_close;
}
#endif

void gq_swap(int64_t h) {
    gq_window *w = gq_of(h);
    if (gq_use(w, "Window.swap")) gq_present(w);
}

/* `color` is packed 0xAARRGGBB, as `rgba()` packs it. */
void gq_clear(int64_t h, int64_t color) {
    gq_window *w = gq_of(h);
    uint32_t c = (uint32_t) color;
    if (!gq_use(w, "Window.clear")) return;
    w->api.glClearColor((float) ((c >> 16) & 0xFF) / 255.0f, (float) ((c >> 8) & 0xFF) / 255.0f,
                        (float) (c & 0xFF) / 255.0f, (float) ((c >> 24) & 0xFF) / 255.0f);
    w->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gq_close(int64_t h) {
    gq_window *w = gq_of(h);
    if (!w || w->closed) return;
#if defined(GQ_X11) || defined(GQ_COCOA)
    if (!gq_on_thread(w, "Window.close")) return;
#endif
    gq_free(w);
}

int64_t gq_width(int64_t h) {
    gq_window *w = gq_of(h);
    return w ? w->width : 0;
}

int64_t gq_height(int64_t h) {
    gq_window *w = gq_of(h);
    return w ? w->height : 0;
}

int64_t gq_event_count(int64_t h) {
    gq_window *w = gq_of(h);
    return w ? w->event_count : 0;
}

/* Copy the queued events out, GQ_EVENT_WORDS integers each, oldest first, and empty the queue. */
void gq_events(int64_t h, int64_t *out, int64_t n) {
    gq_window *w = gq_of(h);
    int64_t words;
    if (!w) return;
    words = w->event_count * GQ_EVENT_WORDS;
    if (n < words) words = n - n % GQ_EVENT_WORDS;
    memcpy(out, w->events, (size_t) words * 8u);
    w->event_count = 0;
    w->text_len = 0;
}

/* The bytes of the queued `Text` events — copy them before `gq_events` empties the queue. */
int64_t gq_text_size(int64_t h) {
    gq_window *w = gq_of(h);
    return w ? w->text_len : 0;
}

void gq_text_copy(int64_t h, uint8_t *out, int64_t n) {
    gq_window *w = gq_of(h);
    if (!w || n < w->text_len) return;
    memcpy(out, w->text, (size_t) w->text_len);
}

int64_t gq_key_down(int64_t h, int64_t key) {
    gq_window *w = gq_of(h);
    return w && key >= 0 && key < 256 && w->keys[key];
}

/* 0: x, 1: y, 2: the buttons held (1 left, 2 right, 4 middle). */
int64_t gq_mouse(int64_t h, int64_t which) {
    gq_window *w = gq_of(h);
    if (!w) return 0;
    return which == 0 ? w->mouse_x : which == 1 ? w->mouse_y : w->buttons;
}

/* The back buffer as 0xAARRGGBB pixels, top row first: read before `swap`, after the frame's
 * draws.  `n` is width * height. */
void gq_read_pixels(int64_t h, uint32_t *out, int64_t n) {
    gq_window *w = gq_of(h);
    unsigned char *rgba;
    int64_t x, y;
    if (!gq_use(w, "Window.capture")) return;
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
            out[y * w->width + x] = ((uint32_t) p[3] << 24) | ((uint32_t) p[0] << 16) |
                                    ((uint32_t) p[1] << 8) | p[2];
        }
    }
    free(rgba);
}

static gq_res *gq_new_res(gq_window *w, int kind) {
    gq_res *r = (gq_res *) calloc(1, sizeof(gq_res));
    if (!r) return NULL;
    r->w = w;
    r->kind = kind;
    w->refs++;
    return r;
}

/* A driver's log without its trailing newlines, so the message ends where it says it ends. */
static void gq_trim(char *log) {
    size_t n = strlen(log);
    while (n > 0 && (log[n - 1] == '\n' || log[n - 1] == '\r' || log[n - 1] == ' ')) log[--n] = 0;
}

#if defined(GQ_ANDROID)
/* GLES 3.0 reads `#version 300 es`, and a fragment shader states its float precision: the
 * library's desktop shaders (`#version 330 core`) take the line the page's rewrite gives them
 * (doc/loft-gl-wasm.js), the rest unchanged.  NULL leaves `source` as it is. */
static char *gq_gles_source(unsigned type, const char *source) {
    const char *at = source, *rest;
    const char *head;
    char *out;
    size_t len;
    while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r') at++;
    if (strncmp(at, "#version 330", 12) != 0) return NULL;
    rest = strchr(at, '\n');
    rest = rest ? rest + 1 : at + strlen(at);
    head = type == GL_FRAGMENT_SHADER ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                                      : "#version 300 es\n";
    len = strlen(head) + strlen(rest) + 1;
    out = (char *) malloc(len);
    if (out) snprintf(out, len, "%s%s", head, rest);
    return out;
}
#endif

static unsigned gq_compile(gq_window *w, unsigned type, const char *source, const char *what) {
    unsigned id = w->api.glCreateShader(type);
    int ok = 0;
#if defined(GQ_ANDROID)
    char *gles = gq_gles_source(type, source);
    if (gles) source = gles;
#endif
    w->api.glShaderSource(id, 1, &source, NULL);
#if defined(GQ_ANDROID)
    free(gles);
#endif
    w->api.glCompileShader(id);
    w->api.glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        log[0] = 0;
        w->api.glGetShaderInfoLog(id, (int) sizeof log, NULL, log);
        gq_trim(log);
        fprintf(stderr, "graphics: the %s shader does not compile:\n%s\n", what, log);
        w->api.glDeleteShader(id);
        return 0;
    }
    return id;
}

/* Compile and link a program; 0, with the driver's log on stderr, when either fails. */
int64_t gq_shader(int64_t win, const char *vertex, const char *fragment) {
    gq_window *w = gq_of(win);
    unsigned vs, fs, program;
    int ok = 0;
    gq_res *r;
    if (!gq_use(w, "Window.shader")) return 0;
    vs = gq_compile(w, GL_VERTEX_SHADER, vertex, "vertex");
    if (!vs) return 0;
    fs = gq_compile(w, GL_FRAGMENT_SHADER, fragment, "fragment");
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
        gq_trim(log);
        fprintf(stderr, "graphics: the shader program does not link:\n%s\n", log);
        w->api.glDeleteProgram(program);
        return 0;
    }
    r = gq_new_res(w, GQ_SHADER);
    if (!r) {
        w->api.glDeleteProgram(program);
        return 0;
    }
    r->id = program;
    return (int64_t) (intptr_t) r;
}

/* Bind shader `sh` and answer the location of its uniform `name`, -1 for none. */
static int gq_uniform_at(gq_res *r, const char *name, const char *what) {
    if (!r || r->kind != GQ_SHADER || !gq_use(r->w, what)) return -1;
    r->w->api.glUseProgram(r->id);
    return r->w->api.glGetUniformLocation(r->id, name);
}

/* A float uniform of `n` components: 1 to 4 a float or vector, 16 a column-major mat4. */
void gq_uniform_f(int64_t sh, const char *name, const double *v, int64_t n) {
    gq_res *r = gq_res_of(sh);
    int at = gq_uniform_at(r, name, "Shader.set");
    gq_api *gl;
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

void gq_uniform_i(int64_t sh, const char *name, int64_t v) {
    gq_res *r = gq_res_of(sh);
    int at = gq_uniform_at(r, name, "Shader.set_int");
    if (at >= 0) r->w->api.glUniform1i(at, (int) v);
}

/* A vertex array over `n` floats, `stride` per vertex, laid out as 0.9 laid it: position at 0;
 * a normal at 1 from 6 floats; a UV at 2 at exactly 8; a colour at 2 from 10; an anchor at 3 and
 * a birth time at 4 from 14. */
int64_t gq_mesh(int64_t win, const float *data, int64_t n, int64_t stride) {
    gq_window *w = gq_of(win);
    gq_api *gl;
    gq_res *r;
    int bytes;
    if (stride < 3 || !gq_use(w, "Window.mesh")) return 0;
    r = gq_new_res(w, GQ_MESH);
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
void gq_draw_as(int64_t mesh, int64_t shader, int64_t mode, int64_t indexed, int64_t instances) {
    gq_res *m = gq_res_of(mesh), *s = gq_res_of(shader);
    unsigned prim = mode == 1 ? GL_LINES : mode == 2 ? GL_POINTS : GL_TRIANGLES;
    gq_api *gl;
    if (!m || !s || m->kind != GQ_MESH || s->kind != GQ_SHADER) return;
    if (m->w != s->w) {
        fprintf(stderr, "graphics: GpuMesh.draw with a shader of another window\n");
        return;
    }
    if (!gq_use(m->w, "GpuMesh.draw")) return;
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

void gq_draw(int64_t mesh, int64_t shader) {
    gq_draw_as(mesh, shader, 0, 0, 0);
}

/* A 2-D draw: over whatever the frame holds, in call order — the depth test is off for this one
 * draw and back as it was after it, so a HUD neither hides behind a 3-D scene nor loses to an
 * earlier rectangle at the same depth. */
void gq_draw_over(int64_t mesh, int64_t shader) {
    gq_res *m = gq_res_of(mesh);
    unsigned depth;
    if (!m || m->kind != GQ_MESH || !gq_use(m->w, "Painter")) return;
    depth = (m->w->caps >> 1) & 1u;
    if (depth) m->w->api.glDisable(GL_DEPTH_TEST);
    gq_draw_as(mesh, shader, 0, 0, 0);
    if (depth) m->w->api.glEnable(GL_DEPTH_TEST);
}

static gq_res *gq_mesh_of(int64_t h, const char *what) {
    gq_res *r = gq_res_of(h);
    if (!r || r->kind != GQ_MESH || !gq_use(r->w, what)) return NULL;
    return r;
}

/* New vertices for the mesh, in the same layout; uploaded for change (DYNAMIC_DRAW). */
void gq_mesh_update(int64_t mesh, const float *data, int64_t n) {
    gq_res *r = gq_mesh_of(mesh, "GpuMesh.update");
    if (!r || r->stride < 3) return;
    r->vertices = (int) (n / r->stride);
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, r->buffer);
    r->w->api.glBufferData(GL_ARRAY_BUFFER, (intptr_t) r->vertices * r->stride * 4, data,
                           GL_DYNAMIC_DRAW);
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* The mesh's index list, 32 bits each; a second call replaces the first. */
void gq_mesh_indices(int64_t mesh, const int64_t *idx, int64_t n) {
    gq_res *r = gq_mesh_of(mesh, "GpuMesh.indices");
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
int64_t gq_instances(int64_t win, const float *data, int64_t n) {
    gq_window *w = gq_of(win);
    gq_res *r;
    if (!gq_use(w, "Window.instances")) return 0;
    r = gq_new_res(w, GQ_BUFFER);
    if (!r) return 0;
    w->api.glGenBuffers(1, &r->id);
    w->api.glBindBuffer(GL_ARRAY_BUFFER, r->id);
    w->api.glBufferData(GL_ARRAY_BUFFER, (intptr_t) n * 4, data, GL_DYNAMIC_DRAW);
    w->api.glBindBuffer(GL_ARRAY_BUFFER, 0);
    return (int64_t) (intptr_t) r;
}

void gq_buffer_update(int64_t buf, const float *data, int64_t n) {
    gq_res *r = gq_res_of(buf);
    if (!r || r->kind != GQ_BUFFER || !gq_use(r->w, "InstanceBuffer.update")) return;
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, r->id);
    r->w->api.glBufferData(GL_ARRAY_BUFFER, (intptr_t) n * 4, data, GL_DYNAMIC_DRAW);
    r->w->api.glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* Feed attribute `location` of `mesh` from `buf`, once per instance: `components` floats at
 * `offset` floats into each record of `stride` floats. */
void gq_instance_attrib(int64_t mesh, int64_t buf, int64_t location, int64_t components,
                        int64_t stride, int64_t offset) {
    gq_res *m = gq_mesh_of(mesh, "GpuMesh.instance_attrib"), *b = gq_res_of(buf);
    gq_api *gl;
    if (!m || !b || b->kind != GQ_BUFFER || b->w != m->w || location < 0) return;
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
void gq_release(int64_t res) {
    gq_res *r = gq_res_of(res);
    gq_window *w;
    if (!r) return;
    w = r->w;
#if defined(GQ_BROWSER)
    if (1) { /* the page's context outlives the window: delete in it whenever */
#else
    if (!w->closed) {
        if (!gq_use(w, "a GL object's drop")) return;
#endif
        if (r->kind == GQ_SHADER) {
            w->api.glDeleteProgram(r->id);
        } else if (r->kind == GQ_MESH) {
            w->api.glDeleteVertexArrays(1, &r->id);
            w->api.glDeleteBuffers(1, &r->buffer);
            if (r->indices_id) w->api.glDeleteBuffers(1, &r->indices_id);
        } else if (r->kind == GQ_BUFFER) {
            w->api.glDeleteBuffers(1, &r->id);
        } else if (r->kind == GQ_TEXTURE) {
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
    gq_forget(w);
}

/* ── Render state: 0.9's own codes, so a ported call keeps its arguments ─────────────────── */

/* GL_DEPTH_TEST = 1, GL_BLEND = 2, GL_CULL_FACE = 3, GL_SCISSOR_TEST = 4 (src/graphics.loft). */
void gq_capability(int64_t win, int64_t cap, int64_t on) {
    static const unsigned caps[] = {0, GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST};
    gq_window *w = gq_of(win);
    if (cap < 1 || cap > 4 || !gq_use(w, on ? "Window.enable" : "Window.disable")) return;
    if (on) w->api.glEnable(caps[cap]);
    else w->api.glDisable(caps[cap]);
    if (on) w->caps |= 1u << cap;
    else w->caps &= ~(1u << cap);
}

/* Whether capability `cap` is on: read from the record, so asking costs no GL call. */
int64_t gq_capability_on(int64_t win, int64_t cap) {
    gq_window *w = gq_of(win);
    if (cap < 1 || cap > 4 || !w || w->closed) return 0;
    return (w->caps >> cap) & 1u;
}

/* BLEND_ZERO = 0 … BLEND_ONE_MINUS_DST_ALPHA = 5; anything else is ONE, as in 0.9. */
static unsigned gq_blend_factor(int64_t f) {
    static const unsigned factors[] = {GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                       GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    return f >= 0 && f <= 5 ? factors[f] : GL_ONE;
}

void gq_blend(int64_t win, int64_t src, int64_t dst) {
    gq_window *w = gq_of(win);
    if (gq_use(w, "Window.blend")) w->api.glBlendFunc(gq_blend_factor(src), gq_blend_factor(dst));
}

/* 0 culls back faces, anything else front faces. */
void gq_cull(int64_t win, int64_t face) {
    gq_window *w = gq_of(win);
    if (gq_use(w, "Window.cull")) w->api.glCullFace(face == 0 ? GL_BACK : GL_FRONT);
}

void gq_depth_write(int64_t win, int64_t on) {
    gq_window *w = gq_of(win);
    if (gq_use(w, "Window.depth_write")) w->api.glDepthMask(on ? 1 : 0);
}

void gq_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    gq_window *w = gq_of(win);
    if (gq_use(w, "Window.viewport"))
        w->api.glViewport((int) x, (int) y, (int) width, (int) height);
}

void gq_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    gq_window *w = gq_of(win);
    if (gq_use(w, "Window.scissor"))
        w->api.glScissor((int) x, (int) y, (int) width, (int) height);
}

void gq_finish(int64_t win) {
    gq_window *w = gq_of(win);
    if (gq_use(w, "Window.finish")) w->api.glFinish();
}

/* A float travels as a one-element vector: `#c` takes no scalar float. */
void gq_line_width(int64_t win, const double *width, int64_t n) {
    gq_window *w = gq_of(win);
    if (n >= 1 && gq_use(w, "Window.line_width")) w->api.glLineWidth((float) width[0]);
}

void gq_point_size(int64_t win, const double *size, int64_t n) {
    gq_window *w = gq_of(win);
    if (n >= 1 && gq_use(w, "Window.point_size")) w->api.glPointSize((float) size[0]);
}

/* ── Textures and framebuffers, made as 0.9 made them ──────────────────────────────────────── */

static gq_res *gq_texture_of(int64_t h, const char *what) {
    gq_res *r = gq_res_of(h);
    if (!r || r->kind != GQ_TEXTURE || !gq_use(r->w, what)) return NULL;
    return r;
}

/* An empty colour texture (RGBA8, linear) or depth texture (32-bit float, nearest, clamped). */
int64_t gq_texture(int64_t win, int64_t width, int64_t height, int64_t depth) {
    gq_window *w = gq_of(win);
    gq_res *r;
    gq_api *gl;
    if (width <= 0 || height <= 0 || !gq_use(w, "Window.texture")) return 0;
    r = gq_new_res(w, GQ_TEXTURE);
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
static unsigned char *gq_rgba(const uint32_t *px, int64_t n) {
    unsigned char *out = (unsigned char *) malloc((size_t) n * 4u);
    int64_t i;
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        uint32_t p = px[i];
        out[4 * i] = (unsigned char) (p >> 16);
        out[4 * i + 1] = (unsigned char) (p >> 8);
        out[4 * i + 2] = (unsigned char) p;
        out[4 * i + 3] = (unsigned char) (p >> 24);
    }
    return out;
}

/* A colour texture holding a canvas's pixels: linear, clamped. */
int64_t gq_texture_from(int64_t win, const uint32_t *px, int64_t n, int64_t width, int64_t height) {
    gq_window *w = gq_of(win);
    unsigned char *rgba;
    gq_res *r;
    gq_api *gl;
    if (width <= 0 || height <= 0 || n < width * height || !gq_use(w, "Window.texture_from"))
        return 0;
    rgba = gq_rgba(px, width * height);
    if (!rgba) return 0;
    r = gq_new_res(w, GQ_TEXTURE);
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
void gq_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                    const uint32_t *px, int64_t n) {
    gq_res *r = gq_texture_of(tex, "Texture.put");
    unsigned char *rgba;
    if (!r || r->depth || width <= 0 || height <= 0 || n < width * height) return;
    rgba = gq_rgba(px, width * height);
    if (!rgba) return;
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
    r->w->api.glTexSubImage2D(GL_TEXTURE_2D, 0, (int) x, (int) y, (int) width, (int) height,
                              GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    r->w->api.glBindTexture(GL_TEXTURE_2D, 0);
    free(rgba);
}

void gq_texture_filter(int64_t tex, int64_t nearest) {
    gq_res *r = gq_texture_of(tex, "Texture.filter");
    int f = nearest ? GL_NEAREST : GL_LINEAR;
    if (!r) return;
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
    r->w->api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    r->w->api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    r->w->api.glBindTexture(GL_TEXTURE_2D, 0);
}

/* Mipmaps from the current pixels, minified trilinearly. */
void gq_texture_mipmaps(int64_t tex) {
    gq_res *r = gq_texture_of(tex, "Texture.mipmaps");
    if (!r) return;
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
    r->w->api.glGenerateMipmap(GL_TEXTURE_2D);
    r->w->api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    r->w->api.glBindTexture(GL_TEXTURE_2D, 0);
}

/* Bind to texture unit `unit`, where a sampler uniform set to `unit` reads it. */
void gq_texture_bind(int64_t tex, int64_t unit) {
    gq_res *r = gq_texture_of(tex, "Texture.bind");
    if (!r || unit < 0) return;
    r->w->api.glActiveTexture(GL_TEXTURE0 + (unsigned) unit);
    r->w->api.glBindTexture(GL_TEXTURE_2D, r->id);
}

/* 0: the width, 1: the height. */
int64_t gq_texture_size(int64_t tex, int64_t which) {
    gq_res *r = gq_res_of(tex);
    if (!r || r->kind != GQ_TEXTURE) return 0;
    return which == 0 ? r->width : r->height;
}

int64_t gq_framebuffer(int64_t win) {
    gq_window *w = gq_of(win);
    gq_res *r;
    if (!gq_use(w, "Window.framebuffer")) return 0;
    r = gq_new_res(w, GQ_FRAMEBUFFER);
    if (!r) return 0;
    w->api.glGenFramebuffers(1, &r->id);
    return (int64_t) (intptr_t) r;
}

/* Attach a texture: a depth texture as the depth buffer, any other as colour 0.  Without a
 * colour attachment the framebuffer draws and reads no colour (a depth-only pass). */
void gq_framebuffer_attach(int64_t fb, int64_t tex) {
    gq_res *f = gq_res_of(fb), *t = gq_res_of(tex);
    gq_api *gl;
    if (!f || !t || f->kind != GQ_FRAMEBUFFER || t->kind != GQ_TEXTURE) return;
    if (f->w != t->w) {
        fprintf(stderr, "graphics: Framebuffer.attach with a texture of another window\n");
        return;
    }
    if (!gq_use(f->w, "Framebuffer.attach")) return;
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
void gq_render_to(int64_t win, int64_t fb) {
    gq_window *w = gq_of(win);
    gq_res *f = gq_res_of(fb);
    unsigned id = 0;
    if (f) {
        if (f->kind != GQ_FRAMEBUFFER || f->w != w) {
            fprintf(stderr, "graphics: Window.render_to with a framebuffer of another window\n");
            return;
        }
        id = f->id;
    }
    if (!gq_use(w, "Window.render_to")) return;
    w->api.glBindFramebuffer(GL_FRAMEBUFFER, id);
    w->target = id;
}

/* Two triangles over the whole target: a vec2 position at location 0, a vec2 UV at 1. */
int64_t gq_quad(int64_t win) {
    static const float verts[24] = {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
                                    -1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
                                    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f};
    gq_window *w = gq_of(win);
    gq_res *r;
    gq_api *gl;
    if (!gq_use(w, "Window.quad")) return 0;
    r = gq_new_res(w, GQ_MESH);
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

int64_t gq_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    (void) width;
    (void) height;
    (void) title;
    (void) fullscreen;
    return 0;
}
int64_t gq_poll(int64_t h) {
    (void) h;
    return 0;
}
void gq_swap(int64_t h) {
    (void) h;
}
void gq_clear(int64_t h, int64_t color) {
    (void) h;
    (void) color;
}
void gq_close(int64_t h) {
    (void) h;
}
int64_t gq_width(int64_t h) {
    (void) h;
    return 0;
}
int64_t gq_height(int64_t h) {
    (void) h;
    return 0;
}
int64_t gq_event_count(int64_t h) {
    (void) h;
    return 0;
}
void gq_events(int64_t h, int64_t *out, int64_t n) {
    (void) h;
    (void) out;
    (void) n;
}
void gq_read_pixels(int64_t h, uint32_t *out, int64_t n) {
    (void) h;
    (void) out;
    (void) n;
}
int64_t gq_shader(int64_t win, const char *vertex, const char *fragment) {
    (void) win;
    (void) vertex;
    (void) fragment;
    return 0;
}
void gq_uniform_f(int64_t sh, const char *name, const double *v, int64_t n) {
    (void) sh;
    (void) name;
    (void) v;
    (void) n;
}
void gq_uniform_i(int64_t sh, const char *name, int64_t v) {
    (void) sh;
    (void) name;
    (void) v;
}
int64_t gq_mesh(int64_t win, const float *data, int64_t n, int64_t stride) {
    (void) win;
    (void) data;
    (void) n;
    (void) stride;
    return 0;
}
void gq_draw_over(int64_t mesh, int64_t shader) {
    (void) mesh;
    (void) shader;
}
void gq_draw(int64_t mesh, int64_t shader) {
    (void) mesh;
    (void) shader;
}
void gq_release(int64_t res) {
    (void) res;
}
void gq_capability(int64_t win, int64_t cap, int64_t on) {
    (void) win;
    (void) cap;
    (void) on;
}
int64_t gq_capability_on(int64_t win, int64_t cap) {
    (void) win;
    (void) cap;
    return 0;
}
void gq_blend(int64_t win, int64_t src, int64_t dst) {
    (void) win;
    (void) src;
    (void) dst;
}
void gq_cull(int64_t win, int64_t face) {
    (void) win;
    (void) face;
}
void gq_depth_write(int64_t win, int64_t on) {
    (void) win;
    (void) on;
}
void gq_viewport(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    (void) win;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
}
void gq_scissor(int64_t win, int64_t x, int64_t y, int64_t width, int64_t height) {
    (void) win;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
}
void gq_finish(int64_t win) {
    (void) win;
}
void gq_line_width(int64_t win, const double *width, int64_t n) {
    (void) win;
    (void) width;
    (void) n;
}
void gq_point_size(int64_t win, const double *size, int64_t n) {
    (void) win;
    (void) size;
    (void) n;
}

int64_t gq_texture(int64_t win, int64_t width, int64_t height, int64_t depth) {
    (void) win;
    (void) width;
    (void) height;
    (void) depth;
    return 0;
}
int64_t gq_texture_from(int64_t win, const uint32_t *px, int64_t n, int64_t width, int64_t height) {
    (void) win;
    (void) px;
    (void) n;
    (void) width;
    (void) height;
    return 0;
}
void gq_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                    const uint32_t *px, int64_t n) {
    (void) tex;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    (void) px;
    (void) n;
}
void gq_texture_filter(int64_t tex, int64_t nearest) {
    (void) tex;
    (void) nearest;
}
void gq_texture_mipmaps(int64_t tex) {
    (void) tex;
}
void gq_texture_bind(int64_t tex, int64_t unit) {
    (void) tex;
    (void) unit;
}
int64_t gq_texture_size(int64_t tex, int64_t which) {
    (void) tex;
    (void) which;
    return 0;
}
int64_t gq_framebuffer(int64_t win) {
    (void) win;
    return 0;
}
void gq_framebuffer_attach(int64_t fb, int64_t tex) {
    (void) fb;
    (void) tex;
}
void gq_render_to(int64_t win, int64_t fb) {
    (void) win;
    (void) fb;
}
int64_t gq_quad(int64_t win) {
    (void) win;
    return 0;
}

void gq_mesh_update(int64_t mesh, const float *data, int64_t n) {
    (void) mesh;
    (void) data;
    (void) n;
}
void gq_mesh_indices(int64_t mesh, const int64_t *idx, int64_t n) {
    (void) mesh;
    (void) idx;
    (void) n;
}
void gq_draw_as(int64_t mesh, int64_t shader, int64_t mode, int64_t indexed, int64_t instances) {
    (void) mesh;
    (void) shader;
    (void) mode;
    (void) indexed;
    (void) instances;
}
int64_t gq_instances(int64_t win, const float *data, int64_t n) {
    (void) win;
    (void) data;
    (void) n;
    return 0;
}
void gq_buffer_update(int64_t buf, const float *data, int64_t n) {
    (void) buf;
    (void) data;
    (void) n;
}
void gq_instance_attrib(int64_t mesh, int64_t buf, int64_t location, int64_t components,
                        int64_t stride, int64_t offset) {
    (void) mesh;
    (void) buf;
    (void) location;
    (void) components;
    (void) stride;
    (void) offset;
}

int64_t gq_text_size(int64_t h) {
    (void) h;
    return 0;
}
void gq_text_copy(int64_t h, uint8_t *out, int64_t n) {
    (void) h;
    (void) out;
    (void) n;
}
int64_t gq_key_down(int64_t h, int64_t key) {
    (void) h;
    (void) key;
    return 0;
}
int64_t gq_mouse(int64_t h, int64_t which) {
    (void) h;
    (void) which;
    return 0;
}

#endif
