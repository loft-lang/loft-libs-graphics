/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The Metal backend of `gpu`: an AppKit window with a `CAMetalLayer`, behind the table
 * gpu/c/gpu_backend.h declares.
 *
 * Everything is reached through the Objective-C runtime — libobjc, AppKit, Metal and QuartzCore
 * opened at run time, each message sent through `objc_msgSend` cast to the method's own type —
 * so this file builds with loft's fixed flags and no SDK.  On a machine that is not a Mac the
 * table is none.
 *
 * What a shader source for this backend follows (tag "msl", the Metal Shading Language):
 *   - the vertex function is `vertex_main`, the fragment function `fragment_main`; each source
 *     is compiled on its own, so both declare what they share;
 *   - the inputs read per vertex are `[[attribute(0)]]` onwards of the `[[stage_in]]` struct, in
 *     the program's order, the inputs read per copy follow them;
 *   - the uniform block is a struct at `[[buffer(0)]]` in either function;
 *   - texture slot n is `[[texture(n)]]` with its sampler at `[[sampler(n)]]`; a depth texture
 *     is a `depth2d<float>`;
 *   - the vertex function hands its position to `gpu_position(...)`, which this backend defines
 *     ahead of the source: it turns the layer's clip space (depth from -1 to 1, a target's first
 *     row at the bottom) into Metal's.
 *
 * The window and its events are AppKit as the graphics library's C drives it (its
 * `graphics_gl.c`, the GG_COCOA parts), with a Metal layer where that has an OpenGL context:
 * the same key codes, the same events, a size in points.  A window lives on the main thread;
 * Metal itself is called on the thread that draws.  Two windows may be drawn on two threads:
 * what they share is locked (`gm_shared`, `gm_made`).
 *
 * A window draws into a picture of its own, which a frame shows a copy of: it has no alpha, and
 * it is there to read before and after the frame was shown.
 *
 * Metal runs a pass's draws when the pass is handed over, and a texture write at once.  So that
 * every call takes effect where it is made, a texture written or given its smaller copies while
 * a pass reads it or draws into it has that pass handed over as far as it came (`gm_hand_over`);
 * the pass then goes on in a new encoder with all it was told (`gm_resume`). */
#include "gpu/gpu_backend.h"
#include <stddef.h>

#if defined(__APPLE__) && !defined(__wasm__)

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── The Objective-C runtime, AppKit and Metal, as much as this file uses ────────────────────── */
typedef void *gm_id;
typedef void *gm_sel;
typedef struct {
    double x, y;
} gm_point;
typedef struct {
    double x, y, width, height;
} gm_rect;
typedef struct {
    double width, height;
} gm_size; /* CGSize */
typedef struct {
    double r, g, b, a;
} gm_clear; /* MTLClearColor */
typedef struct {
    unsigned long x, y, z, width, height, depth;
} gm_region; /* MTLRegion: an origin and a size */
typedef struct {
    unsigned long x, y, z;
} gm_origin; /* MTLOrigin, and MTLSize */
typedef struct {
    double x, y, width, height, near, far;
} gm_viewport; /* MTLViewport */
typedef struct {
    unsigned long x, y, width, height;
} gm_scissor_rect; /* MTLScissorRect */

#define GM_STYLE_TITLED 1ul
#define GM_STYLE_CLOSABLE 2ul
#define GM_STYLE_MINIATURIZABLE 4ul
#define GM_STYLE_RESIZABLE 8ul
#define GM_BACKING_BUFFERED 2ul
#define GM_POLICY_REGULAR 0l
#define GM_MAIN_MENU_LEVEL 24l
#define GM_HIDE_DOCK (1ul << 1)
#define GM_HIDE_MENU_BAR (1ul << 3)

#define GM_LEFT_DOWN 1ul
#define GM_LEFT_UP 2ul
#define GM_RIGHT_DOWN 3ul
#define GM_RIGHT_UP 4ul
#define GM_MOVED 5ul
#define GM_LEFT_DRAGGED 6ul
#define GM_RIGHT_DRAGGED 7ul
#define GM_KEY_DOWN 10ul
#define GM_KEY_UP 11ul
#define GM_FLAGS_CHANGED 12ul
#define GM_SCROLL 22ul
#define GM_OTHER_DOWN 25ul
#define GM_OTHER_UP 26ul
#define GM_OTHER_DRAGGED 27ul

#define GM_FLAG_SHIFT (1ul << 17)
#define GM_FLAG_CONTROL (1ul << 18)
#define GM_FLAG_OPTION (1ul << 19)
#define GM_FLAG_COMMAND (1ul << 20)

/* Metal's numbers (MTLPixelFormat.h, MTLRenderPass.h, MTLRenderPipeline.h, …). */
#define GM_BGRA8 80ul        /* MTLPixelFormatBGRA8Unorm: a 0xAARRGGBB word, byte for byte */
#define GM_DEPTH32 252ul     /* MTLPixelFormatDepth32Float */
#define GM_USE_READ 1ul      /* MTLTextureUsageShaderRead */
#define GM_USE_TARGET 4ul    /* MTLTextureUsageRenderTarget */
#define GM_STORE_SHARED 0ul  /* MTLStorageModeShared */
#define GM_STORE_MANAGED 1ul
#define GM_STORE_PRIVATE 2ul
#define GM_LOAD_KEEP 1ul     /* MTLLoadActionLoad */
#define GM_LOAD_CLEAR 2ul
#define GM_STORE 1ul         /* MTLStoreActionStore */
#define GM_FLOAT1 28ul       /* MTLVertexFormatFloat; Float2, Float3 and Float4 follow it */
#define GM_PER_VERTEX 1ul    /* MTLVertexStepFunctionPerVertex */
#define GM_PER_COPY 2ul
#define GM_LESS 1ul          /* MTLCompareFunctionLess */
#define GM_ALWAYS 7ul
#define GM_CULL_FRONT 1ul    /* MTLCullModeFront */
#define GM_CULL_BACK 2ul
#define GM_CLOCKWISE 0ul     /* MTLWindingClockwise */
#define GM_COUNTER 1ul
#define GM_UINT32 1ul        /* MTLIndexTypeUInt32 */
#define GM_BUFFER_INFO 3ul   /* MTLPipelineOptionBindingInfo | BufferTypeInfo */
#define GM_WRITE_RGB 14ul    /* MTLColorWriteMaskRed | Green | Blue */
#define GM_ARG_BUFFER 0ul    /* MTLBindingTypeBuffer, MTLArgumentTypeBuffer */

/* The buffer slots of the vertex function: the block, the vertices, the values per copy. */
#define GM_SLOT_BLOCK 0ul
#define GM_SLOT_VERTEX 1ul
#define GM_SLOT_COPY 2ul
#define GM_MAX_TEXTURES 16

/* One queued input event: a tag (gpu.loft's `EVENT_WORDS`) and up to four integer fields. */
#define GM_EVENT_WORDS 5
#define GM_EV_KEY_DOWN 1
#define GM_EV_KEY_UP 2
#define GM_EV_TEXT 3
#define GM_EV_MOUSE_DOWN 4
#define GM_EV_MOUSE_UP 5
#define GM_EV_MOUSE_MOVE 6
#define GM_EV_WHEEL 7
#define GM_EV_RESIZE 11
#define GM_MOD_SHIFT 1
#define GM_MOD_CTRL 2
#define GM_MOD_ALT 4
#define GM_MOD_SUPER 8

/* A buffer, an index list, a texture, a target or a pipeline: what a handle names. */
enum { GM_BUFFER = 1, GM_INDEX = 2, GM_TEXTURE = 3, GM_TARGET = 4, GM_PIPE = 5 };
#define GM_MAGIC 0x6d746c31u
typedef struct gm_res {
    uint32_t magic;
    int kind;
    gm_id obj;   /* the MTLBuffer, MTLTexture or MTLRenderPipelineState */
    int64_t held; /* a texture: the targets that draw into it, and its own handle */
    /* a buffer: how many bytes it has room for, and one put aside while a frame still read it */
    unsigned long room, spare_room;
    gm_id spare, spare_used;
    /* a texture */
    int width, height, depth;
    int min_nearest, mag_nearest, mipped; /* how it is sampled */
    int levels;                           /* 1, or its whole chain once `mipmaps` was asked */
    gm_id used; /* the last command buffer that reads it or draws into it: a texture's `put`
                 * waits for it, a buffer's `update` writes elsewhere while it is in flight */
    /* a target */
    struct gm_res *colour, *zbuf;
    /* a pipeline */
    gm_id depth_state;
    gpu_state state;
    int vertex_n, instance_n, textures;
} gm_res;

typedef struct gm_window {
    gm_id window, view, layer;
    int fullscreen;
    pthread_t thread;
    struct gm_window *next_open; /* AppKit has one event queue for every window */
    int width, height, should_close, closed;
    int64_t *events;
    int64_t event_count, event_cap;
    char *text; /* the UTF-8 of the queued text events, end to end */
    int64_t text_len, text_cap;
    unsigned char keys[256];
    int mouse_x, mouse_y, buttons;
    /* the window's picture, which its passes draw into and a frame shows a copy of, and its
     * depth: both `depth_w` x `depth_h` */
    gm_id image, depth;
    int depth_w, depth_h;
    int hidden; /* nothing of the window is on a screen: no frame is asked for */
    gm_id last; /* the last command buffer committed: a capture waits for it */
    /* the pass in progress */
    gm_id cb, enc;
    int pass_w, pass_h, pass_window;
    gm_res *pass_colour, *pass_zbuf; /* a target's textures, held while the pass lasts */
    gm_res *pipe, *index;
    gm_res *vertex, *copy; /* the buffers the next draw reads, and where the copies' floats start */
    int64_t copy_first;
    gm_res *slot[GM_MAX_TEXTURES]; /* the texture named for each slot, held while it is */
    unsigned char *block;          /* the uniform block's bytes as last given */
    unsigned long block_bytes, block_room;
    int64_t port[4], cut[4]; /* the viewport and the scissor as last given */
    int cut_on;
    int recorded; /* a clear or a draw is recorded that the GPU was not handed yet */
    int paused;   /* its work was handed over and the pass goes on once `gm_resume` is called */
    int clipped_out;
} gm_window;

/* The runtime, bound once for the process: system libraries are never unloaded. */
static struct {
    int loaded; /* 1 bound, -1 could not be */
    void *objc, *appkit, *metal, *quartz;
    gm_id (*get_class)(const char *);
    gm_sel (*selector)(const char *);
    gm_id (*allocate_class)(gm_id, const char *, size_t);
    void (*register_class)(gm_id);
    signed char (*add_method)(gm_id, gm_sel, void (*)(void), const char *);
    void (*msg)(void);       /* objc_msgSend, cast at each call to the method's own type */
    void (*msg_stret)(void); /* objc_msgSend_stret: a returned rect on x86-64; none on arm64 */
    void *(*pool_push)(void);
    void (*pool_pop)(void *);
    void (*dispatch_sync_f)(void *, void *, void (*)(void *));
    void *main_queue;
    void *(*run_loop_main)(void);
    unsigned char (*run_loop_waiting)(void *);
    int main_served;
    gm_id app, run_loop_mode, full_class;
    gm_id device, queue;   /* the machine's GPU and the one queue its work goes through */
    int unified;           /* the GPU shares the program's memory: a texture is read where it is */
    gm_id samplers[2][2][2]; /* by [min nearest][mag nearest][mipped], made when first used */
    gm_window *open;
} gm;

/* What windows on two threads share is read and changed under a lock: the list of open windows
 * and what the main thread's event queue writes into each (`gm_shared`); the runtime's binding
 * and the samplers (`gm_made`). */
static pthread_mutex_t gm_shared = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gm_made = PTHREAD_MUTEX_INITIALIZER;

/* The next window after `w` (none: the first) that this thread opened.  What a thread makes is
 * named by that thread's passes only, and only that thread closes its windows. */
static gm_window *gm_own(gm_window *w) {
    pthread_mutex_lock(&gm_shared);
    for (w = w ? w->next_open : gm.open; w; w = w->next_open)
        if (pthread_equal(w->thread, pthread_self())) break;
    pthread_mutex_unlock(&gm_shared);
    return w;
}

/* A selector by its name: registered once a thread, then found by the name's own address (every
 * name here is a string in this file), so a message costs no lookup by text and no lock. */
static gm_sel gm_s(const char *name) {
    static __thread struct {
        const char *name;
        gm_sel sel;
    } known[512];
    size_t at = ((size_t) (uintptr_t) name >> 3) & 511u, tries;
    for (tries = 0; tries < 512; tries++, at = (at + 1) & 511u) {
        if (known[at].name == name) return known[at].sel;
        if (!known[at].name) {
            known[at].sel = gm.selector(name);
            known[at].name = name;
            return known[at].sel;
        }
    }
    return gm.selector(name);
}
static gm_id gm_class(const char *name) {
    return gm.get_class(name);
}
static gm_id gm_o(gm_id o, const char *s) {
    return ((gm_id (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s));
}
static gm_id gm_o_p(gm_id o, const char *s, const void *a) {
    return ((gm_id (*)(gm_id, gm_sel, const void *)) gm.msg)(o, gm_s(s), a);
}
static gm_id gm_o_l(gm_id o, const char *s, unsigned long a) {
    return ((gm_id (*)(gm_id, gm_sel, unsigned long)) gm.msg)(o, gm_s(s), a);
}
static void gm_v(gm_id o, const char *s) {
    ((void (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s));
}
static void gm_v_o(gm_id o, const char *s, gm_id a) {
    ((void (*)(gm_id, gm_sel, gm_id)) gm.msg)(o, gm_s(s), a);
}
static void gm_v_l(gm_id o, const char *s, unsigned long a) { /* an integer, enum or BOOL */
    ((void (*)(gm_id, gm_sel, unsigned long)) gm.msg)(o, gm_s(s), a);
}
static void gm_v_ol(gm_id o, const char *s, gm_id a, unsigned long b) {
    ((void (*)(gm_id, gm_sel, gm_id, unsigned long)) gm.msg)(o, gm_s(s), a, b);
}
static void gm_v_oll(gm_id o, const char *s, gm_id a, unsigned long b, unsigned long c) {
    ((void (*)(gm_id, gm_sel, gm_id, unsigned long, unsigned long)) gm.msg)(o, gm_s(s), a, b, c);
}
static unsigned long gm_l(gm_id o, const char *s) {
    return ((unsigned long (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s));
}
static int gm_b(gm_id o, const char *s) { /* a BOOL: only its low byte is defined */
    return ((signed char (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s)) != 0;
}
static double gm_d(gm_id o, const char *s) {
    return ((double (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s));
}
static gm_point gm_pt(gm_id o, const char *s) {
    return ((gm_point (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s));
}
static gm_rect gm_rc(gm_id o, const char *s) {
#if defined(__x86_64__)
    gm_rect r;
    ((void (*)(gm_rect *, gm_id, gm_sel)) gm.msg_stret)(&r, o, gm_s(s));
    return r;
#else
    return ((gm_rect (*)(gm_id, gm_sel)) gm.msg)(o, gm_s(s));
#endif
}
static int gm_responds(gm_id o, const char *s) {
    return ((signed char (*)(gm_id, gm_sel, gm_sel)) gm.msg)(o, gm_s("respondsToSelector:"),
                                                              gm_s(s)) != 0;
}
static gm_id gm_string(const char *text) {
    return gm_o_p(gm_class("NSString"), "stringWithUTF8String:", text);
}
static void gm_drop(gm_id *o) {
    if (*o) gm_v(*o, "release");
    *o = NULL;
}
static void gm_keep(gm_id *slot, gm_id o) {
    if (o) gm_v(o, "retain");
    gm_drop(slot);
    *slot = o;
}

/* A borderless window takes no keys unless its class says it can. */
static signed char gm_yes(gm_id self, gm_sel cmd) {
    (void) self;
    (void) cmd;
    return 1;
}

#define GM_SYM(lib, field, name) (*(void **) (&gm.field) = dlsym(gm.lib, name), gm.field != NULL)

/* Why the last window could not open: said once per process. */
static const char *gm_why;
static void gm_say_why(void) {
    static int said;
    if (said || !gm_why) return;
    said = 1;
    fprintf(stderr, "gpu_metal: no window — %s\n", gm_why);
}

/* Bind the runtime and find the GPU; 0 quietly on a Mac with no Metal device. */
static int gm_load_once(void) {
    void **mode;
    gm_id (*create)(void);
    if (gm.loaded) return gm.loaded > 0;
    gm.loaded = -1;
    gm_why = "the Objective-C runtime, AppKit, Metal or QuartzCore did not load";
    gm.objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW | RTLD_GLOBAL);
    gm.appkit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW | RTLD_GLOBAL);
    gm.metal = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_NOW | RTLD_GLOBAL);
    gm.quartz =
        dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_NOW | RTLD_GLOBAL);
    if (!gm.objc || !gm.appkit || !gm.metal || !gm.quartz) return 0;
    if (!(GM_SYM(objc, get_class, "objc_getClass") && GM_SYM(objc, selector, "sel_registerName") &&
          GM_SYM(objc, allocate_class, "objc_allocateClassPair") &&
          GM_SYM(objc, register_class, "objc_registerClassPair") &&
          GM_SYM(objc, add_method, "class_addMethod") && GM_SYM(objc, msg, "objc_msgSend") &&
          GM_SYM(objc, pool_push, "objc_autoreleasePoolPush") &&
          GM_SYM(objc, pool_pop, "objc_autoreleasePoolPop")))
        return 0;
#if defined(__x86_64__)
    if (!GM_SYM(objc, msg_stret, "objc_msgSend_stret")) return 0;
#endif
    *(void **) (&gm.dispatch_sync_f) = dlsym(RTLD_DEFAULT, "dispatch_sync_f");
    gm.main_queue = dlsym(RTLD_DEFAULT, "_dispatch_main_q");
    *(void **) (&gm.run_loop_main) = dlsym(RTLD_DEFAULT, "CFRunLoopGetMain");
    *(void **) (&gm.run_loop_waiting) = dlsym(RTLD_DEFAULT, "CFRunLoopIsWaiting");
    mode = (void **) dlsym(RTLD_DEFAULT, "NSDefaultRunLoopMode");
    if (!gm.dispatch_sync_f || !gm.main_queue || !gm.run_loop_main || !gm.run_loop_waiting ||
        !mode || !*mode)
        return 0;
    gm.run_loop_mode = *mode;
    *(void **) (&create) = dlsym(gm.metal, "MTLCreateSystemDefaultDevice");
    gm_why = NULL; /* a Mac with no Metal device is this backend's "not here", said by no one */
    gm.device = create ? create() : NULL;
    if (!gm.device) return 0;
    gm.queue = gm_o(gm.device, "newCommandQueue");
    if (!gm.queue) return 0;
    gm.unified = gm_responds(gm.device, "hasUnifiedMemory") && gm_b(gm.device, "hasUnifiedMemory");
    gm.loaded = 1;
    return 1;
}

static int gm_load(void) {
    int ok;
    pthread_mutex_lock(&gm_made);
    ok = gm_load_once();
    pthread_mutex_unlock(&gm_made);
    return ok;
}

/* Whether the main thread runs its run loop, which drains the main queue there. */
static int gm_main_served(void) {
    int i, served = __atomic_load_n(&gm.main_served, __ATOMIC_RELAXED);
    for (i = 0; !served && i < 200; i++) {
        if (gm.run_loop_waiting(gm.run_loop_main())) {
            served = 1;
            __atomic_store_n(&gm.main_served, 1, __ATOMIC_RELAXED);
        } else {
            usleep(10000);
        }
    }
    return served;
}

/* Run `job` on the main thread, where AppKit makes and drives windows, and wait for it: here
 * when this is the main thread, else through the main queue.  0 when no thread would run it. */
static int gm_on_main(void (*job)(void *), void *arg) {
    if (pthread_main_np()) {
        job(arg);
        return 1;
    }
    if (!gm_main_served()) {
        static int said;
        if (!said)
            fprintf(stderr, "gpu_metal: a window off the main thread needs the main thread in its "
                            "run loop, which this program's main does not run\n");
        said = 1;
        return 0;
    }
    gm.dispatch_sync_f(gm.main_queue, arg, (void (*)(void *)) job);
    return 1;
}

/* The application, once, on the main thread, and the fullscreen window's class. */
static int gm_app(void) {
    if (gm.app) return 1;
    gm_why = "no NSApplication";
    gm.app = gm_o(gm_class("NSApplication"), "sharedApplication");
    if (!gm.app) return 0;
    gm_v_l(gm.app, "setActivationPolicy:", GM_POLICY_REGULAR);
    gm_v(gm.app, "finishLaunching");
    gm.full_class = gm.allocate_class(gm_class("NSWindow"), "GMFullscreenWindow", 0);
    if (gm.full_class) {
#if defined(__x86_64__)
        const char *types = "c@:";
#else
        const char *types = "B@:";
#endif
        gm.add_method(gm.full_class, gm_s("canBecomeKeyWindow"), (void (*)(void)) gm_yes, types);
        gm.add_method(gm.full_class, gm_s("canBecomeMainWindow"), (void (*)(void)) gm_yes, types);
        gm.register_class(gm.full_class);
    } else {
        gm.full_class = gm_class("GMFullscreenWindow"); /* registered by an earlier load */
    }
    return 1;
}

static gm_window *gm_of(int64_t h) {
    return (gm_window *) (intptr_t) h;
}

static gm_res *gm_res_of(int64_t h, int kind) {
    gm_res *r = (gm_res *) (intptr_t) h;
    return r && r->magic == GM_MAGIC && r->kind == kind ? r : NULL;
}

/* Whether `w` is an open window used on the thread that opened it. */
static int gm_use(gm_window *w, const char *what) {
    if (!w || w->closed) return 0;
    if (pthread_equal(w->thread, pthread_self())) return 1;
    fprintf(stderr, "gpu_metal: %s called from a thread other than the one that opened the "
                    "window; a window is used on its own thread only\n", what);
    return 0;
}

/* ── events: AppKit's, in the codes every backend answers ──────────────────────────────────── */

static void gm_push(gm_window *w, int64_t tag, int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t *slot;
    if (w->event_count == w->event_cap) {
        int64_t cap = w->event_cap ? w->event_cap * 2 : 16;
        int64_t *grown = (int64_t *) realloc(w->events, (size_t) cap * GM_EVENT_WORDS * 8u);
        if (!grown) return;
        w->events = grown;
        w->event_cap = cap;
    }
    slot = w->events + w->event_count * GM_EVENT_WORDS;
    slot[0] = tag;
    slot[1] = a;
    slot[2] = b;
    slot[3] = c;
    slot[4] = d;
    w->event_count++;
}

static void gm_text_push(gm_window *w, const char *bytes, int n) {
    if (w->text_len + n > w->text_cap) {
        int64_t cap = (w->text_cap ? w->text_cap * 2 : 64) + n;
        char *grown = (char *) realloc(w->text, (size_t) cap);
        if (!grown) return;
        w->text = grown;
        w->text_cap = cap;
    }
    gm_push(w, GM_EV_TEXT, w->text_len, n, 0, 0);
    memcpy(w->text + w->text_len, bytes, (size_t) n);
    w->text_len += n;
}

/* A key's code from its virtual key code: the named keys by their place on the keyboard, a
 * character key by what it types with Shift alone, lower-cased; -1 for a key with no code. */
static int64_t gm_key_code(unsigned kc, gm_id ev) {
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
    chars = (const char *) gm_o(gm_o(ev, "charactersIgnoringModifiers"), "UTF8String");
    if (chars && chars[0] >= 0x20 && chars[0] <= 0x7e && chars[1] == 0)
        return chars[0] >= 'A' && chars[0] <= 'Z' ? chars[0] + 32 : chars[0];
    return -1;
}

static int64_t gm_mods(unsigned long flags) {
    return ((flags & GM_FLAG_SHIFT) ? GM_MOD_SHIFT : 0) | ((flags & GM_FLAG_CONTROL) ? GM_MOD_CTRL : 0) |
           ((flags & GM_FLAG_OPTION) ? GM_MOD_ALT : 0) | ((flags & GM_FLAG_COMMAND) ? GM_MOD_SUPER : 0);
}

/* A key: its press (a repeat when it is already down) and the text it typed, or its release.
 * Delete types a backspace, forward delete 0x7f, and the keys that type nothing (arrows,
 * F-keys: U+F700-U+F8FF in AppKit) no text. */
static void gm_key(gm_window *w, gm_id ev, int press) {
    unsigned kc = (unsigned) gm_l(ev, "keyCode");
    int64_t code = gm_key_code(kc, ev);
    int64_t mods = gm_mods(gm_l(ev, "modifierFlags"));
    if (!press) {
        if (code >= 0 && code < 256) w->keys[code] = 0;
        gm_push(w, GM_EV_KEY_UP, code, mods, 0, 0);
        return;
    }
    {
        int repeat = code >= 0 && code < 256 && w->keys[code];
        const char *text = (const char *) gm_o(gm_o(ev, "characters"), "UTF8String");
        size_t n = text ? strlen(text) : 0;
        if (code >= 0 && code < 256) w->keys[code] = 1;
        gm_push(w, GM_EV_KEY_DOWN, code, mods, repeat, 0);
        if (kc == 0x33) text = "\b", n = 1;
        else if (kc == 0x75) text = "\x7f", n = 1;
        else if (n == 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] >= 0x9C &&
                 (unsigned char) text[1] <= 0xA3)
            n = 0;
        if (n > 0) gm_text_push(w, text, (int) n);
    }
}

/* Shift, Control, Option, Command: AppKit reports them as the modifier state changing. */
static void gm_flags(gm_window *w, gm_id ev) {
    unsigned kc = (unsigned) gm_l(ev, "keyCode");
    unsigned long flags = gm_l(ev, "modifierFlags"), bit;
    int64_t code = gm_key_code(kc, NULL);
    int press;
    switch (kc) {
    case 0x38: case 0x3C: bit = GM_FLAG_SHIFT; break;
    case 0x3B: case 0x3E: bit = GM_FLAG_CONTROL; break;
    case 0x3A: case 0x3D: bit = GM_FLAG_OPTION; break;
    case 0x37: case 0x36: bit = GM_FLAG_COMMAND; break;
    default: return; /* Caps Lock, Fn */
    }
    press = (flags & bit) != 0;
    if (code >= 0 && code < 256) w->keys[code] = (unsigned char) press;
    gm_push(w, press ? GM_EV_KEY_DOWN : GM_EV_KEY_UP, code,
            gm_mods(press ? flags & ~bit : flags | bit), 0, 0);
}

static void gm_event(gm_window *w, gm_id ev, unsigned long type) {
    int x, y, bit = 0, inside;
    double dy;
    gm_point p;
    switch (type) {
    case GM_KEY_DOWN:
    case GM_KEY_UP:
        gm_key(w, ev, type == GM_KEY_DOWN);
        return;
    case GM_FLAGS_CHANGED:
        gm_flags(w, ev);
        return;
    default:
        break;
    }
    /* in pixels from the content's top left (AppKit's y runs up from the bottom) */
    p = gm_pt(ev, "locationInWindow");
    x = (int) floor(p.x);
    y = (int) floor((double) w->height - p.y);
    inside = x >= 0 && y >= 0 && x < w->width && y < w->height;
    switch (type) {
    case GM_LEFT_DOWN: case GM_LEFT_UP: bit = 1; break;
    case GM_RIGHT_DOWN: case GM_RIGHT_UP: bit = 2; break;
    case GM_OTHER_DOWN: case GM_OTHER_UP: bit = gm_l(ev, "buttonNumber") == 2 ? 4 : 0; break;
    case GM_MOVED: case GM_LEFT_DRAGGED: case GM_RIGHT_DRAGGED: case GM_OTHER_DRAGGED:
        if (!inside && !w->buttons) return; /* over the title bar */
        w->mouse_x = x;
        w->mouse_y = y;
        gm_push(w, GM_EV_MOUSE_MOVE, x, y, 0, 0);
        return;
    case GM_SCROLL:
        dy = gm_d(ev, "scrollingDeltaY");
        if (!inside || dy == 0.0) return;
        w->mouse_x = x;
        w->mouse_y = y;
        gm_push(w, GM_EV_WHEEL, dy > 0.0 ? 1 : -1, x, y, 0);
        return;
    default:
        return;
    }
    if (!bit) return;
    if (type == GM_LEFT_DOWN || type == GM_RIGHT_DOWN || type == GM_OTHER_DOWN) {
        if (!inside) return; /* the title bar or a border: the window's, not the program's */
        w->buttons |= bit;
        w->mouse_x = x;
        w->mouse_y = y;
        gm_push(w, GM_EV_MOUSE_DOWN, bit, x, y, 0);
    } else if (w->buttons & bit) { /* a release ends a press, wherever it lands */
        w->buttons &= ~bit;
        w->mouse_x = x;
        w->mouse_y = y;
        gm_push(w, GM_EV_MOUSE_UP, bit, x, y, 0);
    }
}

/* Take every queued event, each to the window it belongs to.  Keys are answered here and not
 * passed on (AppKit beeps at a key no view takes); everything else goes on to AppKit. */
static void gm_pump(void) {
    gm_id past = gm_o(gm_class("NSDate"), "distantPast");
    for (;;) {
        gm_id ev = ((gm_id (*)(gm_id, gm_sel, unsigned long, gm_id, gm_id, signed char)) gm.msg)(
            gm.app, gm_s("nextEventMatchingMask:untilDate:inMode:dequeue:"), ~0ul, past,
            gm.run_loop_mode, 1);
        unsigned long type;
        gm_id target;
        gm_window *w;
        int ours = 0;
        if (!ev) break;
        type = gm_l(ev, "type");
        target = gm_o(ev, "window");
        if (!target && (type == GM_KEY_DOWN || type == GM_KEY_UP || type == GM_FLAGS_CHANGED))
            target = gm_o(gm.app, "keyWindow");
        pthread_mutex_lock(&gm_shared);
        for (w = gm.open; w; w = w->next_open)
            if (w->window == target) {
                gm_event(w, ev, type);
                ours = 1;
                break;
            }
        pthread_mutex_unlock(&gm_shared);
        if (ours && (type == GM_KEY_DOWN || type == GM_KEY_UP)) continue;
        gm_v_o(gm.app, "sendEvent:", ev);
    }
    gm_v(gm.app, "updateWindows");
}

/* ── the window ────────────────────────────────────────────────────────────────────────────── */

typedef struct {
    gm_window *w;
    double width, height;
    const char *title;
    int fullscreen, ok;
} gm_open_job;

/* The main thread's half of opening: a titled window centred on the main screen, or for
 * `fullscreen` a borderless one over the whole screen; a Metal layer as its content view's
 * layer, sized in points as the window is. */
static void gm_open_main(void *arg) {
    gm_open_job *job = (gm_open_job *) arg;
    gm_window *w = job->w;
    void *pool;
    gm_rect content, bounds;
    gm_size size;
    unsigned long style = GM_STYLE_TITLED | GM_STYLE_CLOSABLE | GM_STYLE_MINIATURIZABLE |
                          GM_STYLE_RESIZABLE;
    gm_id cls;
    if (!gm_app()) return;
    pool = gm.pool_push();
    content.x = content.y = 0.0;
    content.width = job->width;
    content.height = job->height;
    cls = gm_class("NSWindow");
    if (job->fullscreen) {
        gm_id screen = gm_o(gm_class("NSScreen"), "mainScreen");
        gm_why = "no main screen for a fullscreen window";
        if (!screen || !gm.full_class) goto done;
        content = gm_rc(screen, "frame");
        style = 0;
        cls = gm.full_class;
        w->fullscreen = 1;
    }
    gm_why = "a window needs a positive size";
    if (content.width <= 0.0 || content.height <= 0.0) goto done;
    gm_why = "NSWindow did not initialise";
    w->window = ((gm_id (*)(gm_id, gm_sel, gm_rect, unsigned long, unsigned long, signed char)) gm.msg)(
        gm_o(cls, "alloc"), gm_s("initWithContentRect:styleMask:backing:defer:"), content, style,
        GM_BACKING_BUFFERED, 0);
    if (!w->window) goto done;
    gm_v_l(w->window, "setReleasedWhenClosed:", 0);
    gm_v_l(w->window, "setAcceptsMouseMovedEvents:", 1);
    gm_v_o(w->window, "setTitle:", gm_string(job->title));
    if (job->fullscreen) {
        gm_v_l(w->window, "setLevel:", GM_MAIN_MENU_LEVEL + 1);
        gm_v_l(gm.app, "setPresentationOptions:", GM_HIDE_DOCK | GM_HIDE_MENU_BAR);
    } else {
        gm_v(w->window, "center");
    }
    w->view = gm_o(w->window, "contentView");
    bounds = gm_rc(w->view, "bounds");
    w->width = (int) bounds.width;
    w->height = (int) bounds.height;
    gm_why = "the window's content view has no size";
    if (w->width <= 0 || w->height <= 0) goto done;
    gm_why = "no CAMetalLayer";
    w->layer = gm_o(gm_class("CAMetalLayer"), "layer");
    if (!w->layer) goto done;
    gm_v(w->layer, "retain");
    gm_v_o(w->layer, "setDevice:", gm.device);
    gm_v_l(w->layer, "setPixelFormat:", GM_BGRA8);
    gm_v_l(w->layer, "setFramebufferOnly:", 0); /* a frame is copied into, not drawn */
    gm_v_l(w->layer, "setOpaque:", 1);
    size.width = (double) w->width;
    size.height = (double) w->height;
    ((void (*)(gm_id, gm_sel, gm_size)) gm.msg)(w->layer, gm_s("setDrawableSize:"), size);
    gm_v_o(w->view, "setLayer:", w->layer);
    gm_v_l(w->view, "setWantsLayer:", 1);
    gm_v_o(w->window, "makeKeyAndOrderFront:", NULL);
    gm_v_l(gm.app, "activateIgnoringOtherApps:", 1);
    pthread_mutex_lock(&gm_shared);
    w->next_open = gm.open;
    gm.open = w;
    pthread_mutex_unlock(&gm_shared);
    gm_pump();
    job->ok = 1;
done:
    gm.pool_pop(pool);
}

/* Out of the list of open windows: no event is routed to it any more. */
static void gm_unlist(gm_window *w) {
    gm_window **link;
    pthread_mutex_lock(&gm_shared);
    for (link = &gm.open; *link; link = &(*link)->next_open)
        if (*link == w) {
            *link = w->next_open;
            break;
        }
    pthread_mutex_unlock(&gm_shared);
}

/* The main thread's half of closing: out of the routing list, the NSWindow. */
static void gm_close_main(void *arg) {
    gm_window *w = (gm_window *) arg;
    void *pool = gm.pool_push();
    gm_unlist(w);
    if (w->window) {
        gm_v(w->window, "close");
        gm_v(w->window, "release");
        w->window = NULL;
    }
    if (w->fullscreen && gm.app) gm_v_l(gm.app, "setPresentationOptions:", 0);
    gm.pool_pop(pool);
}

static void gm_end_pass(gm_window *w);

static void gm_free(gm_window *w) {
    void *pool = gm.pool_push();
    w->closed = 1;
    gm_end_pass(w);
    gm_drop(&w->last);
    gm_drop(&w->image);
    gm_drop(&w->depth);
    if (!gm_on_main(gm_close_main, w)) gm_unlist(w); /* no thread closes it: nothing routes to it */
    gm_drop(&w->layer);
    gm.pool_pop(pool);
    free(w->block);
    free(w->events);
    free(w->text);
    free(w);
}

static int64_t gm_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    gm_window *w;
    gm_open_job job;
    if (!gm_load()) {
        gm_say_why();
        return 0;
    }
    w = (gm_window *) calloc(1, sizeof(gm_window));
    if (!w) return 0;
    w->thread = pthread_self();
    job.w = w;
    job.width = (double) width;
    job.height = (double) height;
    job.title = title ? title : "";
    job.fullscreen = (int) fullscreen;
    job.ok = 0;
    if (!gm_on_main(gm_open_main, &job)) {
        free(w);
        return 0;
    }
    if (!job.ok) {
        gm_say_why();
        gm_free(w);
        return 0;
    }
    return (int64_t) (intptr_t) w;
}

static void gm_close(int64_t dev) {
    gm_window *w = gm_of(dev);
    if (gm_use(w, "close")) gm_free(w);
}

/* The main thread's half of a poll: every queued event taken, whether the close button was
 * pressed (a minimised window and one of a hidden application are still open), and the view's
 * size. */
static void gm_poll_main(void *arg) {
    gm_window *w = (gm_window *) arg;
    void *pool = gm.pool_push();
    gm_rect bounds;
    int width, height;
    int visible, small;
    gm_pump();
    visible = gm_b(w->window, "isVisible");
    small = gm_b(w->window, "isMiniaturized");
    if (!visible && !small && !gm_b(gm.app, "isHidden")) w->should_close = 1;
    w->hidden = !visible || small;
    bounds = gm_rc(w->view, "bounds");
    width = (int) bounds.width;
    height = (int) bounds.height;
    if (width > 0 && height > 0 && (width != w->width || height != w->height)) {
        w->width = width;
        w->height = height;
        pthread_mutex_lock(&gm_shared);
        gm_push(w, GM_EV_RESIZE, width, height, 0, 0);
        pthread_mutex_unlock(&gm_shared);
    }
    gm.pool_pop(pool);
}

/* Take the queued events; a resized window gets frames of its new size from the next one on. */
static int64_t gm_poll(int64_t dev) {
    gm_window *w = gm_of(dev);
    int width, height;
    if (!gm_use(w, "poll")) return 0;
    width = w->width;
    height = w->height;
    if (!gm_on_main(gm_poll_main, w)) return 0;
    if (w->width != width || w->height != height) {
        void *pool = gm.pool_push();
        gm_size size;
        size.width = (double) w->width;
        size.height = (double) w->height;
        ((void (*)(gm_id, gm_sel, gm_size)) gm.msg)(w->layer, gm_s("setDrawableSize:"), size);
        gm.pool_pop(pool);
    }
    return !w->should_close;
}

static int64_t gm_size_of(int64_t dev, int64_t which) {
    gm_window *w = gm_of(dev);
    if (!w || w->closed) return 0;
    return which ? w->height : w->width;
}

/* What the main thread's event queue writes into a window is read under the lock it is written
 * under: another window's poll may be taking this window's events. */
static int64_t gm_event_count(int64_t dev) {
    gm_window *w = gm_of(dev);
    int64_t n;
    if (!w || w->closed) return 0;
    pthread_mutex_lock(&gm_shared);
    n = w->event_count;
    pthread_mutex_unlock(&gm_shared);
    return n;
}

/* Copy the queued events out and empty the queue, with the text they point into. */
static void gm_events(int64_t dev, int64_t *out, int64_t n) {
    gm_window *w = gm_of(dev);
    int64_t words;
    if (!w || w->closed) return;
    pthread_mutex_lock(&gm_shared);
    words = w->event_count * GM_EVENT_WORDS;
    if (n < words) words = n / GM_EVENT_WORDS * GM_EVENT_WORDS;
    if (words > 0) memcpy(out, w->events, (size_t) words * 8u);
    w->event_count = 0;
    w->text_len = 0;
    pthread_mutex_unlock(&gm_shared);
}

static int64_t gm_key_down(int64_t dev, int64_t key) {
    gm_window *w = gm_of(dev);
    int64_t down;
    if (!w || w->closed || key < 0 || key >= 256) return 0;
    pthread_mutex_lock(&gm_shared);
    down = w->keys[key];
    pthread_mutex_unlock(&gm_shared);
    return down;
}

static int64_t gm_pointer(int64_t dev, int64_t which) {
    gm_window *w = gm_of(dev);
    int64_t at;
    if (!w || w->closed) return 0;
    pthread_mutex_lock(&gm_shared);
    at = which == 0 ? w->mouse_x : which == 1 ? w->mouse_y : w->buttons;
    pthread_mutex_unlock(&gm_shared);
    return at;
}

static int64_t gm_text_size(int64_t dev) {
    gm_window *w = gm_of(dev);
    int64_t n;
    if (!w || w->closed) return 0;
    pthread_mutex_lock(&gm_shared);
    n = w->text_len;
    pthread_mutex_unlock(&gm_shared);
    return n;
}

static void gm_text_copy(int64_t dev, uint8_t *out, int64_t n) {
    gm_window *w = gm_of(dev);
    if (!w || w->closed || n <= 0) return;
    pthread_mutex_lock(&gm_shared);
    if (w->text_len > 0) memcpy(out, w->text, (size_t) (n < w->text_len ? n : w->text_len));
    pthread_mutex_unlock(&gm_shared);
}

/* ── buffers, textures, targets ────────────────────────────────────────────────────────────── */

static gm_res *gm_new_res(int kind) {
    gm_res *r = (gm_res *) calloc(1, sizeof(gm_res));
    if (!r) return NULL;
    r->magic = GM_MAGIC;
    r->kind = kind;
    return r;
}

/* A Metal buffer holding `bytes` bytes of `data`; never an empty one, which Metal refuses. */
static gm_id gm_bytes(const void *data, int64_t bytes) {
    static const uint32_t none = 0;
    if (bytes <= 0 || !data) {
        data = &none;
        bytes = 4;
    }
    return ((gm_id (*)(gm_id, gm_sel, const void *, unsigned long, unsigned long)) gm.msg)(
        gm.device, gm_s("newBufferWithBytes:length:options:"), data, (unsigned long) bytes, 0ul);
}

/* Whether the GPU work `cb` (none: 0) has not finished: not handed over yet, or still running. */
static int gm_in_flight(gm_id cb) {
    return cb && gm_l(cb, "status") < 4ul; /* MTLCommandBufferStatusCompleted */
}

/* The pass in progress reads `r` (a buffer or a texture): remembered until that work is done. */
static void gm_reads(gm_window *w, gm_res *r) {
    if (r->used != w->cb) gm_keep(&r->used, w->cb);
}

/* Hand buffer `b` to the pass in progress in each place it was named for: as the vertices, as
 * the values per copy, or both. */
static void gm_feed(gm_window *w, gm_res *b) {
    if (w->vertex == b)
        gm_v_oll(w->enc, "setVertexBuffer:offset:atIndex:", b->obj, 0, GM_SLOT_VERTEX);
    if (w->copy == b)
        gm_v_oll(w->enc, "setVertexBuffer:offset:atIndex:", b->obj,
                 (unsigned long) w->copy_first * 4, GM_SLOT_COPY);
    gm_reads(w, b);
}

static int64_t gm_buffer(int64_t dev, const float *data, int64_t n) {
    gm_res *r;
    void *pool;
    if (!gm_use(gm_of(dev), "buffer") || n < 0) return 0;
    r = gm_new_res(GM_BUFFER);
    if (!r) return 0;
    pool = gm.pool_push();
    r->obj = gm_bytes(data, n * 4);
    r->room = (unsigned long) (n > 0 ? n * 4 : 4);
    gm.pool_pop(pool);
    return (int64_t) (intptr_t) r;
}

static int64_t gm_index_buffer(int64_t dev, const int64_t *idx, int64_t n) {
    gm_res *r;
    uint32_t *words;
    int64_t i;
    void *pool;
    if (!gm_use(gm_of(dev), "index_buffer") || n < 0) return 0;
    words = (uint32_t *) malloc((size_t) (n > 0 ? n : 1) * sizeof(uint32_t));
    if (!words) return 0;
    for (i = 0; i < n; i++) words[i] = (uint32_t) idx[i];
    r = gm_new_res(GM_INDEX);
    if (r) {
        pool = gm.pool_push();
        r->obj = gm_bytes(words, n * 4);
        gm.pool_pop(pool);
    }
    free(words);
    return (int64_t) (intptr_t) r;
}

/* New floats.  A frame already handed over keeps the ones it was drawn with: while one still
 * reads this buffer the new floats go into another — the one put aside on an earlier update,
 * once its own frame is done, or a new one — so floats replaced every frame move between two
 * buffers and none is made after the first frames. */
static void gm_buffer_update(int64_t buf, const float *data, int64_t n) {
    gm_res *r = gm_res_of(buf, GM_BUFFER);
    unsigned long bytes = (unsigned long) (n > 0 ? n * 4 : 0);
    gm_window *w;
    void *pool;
    if (!r || n < 0 || gm.loaded <= 0) return;
    pool = gm.pool_push();
    if (gm_in_flight(r->used)) {
        gm_id obj = r->obj, used = r->used;
        unsigned long room = r->room;
        r->obj = r->spare;
        r->used = r->spare_used;
        r->room = r->spare_room;
        r->spare = obj;
        r->spare_used = used;
        r->spare_room = room;
    }
    if (!r->obj || r->room < bytes || gm_in_flight(r->used)) {
        gm_drop(&r->obj); /* a frame that reads it keeps it */
        r->room = bytes + bytes / 2 > 4 ? bytes + bytes / 2 : 4; /* room to grow into */
        r->obj = ((gm_id (*)(gm_id, gm_sel, unsigned long, unsigned long)) gm.msg)(
            gm.device, gm_s("newBufferWithLength:options:"), r->room, 0ul);
    }
    gm_drop(&r->used);
    if (r->obj && bytes > 0) memcpy(gm_o(r->obj, "contents"), data, bytes);
    /* a pass that named this buffer reads the new floats from its next draw on */
    for (w = gm_own(NULL); r->obj && w; w = gm_own(w))
        if (w->enc && (w->vertex == r || w->copy == r)) gm_feed(w, r);
    gm.pool_pop(pool);
}

/* A texture of `levels` mip levels: colour (BGRA, 8 bits a channel) or depth (32-bit float). */
static gm_id gm_new_texture(int width, int height, int depth, int levels) {
    gm_id desc = ((gm_id (*)(gm_id, gm_sel, unsigned long, unsigned long, unsigned long, signed char)) gm.msg)(
        gm_class("MTLTextureDescriptor"),
        gm_s("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"),
        depth ? GM_DEPTH32 : GM_BGRA8, (unsigned long) width, (unsigned long) height, levels > 1);
    if (!desc) return NULL;
    gm_v_l(desc, "setUsage:", GM_USE_READ | GM_USE_TARGET);
    gm_v_l(desc, "setStorageMode:",
           depth ? GM_STORE_PRIVATE : gm.unified ? GM_STORE_SHARED : GM_STORE_MANAGED);
    return gm_o_p(gm.device, "newTextureWithDescriptor:", desc);
}

/* An empty colour texture (sampled between its pixels) or depth texture (sampled nearest); both
 * clamped at their edges. */
static int64_t gm_texture(int64_t dev, int64_t width, int64_t height, int64_t depth) {
    gm_res *r;
    void *pool;
    if (!gm_use(gm_of(dev), "texture") || width <= 0 || height <= 0) return 0;
    r = gm_new_res(GM_TEXTURE);
    if (!r) return 0;
    pool = gm.pool_push();
    r->obj = gm_new_texture((int) width, (int) height, depth != 0, 1);
    gm.pool_pop(pool);
    if (!r->obj) {
        free(r);
        return 0;
    }
    r->width = (int) width;
    r->height = (int) height;
    r->depth = depth != 0;
    r->min_nearest = r->mag_nearest = r->depth;
    r->levels = 1;
    r->held = 1;
    return (int64_t) (intptr_t) r;
}

static void gm_hand_over(gm_window *w);
static void gm_resume(gm_window *w);
static void gm_set_slot(gm_window *w, int slot);

/* The next window after `w` (none: the first) with a pass in progress that reads `t` or draws
 * into it. */
static gm_window *gm_touching(const gm_res *t, gm_window *w) {
    int i;
    while ((w = gm_own(w)) != NULL) {
        if (!w->enc) continue;
        if (t->used == w->cb || w->pass_colour == t || w->pass_zbuf == t) return w;
        for (i = 0; i < GM_MAX_TEXTURES; i++)
            if (w->slot[i] == t) return w;
    }
    return NULL;
}

/* Replace a `width` x `height` region at (x, y) of a colour texture: a 0xAARRGGBB word is the
 * texture's own four bytes.  A region that leaves the texture is not written.
 *
 * The pixels reach the texture through the GPU's own queue, copied from a buffer, never written
 * by the CPU: a texture the GPU drew into is not the CPU's to write on every Mac (on a virtual
 * one, a CPU write handed the GPU the whole texture as the CPU last saw it, and lost what the
 * pass had drawn).  What a pass in progress recorded is handed over first and the pass goes on
 * behind the copy, so a draw made before the put shows the old pixels and one made after it the
 * new ones: the queue runs its work in the order it was handed over. */
static void gm_texture_put(int64_t tex, int64_t x, int64_t y, int64_t width, int64_t height,
                           const uint32_t *px, int64_t n) {
    gm_res *r = gm_res_of(tex, GM_TEXTURE);
    gm_origin at, size;
    gm_id staged, cb, blit;
    gm_window *w;
    void *pool;
    if (!r || r->depth || width <= 0 || height <= 0 || n < width * height || gm.loaded <= 0) return;
    if (x < 0 || y < 0 || x + width > r->width || y + height > r->height) return;
    pool = gm.pool_push();
    staged = gm_bytes(px, width * height * 4);
    cb = staged ? gm_o(gm.queue, "commandBuffer") : NULL;
    blit = cb ? gm_o(cb, "blitCommandEncoder") : NULL;
    if (!blit) {
        gm.pool_pop(pool);
        return;
    }
    for (w = gm_touching(r, NULL); w; w = gm_touching(r, w)) gm_hand_over(w);
    at.x = (unsigned long) x;
    at.y = (unsigned long) y;
    at.z = 0;
    size.x = (unsigned long) width;
    size.y = (unsigned long) height;
    size.z = 1;
    ((void (*)(gm_id, gm_sel, gm_id, unsigned long, unsigned long, unsigned long, gm_origin, gm_id,
               unsigned long, unsigned long, gm_origin)) gm.msg)(
        blit,
        gm_s("copyFromBuffer:sourceOffset:sourceBytesPerRow:sourceBytesPerImage:sourceSize:"
             "toTexture:destinationSlice:destinationLevel:destinationOrigin:"),
        staged, 0ul, (unsigned long) width * 4ul, (unsigned long) (width * height) * 4ul, size,
        r->obj, 0ul, 0ul, at);
    gm_v(blit, "endEncoding");
    gm_v(cb, "commit");
    gm_keep(&r->used, cb);
    for (w = gm_own(NULL); w; w = gm_own(w))
        if (w->paused) {
            gm_keep(&w->last, cb);
            gm_resume(w);
        }
    gm.pool_pop(pool);
}

/* RGB565 values as 0xAARRGGBB, each opaque, a channel's high bits repeated in its low ones so
 * the brightest value is 255. */
static void gm_texture_put565(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                              const int32_t *px, int64_t n) {
    uint32_t *wide;
    int64_t i, count = w * h;
    if (count <= 0 || n < count) return;
    wide = (uint32_t *) malloc((size_t) count * sizeof(uint32_t));
    if (!wide) return;
    for (i = 0; i < count; i++) {
        uint32_t v = (uint32_t) px[i];
        uint32_t r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;
        wide[i] = 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
    }
    gm_texture_put(tex, x, y, w, h, wide, count);
    free(wide);
}

/* A pass in progress that named `t` for a slot reads it from its next draw on as it is now: its
 * pixels' own texture, sampled the way last asked. */
static void gm_renamed(gm_res *t) {
    gm_window *w;
    void *pool = gm.pool_push();
    int i;
    for (w = gm_own(NULL); w; w = gm_own(w))
        for (i = 0; w->enc && i < GM_MAX_TEXTURES; i++)
            if (w->slot[i] == t) gm_set_slot(w, i);
    gm.pool_pop(pool);
}

/* Nearest or between pixels, both when drawn larger and smaller: no smaller copies are read. */
static void gm_texture_nearest(int64_t tex, int64_t on) {
    gm_res *r = gm_res_of(tex, GM_TEXTURE);
    if (!r) return;
    r->min_nearest = r->mag_nearest = on != 0;
    r->mipped = 0;
    gm_renamed(r);
}

/* Smaller copies from the pixels it holds now, read between when it is drawn small.  A Metal
 * texture has its levels from the start, so the first call moves the pixels to one that has.
 * A pass in progress that reads the texture or draws into it is handed over first and goes on
 * behind the copies: they hold what was drawn before this call. */
static void gm_texture_mipmaps(int64_t tex) {
    gm_res *r = gm_res_of(tex, GM_TEXTURE);
    gm_id cb, blit, whole = NULL;
    gm_window *w;
    void *pool;
    if (!r || r->depth || gm.loaded <= 0) return;
    pool = gm.pool_push();
    if (r->levels == 1 && (r->width > 1 || r->height > 1)) {
        whole = gm_new_texture(r->width, r->height, 0, 2);
        if (!whole) {
            gm.pool_pop(pool);
            return;
        }
    }
    for (w = gm_touching(r, NULL); w; w = gm_touching(r, w)) gm_hand_over(w);
    cb = gm_o(gm.queue, "commandBuffer");
    blit = gm_o(cb, "blitCommandEncoder");
    if (whole) {
        gm_origin zero = {0, 0, 0}, size;
        size.x = (unsigned long) r->width;
        size.y = (unsigned long) r->height;
        size.z = 1;
        ((void (*)(gm_id, gm_sel, gm_id, unsigned long, unsigned long, gm_origin, gm_origin, gm_id,
                   unsigned long, unsigned long, gm_origin)) gm.msg)(
            blit,
            gm_s("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:"
                 "destinationSlice:destinationLevel:destinationOrigin:"),
            r->obj, 0ul, 0ul, zero, size, whole, 0ul, 0ul, zero);
        gm_drop(&r->obj);
        r->obj = whole;
        r->levels = 2;
    }
    if (r->levels > 1) gm_v_o(blit, "generateMipmapsForTexture:", r->obj);
    gm_v(blit, "endEncoding");
    gm_v(cb, "commit");
    gm_keep(&r->used, cb);
    r->min_nearest = 0;
    r->mipped = 1;
    for (w = gm_own(NULL); w; w = gm_own(w))
        if (w->paused) {
            gm_keep(&w->last, cb);
            gm_resume(w);
        }
    gm_renamed(r);
    gm.pool_pop(pool);
}

static void gm_texture_unhold(gm_res *t) {
    if (!t || --t->held > 0) return;
    gm_drop(&t->used);
    gm_drop(&t->obj);
    t->magic = 0;
    free(t);
}

static int64_t gm_target(int64_t dev, int64_t colour, int64_t depth) {
    gm_res *c = gm_res_of(colour, GM_TEXTURE), *z = gm_res_of(depth, GM_TEXTURE), *r;
    if (!gm_use(gm_of(dev), "target")) return 0;
    if ((c && c->depth) || (z && !z->depth) || (!c && !z)) return 0;
    r = gm_new_res(GM_TARGET);
    if (!r) return 0;
    r->colour = c;
    r->zbuf = z;
    if (c) c->held++;
    if (z) z->held++;
    r->width = c ? c->width : z->width;
    r->height = c ? c->height : z->height;
    return (int64_t) (intptr_t) r;
}

/* ── pipelines ─────────────────────────────────────────────────────────────────────────────── */

/* What every source starts with: Metal's library, and `gpu_position`, which turns a position in
 * the layer's clip space (y up, depth from -1 to 1) into Metal's (depth from 0 to 1).  A target
 * other than the window is drawn upside down, so that its first row — texture coordinate
 * (0, 0) — is the one drawn at clip (-1, -1), as the layer promises. */
static const char gm_prelude_window[] =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "static inline float4 gpu_position(float4 p) { p.z = (p.z + p.w) * 0.5; return p; }\n"
    "#line 1\n";
static const char gm_prelude_target[] =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "static inline float4 gpu_position(float4 p) { p.z = (p.z + p.w) * 0.5; p.y = -p.y; return p; }\n"
    "#line 1\n";

static const char *gm_error_text(gm_id err) {
    const char *text = err ? (const char *) gm_o(gm_o(err, "localizedDescription"), "UTF8String") : NULL;
    return text ? text : "no reason given";
}

/* Compile `source` and answer its function `entry` (retained); NULL, with the compiler's own
 * words on stderr, when it does not compile or has no such function. */
static gm_id gm_function(const char *prelude, const char *source, const char *entry,
                         const char *what) {
    size_t n = strlen(prelude) + strlen(source) + 1;
    char *all = (char *) malloc(n);
    gm_id err = NULL, lib, fn;
    if (!all) return NULL;
    snprintf(all, n, "%s%s", prelude, source);
    lib = ((gm_id (*)(gm_id, gm_sel, gm_id, gm_id, gm_id *)) gm.msg)(
        gm.device, gm_s("newLibraryWithSource:options:error:"), gm_string(all), NULL, &err);
    free(all);
    if (!lib) {
        fprintf(stderr, "gpu_metal: the %s shader does not compile:\n%s\n", what, gm_error_text(err));
        return NULL;
    }
    fn = gm_o_p(lib, "newFunctionWithName:", gm_string(entry));
    gm_v(lib, "release");
    if (!fn) fprintf(stderr, "gpu_metal: the %s shader has no function `%s`\n", what, entry);
    return fn;
}

/* Whether the uniform block one stage of the built pipeline reads (its buffer 0) is laid out as
 * the layer laid `p` out: the same size in whole vectors, every field starting where a declared
 * field starts.  Says both on stderr when not. */
static int gm_block_matches(gm_id reflection, const char *bindings, const char *arguments,
                            const gpu_program *p, const char *stage) {
    gm_id list = gm_responds(reflection, bindings) ? gm_o(reflection, bindings)
                                                   : gm_o(reflection, arguments);
    unsigned long count = list ? gm_l(list, "count") : 0, i, j, fields = 0, size = 0;
    unsigned long offsets[GPU_MAX_FIELDS * 2];
    gm_id block = NULL, members;
    int ok = 1;
    for (i = 0; i < count; i++) {
        gm_id a = gm_o_l(list, "objectAtIndex:", i);
        if (gm_l(a, "type") == GM_ARG_BUFFER && gm_l(a, "index") == GM_SLOT_BLOCK) block = a;
    }
    if (!block) return 1; /* this stage reads no block */
    if (p->block_n == 0) {
        fprintf(stderr, "gpu_metal: the %s shader reads a uniform block at buffer 0 and the program "
                        "declares none\n", stage);
        return 0;
    }
    size = gm_l(block, "bufferDataSize");
    members = gm_o(gm_o(block, "bufferStructType"), "members");
    fields = members ? gm_l(members, "count") : 0;
    if (fields > GPU_MAX_FIELDS * 2) fields = GPU_MAX_FIELDS * 2;
    for (i = 0; i < fields; i++) offsets[i] = gm_l(gm_o_l(members, "objectAtIndex:", i), "offset");
    if (!members || (size + 15) / 16 * 16 != (unsigned long) p->block_floats * 4) ok = 0;
    for (i = 0; ok && i < fields; i++) {
        int found = 0;
        for (j = 0; j < (unsigned long) p->block_n; j++) {
            if (offsets[i] == (unsigned long) p->block[j].offset * 4) found = 1;
        }
        if (!found) ok = 0;
    }
    if (ok) return 1;
    fprintf(stderr, "gpu_metal: the %s shader's uniform block is not laid out as the program "
                    "declares it.\n  declared: %d bytes, fields at", stage, (int) p->block_floats * 4);
    for (j = 0; j < (unsigned long) p->block_n; j++) fprintf(stderr, " %d", (int) p->block[j].offset * 4);
    fprintf(stderr, "\n  compiled: %lu bytes, fields at", size);
    for (i = 0; i < fields; i++) fprintf(stderr, " %lu", offsets[i]);
    fprintf(stderr, "\n");
    return 0;
}

/* The six factors gpu.loft names, as Metal numbers them. */
static unsigned long gm_blend_factor(int64_t f) {
    static const unsigned long factors[] = {0, 1, 4, 5, 8, 9};
    return f >= 0 && f <= 5 ? factors[f] : 1;
}

/* The inputs of a pipeline: attribute i at its place in the vertex's record (buffer 1), then
 * the values per copy (buffer 2). */
static gm_id gm_inputs(const gpu_program *p) {
    gm_id vd = gm_o(gm_class("MTLVertexDescriptor"), "vertexDescriptor");
    gm_id attributes = gm_o(vd, "attributes"), layouts = gm_o(vd, "layouts"), one;
    unsigned long at = 0, index = 0;
    int64_t i;
    for (i = 0; i < p->vertex_n; i++, index++) {
        one = gm_o_l(attributes, "objectAtIndexedSubscript:", index);
        gm_v_l(one, "setFormat:", GM_FLOAT1 + (unsigned long) p->vertex[i] - 1);
        gm_v_l(one, "setOffset:", at * 4);
        gm_v_l(one, "setBufferIndex:", GM_SLOT_VERTEX);
        at += (unsigned long) p->vertex[i];
    }
    if (p->vertex_n > 0) {
        one = gm_o_l(layouts, "objectAtIndexedSubscript:", GM_SLOT_VERTEX);
        gm_v_l(one, "setStride:", at * 4);
        gm_v_l(one, "setStepFunction:", GM_PER_VERTEX);
    }
    at = 0;
    for (i = 0; i < p->instance_n; i++, index++) {
        one = gm_o_l(attributes, "objectAtIndexedSubscript:", index);
        gm_v_l(one, "setFormat:", GM_FLOAT1 + (unsigned long) p->instance[i] - 1);
        gm_v_l(one, "setOffset:", at * 4);
        gm_v_l(one, "setBufferIndex:", GM_SLOT_COPY);
        at += (unsigned long) p->instance[i];
    }
    if (p->instance_n > 0) {
        one = gm_o_l(layouts, "objectAtIndexedSubscript:", GM_SLOT_COPY);
        gm_v_l(one, "setStride:", at * 4);
        gm_v_l(one, "setStepFunction:", GM_PER_COPY);
        gm_v_l(one, "setStepRate:", 1);
    }
    return vd;
}

static int64_t gm_pipeline(int64_t dev, const gpu_program *p, const gpu_state *s) {
    gm_res *r = NULL;
    gm_id vertex = NULL, fragment = NULL, desc, colour, err = NULL, reflection = NULL, state = NULL;
    gm_id depth_desc;
    int64_t i;
    int window = s->into == 0, has_colour = s->into != 3, has_depth = s->into != 1;
    const char *prelude = window ? gm_prelude_window : gm_prelude_target;
    void *pool;
    if (!gm_use(gm_of(dev), "pipeline")) return 0;
    for (i = 0; i < p->vertex_n; i++)
        if (p->vertex[i] < 1 || p->vertex[i] > 4) return 0;
    for (i = 0; i < p->instance_n; i++)
        if (p->instance[i] < 1 || p->instance[i] > 4) return 0;
    if (p->textures < 0 || p->textures > GM_MAX_TEXTURES) return 0;
    if (!strstr(p->vertex_source, "gpu_position(")) {
        fprintf(stderr, "gpu_metal: the vertex shader does not hand its position to "
                        "gpu_position(...), which turns the layer's clip space into Metal's\n");
        return 0;
    }
    pool = gm.pool_push();
    vertex = gm_function(prelude, p->vertex_source, "vertex_main", "vertex");
    if (vertex) fragment = gm_function(prelude, p->fragment_source, "fragment_main", "fragment");
    if (!vertex || !fragment) goto done;
    desc = gm_o(gm_o(gm_class("MTLRenderPipelineDescriptor"), "alloc"), "init");
    gm_v_o(desc, "setVertexFunction:", vertex);
    gm_v_o(desc, "setFragmentFunction:", fragment);
    gm_v_o(desc, "setVertexDescriptor:", gm_inputs(p));
    colour = gm_o_l(gm_o(desc, "colorAttachments"), "objectAtIndexedSubscript:", 0);
    gm_v_l(colour, "setPixelFormat:", has_colour ? GM_BGRA8 : 0);
    if (window) gm_v_l(colour, "setWriteMask:", GM_WRITE_RGB); /* a window has no alpha: it stays 1 */
    if (has_colour && s->blend) {
        unsigned long from = gm_blend_factor((s->blend - 1) / 8), to = gm_blend_factor((s->blend - 1) % 8);
        gm_v_l(colour, "setBlendingEnabled:", 1);
        gm_v_l(colour, "setSourceRGBBlendFactor:", from);
        gm_v_l(colour, "setSourceAlphaBlendFactor:", from);
        gm_v_l(colour, "setDestinationRGBBlendFactor:", to);
        gm_v_l(colour, "setDestinationAlphaBlendFactor:", to);
    }
    if (has_depth) gm_v_l(desc, "setDepthAttachmentPixelFormat:", GM_DEPTH32);
    state = ((gm_id (*)(gm_id, gm_sel, gm_id, unsigned long, gm_id *, gm_id *)) gm.msg)(
        gm.device, gm_s("newRenderPipelineStateWithDescriptor:options:reflection:error:"), desc,
        GM_BUFFER_INFO, &reflection, &err);
    gm_v(desc, "release");
    if (!state) {
        fprintf(stderr, "gpu_metal: the shader program does not link:\n%s\n", gm_error_text(err));
        goto done;
    }
    if (!reflection ||
        !gm_block_matches(reflection, "vertexBindings", "vertexArguments", p, "vertex") ||
        !gm_block_matches(reflection, "fragmentBindings", "fragmentArguments", p, "fragment")) {
        gm_drop(&state);
        goto done;
    }
    r = gm_new_res(GM_PIPE);
    if (!r) {
        gm_drop(&state);
        goto done;
    }
    r->obj = state;
    r->state = *s;
    r->vertex_n = (int) p->vertex_n;
    r->instance_n = (int) p->instance_n;
    r->textures = (int) p->textures;
    /* A draw is tested against a nearer depth and writes its own only where the target has one. */
    depth_desc = gm_o(gm_o(gm_class("MTLDepthStencilDescriptor"), "alloc"), "init");
    gm_v_l(depth_desc, "setDepthCompareFunction:", has_depth && s->depth ? GM_LESS : GM_ALWAYS);
    gm_v_l(depth_desc, "setDepthWriteEnabled:", has_depth && s->depth == 2);
    r->depth_state = gm_o_p(gm.device, "newDepthStencilStateWithDescriptor:", depth_desc);
    gm_v(depth_desc, "release");
done:
    gm_drop(&vertex);
    gm_drop(&fragment);
    gm.pool_pop(pool);
    return (int64_t) (intptr_t) r;
}

static void gm_release(int64_t h) {
    gm_res *r = (gm_res *) (intptr_t) h;
    gm_window *w;
    void *pool;
    if (!r || r->magic != GM_MAGIC || gm.loaded <= 0) return;
    pool = gm.pool_push();
    for (w = gm_own(NULL); w; w = gm_own(w)) {
        if (w->pipe == r) w->pipe = NULL;
        if (w->index == r) w->index = NULL;
        if (w->vertex == r) w->vertex = NULL;
        if (w->copy == r) w->copy = NULL;
    }
    if (r->kind == GM_TEXTURE) {
        gm_texture_unhold(r);
    } else {
        if (r->kind == GM_TARGET) {
            gm_texture_unhold(r->colour);
            gm_texture_unhold(r->zbuf);
        }
        gm_drop(&r->obj);
        gm_drop(&r->used);
        gm_drop(&r->spare);
        gm_drop(&r->spare_used);
        gm_drop(&r->depth_state);
        r->magic = 0;
        free(r);
    }
    gm.pool_pop(pool);
}

/* ── a frame ───────────────────────────────────────────────────────────────────────────────── */

/* The window's picture and a depth of its size, made when first drawn into and again when the
 * window has another size: opaque black, as a window nothing was drawn in. */
static int gm_frame(gm_window *w) {
    int width = w->width, height = w->height;
    uint32_t *black;
    size_t i, n;
    gm_region region;
    if (w->image && w->depth && w->depth_w == width && w->depth_h == height) return 1;
    gm_drop(&w->image);
    gm_drop(&w->depth);
    if (width <= 0 || height <= 0) return 0;
    n = (size_t) width * (size_t) height;
    black = (uint32_t *) malloc(n * sizeof(uint32_t));
    if (!black) return 0;
    for (i = 0; i < n; i++) black[i] = 0xFF000000u;
    w->image = gm_new_texture(width, height, 0, 1);
    w->depth = gm_new_texture(width, height, 1, 1);
    w->depth_w = width;
    w->depth_h = height;
    region.x = region.y = region.z = 0;
    region.width = (unsigned long) width;
    region.height = (unsigned long) height;
    region.depth = 1;
    if (w->image)
        ((void (*)(gm_id, gm_sel, gm_region, unsigned long, const void *, unsigned long)) gm.msg)(
            w->image, gm_s("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), region, 0ul, black,
            (unsigned long) width * 4ul);
    free(black);
    if (w->image && w->depth) return 1;
    gm_drop(&w->image);
    gm_drop(&w->depth);
    return 0;
}

/* One attachment of a pass: its texture, kept or cleared. */
static gm_id gm_attach(gm_id attachment, gm_id texture, int64_t load) {
    gm_v_o(attachment, "setTexture:", texture);
    gm_v_l(attachment, "setLoadAction:", load == 0 ? GM_LOAD_CLEAR : GM_LOAD_KEEP);
    gm_v_l(attachment, "setStoreAction:", GM_STORE);
    return attachment;
}

/* The rectangle (x, y, width, height), origin bottom-left in the layer's terms, as Metal counts
 * the pass's rows: a window's from its top, a target's first row being the layer's bottom. */
static int64_t gm_row(gm_window *w, int64_t y, int64_t height) {
    return w->pass_window ? (int64_t) w->pass_h - y - height : y;
}

/* What the pass was last told, said to its encoder: when it is told, and again when the pass
 * goes on in a new encoder. */
static void gm_set_viewport(gm_window *w) {
    gm_viewport v;
    v.x = (double) w->port[0];
    v.y = (double) gm_row(w, w->port[1], w->port[3]);
    v.width = (double) w->port[2];
    v.height = (double) w->port[3];
    v.near = 0.0;
    v.far = 1.0;
    ((void (*)(gm_id, gm_sel, gm_viewport)) gm.msg)(w->enc, gm_s("setViewport:"), v);
}

/* Metal takes a scissor only inside the target and never an empty one: the rectangle is cut to
 * the target, and one with nothing left draws nothing. */
static void gm_set_scissor(gm_window *w) {
    gm_scissor_rect r;
    int64_t x = 0, y = 0, width = w->pass_w, height = w->pass_h, x1, y1;
    if (w->cut_on) {
        x = w->cut[0];
        y = w->cut[1];
        width = w->cut[2];
        height = w->cut[3];
    }
    y = gm_row(w, y, height);
    x1 = x + width;
    y1 = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > w->pass_w) x1 = w->pass_w;
    if (y1 > w->pass_h) y1 = w->pass_h;
    w->clipped_out = x1 <= x || y1 <= y;
    if (w->clipped_out) return;
    r.x = (unsigned long) x;
    r.y = (unsigned long) y;
    r.width = (unsigned long) (x1 - x);
    r.height = (unsigned long) (y1 - y);
    ((void (*)(gm_id, gm_sel, gm_scissor_rect)) gm.msg)(w->enc, gm_s("setScissorRect:"), r);
}

static void gm_set_pipe(gm_window *w) {
    const gm_res *p = w->pipe;
    gm_v_o(w->enc, "setRenderPipelineState:", p->obj);
    gm_v_o(w->enc, "setDepthStencilState:", p->depth_state);
    /* a target is drawn upside down (gm_prelude_target), which turns every face over */
    gm_v_l(w->enc, "setFrontFacingWinding:", w->pass_window ? GM_COUNTER : GM_CLOCKWISE);
    gm_v_l(w->enc, "setCullMode:", p->state.cull == 1 ? GM_CULL_BACK : p->state.cull == 2 ? GM_CULL_FRONT : 0);
}

/* The block's bytes to both stages: copied into the frame, or a buffer of their own past the
 * size Metal copies. */
static void gm_set_block(gm_window *w) {
    if (w->block_bytes <= 4096) {
        ((void (*)(gm_id, gm_sel, const void *, unsigned long, unsigned long)) gm.msg)(
            w->enc, gm_s("setVertexBytes:length:atIndex:"), w->block, w->block_bytes, GM_SLOT_BLOCK);
        ((void (*)(gm_id, gm_sel, const void *, unsigned long, unsigned long)) gm.msg)(
            w->enc, gm_s("setFragmentBytes:length:atIndex:"), w->block, w->block_bytes, GM_SLOT_BLOCK);
    } else {
        gm_id held = gm_bytes(w->block, (int64_t) w->block_bytes);
        gm_v_oll(w->enc, "setVertexBuffer:offset:atIndex:", held, 0, GM_SLOT_BLOCK);
        gm_v_oll(w->enc, "setFragmentBuffer:offset:atIndex:", held, 0, GM_SLOT_BLOCK);
        gm_drop(&held); /* the frame keeps it */
    }
}

/* How a texture is sampled is the texture's own: the sampler of its three settings. */
static gm_id gm_sampler(const gm_res *t) {
    gm_id *slot = &gm.samplers[t->min_nearest][t->mag_nearest][t->mipped && t->levels > 1], made;
    pthread_mutex_lock(&gm_made);
    if (!*slot) {
        gm_id desc = gm_o(gm_o(gm_class("MTLSamplerDescriptor"), "alloc"), "init");
        gm_v_l(desc, "setMinFilter:", t->min_nearest ? 0 : 1);
        gm_v_l(desc, "setMagFilter:", t->mag_nearest ? 0 : 1);
        gm_v_l(desc, "setMipFilter:", t->mipped && t->levels > 1 ? 2 : 0);
        *slot = gm_o_p(gm.device, "newSamplerStateWithDescriptor:", desc); /* clamped: the default */
        gm_v(desc, "release");
    }
    made = *slot;
    pthread_mutex_unlock(&gm_made);
    return made;
}

static void gm_set_slot(gm_window *w, int slot) {
    gm_res *t = w->slot[slot];
    gm_id sampler;
    if (!t) return;
    sampler = gm_sampler(t);
    gm_v_ol(w->enc, "setVertexTexture:atIndex:", t->obj, (unsigned long) slot);
    gm_v_ol(w->enc, "setFragmentTexture:atIndex:", t->obj, (unsigned long) slot);
    gm_v_ol(w->enc, "setVertexSamplerState:atIndex:", sampler, (unsigned long) slot);
    gm_v_ol(w->enc, "setFragmentSamplerState:atIndex:", sampler, (unsigned long) slot);
    gm_reads(w, t);
}

/* Start recording into what the pass draws into — a target's textures or the window's picture —
 * cleared to `colour` or kept.  A window is cleared opaque. */
static int gm_record(gm_window *w, int64_t load, int64_t colour) {
    gm_id pass, attachment, cb, enc, colour_texture, depth_texture;
    uint32_t c = (uint32_t) colour;
    if (w->pass_window) {
        colour_texture = w->image;
        depth_texture = w->depth;
    } else {
        colour_texture = w->pass_colour ? w->pass_colour->obj : NULL;
        depth_texture = w->pass_zbuf ? w->pass_zbuf->obj : NULL;
    }
    pass = gm_o(gm_class("MTLRenderPassDescriptor"), "renderPassDescriptor");
    if (colour_texture) {
        gm_clear to;
        to.r = (double) ((c >> 16) & 0xFF) / 255.0;
        to.g = (double) ((c >> 8) & 0xFF) / 255.0;
        to.b = (double) (c & 0xFF) / 255.0;
        to.a = w->pass_window ? 1.0 : (double) ((c >> 24) & 0xFF) / 255.0;
        attachment = gm_attach(gm_o_l(gm_o(pass, "colorAttachments"), "objectAtIndexedSubscript:", 0),
                               colour_texture, load);
        ((void (*)(gm_id, gm_sel, gm_clear)) gm.msg)(attachment, gm_s("setClearColor:"), to);
    }
    if (depth_texture) {
        attachment = gm_attach(gm_o(pass, "depthAttachment"), depth_texture, load);
        ((void (*)(gm_id, gm_sel, double)) gm.msg)(attachment, gm_s("setClearDepth:"), 1.0);
    }
    cb = gm_o(gm.queue, "commandBuffer");
    enc = cb ? gm_o_p(cb, "renderCommandEncoderWithDescriptor:", pass) : NULL;
    if (!enc) return 0;
    gm_keep(&w->cb, cb);
    gm_keep(&w->enc, enc);
    if (w->pass_colour) gm_keep(&w->pass_colour->used, cb);
    if (w->pass_zbuf) gm_keep(&w->pass_zbuf->used, cb);
    w->recorded = load == 0;
    w->paused = 0;
    return 1;
}

/* The pass is over: nothing it named is held for it any more. */
static void gm_pass_done(gm_window *w) {
    int i;
    gm_texture_unhold(w->pass_colour);
    gm_texture_unhold(w->pass_zbuf);
    w->pass_colour = w->pass_zbuf = NULL;
    for (i = 0; i < GM_MAX_TEXTURES; i++) {
        gm_texture_unhold(w->slot[i]);
        w->slot[i] = NULL;
    }
    w->pipe = NULL;
    w->index = NULL;
    w->vertex = w->copy = NULL;
    w->block_bytes = 0;
    w->recorded = w->paused = 0;
}

/* Hand what the pass recorded to the GPU.  The pass itself goes on (`gm_resume`) or is over. */
static void gm_hand_over(gm_window *w) {
    if (!w->enc) return;
    gm_v(w->enc, "endEncoding");
    gm_v(w->cb, "commit");
    gm_keep(&w->last, w->cb);
    gm_drop(&w->enc);
    gm_drop(&w->cb);
    w->recorded = 0;
    w->paused = 1;
}

/* Go on with a pass whose work was handed over: what it drew is kept, and what it was told —
 * its pipeline, block, textures, buffers, viewport and scissor — holds for its next draw. */
static void gm_resume(gm_window *w) {
    int i;
    if (!w->paused) return;
    if (!gm_record(w, 1, 0)) {
        gm_pass_done(w);
        return;
    }
    if (w->pipe) gm_set_pipe(w);
    if (w->pipe && w->block_bytes) gm_set_block(w);
    for (i = 0; i < GM_MAX_TEXTURES; i++) gm_set_slot(w, i);
    if (w->vertex) gm_feed(w, w->vertex);
    if (w->copy && w->copy != w->vertex) gm_feed(w, w->copy);
    gm_set_viewport(w);
    gm_set_scissor(w);
}

static void gm_begin(int64_t dev, int64_t target, int64_t load, int64_t colour) {
    gm_window *w = gm_of(dev);
    gm_res *t = gm_res_of(target, GM_TARGET);
    void *pool;
    if (!gm_use(w, "begin") || w->enc || (target && !t)) return;
    pool = gm.pool_push();
    if (t) {
        w->pass_w = t->width;
        w->pass_h = t->height;
    } else {
        if (!gm_frame(w)) {
            gm.pool_pop(pool);
            return;
        }
        w->pass_w = w->depth_w;
        w->pass_h = w->depth_h;
    }
    w->pass_window = t == NULL;
    w->pass_colour = t ? t->colour : NULL;
    w->pass_zbuf = t ? t->zbuf : NULL;
    if (w->pass_colour) w->pass_colour->held++;
    if (w->pass_zbuf) w->pass_zbuf->held++;
    if (gm_record(w, load, colour)) {
        w->port[0] = w->port[1] = 0;
        w->port[2] = w->pass_w;
        w->port[3] = w->pass_h;
        w->cut_on = 0;
        w->clipped_out = 0;
        gm_set_viewport(w);
    } else {
        gm_pass_done(w);
    }
    gm.pool_pop(pool);
}

static void gm_bind(int64_t dev, int64_t pipeline) {
    gm_window *w = gm_of(dev);
    gm_res *p = gm_res_of(pipeline, GM_PIPE);
    void *pool;
    if (!gm_use(w, "bind") || !w->enc || !p) return;
    pool = gm.pool_push();
    w->pipe = p;
    w->index = NULL;
    w->vertex = w->copy = NULL;
    gm_set_pipe(w);
    gm.pool_pop(pool);
}

static void gm_uniforms(int64_t dev, const float *block, int64_t n) {
    gm_window *w = gm_of(dev);
    unsigned long bytes = (unsigned long) n * 4;
    void *pool;
    if (!gm_use(w, "uniforms") || !w->enc || !w->pipe || n <= 0) return;
    if (w->block_room < bytes) {
        unsigned char *grown = (unsigned char *) realloc(w->block, bytes);
        if (!grown) return;
        w->block = grown;
        w->block_room = bytes;
    }
    memcpy(w->block, block, bytes);
    w->block_bytes = bytes;
    pool = gm.pool_push();
    gm_set_block(w);
    gm.pool_pop(pool);
}

static void gm_sample(int64_t dev, int64_t slot, int64_t tex) {
    gm_window *w = gm_of(dev);
    gm_res *t = gm_res_of(tex, GM_TEXTURE);
    void *pool;
    if (!gm_use(w, "sample") || !w->enc || !t || slot < 0 || slot >= GM_MAX_TEXTURES) return;
    pool = gm.pool_push();
    t->held++;
    gm_texture_unhold(w->slot[slot]);
    w->slot[slot] = t;
    gm_set_slot(w, (int) slot);
    gm.pool_pop(pool);
}

static void gm_vertices(int64_t dev, int64_t buf) {
    gm_window *w = gm_of(dev);
    gm_res *b = gm_res_of(buf, GM_BUFFER);
    if (!gm_use(w, "vertices") || !w->enc || !w->pipe || !b || !b->obj) return;
    w->vertex = b;
    gm_feed(w, b);
}

static void gm_instances(int64_t dev, int64_t buf, int64_t first_float) {
    gm_window *w = gm_of(dev);
    gm_res *b = gm_res_of(buf, GM_BUFFER);
    if (!gm_use(w, "instances") || !w->enc || !w->pipe || !b || !b->obj || first_float < 0) return;
    w->copy = b;
    w->copy_first = first_float;
    gm_feed(w, b);
}

static void gm_indices(int64_t dev, int64_t buf) {
    gm_window *w = gm_of(dev);
    gm_res *b = gm_res_of(buf, GM_INDEX);
    if (!gm_use(w, "indices") || !w->enc || !w->pipe || !b) return;
    w->index = b;
}

static void gm_viewport_call(int64_t dev, int64_t x, int64_t y, int64_t width, int64_t height) {
    gm_window *w = gm_of(dev);
    if (!gm_use(w, "viewport") || !w->enc || width < 0 || height < 0) return;
    w->port[0] = x;
    w->port[1] = y;
    w->port[2] = width;
    w->port[3] = height;
    gm_set_viewport(w);
}

static void gm_scissor(int64_t dev, int64_t x, int64_t y, int64_t width, int64_t height, int64_t on) {
    gm_window *w = gm_of(dev);
    if (!gm_use(w, "scissor") || !w->enc) return;
    w->cut[0] = x;
    w->cut[1] = y;
    w->cut[2] = width;
    w->cut[3] = height;
    w->cut_on = on != 0;
    gm_set_scissor(w);
}

static void gm_draw(int64_t dev, int64_t first, int64_t count, int64_t indexed, int64_t copies) {
    gm_window *w = gm_of(dev);
    unsigned long shape, times = copies > 0 ? (unsigned long) copies : 1ul;
    if (!gm_use(w, "draw") || !w->enc || !w->pipe || count <= 0 || first < 0 || w->clipped_out) return;
    if (!w->vertex || (w->pipe->instance_n > 0 && !w->copy)) return; /* Metal reads every input of every draw */
    shape = w->pipe->state.shape == 1 ? 1ul : w->pipe->state.shape == 2 ? 0ul : 3ul;
    if (indexed) {
        if (!w->index) return;
        ((void (*)(gm_id, gm_sel, unsigned long, unsigned long, unsigned long, gm_id, unsigned long,
                   unsigned long)) gm.msg)(
            w->enc,
            gm_s("drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"
                 "instanceCount:"),
            shape, (unsigned long) count, GM_UINT32, w->index->obj, 0ul, times);
    } else {
        ((void (*)(gm_id, gm_sel, unsigned long, unsigned long, unsigned long, unsigned long)) gm.msg)(
            w->enc, gm_s("drawPrimitives:vertexStart:vertexCount:instanceCount:"), shape,
            (unsigned long) first, (unsigned long) count, times);
    }
    w->recorded = 1;
}

/* End the pass in progress and hand its work to the GPU. */
static void gm_end_pass(gm_window *w) {
    if (!w->enc && !w->paused) return;
    gm_hand_over(w);
    gm_pass_done(w);
}

static void gm_end(int64_t dev) {
    gm_window *w = gm_of(dev);
    void *pool;
    if (!gm_use(w, "end")) return;
    pool = gm.pool_push();
    gm_end_pass(w);
    gm.pool_pop(pool);
}

/* Show the frame: a copy of the window's picture, which the window keeps.  The layer waits for
 * the display's next frame before it hands out another; a window that is on no screen is shown
 * nothing and waits about a frame instead. */
static void gm_present(int64_t dev) {
    gm_window *w = gm_of(dev);
    gm_id cb, blit, drawable, texture;
    void *pool;
    if (!gm_use(w, "present") || w->enc || !w->image) return;
    if (w->hidden) {
        usleep(16000);
        return;
    }
    pool = gm.pool_push();
    drawable = gm_o(w->layer, "nextDrawable");
    texture = drawable ? gm_o(drawable, "texture") : NULL;
    /* a frame of another size than the picture's: the window was resized, the next one fits */
    if (texture && (int) gm_l(texture, "width") == w->depth_w && (int) gm_l(texture, "height") == w->depth_h) {
        gm_origin zero = {0, 0, 0}, size;
        size.x = (unsigned long) w->depth_w;
        size.y = (unsigned long) w->depth_h;
        size.z = 1;
        cb = gm_o(gm.queue, "commandBuffer");
        blit = gm_o(cb, "blitCommandEncoder");
        ((void (*)(gm_id, gm_sel, gm_id, unsigned long, unsigned long, gm_origin, gm_origin, gm_id,
                   unsigned long, unsigned long, gm_origin)) gm.msg)(
            blit,
            gm_s("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:"
                 "destinationSlice:destinationLevel:destinationOrigin:"),
            w->image, 0ul, 0ul, zero, size, texture, 0ul, 0ul, zero);
        gm_v(blit, "endEncoding");
        gm_v_o(cb, "presentDrawable:", drawable);
        gm_v(cb, "commit");
        gm_keep(&w->last, cb);
    }
    gm.pool_pop(pool);
}

/* The pixels of `target`'s colour, or of the window's picture, 0xAARRGGBB, the top row first.  A
 * window's are opaque, and the same before and after the frame was shown.  A target's first row
 * is the layer's bottom one. */
static void gm_capture(int64_t dev, int64_t target, uint32_t *out, int64_t n) {
    gm_window *w = gm_of(dev);
    gm_res *t = gm_res_of(target, GM_TARGET);
    gm_id texture, pending;
    gm_region region;
    int64_t width, height;
    void *pool;
    if (!gm_use(w, "capture") || w->enc || n <= 0 || (target && !t)) return;
    pool = gm.pool_push();
    if (t) {
        texture = t->colour ? t->colour->obj : NULL;
    } else {
        texture = gm_frame(w) ? w->image : NULL;
    }
    width = texture ? (int64_t) gm_l(texture, "width") : 0;
    height = texture ? (int64_t) gm_l(texture, "height") : 0;
    if (!texture || n != width * height) {
        gm.pool_pop(pool);
        return;
    }
    pending = t ? t->colour->used : w->last;
    if (gm_l(texture, "storageMode") == GM_STORE_MANAGED) { /* the GPU's copy, brought back */
        gm_id cb = gm_o(gm.queue, "commandBuffer"), blit = gm_o(cb, "blitCommandEncoder");
        gm_v_o(blit, "synchronizeResource:", texture);
        gm_v(blit, "endEncoding");
        gm_v(cb, "commit");
        pending = cb;
    }
    /* the queue runs its work in the order it was handed over */
    if (pending && gm_l(pending, "status") >= 2ul) gm_v(pending, "waitUntilCompleted");
    region.x = region.y = region.z = 0;
    region.width = (unsigned long) width;
    region.height = (unsigned long) height;
    region.depth = 1;
    ((void (*)(gm_id, gm_sel, void *, unsigned long, gm_region, unsigned long)) gm.msg)(
        texture, gm_s("getBytes:bytesPerRow:fromRegion:mipmapLevel:"), out,
        (unsigned long) width * 4ul, region, 0ul);
    if (t) {
        int64_t y, x;
        for (y = 0; y < height / 2; y++) {
            uint32_t *top = out + y * width, *bottom = out + (height - 1 - y) * width;
            for (x = 0; x < width; x++) {
                uint32_t swap = top[x];
                top[x] = bottom[x];
                bottom[x] = swap;
            }
        }
    }
    gm.pool_pop(pool);
}

static const gpu_backend gm_tab = {
    GPU_BACKEND_VERSION, "gpu_metal", "msl",
    gm_open, gm_poll, gm_event_count, gm_events, gm_size_of, gm_close,
    gm_key_down, gm_pointer, gm_text_size, gm_text_copy,
    gm_buffer, gm_index_buffer, gm_buffer_update, gm_texture, gm_texture_put, gm_texture_put565,
    gm_texture_nearest, gm_texture_mipmaps, gm_target, gm_pipeline, gm_release,
    gm_begin, gm_bind, gm_uniforms, gm_sample, gm_vertices, gm_instances,
    gm_indices, gm_viewport_call, gm_scissor, gm_draw, gm_end, gm_present, gm_capture,
};

/* This backend's table: Metal is looked for when a device opens, so the table is there on every
 * Mac, and `open` answers 0 on one with no Metal device. */
int64_t gm_table(void) {
    return (int64_t) (intptr_t) &gm_tab;
}

#else /* not a Mac: no Metal */

int64_t gm_table(void) {
    return 0;
}

#endif
