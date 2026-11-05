/* The guest address space on Linux (docs/LINUX-PLAN.md L-D3): host/addrspace.c's design over mmap. The same three fixed ranges (tokens, modules,
   the guest block), reserved PROT_NONE with MAP_FIXED_NOREPLACE in a constructor before main (NXRT_EARLY_RESERVE). Alias, stack and heap are one
   sparse memfd (no memory used until a page is written); the alias and heap regions are MAP_SHARED views of it, so svcMapMemory maps a heap range a
   second time into the stack region, a real alias as on Windows. A view starts inaccessible (Windows: uncommitted) and as_commit opens it.
   Everything outside the views stays reserved and inaccessible. */
#define _GNU_SOURCE
#include "addrspace.h"
#include "dm.h"
#include "nxrt/runtime.h"

#include <fcntl.h>
#include <sys/mman.h>

static nxrt_range g_r[NXRT_AS_COUNT];
static nxrt_layout g_lay;
static int g_done, g_ok;
static int g_fd = -1;   /* the alias + stack + heap memfd */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

/* Mapped views: base, size, offset in the memfd (as_unmap_view and as_discard need them; Windows asks the kernel) */
#define MAX_VIEWS 4096
typedef struct { uint64_t base, size, off; } view_t;
static view_t g_views[MAX_VIEWS];
static int g_nviews;

static void layout_init(void) {
    g_lay.as_base = NXRT_GUEST_AS_BASE; g_lay.as_size = DM_GAME_MEM_RESERVE;
    g_lay.alias = g_lay.as_base + 0x10000000; g_lay.alias_size = DM_GAME_MEM_ALIAS;
    g_lay.stack = g_lay.alias + g_lay.alias_size; g_lay.stack_size = DM_GAME_MEM_STACK;
    g_lay.heap = g_lay.stack + g_lay.stack_size; g_lay.heap_size = g_lay.as_base + g_lay.as_size - g_lay.heap;
    g_lay.app_pool = DM_GAME_APP_POOL;
    g_r[NXRT_AS_TOKENS] = (nxrt_range){.name = "host-call tokens", .base = NXRT_TOKEN_BASE, .size = NXRT_TOKEN_SIZE};
    g_r[NXRT_AS_MODULES] = (nxrt_range){.name = "modules", .base = NXRT_MODULE_BASE, .size = NXRT_MODULE_REGION_SIZE};
    g_r[NXRT_AS_GUEST] = (nxrt_range){.name = "guest block (alias, stack, heap)", .base = g_lay.as_base, .size = g_lay.as_size};
}

/* What holds part of a range we could not reserve: the first mapping inside it, from /proc/self/maps */
static void describe_conflict(nxrt_range *r) {
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        unsigned long long lo, hi; char perm[8] = ""; int name_at = 0;
        if (sscanf(line, "%llx-%llx %7s %*s %*s %*s %n", &lo, &hi, perm, &name_at) < 3) continue;
        if (hi <= r->base || lo >= r->base + r->size) continue;
        r->conflict_base = lo, r->conflict_size = hi - lo;
        char *name = name_at ? line + name_at : line + strlen(line), *nl = strchr(name, '\n');
        if (nl) *nl = 0;
        snprintf(r->conflict_owner, sizeof r->conflict_owner, "%s %s", perm, *name ? name : "(anonymous)");
        break;
    }
    if (f) fclose(f);
}

static int reserve_at(uint64_t a, uint64_t n) {
    void *p = mmap((void *)(uintptr_t)a, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return 0;
    if (p != (void *)(uintptr_t)a) { munmap(p, n); return 0; }   /* a kernel before 4.17 took the flag as a hint */
    return 1;
}
/* Back to reserved: the pages go, the range stays ours */
static int unreserve_to_reserved(uint64_t a, uint64_t n) {
    void *p = mmap((void *)(uintptr_t)a, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    return p == (void *)(uintptr_t)a ? 0 : -1;
}

int nxrt_as_reserve_early(void) {
    if (g_done) return g_ok;
    g_done = 1;
    layout_init();
    g_ok = 1;
    for (int i = 0; i < NXRT_AS_COUNT; i++) {
        nxrt_range *r = &g_r[i];
        r->reserved = reserve_at(r->base, r->size);
        if (!r->reserved) { g_ok = 0; describe_conflict(r); }
    }
    return g_ok;
}
const nxrt_range *nxrt_as_range(int i) { return i >= 0 && i < NXRT_AS_COUNT ? &g_r[i] : NULL; }
int nxrt_as_ok(void) { return g_ok; }
const nxrt_layout *nxrt_as_layout(void) { if (!g_done) nxrt_as_reserve_early(); return &g_lay; }
int as_have_placeholders(void) { return 1; }

/* ---- Private memory inside a reserved range ---- */

int as_commit_private(uint64_t a, uint64_t n, DWORD prot) {
    void *p = mmap((void *)(uintptr_t)a, n, win32_prot(prot), MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)a) { SetLastError((DWORD)errno); return -1; }
    return 0;
}
int as_release_private(uint64_t a, uint64_t n) { return unreserve_to_reserved(a, n); }

/* ---- The guest section ---- */

static int sec_create(void) {
    if (g_fd >= 0) return 0;
    uint64_t n = g_lay.alias_size + g_lay.stack_size + g_lay.heap_size;
    int fd = memfd_create("nxrt-guest", MFD_CLOEXEC);
    if (fd < 0) { SetLastError((DWORD)errno); return -1; }
    if (ftruncate(fd, (off_t)n)) { SetLastError((DWORD)errno); close(fd); return -1; }   /* sparse: nothing is allocated until written */
    g_fd = fd;
    return 0;
}
int as_section_offset(uint64_t a, uint64_t n, uint64_t *off) {
    if (a < g_lay.alias || n > g_lay.heap + g_lay.heap_size - a) return -1;
    *off = a - g_lay.alias;
    return 0;
}
int as_map_view(uint64_t a, uint64_t n, uint64_t off) {
    if (sec_create()) return -1;
    pthread_mutex_lock(&g_mu);
    int r = -1;
    if (g_nviews < MAX_VIEWS) {
        void *p = mmap((void *)(uintptr_t)a, n, PROT_NONE, MAP_SHARED | MAP_FIXED, g_fd, (off_t)off);
        if (p == (void *)(uintptr_t)a) g_views[g_nviews++] = (view_t){a, n, off}, r = 0;
        else SetLastError((DWORD)errno);
    } else SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    pthread_mutex_unlock(&g_mu);
    return r;
}
int as_unmap_view(uint64_t a) {
    pthread_mutex_lock(&g_mu);
    int r = -1;
    for (int i = 0; i < g_nviews; i++)
        if (g_views[i].base == a) {
            r = unreserve_to_reserved(a, g_views[i].size);
            g_views[i] = g_views[--g_nviews];
            break;
        }
    pthread_mutex_unlock(&g_mu);
    return r;
}
void as_coalesce(uint64_t lo, uint64_t hi) { (void)lo; (void)hi; }   /* Linux has no placeholders to merge */

int as_guest_views(void) {
    static int done, rc;
    if (done) return rc;
    done = 1;
    if (!g_r[NXRT_AS_GUEST].reserved) return rc = -1;
    if (as_map_view(g_lay.alias, g_lay.alias_size, 0) || as_map_view(g_lay.heap, g_lay.heap_size, g_lay.heap - g_lay.alias)) return rc = -1;
    return rc = 0;
}
int as_commit(uint64_t a, uint64_t n) {
    if (!n) return 0;
    if (mprotect((void *)(uintptr_t)a, n, PROT_READ | PROT_WRITE)) { SetLastError((DWORD)errno); return -1; }
    return 0;
}
int as_protect(uint64_t a, uint64_t n, DWORD prot) {
    if (!n) return 0;
    if (mprotect((void *)(uintptr_t)a, n, win32_prot(prot))) { SetLastError((DWORD)errno); return -1; }
    return 0;
}
/* Drop pages' contents and close them: a hole in the memfd where a view backs them (every view of those pages sees zeros afterwards, as a
   discarded section page on Windows), MADV_DONTNEED for private memory */
int as_discard(uint64_t a, uint64_t n) {
    if (!n) return 0;
    int done = 0;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_nviews; i++) {
        view_t *v = &g_views[i];
        if (a >= v->base && a + n <= v->base + v->size) {
            fallocate(g_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)(v->off + (a - v->base)), (off_t)n);
            done = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    if (!done) madvise((void *)(uintptr_t)a, n, MADV_DONTNEED);
    return as_protect(a, n, PAGE_NOACCESS);
}
