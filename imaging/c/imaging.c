/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * The imaging library's native part, in C99: a PNG reader and writer with their own zlib —
 * inflate (stored, fixed and dynamic Huffman blocks), a deflate of one fixed-Huffman block
 * over an LZ77 hash chain, CRC-32 and Adler-32.  Built by loft itself (`[c] shim`, with `cc`);
 * bound by the `#c` declarations in src/imaging.loft.
 *
 * Reading answers 8-bit RGBA whatever the file holds: gray is expanded, a palette looked up,
 * a tRNS chunk becomes the alpha, a channel the file lacks is opaque, 16-bit samples keep
 * their high byte, and an Adam7 interlaced image is put back together.  A chunk whose CRC does not match, a truncated or
 * malformed stream, or a size that does not fit the caller's buffer answers failure. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── CRC-32 (ISO 3309, as PNG uses it) and Adler-32 ────────────────────────────────────── */

static uint32_t lim_crc_table[256];
static int lim_crc_ready = 0;

static uint32_t lim_crc32(uint32_t crc, const unsigned char *p, size_t n) {
    size_t i;
    if (!lim_crc_ready) {
        uint32_t c;
        unsigned k, j;
        for (k = 0; k < 256; k++) {
            c = k;
            for (j = 0; j < 8; j++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            lim_crc_table[k] = c;
        }
        lim_crc_ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < n; i++) crc = lim_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint32_t lim_adler32(const unsigned char *p, size_t n) {
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/* ── A growable byte buffer ─────────────────────────────────────────────────────────────── */

typedef struct { unsigned char *p; size_t len, cap; } lim_buf;

static int lim_push(lim_buf *b, const unsigned char *p, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        unsigned char *np;
        while (cap < b->len + n) cap *= 2;
        np = (unsigned char *) realloc(b->p, cap);
        if (!np) return 0;
        b->p = np;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->len, p, n);
    b->len += n;
    return 1;
}

static int lim_push_byte(lim_buf *b, unsigned char c) { return lim_push(b, &c, 1); }

/* ── Inflate (RFC 1951), after zlib's puff: canonical Huffman tables of counts and symbols ─ */

#define LIM_MAXBITS 15
#define LIM_MAXLCODES 286
#define LIM_MAXDCODES 30
#define LIM_MAXCODES (LIM_MAXLCODES + LIM_MAXDCODES)
#define LIM_FIXLCODES 288

typedef struct {
    const unsigned char *in;
    size_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt;
    lim_buf *out;
    int err;
} lim_inf;

typedef struct { short count[LIM_MAXBITS + 1]; short symbol[LIM_MAXCODES]; } lim_huff;

static int lim_bits(lim_inf *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        val |= (uint32_t) s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int) (val & ((1u << need) - 1));
}

static int lim_decode(lim_inf *s, const lim_huff *h) {
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= LIM_MAXBITS; len++) {
        code |= lim_bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Build the table from code lengths; 0 for a complete code, > 0 incomplete (allowed for a
 * single-code table), < 0 over-subscribed. */
static int lim_construct(lim_huff *h, const short *length, int n) {
    int symbol, len, left;
    short offs[LIM_MAXBITS + 1];
    for (len = 0; len <= LIM_MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= LIM_MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < LIM_MAXBITS; len++) offs[len + 1] = (short) (offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short) symbol;
    return left;
}

static const short lim_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short lim_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short lim_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short lim_dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int lim_codes(lim_inf *s, const lim_huff *lencode, const lim_huff *distcode) {
    int symbol, len;
    unsigned dist;
    do {
        symbol = lim_decode(s, lencode);
        if (symbol < 0) return 0;
        if (symbol < 256) {
            if (!lim_push_byte(s->out, (unsigned char) symbol)) return 0;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return 0;
            len = lim_lbase[symbol] + lim_bits(s, lim_lext[symbol]);
            symbol = lim_decode(s, distcode);
            if (symbol < 0 || symbol >= 30) return 0;
            dist = (unsigned) lim_dbase[symbol] + (unsigned) lim_bits(s, lim_dext[symbol]);
            if (s->err || dist > s->out->len) return 0;
            while (len--) {
                unsigned char c = s->out->p[s->out->len - dist];
                if (!lim_push_byte(s->out, c)) return 0;
            }
        }
    } while (symbol != 256);
    return !s->err;
}

static int lim_stored(lim_inf *s) {
    unsigned len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) return 0;
    len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
    if (s->in[s->inpos + 2] != (~len & 0xFF) || s->in[s->inpos + 3] != ((~len >> 8) & 0xFF)) return 0;
    s->inpos += 4;
    if (s->inpos + len > s->inlen) return 0;
    if (!lim_push(s->out, s->in + s->inpos, len)) return 0;
    s->inpos += len;
    return 1;
}

static int lim_fixed(lim_inf *s) {
    static int built = 0;
    static lim_huff lencode, distcode;
    if (!built) {
        short lengths[LIM_FIXLCODES];
        int symbol;
        for (symbol = 0; symbol < 144; symbol++) lengths[symbol] = 8;
        for (; symbol < 256; symbol++) lengths[symbol] = 9;
        for (; symbol < 280; symbol++) lengths[symbol] = 7;
        for (; symbol < LIM_FIXLCODES; symbol++) lengths[symbol] = 8;
        lim_construct(&lencode, lengths, LIM_FIXLCODES);
        for (symbol = 0; symbol < LIM_MAXDCODES; symbol++) lengths[symbol] = 5;
        lim_construct(&distcode, lengths, LIM_MAXDCODES);
        built = 1;
    }
    return lim_codes(s, &lencode, &distcode);
}

static int lim_dynamic(lim_inf *s) {
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[LIM_MAXCODES];
    lim_huff lencode, distcode;
    int nlen, ndist, ncode, index, err;
    nlen = lim_bits(s, 5) + 257;
    ndist = lim_bits(s, 5) + 1;
    ncode = lim_bits(s, 4) + 4;
    if (s->err || nlen > LIM_MAXLCODES || ndist > LIM_MAXDCODES) return 0;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short) lim_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return 0;
    err = lim_construct(&lencode, lengths, 19);
    if (err != 0) return 0;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = lim_decode(s, &lencode);
        int len;
        if (symbol < 0) return 0;
        if (symbol < 16) {
            lengths[index++] = (short) symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return 0;
                len = lengths[index - 1];
                symbol = 3 + lim_bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + lim_bits(s, 3);
            } else {
                symbol = 11 + lim_bits(s, 7);
            }
            if (s->err || index + symbol > nlen + ndist) return 0;
            while (symbol--) lengths[index++] = (short) len;
        }
    }
    if (lengths[256] == 0) return 0;
    err = lim_construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return 0;
    err = lim_construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return 0;
    return lim_codes(s, &lencode, &distcode);
}

/* A zlib stream (RFC 1950) into `out`; 1 on success. */
static int lim_zlib_inflate(const unsigned char *in, size_t inlen, lim_buf *out) {
    lim_inf s;
    int last, type, ok;
    if (inlen < 6) return 0;
    if ((in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return 0;
    s.in = in;
    s.inlen = inlen - 4;
    s.inpos = 2;
    s.bitbuf = 0;
    s.bitcnt = 0;
    s.out = out;
    s.err = 0;
    do {
        last = lim_bits(&s, 1);
        type = lim_bits(&s, 2);
        if (s.err) return 0;
        ok = type == 0 ? lim_stored(&s) : type == 1 ? lim_fixed(&s) : type == 2 ? lim_dynamic(&s) : 0;
        if (!ok) return 0;
    } while (!last);
    {
        const unsigned char *t = in + inlen - 4;
        uint32_t want = ((uint32_t) t[0] << 24) | ((uint32_t) t[1] << 16) | ((uint32_t) t[2] << 8) | t[3];
        /* The inflated data may end before the trailer's bytes were all consumed as bits;
         * the trailer is the LAST four bytes whatever the bit position. */
        if (lim_adler32(out->p, out->len) != want) return 0;
    }
    return 1;
}

/* ── Deflate: one fixed-Huffman block over an LZ77 hash chain (RFC 1951 § 3.2.6) ─────────── */

typedef struct { lim_buf *out; uint32_t bitbuf; int bitcnt; } lim_def;

static int lim_putbits(lim_def *d, uint32_t v, int n) {
    d->bitbuf |= v << d->bitcnt;
    d->bitcnt += n;
    while (d->bitcnt >= 8) {
        if (!lim_push_byte(d->out, (unsigned char) (d->bitbuf & 0xFF))) return 0;
        d->bitbuf >>= 8;
        d->bitcnt -= 8;
    }
    return 1;
}

/* A Huffman code is sent most-significant bit first. */
static int lim_putcode(lim_def *d, uint32_t code, int n) {
    uint32_t r = 0;
    int i;
    for (i = 0; i < n; i++) { r = (r << 1) | (code & 1); code >>= 1; }
    return lim_putbits(d, r, n);
}

static int lim_put_literal(lim_def *d, int sym) {
    if (sym < 144) return lim_putcode(d, 0x30 + (uint32_t) sym, 8);
    if (sym < 256) return lim_putcode(d, 0x190 + (uint32_t) (sym - 144), 9);
    if (sym < 280) return lim_putcode(d, (uint32_t) (sym - 256), 7);
    return lim_putcode(d, 0xC0 + (uint32_t) (sym - 280), 8);
}

static int lim_put_match(lim_def *d, int len, unsigned dist) {
    int c, dc;
    for (c = 28; c > 0 && lim_lbase[c] > len; c--) {}
    if (!lim_put_literal(d, 257 + c)) return 0;
    if (lim_lext[c] && !lim_putbits(d, (uint32_t) (len - lim_lbase[c]), lim_lext[c])) return 0;
    for (dc = 29; dc > 0 && (unsigned) lim_dbase[dc] > dist; dc--) {}
    if (!lim_putcode(d, (uint32_t) dc, 5)) return 0;
    if (lim_dext[dc] && !lim_putbits(d, dist - (uint32_t) lim_dbase[dc], lim_dext[dc])) return 0;
    return 1;
}

#define LIM_HBITS 15
#define LIM_HSIZE (1 << LIM_HBITS)
#define LIM_WINDOW 32768
#define LIM_MINMATCH 3
#define LIM_MAXMATCH 258
#define LIM_CHAIN 64

static int lim_zlib_deflate(const unsigned char *in, size_t n, lim_buf *out) {
    lim_def d;
    int32_t *head = NULL, *prev = NULL;
    size_t i;
    int ok = 1;
    d.out = out;
    d.bitbuf = 0;
    d.bitcnt = 0;
    if (!lim_push_byte(out, 0x78) || !lim_push_byte(out, 0x9C)) return 0;
    if (!lim_putbits(&d, 1, 1) || !lim_putbits(&d, 1, 2)) return 0; /* BFINAL, BTYPE fixed */
    head = (int32_t *) malloc(sizeof(int32_t) * LIM_HSIZE);
    prev = (int32_t *) malloc(sizeof(int32_t) * LIM_WINDOW);
    if (!head || !prev) { free(head); free(prev); return 0; }
    for (i = 0; i < LIM_HSIZE; i++) head[i] = -1;
    for (i = 0; i < LIM_WINDOW; i++) prev[i] = -1;
    i = 0;
    while (i < n && ok) {
        int best = 0;
        size_t bestpos = 0;
        if (i + LIM_MINMATCH <= n) {
            uint32_t h = ((uint32_t) in[i] << 10 ^ (uint32_t) in[i + 1] << 5 ^ in[i + 2]) & (LIM_HSIZE - 1);
            int32_t cand = head[h];
            int chain = LIM_CHAIN;
            while (cand >= 0 && chain-- > 0 && i - (size_t) cand <= LIM_WINDOW - 1) {
                int l = 0;
                size_t max = n - i < LIM_MAXMATCH ? n - i : LIM_MAXMATCH;
                while ((size_t) l < max && in[cand + l] == in[i + l]) l++;
                if (l > best) { best = l; bestpos = (size_t) cand; if ((size_t) l == max) break; }
                cand = prev[cand & (LIM_WINDOW - 1)];
            }
            prev[i & (LIM_WINDOW - 1)] = head[h];
            head[h] = (int32_t) i;
        }
        if (best >= LIM_MINMATCH) {
            size_t k;
            ok = lim_put_match(&d, best, (unsigned) (i - bestpos));
            /* The skipped positions enter the chain too, so later matches can reach them. */
            for (k = 1; k < (size_t) best && i + k + LIM_MINMATCH <= n; k++) {
                uint32_t h = ((uint32_t) in[i + k] << 10 ^ (uint32_t) in[i + k + 1] << 5 ^ in[i + k + 2]) & (LIM_HSIZE - 1);
                prev[(i + k) & (LIM_WINDOW - 1)] = head[h];
                head[h] = (int32_t) (i + k);
            }
            i += (size_t) best;
        } else {
            ok = lim_put_literal(&d, in[i]);
            i++;
        }
    }
    free(head);
    free(prev);
    if (!ok || !lim_put_literal(&d, 256)) return 0;
    if (d.bitcnt && !lim_putbits(&d, 0, 8 - d.bitcnt)) return 0;
    {
        uint32_t a = lim_adler32(in, n);
        unsigned char t[4];
        t[0] = (unsigned char) (a >> 24); t[1] = (unsigned char) (a >> 16);
        t[2] = (unsigned char) (a >> 8); t[3] = (unsigned char) a;
        return lim_push(out, t, 4);
    }
}

/* ── PNG ────────────────────────────────────────────────────────────────────────────────── */

static const unsigned char lim_sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

static uint32_t lim_be32(const unsigned char *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static int lim_read_file(const char *path, lim_buf *b) {
    FILE *f = fopen(path, "rb");
    unsigned char chunk[65536];
    size_t got;
    if (!f) return 0;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (!lim_push(b, chunk, got)) { fclose(f); return 0; }
    }
    fclose(f);
    return 1;
}

typedef struct {
    uint32_t w, h;
    int depth, color, interlace, channels;
    unsigned char plte[256 * 3];
    int nplte;
    /* tRNS: a palette's alpha table, or the transparent sample key of a gray / RGB image. */
    int has_trns, ntrns;
    unsigned char trns[256];
    unsigned tkey[3];
    lim_buf idat;
} lim_png;

/* The chunks: IHDR, PLTE and the IDATs, each CRC-checked; 1 when the file is a PNG. */
static int lim_parse(const unsigned char *p, size_t n, lim_png *png) {
    size_t pos = 8;
    int seen_ihdr = 0, seen_iend = 0;
    memset(png, 0, sizeof *png);
    if (n < 8 || memcmp(p, lim_sig, 8) != 0) return 0;
    while (pos + 12 <= n && !seen_iend) {
        uint32_t len = lim_be32(p + pos);
        const unsigned char *type = p + pos + 4;
        const unsigned char *data = p + pos + 8;
        uint32_t crc;
        if (len > 0x7FFFFFFFu || pos + 12 + (size_t) len > n) return 0;
        crc = lim_be32(data + len);
        if (lim_crc32(0, type, (size_t) len + 4) != crc) return 0;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13 || seen_ihdr) return 0;
            png->w = lim_be32(data);
            png->h = lim_be32(data + 4);
            png->depth = data[8];
            png->color = data[9];
            png->interlace = data[12];
            if (data[10] != 0 || data[11] != 0 || png->interlace > 1) return 0;
            switch (png->color) {
            case 0: png->channels = 1; if (!(png->depth == 1 || png->depth == 2 || png->depth == 4 || png->depth == 8 || png->depth == 16)) return 0; break;
            case 2: png->channels = 3; if (png->depth != 8 && png->depth != 16) return 0; break;
            case 3: png->channels = 1; if (!(png->depth == 1 || png->depth == 2 || png->depth == 4 || png->depth == 8)) return 0; break;
            case 4: png->channels = 2; if (png->depth != 8 && png->depth != 16) return 0; break;
            case 6: png->channels = 4; if (png->depth != 8 && png->depth != 16) return 0; break;
            default: return 0;
            }
            if (png->w == 0 || png->h == 0 || png->w > 0x7FFFFFFFu || png->h > 0x7FFFFFFFu) return 0;
            seen_ihdr = 1;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (!seen_ihdr || len % 3 != 0 || len > 256 * 3) return 0;
            memcpy(png->plte, data, len);
            png->nplte = (int) (len / 3);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (!seen_ihdr) return 0;
            if (png->color == 3) {
                if (len > 256) return 0;
                memcpy(png->trns, data, len);
                png->ntrns = (int) len;
            } else if (png->color == 0) {
                if (len != 2) return 0;
                png->tkey[0] = ((unsigned) data[0] << 8) | data[1];
            } else if (png->color == 2) {
                if (len != 6) return 0;
                png->tkey[0] = ((unsigned) data[0] << 8) | data[1];
                png->tkey[1] = ((unsigned) data[2] << 8) | data[3];
                png->tkey[2] = ((unsigned) data[4] << 8) | data[5];
            } else {
                return 0;
            }
            png->has_trns = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (!seen_ihdr) return 0;
            if (!lim_push(&png->idat, data, len)) return 0;
        } else if (memcmp(type, "IEND", 4) == 0) {
            seen_iend = 1;
        } else if (!(type[0] & 0x20) && !seen_ihdr) {
            return 0; /* a critical chunk before the header */
        }
        pos += 12 + (size_t) len;
    }
    return seen_ihdr && seen_iend && png->idat.len > 0 && (png->color != 3 || png->nplte > 0);
}

static int lim_paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
}

/* One pass's scanlines unfiltered in place (`rows` rows of `stride` bytes, each preceded by
 * its filter byte in `raw`); 1 on a known filter at every row. */
static int lim_unfilter(unsigned char *raw, uint32_t rows, size_t stride, int bpp) {
    unsigned char *prev = NULL;
    uint32_t y;
    size_t x;
    for (y = 0; y < rows; y++) {
        unsigned char *line = raw + (size_t) y * (stride + 1);
        int ft = line[0];
        unsigned char *cur = line + 1;
        switch (ft) {
        case 0: break;
        case 1: for (x = (size_t) bpp; x < stride; x++) cur[x] = (unsigned char) (cur[x] + cur[x - bpp]); break;
        case 2: if (prev) for (x = 0; x < stride; x++) cur[x] = (unsigned char) (cur[x] + prev[x]); break;
        case 3:
            for (x = 0; x < stride; x++) {
                int a = x >= (size_t) bpp ? cur[x - bpp] : 0, b = prev ? prev[x] : 0;
                cur[x] = (unsigned char) (cur[x] + ((a + b) >> 1));
            }
            break;
        case 4:
            for (x = 0; x < stride; x++) {
                int a = x >= (size_t) bpp ? cur[x - bpp] : 0, b = prev ? prev[x] : 0;
                int c = (prev && x >= (size_t) bpp) ? prev[x - bpp] : 0;
                cur[x] = (unsigned char) (cur[x] + lim_paeth(a, b, c));
            }
            break;
        default: return 0;
        }
        prev = cur;
    }
    return 1;
}

/* Sample `i` of an unfiltered scanline at `depth` bits, as 0..255 (a 16-bit sample's high
 * byte, a narrow sample scaled for gray, raw for a palette index). */
static int lim_sample(const unsigned char *line, size_t i, int depth, int scale) {
    switch (depth) {
    case 16: return line[2 * i];
    case 8: return line[i];
    case 4: { int v = (line[i / 2] >> (4 * (1 - i % 2))) & 15; return scale ? v * 17 : v; }
    case 2: { int v = (line[i / 4] >> (2 * (3 - i % 4))) & 3; return scale ? v * 85 : v; }
    default: { int v = (line[i / 8] >> (7 - i % 8)) & 1; return scale ? v * 255 : v; }
    }
}

/* Sample `i` at its full depth, for the tRNS key compare. */
static unsigned lim_raw(const unsigned char *line, size_t i, int depth) {
    if (depth == 16) return ((unsigned) line[2 * i] << 8) | line[2 * i + 1];
    return (unsigned) lim_sample(line, i, depth, 0);
}

/* Pixel `x` of an unfiltered scanline written as RGBA at `out`: gray expanded, a palette
 * looked up (its tRNS entry the alpha), a transparent key alpha 0, no channel opaque — the
 * `png` crate's `normalize_to_color8` read. */
static void lim_pixel(const lim_png *png, const unsigned char *line, size_t x, unsigned char *out) {
    int d = png->depth;
    out[3] = 255;
    switch (png->color) {
    case 0:
        out[0] = out[1] = out[2] = (unsigned char) lim_sample(line, x, d, 1);
        if (png->has_trns && lim_raw(line, x, d) == png->tkey[0]) out[3] = 0;
        break;
    case 2:
        out[0] = (unsigned char) lim_sample(line, 3 * x, d, 0);
        out[1] = (unsigned char) lim_sample(line, 3 * x + 1, d, 0);
        out[2] = (unsigned char) lim_sample(line, 3 * x + 2, d, 0);
        if (png->has_trns && lim_raw(line, 3 * x, d) == png->tkey[0] && lim_raw(line, 3 * x + 1, d) == png->tkey[1]
            && lim_raw(line, 3 * x + 2, d) == png->tkey[2]) out[3] = 0;
        break;
    case 3: {
        int idx = lim_sample(line, x, d, 0);
        if (idx >= png->nplte) idx = 0;
        memcpy(out, png->plte + 3 * idx, 3);
        if (png->has_trns && idx < png->ntrns) out[3] = png->trns[idx];
        break;
    }
    case 4:
        out[0] = out[1] = out[2] = (unsigned char) lim_sample(line, 2 * x, d, 1);
        out[3] = (unsigned char) lim_sample(line, 2 * x + 1, d, 1);
        break;
    default:
        out[0] = (unsigned char) lim_sample(line, 4 * x, d, 0);
        out[1] = (unsigned char) lim_sample(line, 4 * x + 1, d, 0);
        out[2] = (unsigned char) lim_sample(line, 4 * x + 2, d, 0);
        out[3] = (unsigned char) lim_sample(line, 4 * x + 3, d, 0);
        break;
    }
}

/* The image as RGBA8 into `rgba` (4 * w * h bytes); 1 on success. */
static int lim_png_decode(lim_png *png, unsigned char *rgba) {
    lim_buf raw = { NULL, 0, 0 };
    int bits = png->depth * png->channels;
    int bpp = bits < 8 ? 1 : bits / 8;
    size_t pos = 0;
    int ok = 1;
    if (!lim_zlib_inflate(png->idat.p, png->idat.len, &raw)) { free(raw.p); return 0; }
    if (!png->interlace) {
        size_t stride = ((size_t) png->w * (size_t) bits + 7) / 8;
        uint32_t y;
        if (raw.len != (stride + 1) * png->h) ok = 0;
        else if (!lim_unfilter(raw.p, png->h, stride, bpp)) ok = 0;
        else for (y = 0; y < png->h; y++) {
            const unsigned char *line = raw.p + (size_t) y * (stride + 1) + 1;
            size_t x;
            for (x = 0; x < png->w; x++) lim_pixel(png, line, x, rgba + 4 * ((size_t) y * png->w + x));
        }
    } else {
        static const int xs[7] = { 0, 4, 0, 2, 0, 1, 0 }, ys[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int xd[7] = { 8, 8, 4, 4, 2, 2, 1 }, yd[7] = { 8, 8, 8, 4, 4, 2, 2 };
        int pass;
        for (pass = 0; pass < 7 && ok; pass++) {
            uint32_t pw = (png->w + (uint32_t) xd[pass] - 1 - (uint32_t) xs[pass]) / (uint32_t) xd[pass];
            uint32_t ph = (png->h + (uint32_t) yd[pass] - 1 - (uint32_t) ys[pass]) / (uint32_t) yd[pass];
            size_t stride, y;
            if (png->w <= (uint32_t) xs[pass] || png->h <= (uint32_t) ys[pass]) continue;
            stride = ((size_t) pw * (size_t) bits + 7) / 8;
            if (pos + (stride + 1) * ph > raw.len) { ok = 0; break; }
            if (!lim_unfilter(raw.p + pos, ph, stride, bpp)) { ok = 0; break; }
            for (y = 0; y < ph; y++) {
                const unsigned char *line = raw.p + pos + y * (stride + 1) + 1;
                size_t x;
                for (x = 0; x < pw; x++) {
                    size_t ix = (size_t) xs[pass] + x * (size_t) xd[pass];
                    size_t iy = (size_t) ys[pass] + y * (size_t) yd[pass];
                    lim_pixel(png, line, x, rgba + 4 * (iy * png->w + ix));
                }
            }
            pos += (stride + 1) * ph;
        }
        if (ok && pos != raw.len) ok = 0;
    }
    free(raw.p);
    return ok;
}

int64_t lim_png_size(const char *path);
int lim_png_read(const char *path, void *rgba, int64_t count);
/* The width and height as (w << 32) | h, -1 for a file that is not a readable PNG. */
int lim_png_write(const char *path, int64_t w, int64_t h, const void *rgba, int64_t count);

/* The image's width and height as (w << 32) | h; -1 when the file is not a readable PNG. */
int64_t lim_png_size(const char *path) {
    lim_buf file = { NULL, 0, 0 };
    lim_png png;
    int64_t r = -1;
    if (lim_read_file(path, &file) && lim_parse(file.p, file.len, &png))
        r = ((int64_t) png.w << 32) | (int64_t) png.h;
    free(png.idat.p);
    free(file.p);
    return r;
}

/* The image as RGBA into `rgba`, which holds `count` pixels of four bytes; 1 when it was read
 * and its size is exactly `count`. */
int lim_png_read(const char *path, void *rgba, int64_t count) {
    lim_buf file = { NULL, 0, 0 };
    lim_png png;
    int ok = 0;
    if (lim_read_file(path, &file) && lim_parse(file.p, file.len, &png)
        && (int64_t) png.w * (int64_t) png.h == count && rgba)
        ok = lim_png_decode(&png, (unsigned char *) rgba);
    free(png.idat.p);
    free(file.p);
    return ok;
}

/* One chunk: length, type, data, and the CRC over type and data (`lim_crc32` continues from a
 * previous value). */
static int lim_chunk(lim_buf *out, const char *type, const unsigned char *data, size_t len) {
    unsigned char hdr[8], crcb[4];
    uint32_t crc;
    hdr[0] = (unsigned char) (len >> 24); hdr[1] = (unsigned char) (len >> 16);
    hdr[2] = (unsigned char) (len >> 8); hdr[3] = (unsigned char) len;
    memcpy(hdr + 4, type, 4);
    crc = lim_crc32(0, hdr + 4, 4);
    if (len) crc = lim_crc32(crc, data, len);
    crcb[0] = (unsigned char) (crc >> 24); crcb[1] = (unsigned char) (crc >> 16);
    crcb[2] = (unsigned char) (crc >> 8); crcb[3] = (unsigned char) crc;
    return lim_push(out, hdr, 8) && (len == 0 || lim_push(out, data, len)) && lim_push(out, crcb, 4);
}

/* `rgba` (`count` pixels of four bytes, at least w * h of them) as an 8-bit PNG at `path`:
 * RGB when every pixel is opaque, RGBA otherwise — the colour type the Rust part chose, so an
 * all-opaque image writes the file it always wrote; every scanline unfiltered.  1 when the file
 * was written whole. */
int lim_png_write(const char *path, int64_t w, int64_t h, const void *rgba, int64_t count) {
    lim_buf raw = { NULL, 0, 0 }, z = { NULL, 0, 0 }, out = { NULL, 0, 0 };
    unsigned char ihdr[13];
    const unsigned char *px = (const unsigned char *) rgba;
    int64_t y, i, n;
    int ok = 0, alpha = 0;
    FILE *f;
    if (w <= 0 || h <= 0 || w > 0x7FFFFFFF || h > 0x7FFFFFFF || count < w * h || !rgba) return 0;
    n = w * h;
    for (i = 0; i < n && !alpha; i++) alpha = px[4 * i + 3] != 255;
    for (y = 0; y < h; y++) {
        if (!lim_push_byte(&raw, 0)) goto done;
        if (alpha) {
            if (!lim_push(&raw, px + (size_t) y * (size_t) w * 4, (size_t) w * 4)) goto done;
        } else {
            int64_t x;
            for (x = 0; x < w; x++)
                if (!lim_push(&raw, px + ((size_t) y * (size_t) w + (size_t) x) * 4, 3)) goto done;
        }
    }
    if (!lim_zlib_deflate(raw.p, raw.len, &z)) goto done;
    ihdr[0] = (unsigned char) (w >> 24); ihdr[1] = (unsigned char) (w >> 16);
    ihdr[2] = (unsigned char) (w >> 8); ihdr[3] = (unsigned char) w;
    ihdr[4] = (unsigned char) (h >> 24); ihdr[5] = (unsigned char) (h >> 16);
    ihdr[6] = (unsigned char) (h >> 8); ihdr[7] = (unsigned char) h;
    ihdr[8] = 8; ihdr[9] = (unsigned char) (alpha ? 6 : 2); ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    if (!lim_push(&out, lim_sig, 8) || !lim_chunk(&out, "IHDR", ihdr, 13)
        || !lim_chunk(&out, "IDAT", z.p, z.len) || !lim_chunk(&out, "IEND", NULL, 0)) goto done;
    f = fopen(path, "wb");
    if (!f) goto done;
    ok = fwrite(out.p, 1, out.len, f) == out.len;
    if (fclose(f) != 0) ok = 0;
done:
    free(raw.p);
    free(z.p);
    free(out.p);
    return ok;
}
