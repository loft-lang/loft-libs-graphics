/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* A shader for the Vulkan backend: SPIR-V carried in a loft text as base64, and what this
 * backend reads of it before a pipeline is built — the uniform block's layout, the texture
 * bindings and the vertex inputs — so a shader that does not fit its program is refused here,
 * with both sides said on stderr, rather than handed to the driver. */
#ifndef GV_SPIRV_H
#define GV_SPIRV_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The words of the base64 text `src` (whitespace skipped), or NULL when it is not base64 of a
 * SPIR-V module.  The caller frees them. */
static uint32_t *gv_spirv_words(const char *src, size_t *count) {
    size_t len = strlen(src), n = 0, bytes = 0;
    unsigned char *out = (unsigned char *) malloc(len / 4 * 3 + 4);
    uint32_t acc = 0;
    int bits = 0;
    size_t i;
    if (!out) return NULL;
    for (i = 0; i < len; i++) {
        int c = (unsigned char) src[i], v;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') break;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else {
            free(out);
            return NULL;
        }
        acc = (acc << 6) | (uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[bytes++] = (unsigned char) (acc >> bits);
        }
    }
    n = bytes / 4;
    if (bytes % 4 != 0 || n < 5 || ((uint32_t *) (void *) out)[0] != 0x07230203u) {
        free(out);
        return NULL;
    }
    *count = n;
    return (uint32_t *) (void *) out;
}

#define GV_SPV_MAX_BLOCK 64

/* What a module declares that a pipeline layout and a vertex layout must agree with. */
typedef struct {
    int stage;               /* the execution model of `main`: 0 vertex, 4 fragment, -1 none */
    int bad;                 /* a construct this backend has no binding for, said on stderr */
    int block;               /* a uniform block: 1, its set and binding below */
    int block_set, block_binding;
    int block_n;             /* its members: where each starts and how many bytes it holds */
    int block_offset[GV_SPV_MAX_BLOCK], block_size[GV_SPV_MAX_BLOCK];
    int block_bytes;         /* the end of its last member */
    int row_major;           /* a matrix member laid out by rows */
    int odd_stride;          /* a matrix or array stride other than 16 bytes */
    int samplers;            /* how many combined image samplers, their sets and bindings */
    int sampler_set[32], sampler_binding[32];
    int inputs;              /* the vertex inputs' locations (built-ins left out) */
    int input_location[32];
} gv_spv_info;

/* An id's facts, gathered in one pass. */
typedef struct {
    uint16_t op;          /* the instruction that made it: a type, a constant, a variable */
    uint32_t a, b, c;     /* its operands, by op: see gv_spv_read */
    const uint32_t *members;
    uint32_t member_n;
    int32_t binding, set, location, builtin, block, array_stride;
} gv_spv_id;

/* The bytes a block member of type `t` holds, std140; -1 when this backend has no field kind
 * for it.  `matrix_stride` is the member's own MatrixStride decoration. */
static int gv_spv_size(const gv_spv_id *ids, uint32_t bound, uint32_t t, int matrix_stride,
                       gv_spv_info *out) {
    const gv_spv_id *d;
    if (t >= bound) return -1;
    d = &ids[t];
    switch (d->op) {
    case 22: /* OpTypeFloat: width */
        return d->a == 32 ? 4 : -1;
    case 23: { /* OpTypeVector: component type, count */
        int one = gv_spv_size(ids, bound, d->a, 0, out);
        return one == 4 ? (int) d->b * 4 : -1;
    }
    case 24: { /* OpTypeMatrix: column type, columns */
        if (matrix_stride != 16) out->odd_stride = 1;
        return (int) d->b * 16;
    }
    case 28: { /* OpTypeArray: element type, length id */
        uint32_t len_id = d->b;
        if (len_id >= bound || ids[len_id].op != 43) return -1;
        if (d->array_stride != 16) out->odd_stride = 1;
        return (int) ids[len_id].a * 16;
    }
    default:
        return -1;
    }
}

/* Read `words` into `out`.  Answers 0 when the module is not one this backend can bind. */
static int gv_spv_read(const uint32_t *words, size_t n, gv_spv_info *out) {
    uint32_t bound, entry = 0, struct_id = 0;
    gv_spv_id *ids;
    size_t at;
    int found_entry = 0;
    struct {
        uint32_t s, m, deco, value;
    } *mdeco = NULL;
    size_t mdeco_n = 0, mdeco_cap = 0, k;
    memset(out, 0, sizeof *out);
    out->stage = -1;
    if (n < 5 || words[0] != 0x07230203u) return 0;
    bound = words[3];
    if (bound == 0 || bound > (1u << 22)) return 0;
    ids = (gv_spv_id *) calloc(bound, sizeof(gv_spv_id));
    if (!ids) return 0;
    for (k = 0; k < bound; k++) ids[k].binding = ids[k].set = ids[k].location = ids[k].builtin = -1;
    for (at = 5; at < n;) {
        uint32_t wc = words[at] >> 16, op = words[at] & 0xFFFFu;
        const uint32_t *w = words + at;
        if (wc == 0 || at + wc > n) goto broken;
        switch (op) {
        case 15: /* OpEntryPoint: model, id, name… */
            if (wc >= 4 && strcmp((const char *) (w + 3), "main") == 0 && !found_entry) {
                out->stage = (int) w[1];
                entry = w[2];
                found_entry = 1;
            }
            break;
        case 71: /* OpDecorate: target, decoration, literal */
            if (wc >= 3 && w[1] < bound) {
                gv_spv_id *d = &ids[w[1]];
                if (w[2] == 33 && wc >= 4) d->binding = (int32_t) w[3];
                if (w[2] == 34 && wc >= 4) d->set = (int32_t) w[3];
                if (w[2] == 30 && wc >= 4) d->location = (int32_t) w[3];
                if (w[2] == 11 && wc >= 4) d->builtin = (int32_t) w[3];
                if (w[2] == 2) d->block = 1;
                if (w[2] == 3) d->block = 2; /* BufferBlock: a storage buffer */
                if (w[2] == 6 && wc >= 4) d->array_stride = (int32_t) w[3];
            }
            break;
        case 72: /* OpMemberDecorate: struct, member, decoration, literal */
            if (wc >= 4) {
                if (mdeco_n == mdeco_cap) {
                    size_t cap = mdeco_cap ? mdeco_cap * 2 : 64;
                    void *grown = realloc(mdeco, cap * sizeof *mdeco);
                    if (!grown) goto broken;
                    mdeco = grown;
                    mdeco_cap = cap;
                }
                mdeco[mdeco_n].s = w[1];
                mdeco[mdeco_n].m = w[2];
                mdeco[mdeco_n].deco = w[3];
                mdeco[mdeco_n].value = wc >= 5 ? w[4] : 0;
                mdeco_n++;
            }
            break;
        case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 32: case 43:
            /* types, OpTypePointer (id, storage class, type) and OpConstant (type, id, value) */
            if (op == 43) {
                if (wc >= 4 && w[2] < bound) {
                    ids[w[2]].op = 43;
                    ids[w[2]].a = w[3];
                }
            } else if (wc >= 2 && w[1] < bound) {
                ids[w[1]].op = (uint16_t) op;
                ids[w[1]].a = wc >= 3 ? w[2] : 0;
                ids[w[1]].b = wc >= 4 ? w[3] : 0;
            }
            break;
        case 30: /* OpTypeStruct: id, members… */
            if (wc >= 2 && w[1] < bound) {
                ids[w[1]].op = 30;
                ids[w[1]].members = w + 2;
                ids[w[1]].member_n = wc - 2;
            }
            break;
        case 59: /* OpVariable: pointer type, id, storage class */
            if (wc >= 4 && w[2] < bound) {
                ids[w[2]].op = 59;
                ids[w[2]].a = w[1];
                ids[w[2]].c = w[3];
            }
            break;
        default:
            break;
        }
        at += wc;
    }
    (void) entry;
    for (k = 0; k < bound; k++) {
        gv_spv_id *v = &ids[k];
        uint32_t pointee;
        if (v->op != 59) continue;
        pointee = v->a < bound && ids[v->a].op == 32 ? ids[v->a].b : 0;
        if (pointee >= bound) continue;
        if (v->c == 1 && out->stage == 0) { /* Input, read by the vertex stage */
            if (v->builtin < 0 && ids[pointee].block == 0 && out->inputs < 32)
                out->input_location[out->inputs++] = v->location;
        } else if (v->c == 2 && ids[pointee].op == 30 && ids[pointee].block == 1) { /* Uniform */
            if (out->block) {
                fprintf(stderr, "gpu_vulkan: the shader has more than one uniform block\n");
                out->bad = 1;
            }
            out->block = 1;
            out->block_set = v->set;
            out->block_binding = v->binding;
            struct_id = pointee;
        } else if (v->c == 0) { /* UniformConstant */
            if (ids[pointee].op == 27 && out->samplers < 32) {
                out->sampler_set[out->samplers] = v->set;
                out->sampler_binding[out->samplers] = v->binding;
                out->samplers++;
            } else {
                fprintf(stderr, "gpu_vulkan: the shader reads a resource that is not a sampler2D "
                                "(a texture alone, a sampler alone, or an array of them)\n");
                out->bad = 1;
            }
        } else if (v->c == 2 || v->c == 9 || v->c == 12) {
            fprintf(stderr, "gpu_vulkan: the shader reads a storage buffer or push constants, "
                            "which a gpu program does not declare\n");
            out->bad = 1;
        }
    }
    if (struct_id) {
        const gv_spv_id *s = &ids[struct_id];
        uint32_t m;
        if (s->member_n > GV_SPV_MAX_BLOCK) {
            out->bad = 1;
        } else {
            for (m = 0; m < s->member_n; m++) {
                int offset = -1, stride = 0, size;
                for (k = 0; k < mdeco_n; k++) {
                    if (mdeco[k].s != struct_id || mdeco[k].m != m) continue;
                    if (mdeco[k].deco == 35) offset = (int) mdeco[k].value;
                    if (mdeco[k].deco == 7) stride = (int) mdeco[k].value;
                    if (mdeco[k].deco == 4) out->row_major = 1;
                }
                size = gv_spv_size(ids, bound, s->members[m], stride, out);
                out->block_offset[m] = offset;
                out->block_size[m] = size;
                if (offset >= 0 && size > 0 && offset + size > out->block_bytes)
                    out->block_bytes = offset + size;
            }
            out->block_n = (int) s->member_n;
        }
    }
    free(mdeco);
    free(ids);
    return found_entry;
broken:
    free(mdeco);
    free(ids);
    return 0;
}

#endif
