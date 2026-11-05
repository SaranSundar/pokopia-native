/* Linux counterpart of posix_win.h (docs/LINUX-PLAN.md): the real POSIX headers, plus the BSD and Apple extras the ported nxnative files use
   (strlcpy, strlcat, mergesort, clock_gettime_nsec_np, the one-argument pthread_setname_np), and the Win32 subset nxrt's own code uses
   (linux/windows.h). */
#ifndef NXRT_POSIX_LINUX_H
#define NXRT_POSIX_LINUX_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "plat.h"
#include "windows.h"   /* plat/linux/windows.h */

static inline size_t nxrt_strlcpy(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; }
    return l;
}
static inline size_t nxrt_strlcat(char *d, const char *s, size_t n) {
    size_t l = strnlen(d, n);
    if (l == n) return n + strlen(s);
    return l + nxrt_strlcpy(d + l, s, n - l);
}
#define strlcpy nxrt_strlcpy   /* glibc has them only from 2.38; the kit is built against 2.35 */
#define strlcat nxrt_strlcat

static inline uint64_t clock_gettime_nsec_np(clockid_t clk) {
    struct timespec t; clock_gettime(clk, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
/* Apple's form (the calling thread only); glibc's takes the thread first */
#define pthread_setname_np(name) plat_thread_name(name)

/* BSD's mergesort: a stable sort (hle_hid.c's DM_KEYS steps) */
static inline int mergesort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    if (n < 2) return 0;
    unsigned char *a = (unsigned char *)base, *t = (unsigned char *)malloc(n * sz);
    if (!t) return -1;
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
            while (i < mid && j < hi) { if (cmp(a + j * sz, a + i * sz) < 0) memcpy(t + k++ * sz, a + j++ * sz, sz); else memcpy(t + k++ * sz, a + i++ * sz, sz); }
            while (i < mid) memcpy(t + k++ * sz, a + i++ * sz, sz);
            while (j < hi) memcpy(t + k++ * sz, a + j++ * sz, sz);
        }
        memcpy(a, t, n * sz);
    }
    free(t);
    return 0;
}

#endif
