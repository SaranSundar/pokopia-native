/* The Win32 subset of plat/linux/windows.h over pthreads, futexes and mmap (Linux build only, docs/LINUX-PLAN.md). Written from the documented
   contracts of each Win32 function (Microsoft's documentation) and the Linux man pages (futex(2), mmap(2), mprotect(2), madvise(2)). */
#define _GNU_SOURCE
#include "linux/windows.h"
#include "plat.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <linux/futex.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static _Thread_local DWORD t_last_error;
unsigned long GetLastError(void) { return t_last_error; }
void SetLastError(DWORD e) { t_last_error = e; }

/* ---- condition variables ---- */
static long futex(uint32_t *a, int op, uint32_t v, const struct timespec *t) { return syscall(SYS_futex, a, op | FUTEX_PRIVATE_FLAG, v, t, NULL, 0); }

BOOL SleepConditionVariableSRW(CONDITION_VARIABLE *c, SRWLOCK *l, DWORD ms, ULONG flags) {
    uint32_t seq = __atomic_load_n(&c->seq, __ATOMIC_SEQ_CST);   /* read under the lock: a wake after this changes it */
    pthread_rwlock_unlock(&l->l);
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    long r = futex(&c->seq, FUTEX_WAIT, seq, ms == INFINITE ? NULL : &t);
    int e = r ? errno : 0;
    if (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) pthread_rwlock_rdlock(&l->l);
    else pthread_rwlock_wrlock(&l->l);
    if (e == ETIMEDOUT) { SetLastError(ERROR_TIMEOUT); return FALSE; }
    return TRUE;   /* woken, the counter moved already (EAGAIN), or a signal: a spurious wake, which callers allow for */
}
void WakeConditionVariable(CONDITION_VARIABLE *c) { __atomic_add_fetch(&c->seq, 1, __ATOMIC_SEQ_CST); futex(&c->seq, FUTEX_WAKE, 1, NULL); }
void WakeAllConditionVariable(CONDITION_VARIABLE *c) { __atomic_add_fetch(&c->seq, 1, __ATOMIC_SEQ_CST); futex(&c->seq, FUTEX_WAKE, INT32_MAX, NULL); }

/* ---- kernel objects ---- */
enum { OBJ_EVENT = 0x45564e54, OBJ_SEM = 0x53454d41, OBJ_THREAD = 0x54485244, OBJ_TIMER = 0x54494d52, OBJ_FILE = 0x46494c45, OBJ_PROC = 0x50524f43,
       OBJ_FIND = 0x46494e44 };
typedef struct {
    uint32_t type;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int manual, state;        /* event */
    LONG count, max;          /* semaphore */
    pthread_t th;             /* thread */
    uint32_t tid;
    int joined;
    uint64_t due_ns;          /* waitable timer: CLOCK_MONOTONIC */
    int fd;                   /* file */
    pid_t pid;                /* process (state: reaped, count: its exit code) */
} obj_t;
static DWORD proc_wait(obj_t *o, DWORD ms);
#define CURRENT_THREAD ((HANDLE)(intptr_t)-2)

static obj_t *obj_new(uint32_t type) {
    obj_t *o = calloc(1, sizeof *o);
    if (!o) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    o->type = type;
    pthread_mutex_init(&o->mu, NULL);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&o->cv, &ca);
    pthread_condattr_destroy(&ca);
    return o;
}

HANDLE CreateEventW(void *sa, BOOL manual_reset, BOOL initial, const void *name) {
    (void)sa, (void)name;
    obj_t *o = obj_new(OBJ_EVENT);
    if (o) o->manual = manual_reset, o->state = initial != 0;
    return o;
}
BOOL SetEvent(HANDLE h) {
    obj_t *o = h;
    if (!o || o->type != OBJ_EVENT) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    pthread_mutex_lock(&o->mu);
    o->state = 1;
    if (o->manual) pthread_cond_broadcast(&o->cv); else pthread_cond_signal(&o->cv);
    pthread_mutex_unlock(&o->mu);
    return TRUE;
}
BOOL ResetEvent(HANDLE h) {
    obj_t *o = h;
    if (!o || o->type != OBJ_EVENT) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    pthread_mutex_lock(&o->mu);
    o->state = 0;
    pthread_mutex_unlock(&o->mu);
    return TRUE;
}
HANDLE CreateSemaphoreW(void *sa, LONG initial, LONG max, const void *name) {
    (void)sa, (void)name;
    obj_t *o = obj_new(OBJ_SEM);
    if (o) o->count = initial, o->max = max;
    return o;
}
BOOL ReleaseSemaphore(HANDLE h, LONG n, LONG *previous) {
    obj_t *o = h;
    if (!o || o->type != OBJ_SEM || n <= 0) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    pthread_mutex_lock(&o->mu);
    if (previous) *previous = o->count;
    if (o->count > o->max - n) { pthread_mutex_unlock(&o->mu); SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }   /* ERROR_TOO_MANY_POSTS */
    o->count += n;
    if (n == 1) pthread_cond_signal(&o->cv); else pthread_cond_broadcast(&o->cv);
    pthread_mutex_unlock(&o->mu);
    return TRUE;
}

static void deadline(struct timespec *t, DWORD ms) {
    clock_gettime(CLOCK_MONOTONIC, t);
    t->tv_sec += ms / 1000;
    t->tv_nsec += (long)(ms % 1000) * 1000000;
    if (t->tv_nsec >= 1000000000) t->tv_sec++, t->tv_nsec -= 1000000000;
}

DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    obj_t *o = h;
    if (!o || h == CURRENT_THREAD) { SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED; }
    if (o->type == OBJ_THREAD) {
        if (o->joined) return WAIT_OBJECT_0;
        int r;
        if (ms == INFINITE) r = pthread_join(o->th, NULL);
        else {
            struct timespec t;   /* pthread_timedjoin_np takes CLOCK_REALTIME */
            clock_gettime(CLOCK_REALTIME, &t);
            t.tv_sec += ms / 1000, t.tv_nsec += (long)(ms % 1000) * 1000000;
            if (t.tv_nsec >= 1000000000) t.tv_sec++, t.tv_nsec -= 1000000000;
            r = pthread_timedjoin_np(o->th, NULL, &t);
        }
        if (r == ETIMEDOUT) return WAIT_TIMEOUT;
        if (r) { SetLastError((DWORD)r); return WAIT_FAILED; }
        o->joined = 1;
        return WAIT_OBJECT_0;
    }
    if (o->type == OBJ_PROC) return proc_wait(o, ms);
    if (o->type == OBJ_TIMER) {   /* until the due time (plat_now_ns is CLOCK_MONOTONIC); nxrt waits on its timers with INFINITE */
        struct timespec ts = {(time_t)(o->due_ns / 1000000000u), (long)(o->due_ns % 1000000000u)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
        return WAIT_OBJECT_0;
    }
    struct timespec t;
    if (ms != INFINITE) deadline(&t, ms);
    pthread_mutex_lock(&o->mu);
    int r = 0, timed_out = 0;
    if (o->type == OBJ_EVENT) {
        while (!o->state && !r) r = ms == INFINITE ? pthread_cond_wait(&o->cv, &o->mu) : pthread_cond_timedwait(&o->cv, &o->mu, &t);
        if (o->state) { if (!o->manual) o->state = 0; }   /* an auto-reset event lets one waiter through */
        else timed_out = 1;
    } else if (o->type == OBJ_SEM) {
        while (o->count <= 0 && !r) r = ms == INFINITE ? pthread_cond_wait(&o->cv, &o->mu) : pthread_cond_timedwait(&o->cv, &o->mu, &t);
        if (o->count > 0) o->count--;
        else timed_out = 1;
    }
    pthread_mutex_unlock(&o->mu);
    return timed_out ? WAIT_TIMEOUT : WAIT_OBJECT_0;
}

BOOL CloseHandle(HANDLE h) {
    obj_t *o = h;
    if (!o || h == CURRENT_THREAD || h == INVALID_HANDLE_VALUE) return TRUE;
    if (o->type == OBJ_THREAD) {
        if (!o->joined) pthread_detach(o->th);   /* the thread frees nothing of o: the start record is separate */
    }
    if (o->type == OBJ_FILE) close(o->fd);
    pthread_cond_destroy(&o->cv);
    pthread_mutex_destroy(&o->mu);
    o->type = 0;
    free(o);
    return TRUE;
}

/* Threads: the start record lives until the thread has published its tid, so SetThreadPriority on the new handle can reach it */
typedef struct { obj_t *o; unsigned (*fn)(void *); DWORD (*fn32)(LPVOID); void *arg; pthread_mutex_t mu; pthread_cond_t cv; uint32_t tid; } start_t;
static void *tramp(void *p) {
    start_t *s = p;
    unsigned (*fn)(void *) = s->fn; DWORD (*fn32)(LPVOID) = s->fn32; void *arg = s->arg;
    pthread_mutex_lock(&s->mu);
    s->tid = plat_thread_id();
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);
    if (fn) fn(arg); else fn32(arg);
    return NULL;
}
static obj_t *thread_new(size_t stack, unsigned (*fn)(void *), DWORD (*fn32)(LPVOID), void *arg) {
    obj_t *o = obj_new(OBJ_THREAD);
    if (!o) return NULL;
    start_t s = {o, fn, fn32, arg, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
    pthread_attr_t a;
    pthread_attr_init(&a);
    if (stack) pthread_attr_setstacksize(&a, stack < 65536 ? 65536 : (stack + 0xfff) & ~(size_t)0xfff);
    int r = pthread_create(&o->th, &a, tramp, &s);
    pthread_attr_destroy(&a);
    if (r) { SetLastError((DWORD)r); free(o); return NULL; }
    pthread_mutex_lock(&s.mu);
    while (!s.tid) pthread_cond_wait(&s.cv, &s.mu);
    pthread_mutex_unlock(&s.mu);
    o->tid = s.tid;
    return o;
}
HANDLE CreateThread(void *sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, DWORD *tid) {
    (void)sa, (void)flags;
    obj_t *o = thread_new(stack, NULL, fn, arg);
    if (o && tid) *tid = o->tid;
    return o;
}
uintptr_t _beginthreadex(void *sa, unsigned stack, unsigned (*fn)(void *), void *arg, unsigned flags, unsigned *tid) {
    (void)sa, (void)flags;
    obj_t *o = thread_new(stack, fn, NULL, arg);
    if (o && tid) *tid = o->tid;
    return (uintptr_t)o;
}
HANDLE GetCurrentThread(void) { return CURRENT_THREAD; }
DWORD GetCurrentThreadId(void) { return plat_thread_id(); }
BOOL SetThreadPriority(HANDLE h, int prio) {
    if (prio >= 0) return TRUE;
    uint32_t tid = h == CURRENT_THREAD ? plat_thread_id() : ((obj_t *)h)->type == OBJ_THREAD ? ((obj_t *)h)->tid : 0;
    if (!tid) return FALSE;
    int base = getpriority(PRIO_PROCESS, 0), nice = base + (prio <= THREAD_PRIORITY_IDLE ? 19 : prio <= THREAD_PRIORITY_LOWEST ? 4 : 2);
    return setpriority(PRIO_PROCESS, tid, nice > 19 ? 19 : nice) == 0;
}

void Sleep(DWORD ms) { plat_sleep_ns((uint64_t)ms * 1000000u); }
BOOL SwitchToThread(void) { sched_yield(); return TRUE; }
BOOL QueryPerformanceCounter(LARGE_INTEGER *t) { t->QuadPart = (int64_t)plat_qpc(); return TRUE; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = (int64_t)plat_qpc_freq(); return TRUE; }

/* ---- virtual memory ---- */
int win32_prot(DWORD p) {
    switch (p & 0xff) {
    case PAGE_READONLY: return PROT_READ;
    case PAGE_READWRITE: return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE: case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default: return PROT_NONE;
    }
}

/* Reservations made through VirtualAlloc (base -> size), for MEM_RELEASE */
typedef struct { uintptr_t base; size_t size; } resv_t;
static resv_t *g_resv;
static size_t g_nresv, g_cap;
static pthread_mutex_t g_resv_mu = PTHREAD_MUTEX_INITIALIZER;
static void resv_add(uintptr_t b, size_t n) {
    pthread_mutex_lock(&g_resv_mu);
    if (g_nresv == g_cap) { size_t c = g_cap ? g_cap * 2 : 64; resv_t *r = realloc(g_resv, c * sizeof *r); if (r) g_resv = r, g_cap = c; }
    if (g_nresv < g_cap) g_resv[g_nresv++] = (resv_t){b, n};
    pthread_mutex_unlock(&g_resv_mu);
}
static size_t resv_take(uintptr_t b) {
    size_t n = 0;
    pthread_mutex_lock(&g_resv_mu);
    for (size_t i = 0; i < g_nresv; i++)
        if (g_resv[i].base == b) { n = g_resv[i].size; g_resv[i] = g_resv[--g_nresv]; break; }
    pthread_mutex_unlock(&g_resv_mu);
    return n;
}

LPVOID VirtualAlloc(LPVOID addr, SIZE_T n, DWORD type, DWORD prot) {
    const size_t pg = 4096;
    if (!n) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    if (type & MEM_RESERVE) {   /* a new reservation, committed too with MEM_COMMIT */
        uintptr_t b = (uintptr_t)addr & ~(pg - 1);
        size_t len = ((uintptr_t)addr + n + pg - 1 - b) & ~(pg - 1);
        int p = type & MEM_COMMIT ? win32_prot(prot) : PROT_NONE;
        void *r = mmap((void *)b, len, p, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (b ? MAP_FIXED_NOREPLACE : 0), -1, 0);
        if (r == MAP_FAILED) { SetLastError((DWORD)errno); return NULL; }
        if (b && r != (void *)b) { munmap(r, len); SetLastError(EEXIST); return NULL; }   /* kernels before 4.17 treat the flag as a hint */
        resv_add((uintptr_t)r, len);
        return r;
    }
    if (type & MEM_COMMIT) {    /* pages of an existing reservation become accessible (they read as zero the first time) */
        uintptr_t b = (uintptr_t)addr & ~(pg - 1), e = ((uintptr_t)addr + n + pg - 1) & ~(pg - 1);
        if (mprotect((void *)b, e - b, win32_prot(prot))) { SetLastError((DWORD)errno); return NULL; }
        return addr;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
}
BOOL VirtualFree(LPVOID addr, SIZE_T n, DWORD type) {
    if (type & MEM_RELEASE) {
        size_t len = resv_take((uintptr_t)addr);
        if (!len || n) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }   /* as Windows: the base of a reservation, size 0 */
        return munmap(addr, len) == 0;
    }
    if (type & MEM_DECOMMIT) {  /* the contents go, the range stays reserved */
        if (madvise(addr, n, MADV_DONTNEED) || mprotect(addr, n, PROT_NONE)) { SetLastError((DWORD)errno); return FALSE; }
        return TRUE;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
BOOL VirtualProtect(LPVOID addr, SIZE_T n, DWORD prot, DWORD *old) {
    if (old) *old = 0;   /* nothing in nxrt reads the old protection */
    uintptr_t b = (uintptr_t)addr & ~(uintptr_t)4095, e = ((uintptr_t)addr + n + 4095) & ~(uintptr_t)4095;
    if (mprotect((void *)b, e - b, win32_prot(prot))) { SetLastError((DWORD)errno); return FALSE; }
    return TRUE;
}

/* ---- processes, timers, thread names ---- */
HANDLE GetCurrentProcess(void) { return (HANDLE)(intptr_t)-1; }
DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
DWORD GetModuleFileNameA(HMODULE m, char *out, DWORD n) {
    if (m || !n) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    ssize_t k = readlink("/proc/self/exe", out, n - 1);
    if (k < 0) { SetLastError((DWORD)errno); out[0] = 0; return 0; }
    out[k] = 0;
    return (DWORD)k;
}

/* The C runtime's rules, simplified to what nxrt's tests write: whitespace separates, double quotes group, \" is a quote */
static int split_cmdline(char *s, char **argv, int max) {
    int n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        char *o = s; argv[n++] = o;
        int q = 0;
        for (; *s && (q || (*s != ' ' && *s != '\t')); s++) {
            if (*s == '\\' && s[1] == '"') { *o++ = '"'; s++; }
            else if (*s == '"') q = !q;
            else *o++ = *s;
        }
        if (*s) s++;
        *o = 0;
    }
    argv[n] = NULL;
    return n;
}
BOOL CreateProcessA(const char *app, char *cmdline, void *psa, void *tsa, BOOL inherit, DWORD flags, void *env, const char *cwd,
                    STARTUPINFOA *si, PROCESS_INFORMATION *pi) {
    (void)psa, (void)tsa, (void)inherit, (void)env, (void)si;
    if ((flags & CREATE_SUSPENDED) || cwd) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    char *copy = strdup(cmdline ? cmdline : ""), *argv[64];
    if (!copy) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    int argc = split_cmdline(copy, argv, 64);
    const char *exe = app ? app : argc ? argv[0] : NULL;
    if (!exe) { free(copy); SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    obj_t *o = obj_new(OBJ_PROC);
    if (!o) { free(copy); return FALSE; }
    extern char **environ;
    int r = posix_spawnp(&o->pid, exe, NULL, NULL, argv, environ);
    free(copy);
    if (r) { SetLastError((DWORD)r); CloseHandle(o); return FALSE; }
    pi->hProcess = o, pi->hThread = NULL, pi->dwProcessId = (DWORD)o->pid, pi->dwThreadId = 0;
    return TRUE;
}
static DWORD proc_wait(obj_t *o, DWORD ms) {
    if (o->state) return WAIT_OBJECT_0;
    uint64_t end = plat_now_ns() + (uint64_t)ms * 1000000u;
    for (;;) {
        int st;
        pid_t r = waitpid(o->pid, &st, ms == INFINITE ? 0 : WNOHANG);
        if (r == o->pid) { o->state = 1; o->count = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0); return WAIT_OBJECT_0; }
        if (r < 0 && errno != EINTR) { SetLastError((DWORD)errno); return WAIT_FAILED; }
        if (ms != INFINITE && plat_now_ns() >= end) return WAIT_TIMEOUT;
        if (ms != INFINITE) plat_sleep_ns(5000000);
    }
}
BOOL GetExitCodeProcess(HANDLE h, DWORD *code) {
    obj_t *o = h;
    if (!o || o->type != OBJ_PROC) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *code = o->state || proc_wait(o, 0) == WAIT_OBJECT_0 ? (DWORD)o->count : 259u;   /* STILL_ACTIVE */
    return TRUE;
}

HANDLE CreateWaitableTimerExW(void *sa, const void *name, DWORD flags, DWORD access) {
    (void)sa, (void)name, (void)flags, (void)access;
    return obj_new(OBJ_TIMER);
}
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, void *fn, void *arg, BOOL resume) {
    (void)period, (void)fn, (void)arg, (void)resume;
    obj_t *t = h;
    if (!t || t->type != OBJ_TIMER || due->QuadPart >= 0) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }   /* nxrt sets relative times only */
    t->due_ns = plat_now_ns() + (uint64_t)(-due->QuadPart) * 100u;
    return TRUE;
}
void SetThreadDescription(HANDLE h, const wchar_t *name) {
    (void)h;
    char n[64];
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, n, sizeof n, NULL, NULL)) plat_thread_name(n);
}

/* ---- paths ---- */
int MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, wchar_t *w, int wn) {   /* UTF-8 to UTF-32; n = -1: through the NUL */
    (void)cp, (void)flags;
    const unsigned char *p = (const unsigned char *)s, *e = n < 0 ? NULL : p + n;
    int k = 0;
    while (!e || p < e) {
        uint32_t c = *p++, extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) c &= 0x3Fu >> extra;
        for (uint32_t i = 0; i < extra && (!e || p < e) && (*p & 0xC0) == 0x80; i++) c = c << 6 | (*p++ & 0x3Fu);
        if (w) { if (k >= wn) { SetLastError(122); return 0; } w[k] = (wchar_t)c; }   /* ERROR_INSUFFICIENT_BUFFER */
        k++;
        if (!e && !c) break;
    }
    return k;
}
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *w, int n, char *s, int sn, const char *def, BOOL *used) {
    (void)cp, (void)flags, (void)def;
    if (used) *used = FALSE;
    int k = 0;
    for (int i = 0; n < 0 || i < n; i++) {
        uint32_t c = (uint32_t)w[i];
        unsigned char b[4]; int m;
        if (c < 0x80) b[0] = (unsigned char)c, m = 1;
        else if (c < 0x800) b[0] = (unsigned char)(0xC0 | c >> 6), b[1] = (unsigned char)(0x80 | (c & 0x3F)), m = 2;
        else if (c < 0x10000) b[0] = (unsigned char)(0xE0 | c >> 12), b[1] = (unsigned char)(0x80 | (c >> 6 & 0x3F)), b[2] = (unsigned char)(0x80 | (c & 0x3F)), m = 3;
        else b[0] = (unsigned char)(0xF0 | c >> 18), b[1] = (unsigned char)(0x80 | (c >> 12 & 0x3F)), b[2] = (unsigned char)(0x80 | (c >> 6 & 0x3F)), b[3] = (unsigned char)(0x80 | (c & 0x3F)), m = 4;
        if (s) { if (k + m > sn) { SetLastError(122); return 0; } memcpy(s + k, b, (size_t)m); }
        k += m;
        if (n < 0 && !c) break;
    }
    return k;
}
static void slashes(char *p) { for (; *p; p++) if (*p == '\\') *p = '/'; }
int win32_wpath(const wchar_t *w, char *out, size_t n) {
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL)) return -1;
    slashes(out);
    return 0;
}
static int apath(const char *a, char *out, size_t n) {   /* a narrow Windows-style path: the same rule */
    if (snprintf(out, n, "%s", a) >= (int)n) return -1;
    slashes(out);
    return 0;
}
FILE *_wfopen(const wchar_t *path, const wchar_t *mode) {
    char p[4096], m[16];
    if (win32_wpath(path, p, sizeof p) || WideCharToMultiByte(CP_UTF8, 0, mode, -1, m, sizeof m, NULL, NULL) <= 0) return NULL;
    return fopen(p, m);   /* "b" means nothing on Linux */
}
static BOOL mkdir_a(const char *p) {
    if (!mkdir(p, 0755)) return TRUE;
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : (DWORD)errno);
    return FALSE;
}
BOOL CreateDirectoryW(const wchar_t *path, void *sa) { (void)sa; char p[4096]; return !win32_wpath(path, p, sizeof p) && mkdir_a(p); }
BOOL CreateDirectoryA(const char *path, void *sa) { (void)sa; char p[4096]; return !apath(path, p, sizeof p) && mkdir_a(p); }
static DWORD attrs(const char *p) {
    struct stat st;
    if (stat(p, &st)) { SetLastError((DWORD)errno); return INVALID_FILE_ATTRIBUTES; }
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
DWORD GetFileAttributesW(const wchar_t *path) { char p[4096]; return win32_wpath(path, p, sizeof p) ? INVALID_FILE_ATTRIBUTES : attrs(p); }
DWORD GetFileAttributesA(const char *path) { char p[4096]; return apath(path, p, sizeof p) ? INVALID_FILE_ATTRIBUTES : attrs(p); }
BOOL DeleteFileW(const wchar_t *path) { char p[4096]; return !win32_wpath(path, p, sizeof p) && !unlink(p); }
BOOL DeleteFileA(const char *path) { char p[4096]; return !apath(path, p, sizeof p) && !unlink(p); }
/* rename; without MOVEFILE_REPLACE_EXISTING an existing target fails, as on Windows; WRITE_THROUGH flushes the folder after the rename */
static BOOL move(const char *a, const char *b, DWORD flags) {
    int r = flags & MOVEFILE_REPLACE_EXISTING ? rename(a, b) : renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_NOREPLACE);
    if (r && !(flags & MOVEFILE_REPLACE_EXISTING) && (errno == EINVAL || errno == ENOSYS)) {   /* a file system without RENAME_NOREPLACE */
        struct stat st;
        if (!lstat(b, &st)) errno = EEXIST, r = -1;
        else r = rename(a, b);
    }
    if (r) { SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : (DWORD)errno); return FALSE; }
    if (flags & MOVEFILE_WRITE_THROUGH) {
        char d[4096]; snprintf(d, sizeof d, "%s", b);
        char *s = strrchr(d, '/');
        if (s) *s = 0; else snprintf(d, sizeof d, ".");
        int fd = open(d, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) { fsync(fd); close(fd); }
    }
    return TRUE;
}
BOOL MoveFileExW(const wchar_t *from, const wchar_t *to, DWORD flags) {
    char a[4096], b[4096];
    return !win32_wpath(from, a, sizeof a) && !win32_wpath(to, b, sizeof b) && move(a, b, flags);
}
BOOL MoveFileExA(const char *from, const char *to, DWORD flags) {
    char a[4096], b[4096];
    return !apath(from, a, sizeof a) && !apath(to, b, sizeof b) && move(a, b, flags);
}

/* File handles: a descriptor in a kernel object (CloseHandle closes it) */
static HANDLE file_open(const char *p, DWORD access, DWORD disposition, DWORD flags) {
    int o = (access & GENERIC_WRITE ? (access & GENERIC_READ ? O_RDWR : O_WRONLY) : O_RDONLY) | O_CLOEXEC;
    if (disposition == CREATE_ALWAYS) o |= O_CREAT | O_TRUNC;
    if (flags & FILE_FLAG_WRITE_THROUGH) o |= O_DSYNC;
    int fd = open(p, o, 0644);
    if (fd < 0) { SetLastError((DWORD)errno); return INVALID_HANDLE_VALUE; }
    obj_t *f = obj_new(OBJ_FILE);
    if (!f) { close(fd); return INVALID_HANDLE_VALUE; }
    f->fd = fd;
    return f;
}
HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share, void *sa, DWORD disposition, DWORD flags, HANDLE tmpl) {
    (void)share, (void)sa, (void)tmpl;
    char p[4096];
    return win32_wpath(path, p, sizeof p) ? INVALID_HANDLE_VALUE : file_open(p, access, disposition, flags);
}
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *sa, DWORD disposition, DWORD flags, HANDLE tmpl) {
    (void)share, (void)sa, (void)tmpl;
    char p[4096];
    return apath(path, p, sizeof p) ? INVALID_HANDLE_VALUE : file_open(p, access, disposition, flags);
}
BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *written, void *ov) {
    (void)ov;
    obj_t *f = h;
    DWORD w = 0;
    while (w < n) {
        ssize_t k = write(f->fd, (const char *)buf + w, n - w);
        if (k < 0) { if (errno == EINTR) continue; SetLastError((DWORD)errno); break; }
        w += (DWORD)k;
    }
    if (written) *written = w;
    return w == n;
}
BOOL FlushFileBuffers(HANDLE h) { obj_t *f = h; if (fsync(f->fd)) { SetLastError((DWORD)errno); return FALSE; } return TRUE; }

/* ---- directory listing: "<folder>/<pattern>" (fnmatch), names sorted as NTFS lists them ---- */
typedef struct { uint32_t type; char dir[4096]; char **names; size_t n, at; } find_t;
static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static int find_fill(find_t *f, WIN32_FIND_DATAA *fd) {
    while (f->at < f->n) {
        const char *name = f->names[f->at++];
        char p[8192]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", f->dir, name);
        if (stat(p, &st)) continue;
        memset(fd, 0, sizeof *fd);
        fd->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        uint64_t t = (uint64_t)st.st_mtim.tv_sec * 10000000u + (uint64_t)st.st_mtim.tv_nsec / 100u + 116444736000000000ull;
        fd->ftLastWriteTime.dwLowDateTime = (DWORD)t, fd->ftLastWriteTime.dwHighDateTime = (DWORD)(t >> 32);
        snprintf(fd->cFileName, sizeof fd->cFileName, "%s", name);
        return 1;
    }
    return 0;
}
static void find_free(find_t *f) { for (size_t i = 0; i < f->n; i++) free(f->names[i]); free(f->names); f->type = 0; free(f); }
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd) {
    find_t *f = calloc(1, sizeof *f);
    if (!f) return INVALID_HANDLE_VALUE;
    if (apath(pattern, f->dir, sizeof f->dir)) { free(f); return INVALID_HANDLE_VALUE; }
    char *s = strrchr(f->dir, '/'), pat[256];
    if (!s) { free(f); SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    snprintf(pat, sizeof pat, "%s", s + 1);
    *s = 0;
    f->type = OBJ_FIND;
    DIR *d = opendir(f->dir);
    if (!d) { SetLastError(errno == ENOENT ? 2u : (DWORD)errno); free(f); return INVALID_HANDLE_VALUE; }   /* ERROR_FILE_NOT_FOUND */
    size_t cap = 0; struct dirent *e;
    while ((e = readdir(d))) {
        if (fnmatch(pat, e->d_name, 0)) continue;
        if (f->n == cap) { cap = cap ? cap * 2 : 32; char **x = realloc(f->names, cap * sizeof *x); if (!x) break; f->names = x; }
        if (!(f->names[f->n] = strdup(e->d_name))) break;
        f->n++;
    }
    closedir(d);
    if (f->n) qsort(f->names, f->n, sizeof *f->names, by_name);
    if (!find_fill(f, fd)) { find_free(f); SetLastError(2u); return INVALID_HANDLE_VALUE; }
    return f;
}
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd) { find_t *f = h; return f && f->type == OBJ_FIND && find_fill(f, fd); }
BOOL FindClose(HANDLE h) { find_t *f = h; if (!f || f->type != OBJ_FIND) return FALSE; find_free(f); return TRUE; }
BOOL GetFileAttributesExW(const wchar_t *path, GET_FILEEX_INFO_LEVELS level, void *out) {
    char p[4096]; struct stat st;
    if (level != GetFileExInfoStandard || win32_wpath(path, p, sizeof p) || stat(p, &st)) { SetLastError(2u); return FALSE; }
    WIN32_FILE_ATTRIBUTE_DATA *a = out;
    memset(a, 0, sizeof *a);
    a->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    uint64_t t = (uint64_t)st.st_mtim.tv_sec * 10000000u + (uint64_t)st.st_mtim.tv_nsec / 100u + 116444736000000000ull;
    a->ftLastWriteTime.dwLowDateTime = (DWORD)t, a->ftLastWriteTime.dwHighDateTime = (DWORD)(t >> 32);
    a->nFileSizeLow = (DWORD)st.st_size, a->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    return TRUE;
}
LONG CompareFileTime(const FILETIME *a, const FILETIME *b) {
    uint64_t x = (uint64_t)a->dwHighDateTime << 32 | a->dwLowDateTime, y = (uint64_t)b->dwHighDateTime << 32 | b->dwLowDateTime;
    return x < y ? -1 : x > y;
}
BOOL RemoveDirectoryA(const char *path) { char p[4096]; return !apath(path, p, sizeof p) && !rmdir(p); }
DWORD GetTempPathA(DWORD n, char *out) {
    const char *t = getenv("TMPDIR");
    int k = snprintf(out, n, "%s/", t && *t ? t : "/tmp");
    return k > 0 && (DWORD)k < n ? (DWORD)k : 0;
}

const KNOWNFOLDERID FOLDERID_LocalAppData = 1, FOLDERID_RoamingAppData = 2;
HRESULT SHGetKnownFolderPathC(const KNOWNFOLDERID *id, DWORD flags, HANDLE token, wchar_t **out) {
    (void)flags, (void)token;
    int local = *id == FOLDERID_LocalAppData;
    const char *x = getenv(local ? "XDG_CACHE_HOME" : "XDG_DATA_HOME"), *home = getenv("HOME");
    char p[4096];
    if (x && *x == '/') snprintf(p, sizeof p, "%s", x);
    else if (home && *home) snprintf(p, sizeof p, "%s/%s", home, local ? ".cache" : ".local/share");
    else return -1;
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }   /* a fresh system may not have it yet */
    mkdir(p, 0755);
    int n = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
    *out = malloc(sizeof(wchar_t) * (size_t)n);
    if (!*out) return -1;
    MultiByteToWideChar(CP_UTF8, 0, p, -1, *out, n);
    return S_OK;
}
