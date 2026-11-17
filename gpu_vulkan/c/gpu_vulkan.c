/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The Vulkan backend of `gpu`, on Linux (X11), behind the table gpu/c/gpu_backend.h declares.
 *
 * `libvulkan.so.1` and `libX11.so.6` are opened when a device opens, with the part of each this
 * file uses declared beside it (vk_decl.h, checked against the Khronos header by check_decl.py;
 * x11_host.h), so it builds with loft's fixed flags and no development package.  It needs
 * Vulkan 1.3 with VK_KHR_push_descriptor and VK_EXT_depth_clip_control, which Mesa's drivers
 * and the desktop vendors' have; a machine without them opens no device, and the host asks the
 * next backend.
 *
 * How `gpu`'s conventions are kept:
 *   - Every image is stored with its first row at the bottom, as OpenGL stores it, and drawn
 *     into with the pass's viewport and scissor as given (origin bottom left).  A capture and the
 *     window's frame turn the rows over on the way out.  So a target sampled as a texture shows
 *     the same way up as on OpenGL, and the front face (counter-clockwise in clip space) is
 *     Vulkan's clockwise, since Vulkan counts its rows downwards.
 *   - Clip depth runs from -1 to 1 (VK_EXT_depth_clip_control), and the depth test is LESS.
 *   - The window is an image of its own, presented by copying it to the swapchain: it keeps its
 *     pixels from one frame to the next, and its alpha is held at 1 (a window pipeline writes no
 *     alpha), as a window on OpenGL has no alpha channel.
 *
 * Work is recorded into one command buffer per frame, two frames in flight.  Everything a call
 * changes happens in the order of the calls: a buffer or texture written between two draws is
 * copied there, the pass it falls in split around the copy.  Uniform values are copied at
 * `uniforms`, so each draw reads what was set when it was given.
 *
 * What a shader source for it follows (tag "spirv"): a SPIR-V 1.3 module as base64 text;
 *   - the inputs read per vertex are `location = 0` onwards in the program's order, the inputs
 *     read per copy follow them;
 *   - the uniform block is `set = 0, binding = 0`, std140;
 *   - texture slot n is a `sampler2D` at `set = 0, binding = 1 + n`;
 *   - a program drawing points writes `gl_PointSize`. */
#include "gpu/gpu_backend.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasm__)
#include "vk_decl.h"
#include "x11_host.h"
#include "spirv.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GV_FRAMES 2
#define GV_COLOUR VK_FORMAT_B8G8R8A8_UNORM /* 0xAARRGGBB in memory order */
#define GV_DEPTH VK_FORMAT_D32_SFLOAT
#define GV_CHUNK (4u << 20)
#define GV_ALL VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
#define GV_MEM (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT)

/* ── the loader ────────────────────────────────────────────────────────────────────────────── */

static void *gv_lib = NULL;
static PFN_vkGetInstanceProcAddr gv_gipa = NULL;

/* The loader stays loaded once opened (`RTLD_NODELETE`), as graphics keeps libGL. */
static int gv_load(void) {
    if (gv_gipa) return 1;
    gv_lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!gv_lib) return 0;
    *(void **) (&gv_gipa) = dlsym(gv_lib, "vkGetInstanceProcAddr");
    return gv_gipa != NULL;
}

typedef VkResult (*gv_create_instance_fn)(const VkInstanceCreateInfo *, const void *,
                                          VkInstance *);

/* ── records ───────────────────────────────────────────────────────────────────────────────── */

#define GV_DECLARE(ret, name, args) ret(*name) args;
typedef struct {
    GV_INSTANCE_FUNCTIONS(GV_DECLARE)
    GV_DEVICE_FUNCTIONS(GV_DECLARE)
} gv_fns;

enum { GV_BUFFER = 1, GV_TEXTURE = 2, GV_TARGET = 3, GV_PIPELINE = 4 };

struct gv_dev;

/* What every handle given to `gpu` starts with.  A record outlives its device's close: the
 * device then destroyed what it held (`gone`), and `release` frees only the record. */
typedef struct gv_res {
    int kind;
    int gone;
    struct gv_dev *d;
    struct gv_res *prev, *next;
} gv_res;

/* An image: a texture, a target's attachment, or the window's own colour and depth. */
typedef struct gv_image {
    gv_res res; /* a texture's handle; unused for the window's images */
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view_base, view_all; /* the first level; every level */
    int width, height, levels, depth;
    int layout;   /* what the recorded commands left it in */
    int nearest;  /* sampled at the nearest pixel */
    int mipped;   /* sampled through its smaller copies (`mipmaps` since the last `nearest`) */
    int refs;     /* its handle, and each target holding it */
} gv_image;

typedef struct {
    gv_res res;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size; /* bytes it holds room for */
} gv_buffer;

typedef struct {
    gv_res res;
    gv_image *colour, *depth;
    int width, height;
} gv_target;

typedef struct {
    gv_res res;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout set_layout;
    gpu_state state;
    int has_block, textures, has_instances;
} gv_pipe;

/* Something to destroy once the GPU is past the submission `serial`. */
typedef struct gv_dead {
    uint64_t serial;
    VkImage image;
    VkImageView views[2];
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout set_layout;
    struct gv_dead *next;
} gv_dead;

/* Host-visible memory a frame writes uploads and uniform values into. */
typedef struct gv_chunk {
    VkBuffer buffer;
    VkDeviceMemory memory;
    unsigned char *mapped;
    VkDeviceSize size, used;
    struct gv_chunk *next;
} gv_chunk;

typedef struct {
    VkCommandBuffer cb;
    VkFence fence;
    VkSemaphore acquired;
    uint64_t serial; /* of its last submission, 0 for none */
    gv_chunk *chunks;
} gv_frame;

typedef struct gv_dev {
    gv_x_window x;
    gv_fns f;
    VkInstance instance;
    VkPhysicalDevice pd;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceMemoryProperties memory;
    VkDeviceSize ubo_align;
    int bresenham;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkImage sc_images[16];
    VkSemaphore sc_done[16];
    uint32_t sc_count;
    int sc_format;
    VkExtent2D sc_extent;
    int sc_stale;
    VkCommandPool pool;
    gv_frame frames[GV_FRAMES];
    int cur, recording;
    uint64_t serial_next, serial_done;
    gv_dead *dead;
    VkSampler samplers[3]; /* nearest, between pixels, through the smaller copies */
    gv_image *back, *back_depth;
    gv_res *res; /* every live handle, freed with the device */
    int closed;
    int64_t refs; /* records still pointing here */
    /* the pass */
    int in_pass, rendering, into;
    gv_target *target; /* NULL: the window */
    int pass_w, pass_h;
    gv_pipe *pipe;
    gv_buffer *vb, *ib, *instb;
    VkDeviceSize inst_offset;
    VkBuffer ubo;
    VkDeviceSize ubo_offset, ubo_range;
    float *ubo_data;     /* the values last given, staged again in a frame that has not got them */
    int64_t ubo_n, ubo_cap;
    uint64_t ubo_serial; /* the submission the staged copy belongs to */
    gv_image *tex[32];
    VkViewport viewport;
    VkRect2D scissor;
    int dirty; /* the state to send before the next draw */
} gv_dev;

#define GV_DIRTY_PIPE 1
#define GV_DIRTY_VIEW 2
#define GV_DIRTY_VB 4
#define GV_DIRTY_IB 8
#define GV_DIRTY_SET 16
#define GV_DIRTY_ALL 31

static gv_dev *gv_of(int64_t h) {
    gv_dev *d = (gv_dev *) (intptr_t) h;
    return d && !d->closed ? d : NULL;
}

static void gv_link(gv_dev *d, gv_res *r, int kind) {
    r->kind = kind;
    r->d = d;
    r->next = d->res;
    if (d->res) d->res->prev = r;
    d->res = r;
    d->refs++;
}

static void gv_unlink(gv_res *r) {
    gv_dev *d = r->d;
    if (r->prev) r->prev->next = r->next;
    else if (d->res == r) d->res = r->next;
    if (r->next) r->next->prev = r->prev;
    r->prev = r->next = NULL;
}

/* ── memory ────────────────────────────────────────────────────────────────────────────────── */

static int gv_memory_type(gv_dev *d, uint32_t bits, VkFlags want, VkFlags fallback) {
    uint32_t i;
    for (i = 0; i < d->memory.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & want) == want) return (int) i;
    }
    for (i = 0; i < d->memory.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & fallback) == fallback)
            return (int) i;
    }
    return -1;
}

static int gv_make_buffer(gv_dev *d, VkDeviceSize size, VkFlags usage, VkFlags props,
                          VkBuffer *buffer, VkDeviceMemory *memory) {
    VkBufferCreateInfo bi;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    int type;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (d->f.vkCreateBuffer(d->device, &bi, NULL, buffer) != VK_SUCCESS) return 0;
    d->f.vkGetBufferMemoryRequirements(d->device, *buffer, &req);
    type = gv_memory_type(d, req.memoryTypeBits, props, props & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t) type;
    if (type < 0 || d->f.vkAllocateMemory(d->device, &ai, NULL, memory) != VK_SUCCESS) {
        d->f.vkDestroyBuffer(d->device, *buffer, NULL);
        return 0;
    }
    d->f.vkBindBufferMemory(d->device, *buffer, *memory, 0);
    return 1;
}

/* ── the stream of work ────────────────────────────────────────────────────────────────────── */

/* Destroy what the GPU is past. */
static void gv_bury(gv_dev *d, int all) {
    gv_dead **at = &d->dead, *x;
    while ((x = *at) != NULL) {
        if (!all && x->serial > d->serial_done) {
            at = &x->next;
            continue;
        }
        *at = x->next;
        if (x->pipeline) d->f.vkDestroyPipeline(d->device, x->pipeline, NULL);
        if (x->layout) d->f.vkDestroyPipelineLayout(d->device, x->layout, NULL);
        if (x->set_layout) d->f.vkDestroyDescriptorSetLayout(d->device, x->set_layout, NULL);
        if (x->views[0]) d->f.vkDestroyImageView(d->device, x->views[0], NULL);
        if (x->views[1]) d->f.vkDestroyImageView(d->device, x->views[1], NULL);
        if (x->image) d->f.vkDestroyImage(d->device, x->image, NULL);
        if (x->buffer) d->f.vkDestroyBuffer(d->device, x->buffer, NULL);
        if (x->memory) d->f.vkFreeMemory(d->device, x->memory, NULL);
        free(x);
    }
}

/* Destroy `x` (a filled record) once the work recorded so far has run. */
static void gv_defer(gv_dev *d, const gv_dead *x) {
    gv_dead *n = (gv_dead *) malloc(sizeof *n);
    if (!n) return;
    *n = *x;
    n->serial = d->serial_next;
    n->next = d->dead;
    d->dead = n;
}

/* Wait until every submission up to `serial` has run. */
static void gv_wait(gv_dev *d, uint64_t serial) {
    int i;
    if (serial <= d->serial_done) return;
    for (i = 0; i < GV_FRAMES; i++) {
        gv_frame *f = &d->frames[i];
        if (f->serial && f->serial <= serial)
            d->f.vkWaitForFences(d->device, 1, &f->fence, VK_TRUE, ~0ULL);
    }
    d->serial_done = serial;
    gv_bury(d, 0);
}

/* The command buffer being recorded, begun when the first command of a frame arrives. */
static VkCommandBuffer gv_cmd(gv_dev *d) {
    gv_frame *f = &d->frames[d->cur];
    VkCommandBufferBeginInfo bi;
    gv_chunk *c;
    if (d->recording) return f->cb;
    if (f->serial) gv_wait(d, f->serial);
    d->f.vkResetFences(d->device, 1, &f->fence);
    for (c = f->chunks; c; c = c->next) c->used = 0;
    d->f.vkResetCommandBuffer(f->cb, 0);
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    d->f.vkBeginCommandBuffer(f->cb, &bi);
    d->recording = 1;
    return f->cb;
}

/* `size` bytes of host-visible memory in this frame, `align`ed; its buffer and offset. */
static unsigned char *gv_stage(gv_dev *d, VkDeviceSize size, VkDeviceSize align, VkBuffer *buffer,
                               VkDeviceSize *offset) {
    gv_frame *f;
    gv_chunk *c;
    gv_cmd(d);
    f = &d->frames[d->cur];
    if (align < 16) align = 16;
    for (c = f->chunks; c; c = c->next) {
        VkDeviceSize at = (c->used + align - 1) / align * align;
        if (at + size <= c->size) {
            c->used = at + size;
            *buffer = c->buffer;
            *offset = at;
            return c->mapped + at;
        }
    }
    c = (gv_chunk *) calloc(1, sizeof *c);
    if (!c) return NULL;
    c->size = size > GV_CHUNK ? size : GV_CHUNK;
    if (!gv_make_buffer(d, c->size,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &c->buffer, &c->memory) ||
        d->f.vkMapMemory(d->device, c->memory, 0, VK_WHOLE_SIZE, 0, (void **) &c->mapped) != VK_SUCCESS) {
        free(c);
        return NULL;
    }
    c->next = f->chunks;
    f->chunks = c;
    c->used = size;
    *buffer = c->buffer;
    *offset = 0;
    return c->mapped;
}

/* Every write before it is seen by every read and write after it. */
static void gv_barrier(gv_dev *d) {
    VkMemoryBarrier2 mb;
    VkDependencyInfo di;
    memset(&mb, 0, sizeof mb);
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    mb.srcStageMask = mb.dstStageMask = GV_ALL;
    mb.srcAccessMask = mb.dstAccessMask = GV_MEM;
    memset(&di, 0, sizeof di);
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    d->f.vkCmdPipelineBarrier2(gv_cmd(d), &di);
}

/* Levels `base` .. `base + count` of `im` from layout `from` into `to`, after all before. */
static void gv_levels(gv_dev *d, VkImage image, int depth, uint32_t base, uint32_t count, int from,
                      int to) {
    VkImageMemoryBarrier2 ib;
    VkDependencyInfo di;
    memset(&ib, 0, sizeof ib);
    ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    ib.srcStageMask = ib.dstStageMask = GV_ALL;
    ib.srcAccessMask = ib.dstAccessMask = GV_MEM;
    ib.oldLayout = from;
    ib.newLayout = to;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = image;
    ib.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    ib.subresourceRange.baseMipLevel = base;
    ib.subresourceRange.levelCount = count;
    ib.subresourceRange.layerCount = 1;
    memset(&di, 0, sizeof di);
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &ib;
    d->f.vkCmdPipelineBarrier2(gv_cmd(d), &di);
}

/* Bring every level of `im` into `layout`, after whatever came before it. */
static void gv_layout(gv_dev *d, gv_image *im, int layout) {
    gv_levels(d, im->image, im->depth, 0, (uint32_t) im->levels, im->layout, layout);
    im->layout = layout;
}

/* The layout an image rests in between uses: the window's colour stays drawable, the rest
 * stay sampleable. */
static int gv_rest(gv_dev *d, gv_image *im) {
    if (im == d->back) return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (im == d->back_depth) return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static void gv_pause(gv_dev *d);
static void gv_resume(gv_dev *d, int clear, uint32_t colour);

/* Submit what was recorded.  Presenting swapchain image `present` (-1: none) waits for its
 * acquisition and signals that image's semaphore.  Answers the submission's serial. */
static uint64_t gv_submit(gv_dev *d, int present) {
    gv_frame *f = &d->frames[d->cur];
    VkSubmitInfo si;
    VkFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    uint64_t serial;
    gv_cmd(d);
    d->f.vkEndCommandBuffer(f->cb);
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f->cb;
    if (present >= 0) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &f->acquired;
        si.pWaitDstStageMask = &stage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &d->sc_done[present];
    }
    serial = d->serial_next++;
    if (serial == 0) serial = d->serial_next++;
    d->f.vkQueueSubmit(d->queue, 1, &si, f->fence);
    f->serial = serial;
    d->recording = 0;
    d->cur = (d->cur + 1) % GV_FRAMES;
    return serial;
}

/* ── images ────────────────────────────────────────────────────────────────────────────────── */

static VkImageView gv_view(gv_dev *d, gv_image *im, uint32_t levels) {
    VkImageViewCreateInfo vi;
    VkImageView view = 0;
    memset(&vi, 0, sizeof vi);
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = im->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = im->depth ? GV_DEPTH : GV_COLOUR;
    vi.subresourceRange.aspectMask = im->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = levels;
    vi.subresourceRange.layerCount = 1;
    d->f.vkCreateImageView(d->device, &vi, NULL, &view);
    return view;
}

/* A `width` x `height` image, cleared (colour to transparent black, depth to 1) and resting. */
static gv_image *gv_image_new(gv_dev *d, int width, int height, int depth, int window) {
    gv_image *im = (gv_image *) calloc(1, sizeof *im);
    VkImageCreateInfo ci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    VkImageSubresourceRange all;
    int type, levels = 1, w = width > height ? width : height;
    if (!im) return NULL;
    if (!depth && !window)
        while (w > 1) {
            w /= 2;
            levels++;
        }
    im->width = width;
    im->height = height;
    im->depth = depth;
    im->levels = levels;
    im->refs = 1;
    im->nearest = depth; /* a depth texture starts sampled at the nearest pixel, as on OpenGL */
    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = depth ? GV_DEPTH : GV_COLOUR;
    ci.extent.width = (uint32_t) width;
    ci.extent.height = (uint32_t) height;
    ci.extent.depth = 1;
    ci.mipLevels = (uint32_t) levels;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT |
               (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (d->f.vkCreateImage(d->device, &ci, NULL, &im->image) != VK_SUCCESS) {
        free(im);
        return NULL;
    }
    d->f.vkGetImageMemoryRequirements(d->device, im->image, &req);
    type = gv_memory_type(d, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t) type;
    if (type < 0 || d->f.vkAllocateMemory(d->device, &ai, NULL, &im->memory) != VK_SUCCESS) {
        d->f.vkDestroyImage(d->device, im->image, NULL);
        free(im);
        return NULL;
    }
    d->f.vkBindImageMemory(d->device, im->image, im->memory, 0);
    im->view_base = gv_view(d, im, 1);
    im->view_all = levels > 1 ? gv_view(d, im, (uint32_t) levels) : 0;
    im->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    gv_layout(d, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    memset(&all, 0, sizeof all);
    all.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    all.levelCount = (uint32_t) levels;
    all.layerCount = 1;
    if (depth) {
        VkClearDepthStencilValue one = {1.0f, 0};
        d->f.vkCmdClearDepthStencilImage(gv_cmd(d), im->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         &one, 1, &all);
    } else {
        VkClearColorValue none;
        memset(&none, 0, sizeof none);
        if (window) none.float32[3] = 1.0f; /* the window starts opaque black */
        d->f.vkCmdClearColorImage(gv_cmd(d), im->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &none,
                                  1, &all);
    }
    gv_layout(d, im, window ? (depth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return im;
}

/* One holder of `im` lets go; the image goes once the work recorded so far has run. */
static void gv_image_drop(gv_dev *d, gv_image *im) {
    gv_dead x;
    if (!im || --im->refs > 0) return;
    memset(&x, 0, sizeof x);
    x.image = im->image;
    x.memory = im->memory;
    x.views[0] = im->view_base;
    x.views[1] = im->view_all;
    gv_defer(d, &x);
    free(im);
}

/* ── opening a device ──────────────────────────────────────────────────────────────────────── */

static int gv_has(const VkExtensionProperties *list, uint32_t n, const char *name) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (strcmp(list[i].extensionName, name) == 0) return 1;
    }
    return 0;
}

/* How much a device is preferred: a GPU of its own, then one sharing the CPU's memory, then the
 * rest, and last a device that is the CPU itself (lavapipe). */
static int gv_rank(int type) {
    return type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
           : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
           : type == VK_PHYSICAL_DEVICE_TYPE_CPU ? 1
                                                 : 2;
}

/* Whether `pd` can draw for this window: Vulkan 1.3, the extensions this file needs, a queue
 * that draws and presents to the surface.  Its queue family in `family`. */
static int gv_suits(gv_dev *d, VkPhysicalDevice pd, uint32_t *family, int *line_ext) {
    VkPhysicalDeviceProperties props;
    VkQueueFamilyProperties fams[32];
    VkExtensionProperties *ext;
    uint32_t n = 32, i, en = 0;
    int ok;
    d->f.vkGetPhysicalDeviceProperties(pd, &props);
    if (props.apiVersion < ((1u << 22) | (3u << 12))) return 0;
    d->f.vkEnumerateDeviceExtensionProperties(pd, NULL, &en, NULL);
    ext = (VkExtensionProperties *) calloc(en ? en : 1, sizeof *ext);
    if (!ext) return 0;
    d->f.vkEnumerateDeviceExtensionProperties(pd, NULL, &en, ext);
    ok = gv_has(ext, en, "VK_KHR_swapchain") && gv_has(ext, en, "VK_KHR_push_descriptor") &&
         gv_has(ext, en, "VK_EXT_depth_clip_control");
    *line_ext = gv_has(ext, en, "VK_EXT_line_rasterization");
    free(ext);
    if (!ok) return 0;
    d->f.vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, fams);
    for (i = 0; i < n; i++) {
        VkBool32 present = 0;
        if (!(fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        d->f.vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, d->surface, &present);
        if (present) {
            *family = i;
            return 1;
        }
    }
    return 0;
}

static int gv_make_device(gv_dev *d, int line_ext) {
    static const char *exts[] = {"VK_KHR_swapchain", "VK_KHR_push_descriptor",
                                 "VK_EXT_depth_clip_control", "VK_EXT_line_rasterization"};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi;
    VkDeviceCreateInfo di;
    VkPhysicalDeviceFeatures2 f2;
    VkPhysicalDeviceDynamicRenderingFeatures dr;
    VkPhysicalDeviceSynchronization2Features s2;
    VkPhysicalDeviceDepthClipControlFeaturesEXT dc;
    VkPhysicalDeviceLineRasterizationFeaturesEXT lr;
    memset(&dr, 0, sizeof dr);
    memset(&s2, 0, sizeof s2);
    memset(&dc, 0, sizeof dc);
    memset(&lr, 0, sizeof lr);
    memset(&f2, 0, sizeof f2);
    dr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    s2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    dc.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
    lr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    dr.pNext = &s2;
    s2.pNext = &dc;
    dc.pNext = line_ext ? (void *) &lr : NULL;
    f2.pNext = &dr;
    d->f.vkGetPhysicalDeviceFeatures2(d->pd, &f2);
    if (!dr.dynamicRendering || !s2.synchronization2 || !dc.depthClipControl) return 0;
    d->bresenham = line_ext && lr.bresenhamLines;
    /* ask for exactly the features this file uses */
    memset(f2.features, 0, sizeof f2.features);
    lr.rectangularLines = lr.smoothLines = lr.stippledRectangularLines = 0;
    lr.stippledBresenhamLines = lr.stippledSmoothLines = 0;
    memset(&qi, 0, sizeof qi);
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = d->family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    memset(&di, 0, sizeof di);
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.pNext = &f2;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = line_ext ? 4 : 3;
    di.ppEnabledExtensionNames = exts;
    return d->f.vkCreateDevice(d->pd, &di, NULL, &d->device) == VK_SUCCESS;
}

static VkSampler gv_sampler(gv_dev *d, int filter, int mip) {
    VkSamplerCreateInfo si;
    VkSampler s = 0;
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = filter;
    si.mipmapMode = mip ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f;
    si.compareOp = VK_COMPARE_OP_ALWAYS;
    si.maxLod = mip ? 1000.0f : 0.0f;
    si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    d->f.vkCreateSampler(d->device, &si, NULL, &s);
    return s;
}

/* The swapchain for the window's size now: an 8-bit format stored as it is written (never one
 * that encodes sRGB), presented at the display's rate. */
static int gv_swapchain(gv_dev *d) {
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[64];
    VkSwapchainCreateInfoKHR ci;
    VkSwapchainKHR old = d->swapchain;
    uint32_t n = 64, i, count;
    int format = -1, space = 0;
    if (d->f.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d->pd, d->surface, &caps) != VK_SUCCESS) return 0;
    if (d->f.vkGetPhysicalDeviceSurfaceFormatsKHR(d->pd, d->surface, &n, formats) != VK_SUCCESS) n = 0;
    for (i = 0; i < n && format < 0; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) format = formats[i].format, space = formats[i].colorSpace;
    }
    for (i = 0; i < n && format < 0; i++) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) format = formats[i].format, space = formats[i].colorSpace;
    }
    if (format < 0 || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fprintf(stderr, "gpu_vulkan: the window takes no 8-bit linear format a frame can be copied into\n");
        return 0;
    }
    d->sc_extent = caps.currentExtent;
    if (d->sc_extent.width == 0xFFFFFFFFu) {
        d->sc_extent.width = (uint32_t) d->x.width;
        d->sc_extent.height = (uint32_t) d->x.height;
    }
    if (d->sc_extent.width == 0 || d->sc_extent.height == 0) return 0; /* minimised */
    count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = d->surface;
    ci.minImageCount = count;
    ci.imageFormat = format;
    ci.imageColorSpace = space;
    ci.imageExtent = d->sc_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; /* always there, and waits for the display */
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    d->f.vkDeviceWaitIdle(d->device);
    if (d->f.vkCreateSwapchainKHR(d->device, &ci, NULL, &d->swapchain) != VK_SUCCESS) {
        d->swapchain = 0;
        if (old) d->f.vkDestroySwapchainKHR(d->device, old, NULL);
        return 0;
    }
    if (old) d->f.vkDestroySwapchainKHR(d->device, old, NULL);
    d->sc_count = 16;
    d->f.vkGetSwapchainImagesKHR(d->device, d->swapchain, &d->sc_count, d->sc_images);
    for (i = 0; i < d->sc_count; i++) {
        if (!d->sc_done[i]) {
            VkSemaphoreCreateInfo si;
            memset(&si, 0, sizeof si);
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            d->f.vkCreateSemaphore(d->device, &si, NULL, &d->sc_done[i]);
        }
    }
    d->sc_format = format;
    d->sc_stale = 0;
    return 1;
}

/* The window's own colour and depth at its size now. */
static int gv_back(gv_dev *d) {
    if (d->back && d->back->width == d->x.width && d->back->height == d->x.height) return 1;
    gv_image_drop(d, d->back);
    gv_image_drop(d, d->back_depth);
    d->back = gv_image_new(d, d->x.width, d->x.height, 0, 1);
    d->back_depth = gv_image_new(d, d->x.width, d->x.height, 1, 1);
    return d->back && d->back_depth;
}

static void gv_free(gv_dev *d);

/* Why a device did not open, on stderr, when that is not the stack being absent: a window that
 * did not open on a display that is set, or a driver that suits the window failing to make what
 * a device needs.  Absence (no loader, no X display, no driver that draws into the window) is
 * how a backend says "not here", and says nothing. */
static int gv_say(const char *what, VkResult r) {
    fprintf(stderr, "gpu_vulkan: no device: %s (VkResult %d)\n", what, (int) r);
    return 0;
}

static int64_t gv_t_open(int64_t width, int64_t height, const char *title, int64_t fullscreen) {
    static const char *iexts[] = {"VK_KHR_surface", "VK_KHR_xlib_surface"};
    gv_create_instance_fn create;
    VkApplicationInfo app;
    VkInstanceCreateInfo ii;
    VkXlibSurfaceCreateInfoKHR xi;
    VkPhysicalDevice pds[16];
    VkPhysicalDeviceProperties props;
    VkCommandPoolCreateInfo pi;
    VkCommandBufferAllocateInfo ai;
    uint32_t n = 16, i;
    int best = -1, best_line = 0, line;
    uint32_t best_family = 0, family;
    VkResult r;
    gv_dev *d;
    if (!gv_load()) return 0;
    d = (gv_dev *) calloc(1, sizeof *d);
    if (!d) return 0;
    d->serial_next = 1;
    if (!gv_x_open(&d->x, width, height, title, fullscreen)) {
        if (d->x.display || getenv("DISPLAY")) gv_say("the X window did not open", 0);
        goto fail;
    }
    *(void **) (&create) = (void *) gv_gipa(NULL, "vkCreateInstance");
    if (!create) goto fail;
    memset(&app, 0, sizeof app);
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = title;
    app.pEngineName = "loft gpu";
    app.apiVersion = (1u << 22) | (3u << 12);
    memset(&ii, 0, sizeof ii);
    ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ii.pApplicationInfo = &app;
    ii.enabledExtensionCount = 2;
    ii.ppEnabledExtensionNames = iexts;
    if (create(&ii, NULL, &d->instance) != VK_SUCCESS) goto fail;
#define GV_ILOAD(ret, name, args) *(void **) (&d->f.name) = (void *) gv_gipa(d->instance, #name);
    GV_INSTANCE_FUNCTIONS(GV_ILOAD)
#define GV_ICHECK(ret, name, args) || !d->f.name
    if (0 GV_INSTANCE_FUNCTIONS(GV_ICHECK)) goto fail;
    memset(&xi, 0, sizeof xi);
    xi.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    xi.dpy = d->x.display;
    xi.window = d->x.window;
    if (d->f.vkCreateXlibSurfaceKHR(d->instance, &xi, NULL, &d->surface) != VK_SUCCESS) goto fail;
    if (d->f.vkEnumeratePhysicalDevices(d->instance, &n, pds) < 0) goto fail;
    for (i = 0; i < n; i++) {
        if (!gv_suits(d, pds[i], &family, &line)) continue;
        d->f.vkGetPhysicalDeviceProperties(pds[i], &props);
        if (best < 0 || gv_rank(props.deviceType) > gv_rank(best)) {
            best = props.deviceType;
            d->pd = pds[i];
            best_family = family;
            best_line = line;
        }
    }
    if (best < 0) goto fail; /* no device here draws into this window */
    d->family = best_family;
    if (!gv_make_device(d, best_line)) {
        gv_say("the driver made no device", 0);
        goto fail;
    }
#define GV_DLOAD(ret, name, args)                                                                 \
    *(void **) (&d->f.name) = (void *) d->f.vkGetDeviceProcAddr(d->device, #name);
    GV_DEVICE_FUNCTIONS(GV_DLOAD)
#define GV_DCHECK(ret, name, args) || !d->f.name
    if (0 GV_DEVICE_FUNCTIONS(GV_DCHECK)) goto fail;
    d->f.vkGetDeviceQueue(d->device, d->family, 0, &d->queue);
    d->f.vkGetPhysicalDeviceMemoryProperties(d->pd, &d->memory);
    d->f.vkGetPhysicalDeviceProperties(d->pd, &props);
    d->ubo_align = props.limits.minUniformBufferOffsetAlignment;
    memset(&pi, 0, sizeof pi);
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = d->family;
    if ((r = d->f.vkCreateCommandPool(d->device, &pi, NULL, &d->pool)) != VK_SUCCESS) {
        gv_say("no command pool", r);
        goto fail;
    }
    for (i = 0; i < GV_FRAMES; i++) {
        VkFenceCreateInfo fi;
        VkSemaphoreCreateInfo si;
        memset(&ai, 0, sizeof ai);
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = d->pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        memset(&fi, 0, sizeof fi);
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        memset(&si, 0, sizeof si);
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (d->f.vkAllocateCommandBuffers(d->device, &ai, &d->frames[i].cb) != VK_SUCCESS ||
            d->f.vkCreateFence(d->device, &fi, NULL, &d->frames[i].fence) != VK_SUCCESS ||
            d->f.vkCreateSemaphore(d->device, &si, NULL, &d->frames[i].acquired) != VK_SUCCESS) {
            gv_say("no command buffer, fence or semaphore", 0);
            goto fail;
        }
    }
    d->samplers[0] = gv_sampler(d, VK_FILTER_NEAREST, 0);
    d->samplers[1] = gv_sampler(d, VK_FILTER_LINEAR, 0);
    d->samplers[2] = gv_sampler(d, VK_FILTER_LINEAR, 1);
    if (!d->samplers[0] || !d->samplers[1] || !d->samplers[2]) {
        gv_say("no samplers", 0);
        goto fail;
    }
    if (!gv_swapchain(d)) {
        gv_say("no swapchain for the window", 0);
        goto fail;
    }
    if (!gv_back(d)) {
        gv_say("no image for the window", 0);
        goto fail;
    }
    return (int64_t) (intptr_t) d;
fail:
    gv_free(d);
    return 0;
}

/* Every Vulkan object of `d` destroyed, and the window closed.  The record stays while handles
 * still point at it. */
static void gv_teardown(gv_dev *d) {
    gv_res *r;
    int i;
    if (d->device) {
        d->f.vkDeviceWaitIdle(d->device);
        while ((r = d->res) != NULL) {
            gv_dead x;
            memset(&x, 0, sizeof x);
            if (r->kind == GV_BUFFER) {
                gv_buffer *b = (gv_buffer *) r;
                x.buffer = b->buffer;
                x.memory = b->memory;
            } else if (r->kind == GV_PIPELINE) {
                gv_pipe *p = (gv_pipe *) r;
                x.pipeline = p->pipeline;
                x.layout = p->layout;
                x.set_layout = p->set_layout;
            } else if (r->kind == GV_TARGET) {
                gv_target *t = (gv_target *) r;
                gv_image_drop(d, t->colour);
                gv_image_drop(d, t->depth);
                t->colour = t->depth = NULL;
            } else if (r->kind == GV_TEXTURE) {
                gv_image *im = (gv_image *) r;
                x.image = im->image;
                x.memory = im->memory;
                x.views[0] = im->view_base;
                x.views[1] = im->view_all;
                im->image = 0;
                im->memory = 0;
                im->view_base = im->view_all = 0;
                /* a target still holding it held a reference: the record goes with its handle */
            }
            gv_defer(d, &x);
            gv_unlink(r);
            r->gone = 1;
        }
        gv_image_drop(d, d->back);
        gv_image_drop(d, d->back_depth);
        d->back = d->back_depth = NULL;
        gv_bury(d, 1);
        for (i = 0; i < GV_FRAMES; i++) {
            gv_chunk *c = d->frames[i].chunks, *next;
            for (; c; c = next) {
                next = c->next;
                d->f.vkDestroyBuffer(d->device, c->buffer, NULL);
                d->f.vkFreeMemory(d->device, c->memory, NULL);
                free(c);
            }
            d->frames[i].chunks = NULL;
            if (d->frames[i].fence) d->f.vkDestroyFence(d->device, d->frames[i].fence, NULL);
            if (d->frames[i].acquired) d->f.vkDestroySemaphore(d->device, d->frames[i].acquired, NULL);
        }
        for (i = 0; i < 16; i++) {
            if (d->sc_done[i]) d->f.vkDestroySemaphore(d->device, d->sc_done[i], NULL);
        }
        for (i = 0; i < 3; i++) {
            if (d->samplers[i]) d->f.vkDestroySampler(d->device, d->samplers[i], NULL);
        }
        if (d->pool) d->f.vkDestroyCommandPool(d->device, d->pool, NULL);
        if (d->swapchain) d->f.vkDestroySwapchainKHR(d->device, d->swapchain, NULL);
        d->f.vkDestroyDevice(d->device, NULL);
        d->device = NULL;
    }
    if (d->instance) {
        if (d->surface && d->f.vkDestroySurfaceKHR) d->f.vkDestroySurfaceKHR(d->instance, d->surface, NULL);
        if (d->f.vkDestroyInstance) d->f.vkDestroyInstance(d->instance, NULL);
        d->instance = NULL;
    }
    gv_x_close(&d->x);
    free(d->ubo_data);
    d->ubo_data = NULL;
    d->ubo_n = d->ubo_cap = 0;
    d->closed = 1;
}

static void gv_free(gv_dev *d) {
    gv_teardown(d);
    if (d->refs == 0) free(d);
}

static void gv_t_close(int64_t dev) {
    gv_dev *d = gv_of(dev);
    if (d) gv_free(d);
}

/* ── the host part ─────────────────────────────────────────────────────────────────────────── */

static int64_t gv_t_poll(int64_t dev) {
    gv_dev *d = gv_of(dev);
    if (!d) return 0;
    if (gv_x_poll(&d->x)) d->sc_stale = 1;
    return !d->x.should_close;
}

static int64_t gv_t_size(int64_t dev, int64_t which) {
    gv_dev *d = gv_of(dev);
    if (!d) return 0;
    return which ? d->x.height : d->x.width;
}

static int64_t gv_t_event_count(int64_t dev) {
    gv_dev *d = gv_of(dev);
    return d ? d->x.event_count : 0;
}

static void gv_t_events(int64_t dev, int64_t *out, int64_t n) {
    gv_dev *d = gv_of(dev);
    if (d) gv_x_events(&d->x, out, n);
}

static int64_t gv_t_key_down(int64_t dev, int64_t key) {
    gv_dev *d = gv_of(dev);
    return d && key >= 0 && key < 256 && d->x.keys[key];
}

static int64_t gv_t_pointer(int64_t dev, int64_t which) {
    gv_dev *d = gv_of(dev);
    if (!d) return 0;
    return which == 0 ? d->x.mouse_x : which == 1 ? d->x.mouse_y : d->x.buttons;
}

static int64_t gv_t_text_size(int64_t dev) {
    gv_dev *d = gv_of(dev);
    return d ? d->x.text_len : 0;
}

static void gv_t_text_copy(int64_t dev, uint8_t *out, int64_t n) {
    gv_dev *d = gv_of(dev);
    if (!d || n < d->x.text_len) return;
    if (d->x.text_len > 0) memcpy(out, d->x.text, (size_t) d->x.text_len);
}

/* ── buffers ───────────────────────────────────────────────────────────────────────────────── */

/* Copy `bytes` from host memory into `dst` at offset 0, in the stream's order. */
static void gv_upload(gv_dev *d, VkBuffer dst, const void *data, VkDeviceSize bytes) {
    VkBuffer src;
    VkDeviceSize at;
    VkBufferCopy region;
    unsigned char *p;
    if (bytes == 0) return;
    p = gv_stage(d, bytes, 16, &src, &at);
    if (!p) return;
    memcpy(p, data, (size_t) bytes);
    gv_pause(d);
    gv_barrier(d);
    region.srcOffset = at;
    region.dstOffset = 0;
    region.size = bytes;
    d->f.vkCmdCopyBuffer(gv_cmd(d), src, dst, 1, &region);
    gv_barrier(d);
}

/* Make room for `bytes` in `b`: a new buffer when it has too little, the old one destroyed
 * once the draws already recorded with it have run. */
static int gv_room(gv_dev *d, gv_buffer *b, VkDeviceSize bytes) {
    VkDeviceSize size = bytes < 16 ? 16 : bytes;
    if (b->buffer && b->size >= size) return 1;
    if (b->buffer) {
        gv_dead x;
        memset(&x, 0, sizeof x);
        x.buffer = b->buffer;
        x.memory = b->memory;
        gv_defer(d, &x);
        b->buffer = 0;
        b->memory = 0;
    }
    if (!gv_make_buffer(d, size,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &b->buffer, &b->memory))
        return 0;
    b->size = size;
    if (d->vb == b || d->instb == b) d->dirty |= GV_DIRTY_VB;
    if (d->ib == b) d->dirty |= GV_DIRTY_IB;
    return 1;
}

static int64_t gv_new_buffer(gv_dev *d, const void *data, VkDeviceSize bytes) {
    gv_buffer *b = (gv_buffer *) calloc(1, sizeof *b);
    if (!b) return 0;
    if (!gv_room(d, b, bytes)) {
        free(b);
        return 0;
    }
    gv_link(d, &b->res, GV_BUFFER);
    gv_upload(d, b->buffer, data, bytes);
    return (int64_t) (intptr_t) b;
}

static int64_t gv_t_buffer(int64_t dev, const float *data, int64_t n) {
    gv_dev *d = gv_of(dev);
    if (!d || n < 0) return 0;
    return gv_new_buffer(d, data, (VkDeviceSize) n * 4u);
}

static int64_t gv_t_index_buffer(int64_t dev, const int64_t *idx, int64_t n) {
    gv_dev *d = gv_of(dev);
    uint32_t *words;
    int64_t i, h;
    if (!d || n < 0) return 0;
    words = (uint32_t *) malloc((size_t) (n > 0 ? n : 1) * 4u);
    if (!words) return 0;
    for (i = 0; i < n; i++) words[i] = (uint32_t) idx[i];
    h = gv_new_buffer(d, words, (VkDeviceSize) n * 4u);
    free(words);
    return h;
}

static void gv_t_buffer_update(int64_t buf, const float *data, int64_t n) {
    gv_buffer *b = (gv_buffer *) (intptr_t) buf;
    gv_dev *d;
    if (!b || b->res.gone || b->res.kind != GV_BUFFER || n < 0) return;
    d = b->res.d;
    if (!gv_room(d, b, (VkDeviceSize) n * 4u)) return;
    gv_upload(d, b->buffer, data, (VkDeviceSize) n * 4u);
}

/* ── textures and targets ──────────────────────────────────────────────────────────────────── */

static gv_image *gv_texture_of(int64_t h) {
    gv_image *im = (gv_image *) (intptr_t) h;
    if (!im || im->res.gone || im->res.kind != GV_TEXTURE) return NULL;
    return im;
}

static int64_t gv_t_texture(int64_t dev, int64_t width, int64_t height, int64_t depth) {
    gv_dev *d = gv_of(dev);
    gv_image *im;
    if (!d || width <= 0 || height <= 0) return 0;
    gv_pause(d);
    im = gv_image_new(d, (int) width, (int) height, depth != 0, 0);
    if (!im) return 0;
    gv_link(d, &im->res, GV_TEXTURE);
    return (int64_t) (intptr_t) im;
}

/* Copy `w` x `h` pixels of `px` (0xAARRGGBB, which is GV_COLOUR's byte order) to (x, y). */
static void gv_put(gv_image *im, int64_t x, int64_t y, int64_t w, int64_t h, const uint32_t *px) {
    gv_dev *d = im->res.d;
    VkBuffer src;
    VkDeviceSize at, bytes = (VkDeviceSize) w * (VkDeviceSize) h * 4u;
    VkBufferImageCopy region;
    unsigned char *p;
    if (im->depth || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > im->width || y + h > im->height)
        return;
    p = gv_stage(d, bytes, 16, &src, &at);
    if (!p) return;
    memcpy(p, px, (size_t) bytes);
    gv_pause(d);
    gv_layout(d, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    memset(&region, 0, sizeof region);
    region.bufferOffset = at;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = (int32_t) x;
    region.imageOffset.y = (int32_t) y;
    region.imageExtent.width = (uint32_t) w;
    region.imageExtent.height = (uint32_t) h;
    region.imageExtent.depth = 1;
    d->f.vkCmdCopyBufferToImage(gv_cmd(d), src, im->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                &region);
    gv_layout(d, im, gv_rest(d, im));
}

static void gv_t_texture_put(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                             const uint32_t *px, int64_t n) {
    gv_image *im = gv_texture_of(tex);
    if (im && n >= w * h) gv_put(im, x, y, w, h, px);
}

/* RGB565 values as 0xAARRGGBB, each opaque, a channel's high bits repeated in its low ones so
 * the brightest value is 255: as gpu_gl widens them. */
static void gv_t_texture_put565(int64_t tex, int64_t x, int64_t y, int64_t w, int64_t h,
                                const int32_t *px, int64_t n) {
    gv_image *im = gv_texture_of(tex);
    uint32_t *wide;
    int64_t i, count = w * h;
    if (!im || count <= 0 || n < count) return;
    wide = (uint32_t *) malloc((size_t) count * sizeof(uint32_t));
    if (!wide) return;
    for (i = 0; i < count; i++) {
        uint32_t v = (uint32_t) px[i];
        uint32_t r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;
        wide[i] = 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
    }
    gv_put(im, x, y, w, h, wide);
    free(wide);
}

static void gv_t_texture_nearest(int64_t tex, int64_t on) {
    gv_image *im = gv_texture_of(tex);
    if (!im) return;
    im->nearest = on != 0;
    im->mipped = 0; /* a filter set after `mipmaps` samples the full-size pixels only, as GL's does */
    if (im->res.d) im->res.d->dirty |= GV_DIRTY_SET;
}

/* Each smaller copy made from the one above it, halving with a filter between pixels. */
static void gv_t_texture_mipmaps(int64_t tex) {
    gv_image *im = gv_texture_of(tex);
    gv_dev *d;
    int level, w, h;
    if (!im || im->depth) return;
    d = im->res.d;
    if (im->levels > 1) {
        gv_pause(d);
        gv_layout(d, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        w = im->width;
        h = im->height;
        for (level = 1; level < im->levels; level++) {
            VkImageBlit blit;
            int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
            gv_levels(d, im->image, 0, (uint32_t) level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            memset(&blit, 0, sizeof blit);
            blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = (uint32_t) level - 1;
            blit.dstSubresource.mipLevel = (uint32_t) level;
            blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
            blit.srcOffsets[1].x = w;
            blit.srcOffsets[1].y = h;
            blit.srcOffsets[1].z = 1;
            blit.dstOffsets[1].x = nw;
            blit.dstOffsets[1].y = nh;
            blit.dstOffsets[1].z = 1;
            d->f.vkCmdBlitImage(gv_cmd(d), im->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            w = nw;
            h = nh;
        }
        /* every level but the last is a source now */
        gv_levels(d, im->image, 0, 0, (uint32_t) im->levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        gv_levels(d, im->image, 0, (uint32_t) im->levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        im->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    im->nearest = 0;
    im->mipped = im->levels > 1;
    d->dirty |= GV_DIRTY_SET;
}

static int64_t gv_t_target(int64_t dev, int64_t colour, int64_t depth) {
    gv_dev *d = gv_of(dev);
    gv_image *c = gv_texture_of(colour), *z = gv_texture_of(depth);
    gv_target *t;
    if (!d || (!c && !z)) return 0;
    if ((c && c->depth) || (z && !z->depth)) return 0;
    if (c && z && (c->width != z->width || c->height != z->height)) {
        fprintf(stderr, "gpu_vulkan: a target's colour and depth are of different sizes\n");
        return 0;
    }
    t = (gv_target *) calloc(1, sizeof *t);
    if (!t) return 0;
    t->colour = c;
    t->depth = z;
    if (c) c->refs++;
    if (z) z->refs++;
    t->width = c ? c->width : z->width;
    t->height = c ? c->height : z->height;
    gv_link(d, &t->res, GV_TARGET);
    return (int64_t) (intptr_t) t;
}

/* ── pipelines ─────────────────────────────────────────────────────────────────────────────── */

/* Whether a module's block, textures and inputs are what `p` declares; says why not. */
static int gv_fits(const gv_spv_info *s, const gpu_program *p, const char *stage) {
    int i, j, ok = 1;
    if (s->bad) return 0;
    if (s->block) {
        if (p->block_n == 0) {
            fprintf(stderr, "gpu_vulkan: the %s shader has a uniform block and the program declares none\n", stage);
            return 0;
        }
        if (s->block_set != 0 || s->block_binding != 0) {
            fprintf(stderr, "gpu_vulkan: the %s shader's uniform block is not at set 0, binding 0\n", stage);
            return 0;
        }
        if ((s->block_bytes + 15) / 16 * 16 != (int) p->block_floats * 4 || s->row_major || s->odd_stride)
            ok = 0;
        for (i = 0; ok && i < s->block_n; i++) {
            int found = 0;
            for (j = 0; j < (int) p->block_n; j++) {
                const gpu_field *f = &p->block[j];
                /* a field's bytes: an array is of vec4s, so `floats * count * 4` for every kind */
                if (s->block_offset[i] == (int) f->offset * 4 &&
                    s->block_size[i] == (int) (f->floats * f->count * 4))
                    found = 1;
            }
            if (!found) ok = 0;
        }
        if (!ok) {
            fprintf(stderr, "gpu_vulkan: the %s shader's uniform block is not laid out as the program "
                            "declares it.\n  declared: %d bytes, fields at",
                    stage, (int) p->block_floats * 4);
            for (j = 0; j < (int) p->block_n; j++) fprintf(stderr, " %d", (int) p->block[j].offset * 4);
            fprintf(stderr, "\n  compiled: %d bytes, fields at", (s->block_bytes + 15) / 16 * 16);
            for (i = 0; i < s->block_n; i++) fprintf(stderr, " %d", s->block_offset[i]);
            fprintf(stderr, "%s\n", s->row_major ? " (a matrix by rows)" : s->odd_stride ? " (a stride other than 16)" : "");
            return 0;
        }
    }
    for (i = 0; i < s->samplers; i++) {
        if (s->sampler_set[i] != 0 || s->sampler_binding[i] < 1 ||
            s->sampler_binding[i] > (int) p->textures) {
            fprintf(stderr, "gpu_vulkan: the %s shader samples at set %d, binding %d; the program's %d "
                            "texture slot(s) are at set 0, bindings 1 to %d\n",
                    stage, s->sampler_set[i], s->sampler_binding[i], (int) p->textures, (int) p->textures);
            return 0;
        }
    }
    for (i = 0; i < s->inputs; i++) {
        if (s->input_location[i] < 0 || s->input_location[i] >= (int) (p->vertex_n + p->instance_n)) {
            fprintf(stderr, "gpu_vulkan: the vertex shader reads location %d; the program declares %d "
                            "input(s)\n",
                    s->input_location[i], (int) (p->vertex_n + p->instance_n));
            return 0;
        }
    }
    return 1;
}

static VkShaderModule gv_module(gv_dev *d, const char *source, int stage, const gpu_program *p,
                                const char *name) {
    size_t n = 0;
    uint32_t *words = gv_spirv_words(source, &n);
    gv_spv_info info;
    VkShaderModuleCreateInfo ci;
    VkShaderModule m = 0;
    if (!words) {
        fprintf(stderr, "gpu_vulkan: the %s source is not SPIR-V as base64 text\n", name);
        return 0;
    }
    if (!gv_spv_read(words, n, &info) || info.stage != stage) {
        fprintf(stderr, "gpu_vulkan: the %s source has no %s entry point named main\n", name, name);
        free(words);
        return 0;
    }
    if (!gv_fits(&info, p, name)) {
        free(words);
        return 0;
    }
    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = n * 4;
    ci.pCode = words;
    if (d->f.vkCreateShaderModule(d->device, &ci, NULL, &m) != VK_SUCCESS) m = 0;
    free(words);
    return m;
}

static int gv_float_format(int64_t floats) {
    return floats == 1 ? VK_FORMAT_R32_SFLOAT
           : floats == 2 ? VK_FORMAT_R32G32_SFLOAT
           : floats == 3 ? VK_FORMAT_R32G32B32_SFLOAT
                         : VK_FORMAT_R32G32B32A32_SFLOAT;
}

/* BLEND factor numbers of gpu.loft (ZERO … ONE_MINUS_DST_ALPHA); anything else is ONE. */
static int gv_blend_factor(int64_t f) {
    static const int factors[] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA,
                                  VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
                                  VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA};
    return f >= 0 && f <= 5 ? factors[f] : VK_BLEND_FACTOR_ONE;
}

static int64_t gv_t_pipeline(int64_t dev, const gpu_program *p, const gpu_state *s) {
    gv_dev *d = gv_of(dev);
    gv_pipe *pipe;
    VkDescriptorSetLayoutBinding bindings[33];
    VkDescriptorSetLayoutCreateInfo sl;
    VkPipelineLayoutCreateInfo pl;
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription vbind[2];
    VkVertexInputAttributeDescription attrs[GPU_MAX_INPUTS * 2];
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportDepthClipControlCreateInfoEXT clip;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineRasterizationLineStateCreateInfoEXT line;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState blend;
    VkPipelineColorBlendStateCreateInfo cb;
    VkPipelineDynamicStateCreateInfo dyn;
    VkPipelineRenderingCreateInfo ri;
    VkGraphicsPipelineCreateInfo gi;
    int dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    int colour_format = GV_COLOUR;
    VkShaderModule vs, fs;
    uint32_t nb = 0, nattr = 0, nvb = 0, i, at;
    int has_colour = s->into != 3, has_depth = s->into == 0 || s->into >= 2;
    VkResult r;
    if (!d) return 0;
    if (p->textures > 32) {
        fprintf(stderr, "gpu_vulkan: a program samples at most 32 textures\n");
        return 0;
    }
    vs = gv_module(d, p->vertex_source, 0, p, "vertex");
    if (!vs) return 0;
    fs = gv_module(d, p->fragment_source, 4, p, "fragment");
    if (!fs) {
        d->f.vkDestroyShaderModule(d->device, vs, NULL);
        return 0;
    }
    pipe = (gv_pipe *) calloc(1, sizeof *pipe);
    if (!pipe) goto fail;
    pipe->state = *s;
    pipe->has_block = p->block_n > 0;
    pipe->textures = (int) p->textures;
    pipe->has_instances = p->instance_n > 0;

    memset(bindings, 0, sizeof bindings);
    if (pipe->has_block) {
        bindings[nb].binding = 0;
        bindings[nb].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[nb].descriptorCount = 1;
        bindings[nb].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        nb++;
    }
    for (i = 0; i < (uint32_t) p->textures; i++) {
        bindings[nb].binding = 1 + i;
        bindings[nb].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[nb].descriptorCount = 1;
        bindings[nb].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        nb++;
    }
    memset(&sl, 0, sizeof sl);
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    sl.bindingCount = nb;
    sl.pBindings = bindings;
    if (d->f.vkCreateDescriptorSetLayout(d->device, &sl, NULL, &pipe->set_layout) != VK_SUCCESS) goto fail;
    memset(&pl, 0, sizeof pl);
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &pipe->set_layout;
    if (d->f.vkCreatePipelineLayout(d->device, &pl, NULL, &pipe->layout) != VK_SUCCESS) goto fail;

    memset(stages, 0, sizeof stages);
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[0].pName = stages[1].pName = "main";

    /* binding 0 per vertex, binding 1 per copy; locations in the program's order */
    for (i = 0, at = 0; i < (uint32_t) p->vertex_n; i++) {
        attrs[nattr].location = nattr;
        attrs[nattr].binding = 0;
        attrs[nattr].format = gv_float_format(p->vertex[i]);
        attrs[nattr].offset = at * 4;
        at += (uint32_t) p->vertex[i];
        nattr++;
    }
    if (p->vertex_n > 0) {
        vbind[nvb].binding = 0;
        vbind[nvb].stride = at * 4;
        vbind[nvb].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        nvb++;
    }
    for (i = 0, at = 0; i < (uint32_t) p->instance_n; i++) {
        attrs[nattr].location = nattr;
        attrs[nattr].binding = 1;
        attrs[nattr].format = gv_float_format(p->instance[i]);
        attrs[nattr].offset = at * 4;
        at += (uint32_t) p->instance[i];
        nattr++;
    }
    if (p->instance_n > 0) {
        vbind[nvb].binding = 1;
        vbind[nvb].stride = at * 4;
        vbind[nvb].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        nvb++;
    }
    memset(&vi, 0, sizeof vi);
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = nvb;
    vi.pVertexBindingDescriptions = vbind;
    vi.vertexAttributeDescriptionCount = nattr;
    vi.pVertexAttributeDescriptions = attrs;

    memset(&ia, 0, sizeof ia);
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = s->shape == 1   ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                  : s->shape == 2 ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                                  : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    memset(&clip, 0, sizeof clip);
    clip.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT;
    clip.negativeOneToOne = VK_TRUE;
    memset(&vp, 0, sizeof vp);
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.pNext = &clip;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    memset(&line, 0, sizeof line);
    line.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
    line.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
    memset(&rs, 0, sizeof rs);
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.pNext = s->shape == 1 && d->bresenham ? &line : NULL;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = s->cull == 1 ? VK_CULL_MODE_BACK_BIT : s->cull == 2 ? VK_CULL_MODE_FRONT_BIT
                                                                     : VK_CULL_MODE_NONE;
    /* the images are stored bottom row first, so clip space's counter-clockwise is Vulkan's
     * clockwise (see the top of this file) */
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;

    memset(&ms, 0, sizeof ms);
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    memset(&ds, 0, sizeof ds);
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = has_depth && s->depth != 0;
    ds.depthWriteEnable = has_depth && s->depth == 2;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.maxDepthBounds = 1.0f;

    memset(&blend, 0, sizeof blend);
    blend.blendEnable = s->blend != 0;
    if (s->blend) {
        blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = gv_blend_factor((s->blend - 1) / 8);
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = gv_blend_factor((s->blend - 1) % 8);
    }
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           (s->into == 0 ? 0 : VK_COLOR_COMPONENT_A_BIT);
    memset(&cb, 0, sizeof cb);
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    /* a depth-only pass has no colour: what its shader writes there is discarded, as OpenGL
     * discards it (the validation layer notes the unused write as a warning) */
    cb.attachmentCount = has_colour ? 1 : 0;
    cb.pAttachments = &blend;

    memset(&dyn, 0, sizeof dyn);
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamic;

    memset(&ri, 0, sizeof ri);
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = has_colour ? 1 : 0;
    ri.pColorAttachmentFormats = &colour_format;
    ri.depthAttachmentFormat = has_depth ? GV_DEPTH : VK_FORMAT_UNDEFINED;

    memset(&gi, 0, sizeof gi);
    gi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gi.pNext = &ri;
    gi.stageCount = 2;
    gi.pStages = stages;
    gi.pVertexInputState = &vi;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vp;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pDepthStencilState = &ds;
    gi.pColorBlendState = &cb;
    gi.pDynamicState = &dyn;
    gi.layout = pipe->layout;
    gi.basePipelineIndex = -1;
    r = d->f.vkCreateGraphicsPipelines(d->device, 0, 1, &gi, NULL, &pipe->pipeline);
    d->f.vkDestroyShaderModule(d->device, vs, NULL);
    d->f.vkDestroyShaderModule(d->device, fs, NULL);
    vs = fs = 0;
    if (r != VK_SUCCESS) {
        fprintf(stderr, "gpu_vulkan: the driver built no pipeline from these shaders (%d)\n", (int) r);
        goto fail;
    }
    gv_link(d, &pipe->res, GV_PIPELINE);
    return (int64_t) (intptr_t) pipe;
fail:
    if (vs) d->f.vkDestroyShaderModule(d->device, vs, NULL);
    if (fs) d->f.vkDestroyShaderModule(d->device, fs, NULL);
    if (pipe) {
        if (pipe->layout) d->f.vkDestroyPipelineLayout(d->device, pipe->layout, NULL);
        if (pipe->set_layout) d->f.vkDestroyDescriptorSetLayout(d->device, pipe->set_layout, NULL);
        free(pipe);
    }
    return 0;
}

/* ── releasing ─────────────────────────────────────────────────────────────────────────────── */

static void gv_t_release(int64_t handle) {
    gv_res *r = (gv_res *) (intptr_t) handle;
    gv_dev *d;
    gv_dead x;
    int i;
    if (!r) return;
    d = r->d;
    if (!r->gone) {
        memset(&x, 0, sizeof x);
        gv_unlink(r);
        if (r->kind == GV_BUFFER) {
            gv_buffer *b = (gv_buffer *) r;
            if (d->vb == b) d->vb = NULL;
            if (d->ib == b) d->ib = NULL;
            if (d->instb == b) d->instb = NULL;
            x.buffer = b->buffer;
            x.memory = b->memory;
            gv_defer(d, &x);
        } else if (r->kind == GV_PIPELINE) {
            gv_pipe *p = (gv_pipe *) r;
            if (d->pipe == p) d->pipe = NULL;
            x.pipeline = p->pipeline;
            x.layout = p->layout;
            x.set_layout = p->set_layout;
            gv_defer(d, &x);
        } else if (r->kind == GV_TARGET) {
            gv_target *t = (gv_target *) r;
            gv_image_drop(d, t->colour);
            gv_image_drop(d, t->depth);
        } else if (r->kind == GV_TEXTURE) {
            gv_image *im = (gv_image *) r;
            for (i = 0; i < 32; i++) {
                if (d->tex[i] == im) d->tex[i] = NULL;
            }
            r->gone = 1;
            d->refs--;
            gv_image_drop(d, im); /* frees the record when no target holds it */
            if (d->closed && d->refs == 0) free(d);
            return;
        }
    } else if (r->kind == GV_TEXTURE) {
        /* the device closed and destroyed it; a target's reference went with the target */
        free(r);
        if (--d->refs == 0 && d->closed) free(d);
        return;
    }
    free(r);
    if (--d->refs == 0 && d->closed) free(d);
}

/* ── a pass ────────────────────────────────────────────────────────────────────────────────── */

/* Stop drawing for a copy or a read in the middle of a pass; the next draw draws on. */
static void gv_pause(gv_dev *d) {
    if (!d->rendering) return;
    d->f.vkCmdEndRendering(gv_cmd(d));
    d->rendering = 0;
}

/* Begin drawing into the pass's images, cleared to `colour` (and depth 1) or kept. */
static void gv_resume(gv_dev *d, int clear, uint32_t colour) {
    VkRenderingAttachmentInfo ca, da;
    VkRenderingInfo ri;
    gv_image *c = d->target ? d->target->colour : d->back;
    gv_image *z = d->target ? d->target->depth : d->back_depth;
    int i;
    if (d->rendering) return;
    /* what this pass samples rests sampleable; what it draws into is made drawable */
    for (i = 0; i < 32; i++) {
        if (d->tex[i] && d->tex[i] != c && d->tex[i] != z &&
            d->tex[i]->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            gv_layout(d, d->tex[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (c && c->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        gv_layout(d, c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (z && z->layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
        gv_layout(d, z, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    memset(&ca, 0, sizeof ca);
    ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ca.imageView = c ? c->view_base : 0;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color.float32[0] = (float) ((colour >> 16) & 0xFF) / 255.0f;
    ca.clearValue.color.float32[1] = (float) ((colour >> 8) & 0xFF) / 255.0f;
    ca.clearValue.color.float32[2] = (float) (colour & 0xFF) / 255.0f;
    ca.clearValue.color.float32[3] = d->target ? (float) ((colour >> 24) & 0xFF) / 255.0f : 1.0f;
    da = ca;
    da.imageView = z ? z->view_base : 0;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    memset(&da.clearValue, 0, sizeof da.clearValue);
    da.clearValue.depthStencil.depth = 1.0f;
    memset(&ri, 0, sizeof ri);
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent.width = (uint32_t) d->pass_w;
    ri.renderArea.extent.height = (uint32_t) d->pass_h;
    ri.layerCount = 1;
    ri.colorAttachmentCount = c ? 1 : 0;
    ri.pColorAttachments = &ca;
    ri.pDepthAttachment = z ? &da : NULL;
    d->f.vkCmdBeginRendering(gv_cmd(d), &ri);
    d->rendering = 1;
    d->dirty = GV_DIRTY_ALL;
}

static void gv_t_begin(int64_t dev, int64_t target, int64_t load, int64_t colour) {
    gv_dev *d = gv_of(dev);
    gv_target *t = (gv_target *) (intptr_t) target;
    if (!d) return;
    if (t && (t->res.gone || t->res.kind != GV_TARGET)) return;
    if (!t && !gv_back(d)) return;
    gv_pause(d);
    d->in_pass = 1;
    d->target = t;
    d->into = t ? (t->colour && t->depth ? 2 : t->colour ? 1 : 3) : 0;
    d->pass_w = t ? t->width : d->back->width;
    d->pass_h = t ? t->height : d->back->height;
    d->pipe = NULL;
    d->vb = d->ib = d->instb = NULL;
    d->ubo = 0;
    memset(d->tex, 0, sizeof d->tex);
    d->viewport.x = d->viewport.y = 0.0f;
    d->viewport.width = (float) d->pass_w;
    d->viewport.height = (float) d->pass_h;
    d->viewport.minDepth = 0.0f;
    d->viewport.maxDepth = 1.0f;
    d->scissor.offset.x = d->scissor.offset.y = 0;
    d->scissor.extent.width = (uint32_t) d->pass_w;
    d->scissor.extent.height = (uint32_t) d->pass_h;
    gv_resume(d, load == 0, (uint32_t) colour);
}

static void gv_t_use(int64_t dev, int64_t pipeline) {
    gv_dev *d = gv_of(dev);
    gv_pipe *p = (gv_pipe *) (intptr_t) pipeline;
    if (!d || !d->in_pass || !p || p->res.gone) return;
    d->pipe = p;
    d->vb = d->ib = d->instb = NULL;
    d->ubo = 0;
    memset(d->tex, 0, sizeof d->tex);
    d->dirty |= GV_DIRTY_PIPE | GV_DIRTY_SET;
}

/* Copy the block's values into this frame's memory, where the next draws read them. */
static void gv_stage_block(gv_dev *d) {
    unsigned char *p = gv_stage(d, (VkDeviceSize) d->ubo_n * 4u, d->ubo_align, &d->ubo, &d->ubo_offset);
    if (!p) {
        d->ubo = 0;
        return;
    }
    memcpy(p, d->ubo_data, (size_t) d->ubo_n * 4u);
    d->ubo_range = (VkDeviceSize) d->ubo_n * 4u;
    d->ubo_serial = d->serial_next;
    d->dirty |= GV_DIRTY_SET;
}

static void gv_t_uniforms(int64_t dev, const float *block, int64_t n) {
    gv_dev *d = gv_of(dev);
    if (!d || !d->pipe || n <= 0) return;
    if (n > d->ubo_cap) {
        float *grown = (float *) realloc(d->ubo_data, (size_t) n * 4u);
        if (!grown) return;
        d->ubo_data = grown;
        d->ubo_cap = n;
    }
    memcpy(d->ubo_data, block, (size_t) n * 4u);
    d->ubo_n = n;
    gv_stage_block(d);
}

static void gv_t_sample(int64_t dev, int64_t slot, int64_t tex) {
    gv_dev *d = gv_of(dev);
    gv_image *im = gv_texture_of(tex);
    if (!d || slot < 0 || slot >= 32) return;
    d->tex[slot] = im;
    d->dirty |= GV_DIRTY_SET;
}

static void gv_t_vertices(int64_t dev, int64_t buf) {
    gv_dev *d = gv_of(dev);
    gv_buffer *b = (gv_buffer *) (intptr_t) buf;
    if (!d || !b || b->res.gone) return;
    d->vb = b;
    d->dirty |= GV_DIRTY_VB;
}

static void gv_t_instances(int64_t dev, int64_t buf, int64_t first_float) {
    gv_dev *d = gv_of(dev);
    gv_buffer *b = (gv_buffer *) (intptr_t) buf;
    if (!d || !b || b->res.gone || first_float < 0) return;
    d->instb = b;
    d->inst_offset = (VkDeviceSize) first_float * 4u;
    d->dirty |= GV_DIRTY_VB;
}

static void gv_t_indices(int64_t dev, int64_t buf) {
    gv_dev *d = gv_of(dev);
    gv_buffer *b = (gv_buffer *) (intptr_t) buf;
    if (!d || !b || b->res.gone) return;
    d->ib = b;
    d->dirty |= GV_DIRTY_IB;
}

static void gv_t_viewport(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h) {
    gv_dev *d = gv_of(dev);
    if (!d) return;
    d->viewport.x = (float) x;
    d->viewport.y = (float) y;
    d->viewport.width = (float) (w > 0 ? w : 1);
    d->viewport.height = (float) (h > 0 ? h : 1);
    d->dirty |= GV_DIRTY_VIEW;
}

/* The rectangle draws are kept inside, cut to the pass's images as Vulkan requires. */
static void gv_t_scissor(int64_t dev, int64_t x, int64_t y, int64_t w, int64_t h, int64_t on) {
    gv_dev *d = gv_of(dev);
    int64_t x1, y1;
    if (!d) return;
    if (!on) {
        x = y = 0;
        w = d->pass_w;
        h = d->pass_h;
    }
    x1 = x + (w > 0 ? w : 0);
    y1 = y + (h > 0 ? h : 0);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > d->pass_w) x1 = d->pass_w;
    if (y1 > d->pass_h) y1 = d->pass_h;
    d->scissor.offset.x = (int32_t) x;
    d->scissor.offset.y = (int32_t) y;
    d->scissor.extent.width = (uint32_t) (x1 > x ? x1 - x : 0);
    d->scissor.extent.height = (uint32_t) (y1 > y ? y1 - y : 0);
    d->dirty |= GV_DIRTY_VIEW;
}

/* Send what changed since the last draw. */
static void gv_ready(gv_dev *d) {
    VkCommandBuffer cb;
    gv_pipe *p = d->pipe;
    int i;
    /* a texture written since the pass began is made sampleable again, outside the drawing */
    for (i = 0; i < p->textures; i++) {
        gv_image *c = d->target ? d->target->colour : d->back;
        gv_image *z = d->target ? d->target->depth : d->back_depth;
        if (d->tex[i] && d->tex[i] != c && d->tex[i] != z &&
            d->tex[i]->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            gv_pause(d);
    }
    gv_resume(d, 0, 0);
    cb = gv_cmd(d);
    /* the block's values were staged in a frame already submitted: stage them in this one */
    if (p->has_block && d->ubo && d->ubo_serial != d->serial_next) gv_stage_block(d);
    if (d->dirty & GV_DIRTY_PIPE) d->f.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
    if (d->dirty & GV_DIRTY_VIEW) {
        d->f.vkCmdSetViewport(cb, 0, 1, &d->viewport);
        d->f.vkCmdSetScissor(cb, 0, 1, &d->scissor);
    }
    if ((d->dirty & GV_DIRTY_VB) && d->vb) {
        VkDeviceSize zero = 0;
        d->f.vkCmdBindVertexBuffers(cb, 0, 1, &d->vb->buffer, &zero);
    }
    if ((d->dirty & GV_DIRTY_VB) && d->instb && p->has_instances)
        d->f.vkCmdBindVertexBuffers(cb, 1, 1, &d->instb->buffer, &d->inst_offset);
    if ((d->dirty & GV_DIRTY_IB) && d->ib) d->f.vkCmdBindIndexBuffer(cb, d->ib->buffer, 0, VK_INDEX_TYPE_UINT32);
    if ((d->dirty & GV_DIRTY_SET) && (p->has_block || p->textures)) {
        VkWriteDescriptorSet writes[33];
        VkDescriptorImageInfo images[32];
        VkDescriptorBufferInfo ubo;
        uint32_t n = 0;
        memset(writes, 0, sizeof writes);
        if (p->has_block && d->ubo) {
            ubo.buffer = d->ubo;
            ubo.offset = d->ubo_offset;
            ubo.range = d->ubo_range;
            writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[n].dstBinding = 0;
            writes[n].descriptorCount = 1;
            writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[n].pBufferInfo = &ubo;
            n++;
        }
        for (i = 0; i < p->textures; i++) {
            gv_image *im = d->tex[i];
            if (!im) continue;
            images[i].sampler = d->samplers[im->nearest ? 0 : im->mipped ? 2 : 1];
            images[i].imageView = im->mipped ? im->view_all : im->view_base;
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[n].dstBinding = 1 + (uint32_t) i;
            writes[n].descriptorCount = 1;
            writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[n].pImageInfo = &images[i];
            n++;
        }
        if (n) d->f.vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, n, writes);
    }
    d->dirty = 0;
}

static void gv_t_draw(int64_t dev, int64_t first, int64_t count, int64_t indexed, int64_t copies) {
    gv_dev *d = gv_of(dev);
    uint32_t times = copies > 0 ? (uint32_t) copies : 1u;
    if (!d || !d->in_pass || !d->pipe || count <= 0) return;
    gv_ready(d);
    if (indexed) {
        if (!d->ib) return;
        d->f.vkCmdDrawIndexed(gv_cmd(d), (uint32_t) count, times, 0, 0, 0);
    } else {
        d->f.vkCmdDraw(gv_cmd(d), (uint32_t) count, times, (uint32_t) first, 0);
    }
}

static void gv_t_end(int64_t dev) {
    gv_dev *d = gv_of(dev);
    if (!d || !d->in_pass) return;
    gv_pause(d);
    if (d->target) { /* a target's images rest sampleable */
        if (d->target->colour) gv_layout(d, d->target->colour, gv_rest(d, d->target->colour));
        if (d->target->depth) gv_layout(d, d->target->depth, gv_rest(d, d->target->depth));
    }
    d->in_pass = 0;
    d->target = NULL;
    d->pipe = NULL;
}

/* ── showing and reading back ──────────────────────────────────────────────────────────────── */

/* Copy the window's image to the swapchain, its rows turned over, and show it. */
static void gv_t_present(int64_t dev) {
    gv_dev *d = gv_of(dev);
    VkPresentInfoKHR pi;
    VkImageBlit blit;
    uint32_t index = 0;
    VkResult r;
    if (!d || d->in_pass || !d->back) return;
    if ((d->sc_stale || !d->swapchain) && !gv_swapchain(d)) return;
    gv_cmd(d); /* the frame's acquire semaphore is free once its last submission ran */
    r = d->f.vkAcquireNextImageKHR(d->device, d->swapchain, ~0ULL, d->frames[d->cur].acquired, 0, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        d->sc_stale = 1;
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return;
    if (r == VK_SUBOPTIMAL_KHR) d->sc_stale = 1;
    gv_layout(d, d->back, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    gv_levels(d, d->sc_images[index], 0, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    memset(&blit, 0, sizeof blit);
    blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
    blit.srcOffsets[1].x = d->back->width;
    blit.srcOffsets[1].y = d->back->height;
    blit.srcOffsets[1].z = 1;
    blit.dstOffsets[0].y = (int32_t) d->sc_extent.height;
    blit.dstOffsets[1].x = (int32_t) d->sc_extent.width;
    blit.dstOffsets[1].z = 1;
    d->f.vkCmdBlitImage(gv_cmd(d), d->back->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        d->sc_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                        (int) d->sc_extent.width == d->back->width &&
                                (int) d->sc_extent.height == d->back->height
                            ? VK_FILTER_NEAREST
                            : VK_FILTER_LINEAR);
    gv_levels(d, d->sc_images[index], 0, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    gv_layout(d, d->back, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    gv_submit(d, (int) index);
    memset(&pi, 0, sizeof pi);
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &d->sc_done[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &d->swapchain;
    pi.pImageIndices = &index;
    r = d->f.vkQueuePresentKHR(d->queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) d->sc_stale = 1;
}

/* The pixels of `target`'s colour (0: the window's), 0xAARRGGBB, the top row first. */
static void gv_t_capture(int64_t dev, int64_t target, uint32_t *out, int64_t n) {
    gv_dev *d = gv_of(dev);
    gv_target *t = (gv_target *) (intptr_t) target;
    gv_image *im;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkBufferImageCopy region;
    const uint32_t *px;
    int was, y;
    if (!d) return;
    if (t && (t->res.gone || t->res.kind != GV_TARGET)) return;
    im = t ? t->colour : d->back;
    if (!im || n != (int64_t) im->width * im->height || n <= 0) return;
    if (!gv_make_buffer(d, (VkDeviceSize) n * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &buffer, &memory))
        return;
    gv_pause(d);
    was = im->layout;
    gv_layout(d, im, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    memset(&region, 0, sizeof region);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t) im->width;
    region.imageExtent.height = (uint32_t) im->height;
    region.imageExtent.depth = 1;
    d->f.vkCmdCopyImageToBuffer(gv_cmd(d), im->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                                &region);
    gv_barrier(d);
    gv_layout(d, im, was);
    gv_wait(d, gv_submit(d, -1));
    if (d->f.vkMapMemory(d->device, memory, 0, VK_WHOLE_SIZE, 0, (void **) &px) == VK_SUCCESS) {
        for (y = 0; y < im->height; y++)
            memcpy(out + (size_t) y * im->width, px + (size_t) (im->height - 1 - y) * im->width,
                   (size_t) im->width * 4u);
    }
    d->f.vkDestroyBuffer(d->device, buffer, NULL);
    d->f.vkFreeMemory(d->device, memory, NULL);
}

static const gpu_backend gv_tab = {
    GPU_BACKEND_VERSION, "gpu_vulkan", "spirv",
    gv_t_open, gv_t_poll, gv_t_event_count, gv_t_events, gv_t_size, gv_t_close,
    gv_t_key_down, gv_t_pointer, gv_t_text_size, gv_t_text_copy,
    gv_t_buffer, gv_t_index_buffer, gv_t_buffer_update, gv_t_texture, gv_t_texture_put,
    gv_t_texture_put565,
    gv_t_texture_nearest, gv_t_texture_mipmaps, gv_t_target, gv_t_pipeline, gv_t_release,
    gv_t_begin, gv_t_use, gv_t_uniforms, gv_t_sample, gv_t_vertices, gv_t_instances,
    gv_t_indices, gv_t_viewport, gv_t_scissor, gv_t_draw, gv_t_end, gv_t_present, gv_t_capture,
};

/* This backend's table: Vulkan is looked for when a device opens, so the table is there on every
 * Linux machine, and `open` answers 0 where no Vulkan driver draws into an X window. */
int64_t gv_table(void) {
    return (int64_t) (intptr_t) &gv_tab;
}

#else /* not Linux: no window host for Vulkan here */

int64_t gv_table(void) {
    return 0;
}

#endif
