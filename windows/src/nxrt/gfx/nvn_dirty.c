// from nxnative dm/gfx/nvn/nvn_dirty.c (MIT, (c) seedds), ported to Win32.
// Write-protect dirty-page tracking for guest memory: pool pages of packaged textures (CPU writes, GPU reads) are set read-only after upload, and as soon as the game writes it faults -> mark dirty, unprotect;
// before use, check the write generation of the covered pages and re-upload if newer than at upload time. NVN semantics: a texture descriptor points to pool memory, and the GPU reads the current content every time
// (measured: the game writes streamed assets into texture storage that is still being sampled, without re-registering).
// Signal handler constraint: the page-fault path uses only atomic operations and mprotect, no allocation, no locks.
// Windows: VirtualProtect in place of mprotect (the fault path is a vectored exception handler, crash.c), QueryPerformanceCounter in place of mach time, 4 KB pages.
#include "nvn_dirty.h"
#include "dm.h"
#include <stdatomic.h>
#define PROT_READ 1
#define PROT_WRITE 2
static uint64_t mach_absolute_time(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t)c.QuadPart; }
typedef struct { uint64_t numer, denom; } mach_timebase_info_data_t;   // ticks * numer / denom = ns
static void mach_timebase_info(mach_timebase_info_data_t *tb) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); tb->numer = 1000000000ull; tb->denom = (uint64_t)f.QuadPart; }
static _Atomic int g_vp_red;
static int mprotect_(void *a, size_t len, int prot, int quiet) {   // 0 = done
    DWORD old; if (VirtualProtect(a, len, prot & PROT_WRITE ? PAGE_READWRITE : PAGE_READONLY, &old)) return 0;
    if (!quiet && !atomic_exchange(&g_vp_red, 1))
        LOG("[RED] dirty-page tracking: VirtualProtect %p+%#zx failed (error %lu): those pages stay writable and count as always written; later failures are not logged",
            a, len, (unsigned long)GetLastError());
    return -1;
}

// Order of page state and mprotect: the side changing protection first sets the state to BUSY and sets the final state only after mprotect has landed; other threads wait when they see BUSY.
//   set read-only: NONE -> BUSY -> mprotect(read-only) -> PROT; unprotect: PROT -> BUSY -> mprotect(writable) -> NONE.
// Invariant: a page whose state is NONE is always writable, one whose state is PROT is always read-only. Setting read-only used to be "record PROT, then mprotect"; in between, another thread's unprotect-before-write
// (the movie decoder thread writing a frame into texture storage, a disk read into a guest buffer) could set the state back to NONE, then the mprotect landed => the page read-only with state NONE: a game thread writing it got only
// "re-execute" from the fault handler and spun forever; a host system call reading into it saw NONE in prep_write, skipped, and failed with EFAULT (reproduced by t_dirty_race).
// BUSY lasts at most one mprotect (plus a few atomic writes when setting read-only). The wait is capped at BUSY_WAIT_MS and reports red beyond it: in the fault handler it then returns "not our page" so the crash handler reports truthfully
enum { PG_NONE = 0, PG_PROT = 1, PG_BUSY = 2 };
enum { BUSY_WAIT_MS = 2000 };
// Tracked ranges = the memory pools (registered by MemoryPoolInitialize). After a pool is destroyed its live=0 slot is reused; the signal handler scans without locks.
// The table grows in blocks of 1,024 slots, up to 262,144; a block is never moved or freed, so the signal handler reads it without locks. A pool past the end goes untracked: its textures count
// as dirty and are re-uploaded at every use. The table was a fixed 256 slots (Zelda has about 300 pools at the title), then a fixed 4,096, and Breath of the Wild reaches 4,006 pools in its first
// shrine. The scans only cover slots in use, so the capacity itself costs nothing
enum { BLK_SHIFT = 10, BLK = 1 << BLK_SHIFT, NBLK = 256, MAX_RANGE = BLK * NBLK };
// Ranges are rounded to host pages (16 KB) but guest memory pools are only 4 KB aligned: adjacent pools share their first / last pages, so one address can fall in several registered ranges.
// So: "is this texture tracked" looks for a range that holds its whole span; a fault / host pre-write unprotects and stamps every range covering that page; pool destruction identifies its range by the pool's exact start mem.
// (Previously the first range containing the address was always taken: two SMBW ASTC 6x5 textures whose pool's first page is shared with a small preceding pool were judged untracked and re-uploaded on every use, about 6 MB per frame)
typedef struct { uintptr_t base, end, mem; _Atomic uint8_t *state; _Atomic uint64_t *gen; size_t cap; _Atomic int live;
                 _Atomic int k; _Atomic int64_t kn; uintptr_t klo, khi; _Atomic uint64_t hv; } range_t;   // the kernel tracks it (Linux, below): protected pages, their extent, the submission last harvested
static _Atomic(range_t *) g_blk[NBLK];
static inline range_t *rg(int i) { return &atomic_load_explicit(&g_blk[i >> BLK_SHIFT], memory_order_acquire)[i & (BLK - 1)]; }
static _Atomic int g_nr;
static uintptr_t g_pg, g_shift;
static _Atomic uint64_t g_now = 1;
static _Atomic uint64_t g_faults, g_preps;
static _Atomic uint64_t g_adds_total;
static _Atomic int g_live, g_live_peak;   // number of registered pools and peak (for the memory readout line)
void nvn_dirty_stat(int *live, int *peak, int *cap) { *live = atomic_load(&g_live); *peak = atomic_load(&g_live_peak); *cap = MAX_RANGE; }
static _Atomic int g_overflow_logged;
static pthread_mutex_t g_slot_mu = PTHREAD_MUTEX_INITIALIZER;   // Pool creation, retirement and protection serialize descriptor lifetime; the page-fault path does not touch this mutex
static _Atomic uint64_t g_mp_calls, g_mp_pages, g_mp_ticks;   // This module's mprotect: call count, pages covered, time spent (mach timebase, safe in the page-fault path too)
static int mp_(uintptr_t a, size_t len, int prot, int quiet) {
    uint64_t t = mach_absolute_time(); int r = mprotect_((void *)a, len, prot, quiet);
    atomic_fetch_add(&g_mp_ticks, mach_absolute_time() - t); atomic_fetch_add(&g_mp_calls, 1); atomic_fetch_add(&g_mp_pages, len >> g_shift);
    return r;
}
static int mp(uintptr_t a, size_t len, int prot) { return mp_(a, len, prot, 0); }

// Linux 6.7+, NXRT_DIRTY_KERNEL=1 (off by default): the kernel tracks the writes (docs/LINUX-PLAN.md, the Steam Deck). Each range is registered with a
// userfaultfd in asynchronous write-protect mode: a write to a protected page is resolved inside the kernel (no SIGSEGV, no handler, no mprotect, no mmap
// lock taken for writing), and a range's first check after a submission starts (nvn_dirty_submission) runs one PAGEMAP_SCAN over its protected pages: those
// written count as after a fault (written now, protected no more). The same pages are protected as with signals; a page already protected is left alone
// (protecting it again would make the kernel forget a write). A write is seen at the next submission instead of at once; our own writes into guest memory
// (prep_write) are stamped at once. A failure moves the range back to signals.
// Why off: a scan walks every protected page's table entry (about 4 ns each), and Pokopia keeps nearly all of its five pools (5 GB) protected. In WSL at
// the title: 0.92 ms a frame of tracking this way against 0.07 ms with signals, for the same 3.4 writes a frame (400 scattered writes into a 64 MB range
// were 0.17 ms against 3.1 ms). Worth it only for the few regions written often, not whole pools
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#ifndef UFFD_USER_MODE_ONLY
#define UFFD_USER_MODE_ONLY 1
#endif
#ifndef UFFD_FEATURE_WP_HUGETLBFS_SHMEM
#define UFFD_FEATURE_WP_HUGETLBFS_SHMEM (1 << 12)
#endif
#ifndef UFFD_FEATURE_WP_UNPOPULATED
#define UFFD_FEATURE_WP_UNPOPULATED (1 << 13)
#endif
#ifndef UFFD_FEATURE_WP_ASYNC
#define UFFD_FEATURE_WP_ASYNC (1 << 15)
#endif
#ifndef PAGEMAP_SCAN   // include/uapi/linux/fs.h, Linux 6.7
struct page_region { uint64_t start, end, categories; };
struct pm_scan_arg { uint64_t size, flags, start, end, walk_end, vec, vec_len, max_pages, category_inverted, category_mask, category_anyof_mask, return_mask; };
#define PAGEMAP_SCAN _IOWR('f', 16, struct pm_scan_arg)
#define PM_SCAN_WP_MATCHING (1 << 0)
#define PM_SCAN_CHECK_WPASYNC (1 << 1)
#define PAGE_IS_WRITTEN (1 << 1)
#endif
static int g_uffd = -1, g_pmfd = -1, g_kmode;   // g_kmode: the kernel's tracking is available (decided at the first range)
static _Atomic uint64_t g_epoch = 1;   // the submission count (nvn_dirty_submission)
static void kmode_init(void) {
    const char *e = getenv("NXRT_DIRTY_KERNEL");
    if (!e || *e != '1') return;
    int fd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_HUGETLBFS_SHMEM | UFFD_FEATURE_WP_UNPOPULATED};
    int pm = fd >= 0 ? open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC) : -1;
    const char *why = fd < 0 ? "no userfaultfd" : ioctl(fd, UFFDIO_API, &api) ? "no asynchronous write-protect (Linux 6.7+)" : pm < 0 ? "no /proc/self/pagemap" : NULL;
    if (why) { int err = errno; if (pm >= 0) close(pm); if (fd >= 0) close(fd);
        LOG("dirty-page tracking: signals and mprotect (the kernel's tracking: %s, %s)", why, strerror(err)); return; }
    g_uffd = fd; g_pmfd = pm; g_kmode = 1;
    LOG("dirty-page tracking: by the kernel (userfaultfd asynchronous write-protect, PAGEMAP_SCAN at each submission)");
}
static int k_wp(uintptr_t a, uintptr_t e, int on) {   // write-protect [a, e) again (on) or for good no more (off); 0 = done
    uint64_t t = mach_absolute_time();
    struct uffdio_writeprotect wp = {.range = {(uint64_t)a, (uint64_t)(e - a)}, .mode = on ? UFFDIO_WRITEPROTECT_MODE_WP : 0};
    int r = ioctl(g_uffd, UFFDIO_WRITEPROTECT, &wp);
    atomic_fetch_add(&g_mp_ticks, mach_absolute_time() - t); atomic_fetch_add(&g_mp_calls, 1); atomic_fetch_add(&g_mp_pages, (e - a) >> g_shift);
    return r;
}
static int k_register(range_t *r) {   // 1 = the kernel tracks the range from now on (nothing protected yet)
    struct uffdio_register reg = {.range = {(uint64_t)r->base, (uint64_t)(r->end - r->base)}, .mode = UFFDIO_REGISTER_MODE_WP};
    if (!ioctl(g_uffd, UFFDIO_REGISTER, &reg)) return 1;
    static _Atomic int red; if (!atomic_exchange(&red, 1)) LOG("[RED] dirty-page tracking: the kernel would not track %p+%#zx (%s): signals and mprotect for it and any like it", (void *)r->base, (size_t)(r->end - r->base), strerror(errno));
    return 0;
}
static void k_unregister(range_t *r) {
    k_wp(r->base, r->end, 0);
    struct uffdio_range ur = {(uint64_t)r->base, (uint64_t)(r->end - r->base)}; ioctl(g_uffd, UFFDIO_UNREGISTER, &ur);
}
static int k_release(range_t *r, uintptr_t p, uint64_t g) {   // a protected page written: written at g, protected no more (as after a fault)
    uint8_t st = PG_PROT;
    if (!atomic_compare_exchange_strong(&r->state[p], &st, PG_NONE)) return 0;
    atomic_store(&r->gen[p], g); atomic_fetch_sub(&r->kn, 1);
    return 1;
}
static void k_fail(range_t *r, const char *what) {   // under g_slot_mu: back to signals, every protected page counting as written
    static _Atomic int red; if (!atomic_exchange(&red, 1)) LOG("[RED] dirty-page tracking: %s failed on %p+%#zx (%s): signals and mprotect for that range", what, (void *)r->base, (size_t)(r->end - r->base), strerror(errno));
    uint64_t g = atomic_fetch_add(&g_now, 1) + 1;
    for (uintptr_t p = r->klo; p < r->khi; p++) k_release(r, p, g);
    atomic_fetch_add(&g_now, 1);
    k_unregister(r); atomic_store(&r->k, 0);
}
static void k_harvest(range_t *r) {   // under g_slot_mu: the protected pages written since the last scan
    uint64_t ep = atomic_load(&g_epoch);
    if (atomic_load(&r->hv) == ep) return;
    atomic_store(&r->hv, ep);
    if (atomic_load(&r->kn) <= 0) { r->klo = (r->end - r->base) >> g_shift; r->khi = 0; return; }
    enum { VEC = 1024 }; static struct page_region vec[VEC];
    struct pm_scan_arg s = {.size = sizeof s, .flags = PM_SCAN_CHECK_WPASYNC, .start = r->base + (r->klo << g_shift), .end = r->base + (r->khi << g_shift),
                            .vec = (uint64_t)(uintptr_t)vec, .vec_len = VEC, .category_mask = PAGE_IS_WRITTEN, .return_mask = PAGE_IS_WRITTEN};
    uint64_t g = atomic_fetch_add(&g_now, 1) + 1, n = 0, t = mach_absolute_time();
    for (;;) {   // pages never protected count as written too: only those protected are taken
        long m = ioctl(g_pmfd, PAGEMAP_SCAN, &s);
        if (m < 0) { k_fail(r, "PAGEMAP_SCAN"); break; }
        for (long i = 0; i < m; i++)
            for (uintptr_t p = (uintptr_t)(vec[i].start - r->base) >> g_shift, q = (uintptr_t)(vec[i].end - r->base) >> g_shift; p < q; p++) n += (uint64_t)k_release(r, p, g);
        if (m < VEC || s.walk_end >= s.end) break;
        s.start = s.walk_end;
    }
    atomic_fetch_add(&g_now, 1);
    atomic_fetch_add(&g_faults, n); atomic_fetch_add(&g_mp_ticks, mach_absolute_time() - t);
}
// Protect pages [p, q] of a kernel-tracked range: the runs not protected yet, one ioctl each. 0 = it failed and the range is back on signals
static int k_protect(range_t *r, uintptr_t p, uintptr_t q) {
    for (uintptr_t i = p; i <= q; ) {
        if (atomic_load(&r->state[i]) != PG_NONE) { i++; continue; }   // protected: the kernel keeps its writes for the next scan
        uintptr_t j = i; while (j <= q && atomic_load(&r->state[j]) == PG_NONE) j++;
        if (k_wp(r->base + (i << g_shift), r->base + (j << g_shift), 1)) { k_fail(r, "UFFDIO_WRITEPROTECT"); return 0; }
        for (uintptr_t k = i; k < j; k++) atomic_store(&r->state[k], PG_PROT);
        atomic_fetch_add(&r->kn, (int64_t)(j - i));
        if (i < r->klo) r->klo = i;
        if (j > r->khi) r->khi = j;
        i = j;
    }
    return 1;
}
#endif
void nvn_dirty_submission(void) {
#ifndef _WIN32
    atomic_fetch_add(&g_epoch, 1);
#endif
}
int nvn_dirty_kernel(void) {
#ifndef _WIN32
    return g_kmode;
#else
    return 0;
#endif
}

static mach_timebase_info_data_t g_tb;   // obtained when the first pool is registered (the fault path only multiplies and divides)
static _Atomic int g_busy_red;
// Wait for this page to leave BUSY. Returns 1 = it has left; 0 = waited past the limit (reported red once). Usable in a signal handler: only atomic reads, mach_absolute_time and sched_yield
static int busy_wait(range_t *r, uintptr_t p) {
    uint64_t t0 = 0;
    for (unsigned n = 1; atomic_load(&r->state[p]) == PG_BUSY; n++) {
        if (n & 0x3ff) continue;
        uint64_t now = mach_absolute_time(); if (!t0) t0 = now;
        if ((now - t0) * g_tb.numer / g_tb.denom > (uint64_t)BUSY_WAIT_MS * 1000000ull) {
            if (!atomic_exchange(&g_busy_red, 1)) LOG("[RED] dirty-page tracking: page %p stuck 'busy' for over %d ms (the thread changing its protection has not finished), no longer waiting", (void *)(r->base + (p << g_shift)), BUSY_WAIT_MS);
            return 0; }
        sched_yield();
    }
    return 1;
}
static int live_at(int i) { return atomic_load_explicit(&rg(i)->live, memory_order_acquire); }
// The registered range that holds all of [a, a+len) (used for a texture's tracking / protection / dirty check)
static range_t *find_span(uintptr_t a, size_t len) {
    int n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++) { range_t *r = rg(i); if (live_at(i) && a >= r->base && a + len <= r->end) return r; }
    return NULL;
}
static void stamp_page(range_t *r, uintptr_t p) {
    atomic_store(&r->gen[p], atomic_fetch_add(&g_now, 1) + 1);
    // Publish completion after the generation: a checker may have cached "clean" using the incremented clock before the generation store. Both fault and host-write paths
    // must invalidate that cached clock, as the page stays writable until the next upload protects it again.
    atomic_fetch_add(&g_now, 1);
}
static void unprotect_page(range_t *r, uintptr_t p) {   // The caller has already changed the state from PROT to BUSY
    mp(r->base + (p << g_shift), g_pg, PROT_READ | PROT_WRITE);
    stamp_page(r, p);
    atomic_store(&r->state[p], PG_NONE);
}
// Unprotect one page of a range: if protected, unprotect and stamp it; if another thread is changing its protection, wait for it to finish and look again -- if the other side was setting read-only, the page is PROT after the wait and still has to be unprotected
// (an unprotect-before-write never reaches the fault handler, so it must be done here). Returns 1 = this page is managed by this module; 0 = it is not, or the wait on BUSY timed out
static int release_page(range_t *r, uintptr_t p, _Atomic uint64_t *count) {
    for (;;) {
        uint8_t st = PG_PROT;
        if (atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY)) { unprotect_page(r, p); if (count) atomic_fetch_add(count, 1); return 1; }
        if (st != PG_BUSY) return atomic_load(&r->gen[p]) != 0;   // already NONE: just unprotected by another thread (the fault happened before the unprotect) -> re-execute; pages never protected are not ours
        if (!busy_wait(r, p)) return 0;
    }
}
// Windows: whether a tracked range has this page protected or is changing it, for the heap's fault hook ahead of on_fault (dm_fault_is_wp)
static int page_wp(void *addr) {
    uintptr_t a = (uintptr_t)addr; int n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++) { range_t *r = rg(i); if (live_at(i) && !atomic_load(&r->k) && a >= r->base && a < r->end && atomic_load(&r->state[(a - r->base) >> g_shift]) != PG_NONE) return 1; }
    return 0;
}
static int on_fault(void *addr, int is_write, void *uc) {
    (void)uc;
    if (!is_write) return 0;
    uintptr_t a = (uintptr_t)addr; int ours = 0, n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++)   // Every range sharing the page must be released: releasing only one leaves the other range's state at "protected", and later writes no longer fault and are missed
        { range_t *r = rg(i); if (live_at(i) && !atomic_load(&r->k) && a >= r->base && a < r->end) ours |= release_page(r, (a - r->base) >> g_shift, &g_faults); }   // the kernel's ranges never fault
    return ours;
}
// Unprotect pages p..q of a range with one mprotect per run of protected pages (a host write over a whole texture, e.g. WriteTexels, used to make one system call per 16 KB page:
// Link's Awakening, about 275 a frame). Pages another thread is changing are handled one by one after the run so far is finished, so no BUSY page is held while waiting
static void release_pages(range_t *r, uintptr_t p, uintptr_t q, _Atomic uint64_t *count) {
    uintptr_t run = 0; int open = 0;
    for (uintptr_t i = p; i <= q + 1; i++) {
        uint8_t st = PG_PROT; int took = i <= q && atomic_compare_exchange_strong(&r->state[i], &st, PG_BUSY);
        if (took) { if (!open) { run = i; open = 1; } continue; }
        if (open) {
            mp(r->base + (run << g_shift), (i - run) << g_shift, PROT_READ | PROT_WRITE);
            for (uintptr_t k = run; k < i; k++) { stamp_page(r, k); atomic_store(&r->state[k], PG_NONE); }
            if (count) atomic_fetch_add(count, i - run);
            open = 0;
        }
        if (i <= q && st == PG_BUSY) release_page(r, i, count);   // another thread is changing it: wait, then unprotect it if it ended up protected
    }
}
static void prep_write(void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr, e = a + len; int n = atomic_load(&g_nr);
    if (!len) return;
    for (int i = 0; i < n; i++) {
        range_t *r = rg(i);
        if (!live_at(i) || e <= r->base || a >= r->end) continue;
        uintptr_t lo = a > r->base ? a : r->base, hi = e < r->end ? e : r->end;
#ifndef _WIN32
        if (atomic_load(&r->k)) {   // the kernel lets our write through: no system call, the protected pages count as written now
            uint64_t g = atomic_fetch_add(&g_now, 1) + 1, n = 0;
            for (uintptr_t p = (lo - r->base) >> g_shift, q = (hi - 1 - r->base) >> g_shift; p <= q; p++) n += (uint64_t)k_release(r, p, g);
            atomic_fetch_add(&g_now, 1); atomic_fetch_add(&g_preps, n);
            continue;
        }
#endif
        release_pages(r, (lo - r->base) >> g_shift, (hi - 1 - r->base) >> g_shift, &g_preps);
    }
}

void nvn_dirty_add_range(const void *mem, size_t size) {
    static int inited;
    if (!inited) { inited = 1; g_pg = (uintptr_t)getpagesize(); g_shift = (uintptr_t)__builtin_ctzll(g_pg); mach_timebase_info(&g_tb); dm_fault_hook_register(on_fault); dm_guest_write_prep_register(prep_write); dm_fault_wp_register(page_wp);
#ifndef _WIN32
        kmode_init();
#endif
    }
    atomic_fetch_add(&g_adds_total, 1);
    pthread_mutex_lock(&g_slot_mu);
    int n = atomic_load(&g_nr);
    int slot = -1;
    for (int i = 0; i < n; i++) if (!live_at(i)) { slot = i; break; }
    if (slot < 0) {
        if (n >= MAX_RANGE) {
            int expected = 0;
            if (atomic_compare_exchange_strong(&g_overflow_logged, &expected, 1)) LOG("[RED] dirty-page tracking ranges are full (%d), pool %p not tracked", MAX_RANGE, mem);
            pthread_mutex_unlock(&g_slot_mu); return;
        }
        slot = n;
        if (!atomic_load_explicit(&g_blk[n >> BLK_SHIFT], memory_order_acquire)) {   // published before g_nr counts the slot (the scans read g_nr first)
            range_t *b = calloc(BLK, sizeof *b); if (!b) die("dirty-page tracking range table allocation failed (block %d)", n >> BLK_SHIFT);
            atomic_store_explicit(&g_blk[n >> BLK_SHIFT], b, memory_order_release);
        }
    }
    { int lv = atomic_fetch_add(&g_live, 1) + 1, pk = atomic_load(&g_live_peak); while (lv > pk && !atomic_compare_exchange_weak(&g_live_peak, &pk, lv)) {} }
    range_t *r = rg(slot);
    uintptr_t base = (uintptr_t)mem & ~(g_pg - 1), end = ((uintptr_t)mem + size + g_pg - 1) & ~(g_pg - 1);
    size_t np = (end - base) >> g_shift;
    _Atomic uint8_t *state = r->state;
    _Atomic uint64_t *gen = r->gen;
    if (r->cap < np) {
        state = calloc(np, 1); gen = calloc(np, sizeof *gen);
        if (!state || !gen) die("dirty-page table allocation failed (%zu pages)", np);
        // The old array is not freed: the signal handler may still have a very brief stale reader; leak = 9 bytes per page.
        r->cap = np;
    } else {
        memset((void *)state, 0, np); memset((void *)gen, 0, np * sizeof *gen);
    }
    r->base = base; r->end = end; r->mem = (uintptr_t)mem; r->state = state; r->gen = gen;
    atomic_store(&r->k, 0); atomic_store(&r->kn, 0); r->klo = np; r->khi = 0; atomic_store(&r->hv, 0);
#ifndef _WIN32
    if (g_kmode) atomic_store(&r->k, k_register(r));
#endif
    if (slot == n) atomic_store(&g_nr, n + 1);
    atomic_store_explicit(&r->live, 1, memory_order_release);
    pthread_mutex_unlock(&g_slot_mu);
}
// Pool destruction: first restore still-protected pages to writable (the pool memory may be reused for something else once returned to the game), then mark invalid
void nvn_dirty_remove_range(const void *mem) {
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = NULL; int n = atomic_load(&g_nr);
    for (int i = 0; i < n && !r; i++) if (live_at(i) && rg(i)->mem == (uintptr_t)mem) r = rg(i);   // Match by the pool's exact start: an adjacent pool sharing a page may have the same rounded base
#ifndef _WIN32
    if (r && atomic_load(&r->k)) { k_unregister(r); atomic_store(&r->k, 0); atomic_store_explicit(&r->live, 0, memory_order_release); atomic_fetch_sub(&g_live, 1); r = NULL; }
#endif
    if (r) { prep_write((void *)r->base, r->end - r->base); atomic_store_explicit(&r->live, 0, memory_order_release); atomic_fetch_sub(&g_live, 1); }
    pthread_mutex_unlock(&g_slot_mu);
}
int nvn_dirty_tracked(const void *addr, size_t len) {
    return find_span((uintptr_t)addr, len) != NULL;
}
// The tracked range holding [addr, addr+len), as a handle a caller keeps (packaged textures: the scan over every range ran twice per texture lookup). NULL = untracked
const void *nvn_dirty_span(const void *addr, size_t len) { return len ? find_span((uintptr_t)addr, len) : NULL; }
static void mark_prot(range_t *r, uintptr_t k0, uintptr_t k1) {   // pages [k0, k1) now read-only: a page that counted as always written counts as written now
    for (uintptr_t k = k0; k < k1; k++) {
        if (atomic_load(&r->gen[k]) == UINT64_MAX) atomic_store(&r->gen[k], atomic_fetch_add(&g_now, 1) + 1);
        atomic_store(&r->state[k], PG_PROT);
    }
}
static void mark_open(range_t *r, uintptr_t k0, uintptr_t k1) { for (uintptr_t k = k0; k < k1; k++) { atomic_store(&r->gen[k], UINT64_MAX); atomic_store(&r->state[k], PG_NONE); } }
// Windows refused a run: part of it is not committed (the heap commits 1 MB chunks lazily, and a texture's storage can span one the game has not touched; #27: three
// 4 MB textures with a 2 MB hole each). Its committed regions are protected one by one and only the rest stays writable and counts as always written: a write there
// commits its chunk first, unseen, so it must count as written, and a later protect tracks it from then on. The whole run used to count as always written
#ifdef _WIN32
static void protect_parts(range_t *r, uintptr_t k0, uintptr_t k1) {
    for (uintptr_t k = k0; k < k1; ) {
        uintptr_t a = r->base + (k << g_shift), stop = k + 1; MEMORY_BASIC_INFORMATION mi;
        int ok = VirtualQuery((void *)a, &mi, sizeof mi) != 0;
        if (ok) { uintptr_t e = (uintptr_t)mi.BaseAddress + mi.RegionSize, ke = (e - r->base) >> g_shift; if (ke > k) stop = ke < k1 ? ke : k1; }
        if (ok && mi.State == MEM_COMMIT && !mp(a, (stop - k) << g_shift, PROT_READ)) mark_prot(r, k, stop);
        else mark_open(r, k, stop);
        k = stop;
    }
}
// Windows can refuse (uncommitted pages): those stay writable and untracked, so they count as always written (UINT64_MAX) rather than silently clean;
// once a later protect takes, such a page counts as written now and is tracked from there
static void protect_run(range_t *r, uintptr_t k0, uintptr_t k1) {
    if (!mp_(r->base + (k0 << g_shift), (k1 - k0) << g_shift, PROT_READ, 1)) mark_prot(r, k0, k1);
    else protect_parts(r, k0, k1);
}
#else
int dm_guest_readable_run(uint64_t a, uint64_t *end);   // host/hle_svc.c: the guest heap's and the alias's commit bitmaps, else a probe
// Linux: a read-only protect succeeds on pages the guest heap has not committed too (committing is a protection change there). The first write would then be
// the tracker's fault, not the heap's, so the chunk would never count as committed and its uploads would be stepped over. Only the committed parts are
// protected; the rest stays as it is and counts as always written, as protect_parts does on Windows (#27). Pages alike are merged into one mprotect
static void protect_run(range_t *r, uintptr_t k0, uintptr_t k1) {
    for (uintptr_t k = k0; k < k1; ) {
        uint64_t e = 0; int ok = dm_guest_readable_run(r->base + (k << g_shift), &e);
        uintptr_t stop = k + 1;
        for (;;) {   // as far as the pages are alike: a bitmap answers a whole run at once, a probe one page
            uintptr_t ke = e > r->base ? (uintptr_t)((e - r->base) >> g_shift) : 0;
            if (ke > stop) stop = ke;
            if (stop >= k1) { stop = k1; break; }
            if (dm_guest_readable_run(r->base + (stop << g_shift), &e) != ok) break;
            stop++;
        }
        if (ok && !mp_(r->base + (k << g_shift), (stop - k) << g_shift, PROT_READ, 1)) mark_prot(r, k, stop);
        else mark_open(r, k, stop);
        k = stop;
    }
}
#endif
// Set read-only: mark the pages BUSY first and mark them PROT only after mprotect has landed (for the order and the invariant see the state notes at the top of the file)
void nvn_dirty_protect(const void *addr, size_t len) {
    // Keep this descriptor live through the last mprotect. Retirement restores
    // RW under this same mutex; it must not finish between lookup and PG_BUSY.
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = len ? find_span((uintptr_t)addr, len) : NULL;
    if (!r) { pthread_mutex_unlock(&g_slot_mu); return; }
    uintptr_t a = (uintptr_t)addr, p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
#ifndef _WIN32
    if (atomic_load(&r->k) && k_protect(r, p, q)) { pthread_mutex_unlock(&g_slot_mu); return; }
#endif
    // Consecutive pages are merged into one mprotect (per-page calls reach thousands per frame on the first upload of large textures; character select page startup frame 5: 4888 calls).
    // Pages already PROT / BUSY break the consecutive run: do not protect again, and do not touch pages another thread is changing.
    // A BUSY page is waited out (the run so far is protected first, so none of ours is BUSY meanwhile) and looked at again: an unprotect in progress leaves the page writable
    // with a generation the caller's stamp may cover, so skipped, its later writes never faulted and the texture stayed stale (MacNativeGames a166602)
    uintptr_t run = 0; int open = 0;
    for (uintptr_t i = p; i <= q + 1; i++) {
        uint8_t st = PG_NONE;
        int took = i <= q && atomic_compare_exchange_strong(&r->state[i], &st, PG_BUSY);
        if (took && !open) { run = i; open = 1; }
        if (!took && open) { protect_run(r, run, i); open = 0; }
        if (!took && i <= q && st == PG_BUSY && busy_wait(r, i)) i--;   // this page again
    }
    pthread_mutex_unlock(&g_slot_mu);
}
uint64_t nvn_dirty_now(void) { return atomic_load(&g_now); }
int nvn_dirty_since_span(const void *span, const void *addr, size_t len, uint64_t stamp) {
    if (!len) return 0;
    range_t *r = (range_t *)span; uintptr_t a0 = (uintptr_t)addr;   // the kept handle still holds the span (a slot reused by another pool over the same memory is as good): no scan
    if (!r || !atomic_load_explicit(&r->live, memory_order_acquire) || a0 < r->base || a0 + len > r->end) r = find_span(a0, len);
    if (!r) return 1;   // When untracked, conservatively treat as dirty: better to upload more than to show stale content
#ifndef _WIN32
    if (atomic_load(&r->k) && atomic_load(&r->hv) != atomic_load(&g_epoch)) {
        pthread_mutex_lock(&g_slot_mu);
        if (atomic_load_explicit(&r->live, memory_order_acquire) && atomic_load(&r->k)) k_harvest(r);
        pthread_mutex_unlock(&g_slot_mu);
    }
#endif
    uintptr_t a = (uintptr_t)addr, p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    for (uintptr_t i = p; i <= q; i++) if (atomic_load(&r->gen[i]) > stamp) return 1;   // The generation of the last write before upload is <= the stamp; any later write must be > the stamp
    return 0;
}
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp) { return nvn_dirty_since_span(NULL, addr, len, stamp); }
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total) {
    int n = atomic_load(&g_nr), active = 0;
    for (int i = 0; i < n; i++) active += live_at(i) != 0;
    *live = active; *used_slots = n; *adds_total = atomic_load(&g_adds_total);
}
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps) { *faults = atomic_load(&g_faults); *preps = atomic_load(&g_preps); }
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    *calls = atomic_load(&g_mp_calls); *pages = atomic_load(&g_mp_pages); *ns = (uint64_t)((double)atomic_load(&g_mp_ticks) * (double)tb.numer / (double)tb.denom);
}
