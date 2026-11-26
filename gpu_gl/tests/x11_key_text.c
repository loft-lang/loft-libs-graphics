/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The X11 key handler's typed text, checked on its C: what an input method commits reaches the
 * queue whole, at any length, as one Text event after the KeyDown.  The loft tests cannot reach
 * this — it needs an X server with an input method that commits text — so the handler runs here
 * against an `Xutf8LookupString` that answers as Xlib documents it: text longer than the buffer
 * is XBufferOverflow, nothing written and the length it needs returned.  Not part of `loft
 * test`; from this package's directory, with loft's C headers for `loft_width.h`:
 *
 *   cc -std=gnu99 -g -fsanitize=address,undefined -I"$LOFT/src/c" tests/x11_key_text.c \
 *      -o /tmp/gl_x11_key_text && /tmp/gl_x11_key_text
 *
 * On Linux as written; elsewhere add -D__linux__, which selects the X11 host (it opens libX11 at
 * run time and needs no X headers).  It prints "ok" and exits 0.
 *
 * @falsified-at: 1cca065 — by hand: gq_key in gl_base.h put back to push from its 64-byte buffer
 *   whatever length the lookup answered → under ASan a stack-buffer-overflow read in the
 *   101-byte cell; without ASan the 64-, 101- and 4999-byte cells and the never-fits cell
 *   FAIL; restored, "ok". */
#include "../c/gl_base.h"

#if !defined(GQ_X11)
#error "the X11 host is selected on Linux: build with -D__linux__ elsewhere"
#endif

#define X_BUFFER_OVERFLOW (-1)
#define X_LOOKUP_CHARS 2

static char committed[5000];
static int committed_len, always_overflows, lookups;
static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int lookup(void *ic, gx_input_event *e, char *out, int cap, gx_xid *sym, int *status) {
    (void) ic;
    (void) e;
    lookups++;
    *sym = 0;
    if (always_overflows || cap < committed_len) {
        *status = X_BUFFER_OVERFLOW;
        return always_overflows ? cap + 1 : committed_len;
    }
    memcpy(out, committed, (size_t) committed_len);
    *status = X_LOOKUP_CHARS;
    return committed_len;
}

/* `n` bytes of UTF-8: "é" (two bytes) repeated, an "a" first when `n` is odd. */
static void commit(int n) {
    int i = 0;
    if (n % 2) committed[i++] = 'a';
    while (i < n) {
        committed[i++] = (char) 0xc3;
        committed[i++] = (char) 0xa9;
    }
    committed_len = n;
}

static gq_window *window(void) {
    gq_window *w = (gq_window *) calloc(1, sizeof *w);
    w->xa.Xutf8LookupString = lookup;
    w->ic = (void *) w; /* any non-null input context: the handler only passes it on */
    return w;
}

static void release(gq_window *w) {
    free(w->events);
    free(w->text);
    free(w);
}

/* One key press with `n` bytes committed: a KeyDown, then one Text event holding them. */
static void typed(int n) {
    gq_window *w = window();
    gx_input_event e;
    memset(&e, 0, sizeof e);
    commit(n);
    gq_key(w, &e, 1);
    CHECK(w->event_count == 2, "%d bytes: %lld events, want KeyDown and Text", n, (long long) w->event_count);
    if (w->event_count == 2) {
        int64_t *t = w->events + GQ_EVENT_WORDS;
        CHECK(w->events[0] == GQ_EV_KEY_DOWN, "%d bytes: the first event is %lld", n, (long long) w->events[0]);
        CHECK(t[0] == GQ_EV_TEXT && t[1] == 0 && t[2] == n, "%d bytes: Text event (%lld, %lld, %lld)", n,
              (long long) t[0], (long long) t[1], (long long) t[2]);
    }
    CHECK(w->text_len == n && memcmp(w->text, committed, (size_t) n) == 0, "%d bytes: the queued text differs (%lld bytes)",
          n, (long long) w->text_len);
    release(w);
}

int main(void) {
    gq_window *w;
    gx_input_event e;
    typed(1);
    typed(62);
    typed(63);   /* the most the 64-byte buffer takes, with its terminator */
    typed(64);   /* the first length it does not */
    typed(101);
    typed(4999);

    /* An input method that overflows the buffer it was asked again with: the text is dropped,
     * the key is still reported. */
    always_overflows = 1;
    lookups = 0;
    w = window();
    memset(&e, 0, sizeof e);
    commit(80);
    gq_key(w, &e, 1);
    CHECK(lookups == 2, "asked %d times, want 2", lookups);
    CHECK(w->event_count == 1 && w->events[0] == GQ_EV_KEY_DOWN && w->text_len == 0,
          "text that never fits is dropped: %lld events, %lld text bytes", (long long) w->event_count,
          (long long) w->text_len);
    release(w);

    if (fails) return 1;
    printf("ok\n");
    return 0;
}
