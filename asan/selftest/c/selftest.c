/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* On purpose: a read of a block after its release.  Under AddressSanitizer this stops the
 * program with "heap-use-after-free"; without it the read answers something and the program
 * goes on, which is what `asan/run.sh` must tell apart. */
#include <stdint.h>
#include <stdlib.h>

int64_t asan_selftest_read_freed(int64_t n) {
    volatile int64_t *block = (volatile int64_t *) malloc(8 * sizeof(int64_t));
    int64_t got;
    if (!block) return -1;
    block[n & 7] = 41 + n;
    free((void *) block);
    got = block[n & 7];
    return got;
}
