// from nxnative dm/host/crash.c (MIT, © seedds): crash state with module-relative addresses, page-fault hooks, the guest frame-pointer backtrace.
// Windows: signals become a vectored exception handler (fault hooks: write-protect dirty tracking, the code table's lazy pages) and an unhandled
// exception filter (the crash report). The report goes to the log and to <log>.crash.txt, with the faulting thread's guest context: its NxwGuestCpu.
// Dropped: the progress watchdog (DM_WATCH; diagnostic), the Mac sampler, the iOS dump port.
// Linux (docs/LINUX-PLAN.md L-D4): SIGSEGV / SIGBUS on an alternate stack run the same hook chain, then a plat_guard recovery point, then the report.
#include "dm.h"
#include "nxrt/runtime.h"

#include <signal.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <dlfcn.h>
#include <execinfo.h>
#include <ucontext.h>
#endif

// ---------- Page fault hooks (write-protect dirty page tracking, lazily committed tables) ----------
// Called in registration order; returning 1 = handled (the faulting instruction runs again); the crash path is taken only when all return 0.
// Hooks run inside the exception dispatch: only table lookups / atomic operations / VirtualProtect / VirtualAlloc, no locks.
#define MAX_FAULT_HOOK 4
static int (*g_fault_hook[MAX_FAULT_HOOK])(void *addr, int is_write, void *ctx);
static void (*g_write_prep[MAX_FAULT_HOOK])(void *addr, size_t len);
static int g_nfault_hook, g_nwrite_prep;
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *ctx)) {
    if (g_nfault_hook >= MAX_FAULT_HOOK) die("too many page fault hooks");
    g_fault_hook[g_nfault_hook++] = fn;
}
void dm_guest_write_prep_register(void (*fn)(void *addr, size_t len)) {
    if (g_nwrite_prep >= MAX_FAULT_HOOK) die("too many host pre-write hooks");
    g_write_prep[g_nwrite_prep++] = fn;
}
// The write-protect tracker's own view of a page (gfx/nvn_dirty.c): a hook ahead of it in the chain (the heap's lazy commit) asks this before its VirtualQuery,
// a system call that cost one on every tracked write in gameplay (M6)
static int (*g_wp_query)(void *addr);
void dm_fault_wp_register(int (*fn)(void *addr)) { g_wp_query = fn; }
int dm_fault_is_wp(void *addr) { return g_wp_query && g_wp_query(addr); }
// Called before the host itself writes guest memory (a file read straight into a guest buffer): a kernel write into a protected page fails instead of faulting
void dm_guest_write_prepare(void *addr, size_t len) { for (int i = 0; i < g_nwrite_prep; i++) g_write_prep[i](addr, len); }

static void (*g_dump_hook)(void);
void crash_set_dump_hook(void (*fn)(void)) { g_dump_hook = fn; }

// "main+0x1234 symbol+0x10", "game:sdk+0x1234 symbol (+0x20)" in the translated code of a guest function, "nxrt.exe+0x1234" or the raw address
static void addr_name(uint64_t a, char *out, size_t n) {
    if (module_of((void *)(uintptr_t)a) || nxrt_token_name(a) || a == NXRT_RETURN_TO_HOST) { nxrt_symbolize(a, out, n); return; }
    uint64_t off = 0, g = nxrt_host_to_guest(a, &off);
    if (g) { char s[160]; nxrt_symbolize(g, s, sizeof s); snprintf(out, n, "game:%s (+%#llx)", s, (unsigned long long)off); return; }
#ifndef _WIN32
    Dl_info di;
    if (a > 0x10000 && dladdr((void *)(uintptr_t)a, &di) && di.dli_fname) {
        const char *b = strrchr(di.dli_fname, '/');
        snprintf(out, n, "%s+%#llx", b ? b + 1 : di.dli_fname, (unsigned long long)(a - (uint64_t)(uintptr_t)di.dli_fbase));
        return;
    }
#else
    HMODULE h = NULL; char path[MAX_PATH];
    if (a > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)a, &h) &&
        GetModuleFileNameA(h, path, sizeof path)) {
        const char *b = strrchr(path, '\\');
        snprintf(out, n, "%s+%#llx", b ? b + 1 : path, (unsigned long long)(a - (uint64_t)(uintptr_t)h));
        return;
    }
#endif
    snprintf(out, n, "%#llx", (unsigned long long)a);
}

// Frame pointer backtrace: x29 chain [fp]=previous frame's fp, [fp+8]=return address; only 8-byte aligned, monotonically increasing frames are walked.
// Each frame is read with plat_read, which fails instead of faulting: code built without frame pointers keeps data in x29
void dm_backtrace(uint64_t fp) {
    char line[1536]; size_t q = (size_t)snprintf(line, sizeof line, "guest bt:");
    for (int i = 0; i < 32 && fp && !(fp & 7) && q < sizeof line - 80; i++) {
        uint64_t w[2];
        if (plat_read(fp, w, sizeof w)) break;
        char nm[96]; addr_name(w[1], nm, sizeof nm);
        q += (size_t)snprintf(line + q, sizeof line - q, " %s", nm);
        if (w[0] <= fp) break;
        fp = w[0];
    }
    LOG("%s", line);
}

static void report_cpu(FILE *f, const NxwGuestCpu *c) {
    char a[96], b[96];
    addr_name(c->pc, a, sizeof a); addr_name(c->x[30], b, sizeof b);
    fprintf(f, "guest context (the thread's NxwGuestCpu: exact at the last SVC or helper call, else as recompiled code last stored it):\n");
    fprintf(f, "  pc=%s lr=%s sp=%#llx nzcv=%#llx fpcr=%#llx fpsr=%#llx tpidr=%#llx tpidrro=%#llx\n", a, b, (unsigned long long)c->sp,
            (unsigned long long)c->nzcv, (unsigned long long)c->fpcr, (unsigned long long)c->fpsr, (unsigned long long)c->tpidr_el0, (unsigned long long)c->tpidrro_el0);
    for (int i = 0; i < 31; i++) fprintf(f, "%sx%-2d=%016llx%s", i % 4 ? " " : "  ", i, (unsigned long long)c->x[i], i % 4 == 3 || i == 30 ? "\n" : "");
    // What the registers point at: the first four words of each readable target; a word inside a module prints as a module offset, so an object's vtable names its class
    for (int i = 0; i < 31; i++) {
        uint64_t p = c->x[i], w[4];
        if (p < 0x100000000ull || (p & 7) || plat_read(p, w, sizeof w)) continue;
        fprintf(f, "  *x%d:", i);
        for (int k = 0; k < 4; k++) { char nm[96]; addr_name(w[k], nm, sizeof nm); fprintf(f, " %s", nm); }
        fputc('\n', f);
    }
    fprintf(f, "  guest frames:");
    uint64_t fp = c->x[29];
    for (int i = 0; i < 32 && fp && !(fp & 7); i++) {
        uint64_t w[2];
        if (plat_read(fp, w, sizeof w)) break;
        char nm[96]; addr_name(w[1], nm, sizeof nm); fprintf(f, " %s", nm);
        if (w[0] <= fp) break;
        fp = w[0];
    }
    fputc('\n', f);
}

#ifndef _WIN32
// The faulting thread's own frames from the unwinder (game.so and nxrt carry .eh_frame), starting at the signal handler; the fault's pc first
typedef struct { int sig; siginfo_t *si; ucontext_t *uc; } sig_ctx;
static void report_host_stack(FILE *f, const sig_ctx *sc) {
    void *fr[48];
    int n = backtrace(fr, 48);
    char nm[96];
    fprintf(f, "host frames:");
    if (sc->uc) { addr_name((uint64_t)sc->uc->uc_mcontext.gregs[REG_RIP], nm, sizeof nm); fprintf(f, " %s |", nm); }
    for (int i = 0; i < n; i++) { addr_name((uint64_t)(uintptr_t)fr[i], nm, sizeof nm); fprintf(f, " %s", nm); }
    fputc('\n', f);
}
// The thread's name and the mappings (/proc/self/maps) that hold the pc and the faulting address: code outside every module is a driver's JIT (lavapipe)
// or the game's; an address in no mapping prints its neighbours
static void report_maps(FILE *f, uint64_t pc, uint64_t addr, int has_addr) {
    char p[64], name[32] = "?"; snprintf(p, sizeof p, "/proc/self/task/%lu/comm", (unsigned long)GetCurrentThreadId());
    FILE *c = fopen(p, "r");
    if (c) { if (fgets(name, sizeof name, c)) name[strcspn(name, "\n")] = 0; fclose(c); }
    fprintf(f, "thread name: %s\n", name);
    FILE *m = fopen("/proc/self/maps", "r");
    if (!m) return;
    char line[512], below[512] = "", above[512] = "";
    int pc_seen = 0, addr_seen = !has_addr;
    while (fgets(line, sizeof line, m)) {
        unsigned long long lo, hi;
        if (sscanf(line, "%llx-%llx", &lo, &hi) != 2) continue;
        line[strcspn(line, "\n")] = 0;
        if (pc >= lo && pc < hi) { fprintf(f, "  pc in: %s\n", line); pc_seen = 1; }
        if (has_addr && addr >= lo && addr < hi) { fprintf(f, "  address in: %s\n", line); addr_seen = 1; }
        if (has_addr && hi <= addr) snprintf(below, sizeof below, "%s", line);
        if (has_addr && lo > addr && !*above) snprintf(above, sizeof above, "%s", line);
    }
    fclose(m);
    if (!pc_seen) fprintf(f, "  pc in no mapping\n");
    if (!addr_seen) fprintf(f, "  address in no mapping; below it: %s; above it: %s\n", *below ? below : "-", *above ? above : "-");
}
#else
static void report_host_stack(FILE *f, const CONTEXT *ctx) {
    CONTEXT c = *ctx;
    fprintf(f, "host frames:");
    for (int i = 0; i < 32 && c.Rip; i++) {
        char nm[96]; addr_name(c.Rip, nm, sizeof nm); fprintf(f, " %s", nm);
        DWORD64 base = 0; PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (!fn) {   // a leaf: the return address is at rsp
            if (plat_read(c.Rsp, &c.Rip, 8)) break;
            c.Rsp += 8;
            continue;
        }
        void *hd; DWORD64 ef;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &hd, &ef, NULL);
    }
    fputc('\n', f);
}
#endif

// Only the first crashing thread prints its state: when several threads crash at once the dumps used to interleave. Later threads wait for the
// process to end; the first thread crashing again while printing exits at once
static _Atomic uint32_t g_crash_owner;
static int crash_gate(void) {
    uint32_t me = GetCurrentThreadId(), none = 0;
    if (atomic_compare_exchange_strong(&g_crash_owner, &none, me)) return 1;
#ifdef _WIN32
    if (none == me) ExitProcess(70);
    for (;;) Sleep(INFINITE);
#else
    if (none == me) _exit(70);
    for (;;) pause();
#endif
}

// The report: the log gets it too, and <log>.crash.txt keeps it whatever happens to the log afterwards
void crash_report(const char *why, const void *ctx_) {
#ifdef _WIN32
    const EXCEPTION_POINTERS *ep = ctx_;
#else
    const sig_ctx *ep = ctx_;
#endif
    crash_gate();
    char path[MAX_PATH + 16]; snprintf(path, sizeof path, "%s.crash.txt", *nxrt_log_path() ? nxrt_log_path() : "nxrt");
    FILE *f = fopen(path, "w");
    for (int pass = 0; pass < 2; pass++) {
        FILE *o = pass ? stderr : f;
        if (!o) continue;
        fprintf(o, "nxrt crash report: %s\nthread %lu, game build %s\n", why, (unsigned long)GetCurrentThreadId(), dm_main_build());
#ifndef _WIN32
        if (ep) {
            char at[96], a2[96];
            addr_name(ep->uc ? (uint64_t)ep->uc->uc_mcontext.gregs[REG_RIP] : 0, at, sizeof at);
            fprintf(o, "signal %d (%s) at %s", ep->sig, strsignal(ep->sig), at);
            if ((ep->sig == SIGSEGV || ep->sig == SIGBUS) && ep->si) {
                addr_name((uint64_t)(uintptr_t)ep->si->si_addr, a2, sizeof a2);
                long err = ep->uc ? (long)ep->uc->uc_mcontext.gregs[REG_ERR] : 0;
                fprintf(o, ": %s %s", err & 2 ? "write to" : err & 16 ? "execute" : "read of", a2);
            }
            fputc('\n', o);
            report_host_stack(o, ep);
            report_maps(o, ep->uc ? (uint64_t)ep->uc->uc_mcontext.gregs[REG_RIP] : 0, (uint64_t)(uintptr_t)(ep->si ? ep->si->si_addr : 0),
                        (ep->sig == SIGSEGV || ep->sig == SIGBUS) && ep->si);
        }
#else
        if (ep) {
            const EXCEPTION_RECORD *er = ep->ExceptionRecord; char at[96], a2[96];
            addr_name((uint64_t)(uintptr_t)er->ExceptionAddress, at, sizeof at);
            fprintf(o, "exception %#lx at %s", er->ExceptionCode, at);
            if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
                addr_name(er->ExceptionInformation[1], a2, sizeof a2);
                fprintf(o, ": %s %s", er->ExceptionInformation[0] == 1 ? "write to" : er->ExceptionInformation[0] == 8 ? "execute" : "read of", a2);
            }
            fputc('\n', o);
            report_host_stack(o, ep->ContextRecord);
        }
#endif
        NxwGuestCpu *c = nxrt_cur_cpu();
        if (c) report_cpu(o, c); else fprintf(o, "not a guest thread\n");
        fflush(o);
    }
    if (f) fclose(f);
    LOG("crash report written to %s", path);
    if (g_dump_hook) g_dump_hook();
}

#ifndef _WIN32
__thread plat_guard *plat_guard_top;
void crash_thread_init(void);
// One handler for every fatal signal. SA_NODEFER: a plat_guard recovery leaves the handler with siglongjmp and no signal mask to restore
static void on_signal(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    if ((sig == SIGSEGV || sig == SIGBUS) && si) {
        int is_write = uc && (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
        int saved = errno;
        for (int i = 0; i < g_nfault_hook; i++) if (g_fault_hook[i](si->si_addr, is_write, uc)) { errno = saved; return; }
        plat_guard *g = plat_guard_top;
        if (g) { plat_guard_top = g->prev; siglongjmp(g->jb, 1); }
    }
    char why[64]; snprintf(why, sizeof why, "signal %d (%s)", sig, sig == SIGABRT ? "abort()" : strsignal(sig));
    sig_ctx sc = {sig, si, uc};
    crash_report(why, &sc);
    _exit(128 + sig);
}
void crash_install(void) {
    static int done;
    if (done) return;
    done = 1;
    void *warm[2]; backtrace(warm, 2);   // loads the unwinder now: the first backtrace may allocate, which a crashing thread must not
    crash_thread_init();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    static const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};
    for (int s = 0; s < 6; s++) sigaction(sigs[s], &sa, NULL);
    signal(SIGPIPE, SIG_IGN);   // a closed Link Play socket is an error code, never the end of the game
}
// Per-thread setup: an alternate signal stack, so the hooks and the report run after a stack overflow too
void crash_thread_init(void) {
    static _Thread_local void *alt;
    if (alt) return;
    size_t n = 64 * 1024;
    alt = malloc(n);
    if (!alt) return;
    stack_t ss = {.ss_sp = alt, .ss_size = n, .ss_flags = 0};
    sigaltstack(&ss, NULL);
}
#else
static LONG CALLBACK on_vectored(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    void *addr = (void *)(uintptr_t)er->ExceptionInformation[1];
    int is_write = er->ExceptionInformation[0] == 1;
    for (int i = 0; i < g_nfault_hook; i++) if (g_fault_hook[i](addr, is_write, ep)) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI on_unhandled(EXCEPTION_POINTERS *ep) {
    char why[64]; snprintf(why, sizeof why, "unhandled exception %#lx", ep->ExceptionRecord->ExceptionCode);
    crash_report(why, ep);
    ExitProcess(128 + 11);
}
static void on_abort(int sig) { (void)sig; crash_report("abort()", NULL); ExitProcess(128 + 6); }
static void on_invalid_parameter(const wchar_t *e, const wchar_t *f, const wchar_t *file, unsigned line, uintptr_t r) {
    (void)e; (void)f; (void)file; (void)line; (void)r;
    crash_report("C runtime invalid parameter", NULL); ExitProcess(128 + 6);
}

void crash_install(void) {
    static int done;
    if (done) return;
    done = 1;
    AddVectoredExceptionHandler(1, on_vectored);
    SetUnhandledExceptionFilter(on_unhandled);
    signal(SIGABRT, on_abort);
    _set_invalid_parameter_handler(on_invalid_parameter);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}
// Per-thread setup: room on the stack for the report after a stack overflow
void crash_thread_init(void) { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }
#endif
void crash_dump_now(const char *why) { LOG("state dump (%s)", why); if (g_dump_hook) g_dump_hook(); }
