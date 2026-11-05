/* Linux implementation of plat.h (docs/LINUX-PLAN.md). Thread ids are kernel thread ids (gettid), so they work with setpriority and /proc. */
#define _GNU_SOURCE
#include "plat.h"

#include <cpuid.h>
#include <errno.h>
#include <link.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static uint64_t ts_ns(clockid_t c) { struct timespec t; clock_gettime(c, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
uint64_t plat_now_ns(void) { return ts_ns(CLOCK_MONOTONIC); }
uint64_t plat_qpc(void) { return ts_ns(CLOCK_MONOTONIC); }   /* the "performance counter" is the monotonic clock in nanoseconds */
uint64_t plat_qpc_freq(void) { return 1000000000u; }
uint64_t plat_realtime_ns(void) { return ts_ns(CLOCK_REALTIME); }

void plat_sleep_ns(uint64_t ns) {
    if (!ns) { sched_yield(); return; }
    struct timespec d = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
    while (clock_nanosleep(CLOCK_MONOTONIC, 0, &d, &d) == EINTR) {}
}
void plat_yield(void) { sched_yield(); }

/* Linux sleeps already wake within the thread's timer slack (50 us by default); 1 us keeps 60 Hz pacing as tight as Windows' 1 ms period
   with high-resolution timers. Threads inherit the slack of the thread that creates them, so this runs before any other thread starts. */
void plat_timer_resolution(void) { prctl(PR_SET_TIMERSLACK, 1000UL, 0, 0, 0); }

/* process_vm_readv on our own process: an unmapped or protected source fails with EFAULT instead of faulting */
int plat_read(uint64_t addr, void *out, size_t n) {
    if (!addr) return -1;
    struct iovec l = {out, n}, r = {(void *)(uintptr_t)addr, n};
    ssize_t got = process_vm_readv(getpid(), &l, 1, &r, 1, 0);
    return got == (ssize_t)n ? 0 : -1;
}

/* The caller wants the new thread's kernel id (glibc 2.35 has no pthread_gettid_np): the thread publishes it before it runs fn */
typedef struct { plat_thread_fn fn; void *arg; pthread_mutex_t mu; pthread_cond_t cv; uint32_t tid; } start_t;
static void *thread_tramp(void *p) {
    start_t *s = p;
    plat_thread_fn fn = s->fn; void *arg = s->arg;
    pthread_mutex_lock(&s->mu);
    s->tid = plat_thread_id();
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);   /* s belongs to the creator from here */
    fn(arg);
    return NULL;
}
uint32_t plat_thread_start(plat_thread_fn fn, void *arg, size_t stack_reserve) {
    start_t s = {fn, arg, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (stack_reserve) pthread_attr_setstacksize(&a, stack_reserve < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : (stack_reserve + 0xfff) & ~(size_t)0xfff);
    pthread_t t;
    int rc = pthread_create(&t, &a, thread_tramp, &s);
    pthread_attr_destroy(&a);
    if (rc) return 0;
    pthread_mutex_lock(&s.mu);
    while (!s.tid) pthread_cond_wait(&s.cv, &s.mu);
    pthread_mutex_unlock(&s.mu);
    return s.tid;
}
uint32_t plat_thread_id(void) { return (uint32_t)syscall(SYS_gettid); }
void plat_thread_name(const char *name) {
    char n[16];
    snprintf(n, sizeof n, "%s", name);   /* the kernel keeps 15 bytes */
    pthread_setname_np(pthread_self(), n);
}
/* Horizon priorities run 0 (highest) to 63. Linux only lets an unprivileged process lower a thread's priority, never raise it again, so guest
   threads keep the process's priority (the game raises and lowers its own threads at run time); plat_priority_for is the nice offset */
int plat_priority_for(int p) { return p <= 59 ? 0 : 1; }
void plat_thread_set_priority(uint32_t tid, int horizon_prio) { (void)tid; (void)horizon_prio; }
void plat_thread_lower(int nice_offset) {
    int base = getpriority(PRIO_PROCESS, 0);
    if (nice_offset > 0) setpriority(PRIO_PROCESS, plat_thread_id(), base + nice_offset > 19 ? 19 : base + nice_offset);
}
double plat_thread_cpu_seconds(uint32_t tid) {
    char p[64], b[1024];
    snprintf(p, sizeof p, "/proc/self/task/%u/stat", tid);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    const char *s = strrchr(b, ')');   /* the name may hold spaces; fields 14 and 15 (utime, stime) follow ") state ..." */
    unsigned long long ut = 0, st = 0;
    if (!s || sscanf(s + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) != 2) return -1;
    return (double)(ut + st) / (double)sysconf(_SC_CLK_TCK);
}
_Noreturn void plat_thread_exit(void) { pthread_exit(NULL); }

/* The first object dl_iterate_phdr reports is the executable */
typedef struct { uintptr_t base, end; } image_t;
static int exe_phdr(struct dl_phdr_info *i, size_t sz, void *p) {
    (void)sz;
    image_t *x = p;
    x->base = i->dlpi_addr;
    for (int k = 0; k < i->dlpi_phnum; k++)
        if (i->dlpi_phdr[k].p_type == PT_LOAD && i->dlpi_addr + i->dlpi_phdr[k].p_vaddr + i->dlpi_phdr[k].p_memsz > x->end)
            x->end = i->dlpi_addr + i->dlpi_phdr[k].p_vaddr + i->dlpi_phdr[k].p_memsz;
    return 1;
}
void *plat_exe_image(size_t *size) {
    image_t x = {0, 0};
    dl_iterate_phdr(exe_phdr, &x);
    if (size) *size = x.end > x.base ? x.end - x.base : 0;
    return (void *)x.base;
}

void plat_cpu_brand(char *out, size_t n) {
    unsigned r[4]; char b[49] = {0};
    if (__get_cpuid(0x80000000u, &r[0], &r[1], &r[2], &r[3]) && r[0] >= 0x80000004u)
        for (unsigned i = 0; i < 3; i++) { __get_cpuid(0x80000002u + i, &r[0], &r[1], &r[2], &r[3]); memcpy(b + 16 * i, r, 16); }
    const char *s = b; while (*s == ' ') s++;
    snprintf(out, n, "%s", *s ? s : "unknown x86-64 CPU");
}
static uint64_t xgetbv0(void) { uint32_t lo, hi; __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0)); return (uint64_t)hi << 32 | lo; }
int plat_cpu_x86_64_v3(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
    int fma = (c >> 12) & 1, osxsave = (c >> 27) & 1, avx = (c >> 28) & 1, movbe = (c >> 22) & 1;
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
    int avx2 = (b >> 5) & 1, bmi1 = (b >> 3) & 1, bmi2 = (b >> 8) & 1;
    int ymm = osxsave && (xgetbv0() & 6) == 6;   /* the OS saves YMM state */
    return fma && avx && avx2 && bmi1 && bmi2 && movbe && ymm;
}
unsigned plat_cpu_count(void) {
    cpu_set_t s;
    if (!sched_getaffinity(0, sizeof s, &s)) return (unsigned)CPU_COUNT(&s);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (unsigned)n : 1;
}
/* "Ubuntu 22.04.5 LTS, kernel 5.15.167.4-microsoft-standard-WSL2" (os-release's PRETTY_NAME and uname) */
void plat_windows_version(char *out, size_t n) {
    char name[128] = "Linux", line[256];
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) f = fopen("/usr/lib/os-release", "r");
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "PRETTY_NAME=", 12)) {
            char *v = line + 12, *e;
            if (*v == '"') v++;
            if ((e = strpbrk(v, "\"\n"))) *e = 0;
            snprintf(name, sizeof name, "%s", v);
        }
    if (f) fclose(f);
    struct utsname u;
    if (uname(&u)) snprintf(u.release, sizeof u.release, "?");
    snprintf(out, n, "%s, kernel %s", name, u.release);
}
uint64_t plat_ram_bytes(void) {
    long p = sysconf(_SC_PHYS_PAGES), s = sysconf(_SC_PAGESIZE);
    return p > 0 && s > 0 ? (uint64_t)p * (uint64_t)s : 0;
}

void plat_random(void *out, size_t n) {
    uint8_t *p = out;
    while (n) {
        ssize_t k = getrandom(p, n, 0);
        if (k < 0) {
            if (errno == EINTR) continue;
            uint64_t x = plat_qpc() ^ 0x9E3779B97F4A7C15ull;
            for (size_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; p[i] = (uint8_t)x; }
            return;
        }
        p += k, n -= (size_t)k;
    }
}
