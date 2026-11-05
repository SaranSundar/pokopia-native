/* What the game DLL imports (include/nxrt/rt_abi.h): the indirect-branch table, misses, SVCs, system registers, traps, yields; and host-to-guest calls.
   Replaces nxnative's gate machinery (entry.S dm_gate, hle.c gate_dispatch): recompiled code calls rt_svc / rt_mrs directly with its NxwGuestCpu, whose
   first 800 bytes are the gate frame nxnative's handlers already read. */
#include "dm.h"
#include "nxrt/runtime.h"
#include "coop.h"
#include <stddef.h>
#include <stdatomic.h>

const uint32_t *rt_code_table;
uint64_t rt_code_base, rt_code_words, rt_host_base;
volatile uint8_t rt_trace_on;
static uint32_t *g_table;
static size_t g_table_bytes;
static uint8_t *g_image;          /* the base the table's RVAs count from: the game DLL (or, in tests, the executable) */
static size_t g_image_size;
static NxwGuestFn (*g_interp)(uint64_t);
/* The game DLL's functions sorted by RVA: host addresses in crash reports name their guest function */
static NxwGameFunction *g_by_rva;
static uint32_t g_nfn;

void nxrt_host_call(NxwGuestCpu *cpu, uint32_t token);   /* hle.c */
void thr_svc_enter(uint64_t *regs, uint64_t pc);          /* hle_thread.c */
void thr_svc_leave(void);
void thr_safepoint(void);
void dm_spin_wait(uint64_t site);
uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr);   /* hle_svc.c */
void crash_report(const char *why, const void *ctx);

/* ---- The code table ---- */
/* Pages of the table are committed when first touched: a read of a page nothing was written to sees zeros (a miss) */
static int table_fault(void *addr, int is_write, void *ctx) {
    (void)is_write; (void)ctx;
    uint8_t *a = addr;
    if (!g_table || a < (uint8_t *)g_table || a >= (uint8_t *)g_table + g_table_bytes) return 0;
    return VirtualAlloc((void *)((uintptr_t)a & ~(uintptr_t)0xfff), 0x1000, MEM_COMMIT, PAGE_READWRITE) != NULL;
}
int nxrt_code_table_create(uint64_t base, uint64_t end) {
    if (g_table) return 0;
    rt_code_base = base & ~3ull; rt_code_words = (end - rt_code_base + 3) / 4;
    g_table_bytes = (rt_code_words * 4 + 0xfff) & ~(size_t)0xfff;
    g_table = VirtualAlloc(NULL, g_table_bytes, MEM_RESERVE, PAGE_READWRITE);
    if (!g_table) return -1;
    dm_fault_hook_register(table_fault);
    rt_code_table = g_table;
    LOG("code table: %#llx-%#llx, %llu slots (%.1f MB reserved, committed on first touch)", (unsigned long long)rt_code_base, (unsigned long long)end,
        (unsigned long long)rt_code_words, g_table_bytes / 1048576.0);
    return 0;
}
static int table_put(uint64_t va, uint32_t rva) {
    uint64_t i = (va - rt_code_base) >> 2;
    if (!g_table || va < rt_code_base || i >= rt_code_words || (va & 3)) return -1;
    uint32_t *slot = &g_table[i];
    if (!VirtualAlloc((void *)((uintptr_t)slot & ~(uintptr_t)0xfff), 0x1000, MEM_COMMIT, PAGE_READWRITE)) return -1;
    *slot = rva;
    return 0;
}

/* The token thunks: "mov edx, k; jmp common", common = "jmp [rip]; .quad nxrt_host_call". A table entry for token k is its thunk's RVA, so an indirect
   call through a token reaches nxrt_host_call(cpu, k) with the caller's rcx (cpu) intact. The block must lie within 4 GB above the image */
static uint8_t *thunks_place(size_t n) {
    uint64_t lo = ((uint64_t)(uintptr_t)g_image + g_image_size + 0xffff) & ~0xffffull, hi = (uint64_t)(uintptr_t)g_image + 0xFFFFFFFFull - n;
    for (uint64_t a = lo; a < hi;) {
        void *p = VirtualAlloc((void *)(uintptr_t)a, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return p;
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)(uintptr_t)a, &m, sizeof m)) break;
        a = ((uint64_t)(uintptr_t)m.BaseAddress + m.RegionSize + 0xffff) & ~0xffffull;
    }
    return NULL;
}
int nxrt_code_attach(void *image, size_t image_size) {
    if (!g_table) return -1;
    g_image = image; g_image_size = image_size; rt_host_base = (uint64_t)(uintptr_t)image;
    size_t ntok = NXRT_TOKEN_SIZE / NXRT_TOKEN_STRIDE, n = 16 + ntok * 16;
    uint8_t *b = thunks_place(n);
    if (!b) { LOG("[RED] no room for the host-call thunks within 4 GB of %p", image); return -1; }
    b[0] = 0xFF; b[1] = 0x25; memset(b + 2, 0, 4);
    void (*target)(NxwGuestCpu *, uint32_t) = nxrt_host_call; memcpy(b + 6, &target, 8);
    for (size_t k = 0; k < ntok; k++) {
        uint8_t *t = b + 16 + 16 * k;
        int32_t rel = (int32_t)(b - (t + 10));
        t[0] = 0xBA; uint32_t kk = (uint32_t)k; memcpy(t + 1, &kk, 4);
        t[5] = 0xE9; memcpy(t + 6, &rel, 4);
        memset(t + 10, 0xCC, 6);
    }
    DWORD old;
    if (!VirtualProtect(b, n, PAGE_EXECUTE_READ, &old)) return -1;
    FlushInstructionCache(GetCurrentProcess(), b, n);
    for (size_t k = 0; k < ntok; k++)
        if (table_put(NXRT_TOKEN_BASE + k * NXRT_TOKEN_STRIDE, (uint32_t)(b + 16 + 16 * k - (uint8_t *)image))) return -1;
    LOG("code image %p (+%#zx), host-call thunks at %p", image, image_size, (void *)b);
    return 0;
}
int nxrt_code_set(uint64_t va, NxwGuestFn fn) {
    uint64_t d = (uint64_t)(uintptr_t)fn - (uint64_t)(uintptr_t)g_image;
    if (!g_image || (uintptr_t)fn <= (uintptr_t)g_image || d > 0xFFFFFFFFull) return -1;
    return table_put(va, (uint32_t)d);
}
NxwGuestFn nxrt_code_lookup(uint64_t va) {
    uint64_t i = (va - rt_code_base) >> 2;
    uint32_t f = g_table && va >= rt_code_base && i < rt_code_words ? g_table[i] : 0;
    return f ? (NxwGuestFn)(void *)(g_image + f) : rt_miss(nxrt_cur_cpu(), va);
}
void nxrt_set_interpreter(NxwGuestFn (*entry_for)(uint64_t)) { g_interp = entry_for; }

static int by_rva(const void *a, const void *b) {
    uint32_t x = ((const NxwGameFunction *)a)->rva, y = ((const NxwGameFunction *)b)->rva;
    return x < y ? -1 : x > y;
}
/* The guest function whose translated code holds a host address (0 if none): crash reports and backtraces */
uint64_t nxrt_host_to_guest(uint64_t host, uint64_t *off) {
    if (!g_nfn || host < (uint64_t)(uintptr_t)g_image || host >= (uint64_t)(uintptr_t)g_image + g_image_size) return 0;
    uint32_t rva = (uint32_t)(host - (uint64_t)(uintptr_t)g_image), lo = 0, hi = g_nfn;
    while (hi - lo > 1) { uint32_t mid = (lo + hi) / 2; if (g_by_rva[mid].rva <= rva) lo = mid; else hi = mid; }
    if (g_by_rva[lo].rva > rva) return 0;
    if (off) *off = rva - g_by_rva[lo].rva;
    return g_by_rva[lo].guest_va;
}

/* ---- Interpreted functions with site hooks: "Interact everywhere" (src/coop/coop_interact.c, DM_COOP_INTERACT=1). The translated function holding a site
   jumps to the interpreter instead (its entry is overwritten: mov rax, va; mov [rcx + pc], rax; mov rax, nxw_interp_trampoline; jmp rax), and the
   interpreter calls the shared callback right before the site's instruction, as nxnative's observation gate does ---- */
void nxw_interp_trampoline(NxwGuestCpu *cpu);   /* src/interp */
typedef void (*NxwInterpSiteHook)(uint64_t pc, uint64_t *x);
void nxw_interp_set_site_hook(NxwInterpSiteHook hook, uint64_t lo, uint64_t hi);
static const coop_site *g_isite; static int g_nisite; static uint64_t g_isite_base;
static void interact_hook(uint64_t pc, uint64_t *x) {
    static _Atomic int said;
    for (int i = 0; i < g_nisite; i++) {
        if (pc != g_isite_base + g_isite[i].off) continue;
        uint64_t was = x[13]; coop_interact_site(g_isite[i].id, x);
        if (x[13] != was && !atomic_exchange(&said, 1)) LOG("[coop] Interact everywhere: this story area's Link Play session has Palette Town's permissions");
    }
}
static int interpret_function(uint64_t va) {
    uint64_t i = (va - rt_code_base) >> 2;
    uint32_t rva = va >= rt_code_base && i < rt_code_words ? g_table[i] : 0;
    if (!rva) return -1;
    uint8_t *f = g_image + rva, c[29];
    void (*tramp)(NxwGuestCpu *) = nxw_interp_trampoline;
    uint32_t pc_off = (uint32_t)offsetof(NxwGuestCpu, pc);
    c[0] = 0x48; c[1] = 0xB8; memcpy(c + 2, &va, 8);
    c[10] = 0x48; c[11] = 0x89; c[12] = 0x81; memcpy(c + 13, &pc_off, 4);
    c[17] = 0x48; c[18] = 0xB8; memcpy(c + 19, &tramp, 8);
    c[27] = 0xFF; c[28] = 0xE0;
    DWORD old;
    if (!VirtualProtect(f, sizeof c, PAGE_EXECUTE_READWRITE, &old)) return -1;
    memcpy(f, c, sizeof c);
    VirtualProtect(f, sizeof c, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f, sizeof c);
    return 0;
}
static void interact_attach(void) {
    module_t *m = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) m = &g_mod[i];
    const coop_site *s; int n = m ? coop_interact_sites(m->build_id, &s) : 0;
    if (!n) { LOG("[RED] DM_COOP_INTERACT: Interact everywhere has no sites for main %.16s: off", m ? m->build_id : "?"); return; }
    uint64_t base = (uint64_t)(uintptr_t)m->base, lo = UINT64_MAX, hi = 0;
    for (int k = 0; k < n; k++) {
        uint32_t w = *(const uint32_t *)(uintptr_t)(base + s[k].off);
        if (w != s[k].word) { LOG("[RED] DM_COOP_INTERACT: main+%#x is %08x, not %08x: off", s[k].off, w, s[k].word); return; }
    }
    for (int k = 0; k < n; k++) {
        if (interpret_function(base + s[k].fn)) { LOG("[RED] DM_COOP_INTERACT: no translated function at main+%#x: off", s[k].fn); return; }
        if (base + s[k].off < lo) lo = base + s[k].off;
        if (base + s[k].off + 4 > hi) hi = base + s[k].off + 4;
        LOG("DM_COOP_INTERACT: main+%#x (%s) runs in the interpreter with a hook at main+%#x", s[k].fn, s[k].what, s[k].off);
    }
    g_isite = s; g_nisite = n; g_isite_base = base;
    nxw_interp_set_site_hook(interact_hook, lo, hi);
    LOG("Interact everywhere: on (%d site%s); every player in a session needs it", n, n == 1 ? "" : "s");
}

/* The game DLL (the contract in rt_abi.h): its function list fills the table */
int nxrt_attach_game_dll(const char *path) {
    uint64_t t0 = now_ns();
    HMODULE h = LoadLibraryA(path);
    if (!h) { LOG("[RED] cannot load the game DLL %s (error %lu)", path, GetLastError()); return -1; }
    const uint32_t *abi = (const uint32_t *)(void *)GetProcAddress(h, "nxw_game_abi");
    const NxwGameFunction *fns = (const NxwGameFunction *)(void *)GetProcAddress(h, "nxw_game_functions");
    const uint32_t *nfn = (const uint32_t *)(void *)GetProcAddress(h, "nxw_game_function_count");
    const char *bid = (const char *)(void *)GetProcAddress(h, "nxw_game_build_id");
    if (!abi || !fns || !nfn) { LOG("[RED] %s does not export the nxw_game_* contract", path); return -1; }
    if (*abi != NXW_ABI_VERSION) { LOG("[RED] %s was built for runtime ABI %u, this runtime is %d", path, *abi, NXW_ABI_VERSION); return -1; }
    if (bid && strncmp(bid, dm_main_build(), 32)) { LOG("[RED] %s is for main %s, the loaded main is %s", path, bid, dm_main_build()); return -1; }
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((uint8_t *)h + ((IMAGE_DOS_HEADER *)h)->e_lfanew);
    if (nxrt_code_attach(h, nt->OptionalHeader.SizeOfImage)) return -1;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < *nfn; i++) bad += table_put(fns[i].guest_va, fns[i].rva) != 0;
    g_by_rva = malloc(sizeof *g_by_rva * (*nfn ? *nfn : 1));
    if (g_by_rva) { memcpy(g_by_rva, fns, sizeof *g_by_rva * *nfn); qsort(g_by_rva, *nfn, sizeof *g_by_rva, by_rva); g_nfn = *nfn; }
    LOG("game DLL %s: base %p, %.1f MB, %u functions (main %.16s), table filled in %.1f ms%s", path, (void *)h, nt->OptionalHeader.SizeOfImage / 1048576.0,
        *nfn, bid ? bid : "?", (now_ns() - t0) / 1e6, bad ? " [RED] (some outside the table)" : "");
    if (!bad && coop_interact_on()) interact_attach();
    return bad ? -1 : 0;
}

/* ---- Host-to-guest calls ---- */
uint64_t nxrt_guest_call(uint64_t va, int n, const uint64_t *args) {
    NxwGuestCpu *c = nxrt_cur_cpu();
    if (!c) die("guest call to %#llx from a host thread", (unsigned long long)va);
    if (n > 8) die("guest call to %#llx with %d arguments", (unsigned long long)va, n);
    uint64_t lr = c->x[30], pc = c->pc;
    for (int i = 0; i < n; i++) c->x[i] = args[i];
    c->x[30] = NXRT_RETURN_TO_HOST; c->pc = va;
    nxrt_code_lookup(va)(c);
    c->x[30] = lr; c->pc = pc;
    return c->x[0];
}

/* ---- Exports ---- */
static _Thread_local uint32_t t_token;
static void token_tramp(NxwGuestCpu *cpu) { nxrt_host_call(cpu, t_token); }
void rt_return_to_host(NxwGuestCpu *cpu) { (void)cpu; }

/* Misses inside a module go to the profile list the next build compiles (research/03 §5.4, kirikae's loop) */
static void miss_log(uint64_t va) {
    static SRWLOCK lk = SRWLOCK_INIT; static uint64_t seen[1024]; static int n;
    AcquireSRWLockExclusive(&lk);
    int dup = 0; for (int i = 0; i < n; i++) dup |= seen[i] == va;
    if (!dup && n < 1024) {
        seen[n++] = va;
        char p[MAX_PATH + 16]; snprintf(p, sizeof p, "%s.misses.txt", nxrt_log_path());
        FILE *f = fopen(p, "a");
        module_t *m = module_of((void *)(uintptr_t)va);
        if (f) { fprintf(f, "%s+%#llx\n", m ? m->name : "?", (unsigned long long)(m ? va - (uint64_t)(uintptr_t)m->base : va)); fclose(f); }
    }
    ReleaseSRWLockExclusive(&lk);
}
NxwGuestFn rt_miss(NxwGuestCpu *cpu, uint64_t target) {
    if (target >= NXRT_TOKEN_BASE && target < NXRT_TOKEN_BASE + NXRT_TOKEN_SIZE && !(target & (NXRT_TOKEN_STRIDE - 1))) {
        uint32_t k = (uint32_t)((target - NXRT_TOKEN_BASE) / NXRT_TOKEN_STRIDE);
        if (!k) return rt_return_to_host;
        t_token = k; return token_tramp;
    }
    module_t *m = module_of((void *)(uintptr_t)target);
    if (m && target - (uint64_t)(uintptr_t)m->base < m->text_end && !(target & 3)) {
        miss_log(target);
        if (cpu) cpu->pc = target;   /* the interpreter's trampoline starts at cpu->pc */
        NxwGuestFn f = g_interp ? g_interp(target) : NULL;
        if (f) return f;
    }
    if (cpu) cpu->pc = target;
    char why[160]; snprintf(why, sizeof why, "indirect branch to %s%s%#llx with no translated function%s", m ? m->name : "", m ? "+" : "",
                            (unsigned long long)(m ? target - (uint64_t)(uintptr_t)m->base : target), g_interp ? "" : " (no interpreter attached)");
    LOG("[RED] %s", why);
    crash_report(why, NULL);
    ExitProcess(66);
}

void rt_svc(NxwGuestCpu *cpu, uint32_t imm) {
    thr_svc_enter((uint64_t *)cpu, cpu->pc);
    svc_dispatch((int)imm, (uint64_t *)cpu, cpu->pc);
    thr_svc_leave();
}

uint64_t rt_mrs(NxwGuestCpu *cpu, uint32_t sr) {
    switch (sr) {
    case NXRT_SR_CNTPCT_EL0: case NXRT_SR_CNTVCT_EL0: guest_clock_read_wait(); return guest_ticks();
    case NXRT_SR_CNTFRQ_EL0: return 31250000;   /* nothing reads it (research/05); answer the SDK's own constant */
    case NXRT_SR_TPIDR_EL0: return cpu->tpidr_el0;
    case NXRT_SR_TPIDRRO_EL0: return cpu->tpidrro_el0;
    case NXRT_SR_NZCV: return cpu->nzcv;
    case NXRT_SR_FPCR: return cpu->fpcr;
    case NXRT_SR_FPSR: return cpu->fpsr;
    case NXRT_SR_CTR_EL0: return 0x8444c004;    /* DminLine 4 (64-byte lines, nxnative's constant); the other fields as Cortex-A57's */
    case NXRT_SR_DCZID_EL0: return 4;           /* DC ZVA allowed, 64-byte blocks */
    case NXRT_SR_MIDR_EL1: return 0x411fd071;   /* Cortex-A57 r1p1 */
    case NXRT_SR_MPIDR_EL1: return 0x80000000;
    default: {
        static uint8_t seen[64]; uint32_t h = (sr * 2654435761u) >> 26;
        if (!seen[h]) { seen[h] = 1; LOG("[RED] mrs of system register %#x at %#llx: 0", sr, (unsigned long long)cpu->pc); }
        return 0;
    }
    }
}
void rt_msr(NxwGuestCpu *cpu, uint32_t sr, uint64_t v) {
    switch (sr) {
    case NXRT_SR_TPIDR_EL0: cpu->tpidr_el0 = v; return;
    case NXRT_SR_NZCV: cpu->nzcv = v & 0xf0000000u; return;
    case NXRT_SR_FPSR: cpu->fpsr = v; return;
    case NXRT_SR_FPCR:   /* research/05: never written in play. The lifter's exact FP helpers read cpu->fpcr; MXCSR is left at Windows' default (0x1F80) */
        if (v != cpu->fpcr) { static _Atomic int once; if (!atomic_exchange(&once, 1)) LOG("[RED] msr fpcr %#llx at %#llx (only 0 is modelled so far)", (unsigned long long)v, (unsigned long long)cpu->pc); }
        cpu->fpcr = v; return;
    default: {
        static uint8_t seen[64]; uint32_t h = (sr * 2654435761u) >> 26;
        if (!seen[h]) { seen[h] = 1; LOG("[RED] msr of system register %#x = %#llx at %#llx: ignored", sr, (unsigned long long)v, (unsigned long long)cpu->pc); }
    }
    }
}

void rt_yield(NxwGuestCpu *cpu, uint32_t kind, uint64_t site) {
    (void)kind;
    thr_safepoint();
    dm_spin_wait(site ? site : cpu->pc);
}

static const char *trap_name(uint32_t why) {
    switch (why) {
    case NXRT_TRAP_UDF: return "udf"; case NXRT_TRAP_BRK: return "brk"; case NXRT_TRAP_HLT: return "hlt";
    case NXRT_TRAP_UNREACHABLE: return "unreachable code"; case NXRT_TRAP_UNDECODABLE: return "undecodable instruction";
    case NXRT_TRAP_JUMP_TABLE: return "jump-table index outside the decoded table";
    default: return "trap";
    }
}
void rt_trap(NxwGuestCpu *cpu, uint64_t pc, uint32_t why) {
    if (cpu) cpu->pc = pc;
    module_t *m = module_of((void *)(uintptr_t)pc);
    char msg[160]; snprintf(msg, sizeof msg, "%s at %s+%#llx", trap_name(why), m ? m->name : "?", (unsigned long long)(m ? pc - (uint64_t)(uintptr_t)m->base : pc));
    LOG("[RED] %s", msg);
    crash_report(msg, NULL);
    ExitProcess(67);
}
void rt_nonlocal(NxwGuestCpu *cpu) {
    /* the lifter's context functions arrive with cpu->pc = the function itself (sdk's DispatchFiberContext, UserExceptionHandler, longjmp, jumpto) */
    char fn[160]; nxrt_symbolize(cpu->pc, fn, sizeof fn);
    char msg[320]; snprintf(msg, sizeof msg, "non-local transfer: %s called from %#llx (unwind pc %#llx sp %#llx; fiber switches, C++ exception resume and "
                            "longjmp are not supported yet, DESIGN D5)", fn, (unsigned long long)cpu->x[30],
                            (unsigned long long)cpu->unwind_pc, (unsigned long long)cpu->unwind_sp);
    LOG("[RED] %s", msg);
    crash_report(msg, NULL);
    ExitProcess(68);
}

/* ---- The interpreter (src/interp): misses inside a module's text run there until the next build compiles them ---- */
void nxw_interp_trampoline(NxwGuestCpu *cpu);   /* interprets from cpu->pc, which rt_miss sets */
static NxwGuestFn interp_entry(uint64_t va) { (void)va; return nxw_interp_trampoline; }
void nxrt_interp_attach(void) { if (!g_interp && !getenv("NXRT_NO_INTERP")) g_interp = interp_entry; }

/* ---- Function-entry trace (NXRT_TRACE; the hook needs objects lifted with entry tracing, nxlift's default) ----
   NXRT_TRACE=calls:N   the first N function entries of any thread, with x0-x2 and the caller
   NXRT_TRACE=first:N   the first execution of each function, the first N of them, numbered as research/05's executed lists are
   Both: NXRT_TRACE=calls:N,first:M. Written to <log>.trace.txt, a line per entry; the hook turns itself off when both counts run out. */
static FILE *g_trace;
static _Atomic int64_t g_trace_calls, g_trace_first;
static _Atomic uint64_t g_trace_seq;
static _Atomic uint8_t *g_seen;   /* one bit per code-table slot: functions entered so far */
static SRWLOCK g_trace_lk = SRWLOCK_INIT;

/* ---- Watched functions (NXRT_WATCH=main+0x98c7f0,main+0x376c4,...): entries counted per caller, the changes written once a second to <log>.watch.txt
   with the time, so a run can tell which callers ran during one action. Uses the same entry hook. NXRT_WATCH_PEEK=<main+off>:<off2>:<len> adds
   len bytes at *(main+off)+off2 to each second's line (a game state to read the counts against). NXRT_WATCH_ALL=1 also counts every function's entries;
   a file <log>.snap holding a name makes the next quarter second write those counts to <log>.snap-<name>.txt (two snapshots around an action, diffed,
   are what ran during it). A file <log>.eval with lines "<fn> <first> <last>" makes the next watched entry on a game thread call each fn with x0 = first..last
   and write the results to <log>.eval.txt (the game's own getters, read at run time) ---- */
#define WATCH_MAX 64
#define WATCH_SLOTS 8192
static uint64_t g_watch_va[WATCH_MAX]; static int g_nwatch;
static uint8_t *g_watch_bits;
typedef struct { _Atomic uint64_t va, lr, n; uint64_t shown; } watch_ent;
static watch_ent *g_watch_tab;
static SRWLOCK g_watch_lk = SRWLOCK_INIT;
static FILE *g_watch_f;
static uint64_t g_peek_slot, g_peek_off; static int g_peek_len;
static _Atomic uint32_t *g_cnt;   /* NXRT_WATCH_ALL: entries per code-table slot */
static _Atomic int g_eval;         /* an eval request is waiting */

static uint64_t watch_parse(const char *s, char **end) {   /* "main+0x…" or a plain address */
    for (int i = 0; i < g_nmod; i++) {
        size_t k = strlen(g_mod[i].name);
        if (!strncmp(s, g_mod[i].name, k) && s[k] == '+') return (uint64_t)(uintptr_t)g_mod[i].base + strtoull(s + k + 1, end, 0);
    }
    return strtoull(s, end, 0);
}
static void watch_count(uint64_t va, uint64_t lr) {
    uint64_t h = (va * 0x9E3779B97F4A7C15ull ^ lr * 0xC2B2AE3D27D4EB4Full) >> 51;   /* 13 bits */
    for (int p = 0; p < WATCH_SLOTS; p++) {
        watch_ent *e = &g_watch_tab[(h + p) & (WATCH_SLOTS - 1)];
        uint64_t v = atomic_load_explicit(&e->va, memory_order_acquire);
        if (!v) {
            AcquireSRWLockExclusive(&g_watch_lk);
            if (!atomic_load(&e->va)) { atomic_store(&e->lr, lr); atomic_store_explicit(&e->va, va, memory_order_release); }
            ReleaseSRWLockExclusive(&g_watch_lk);
            v = atomic_load(&e->va);
        }
        if (v == va && atomic_load(&e->lr) == lr) { atomic_fetch_add_explicit(&e->n, 1, memory_order_relaxed); return; }
    }
}
static void watch_snap(void) {
    char p[MAX_PATH + 16], name[64] = "";
    snprintf(p, sizeof p, "%s.snap", nxrt_log_path());
    FILE *t = fopen(p, "r");
    if (!t) return;
    if (!fgets(name, sizeof name, t)) name[0] = 0;
    fclose(t); DeleteFileA(p);
    name[strcspn(name, "\r\n /\\:")] = 0;
    snprintf(p, sizeof p, "%s.snap-%s.txt", nxrt_log_path(), name[0] ? name : "x");
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "# t=%llu\n", (unsigned long long)((now_ns() - g_t0_ns) / 1000000));
    for (uint64_t i = 0; i < rt_code_words; i++) {
        uint32_t n = atomic_load_explicit(&g_cnt[i], memory_order_relaxed);
        if (!n) continue;
        uint64_t va = rt_code_base + 4 * i; module_t *m = module_of((void *)(uintptr_t)va);
        fprintf(f, "%s+%#llx %u\n", m ? m->name : "?", (unsigned long long)(m ? va - (uint64_t)(uintptr_t)m->base : va), n);
    }
    fclose(f);
}
static void watch_eval(void) {   /* on a game thread, at a function's entry (its state is in its own frame, so x0-x18 are free here) */
    if (!atomic_exchange(&g_eval, 0)) return;
    char p[MAX_PATH + 16], line[160]; snprintf(p, sizeof p, "%s.eval", nxrt_log_path());
    FILE *t = fopen(p, "r");
    if (!t) return;
    snprintf(p, sizeof p, "%s.eval.txt", nxrt_log_path());
    FILE *f = fopen(p, "a");
    while (f && fgets(line, sizeof line, t)) {
        char *e; uint64_t fn = watch_parse(line, &e); long lo = strtol(e, &e, 0), hi = strtol(e, &e, 0);
        if (e == line || !fn) continue;
        fprintf(f, "%s", line);
        for (long i = lo; i <= hi && i - lo < 256; i++) { uint64_t a = (uint64_t)i; fprintf(f, "  %ld -> %llu\n", i, (unsigned long long)nxrt_guest_call(fn, 1, &a)); }
    }
    fclose(t);
    if (f) fclose(f);
    snprintf(p, sizeof p, "%s.eval", nxrt_log_path()); DeleteFileA(p);
}
static DWORD WINAPI watch_thread(void *u) {
    (void)u;
    for (unsigned tick = 1;; tick++) {
        Sleep(250);
        { char p[MAX_PATH + 16]; snprintf(p, sizeof p, "%s.eval", nxrt_log_path()); if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) atomic_store(&g_eval, 1); }
        if (g_cnt) watch_snap();
        if (tick % 4) continue;
        char peek[80] = "";
        if (g_peek_len) {
            uint64_t obj = *(volatile uint64_t *)(uintptr_t)g_peek_slot;   /* a slot in main's data: always mapped */
            MEMORY_BASIC_INFORMATION m;
            if (obj && VirtualQuery((void *)(uintptr_t)(obj + g_peek_off), &m, sizeof m) && m.State == MEM_COMMIT && (m.Protect & (PAGE_READWRITE | PAGE_READONLY))) {
                int o = snprintf(peek, sizeof peek, " peek=");
                for (int i = 0; i < g_peek_len && o < (int)sizeof peek - 3; i++) o += snprintf(peek + o, sizeof peek - o, "%02x", ((volatile uint8_t *)(uintptr_t)(obj + g_peek_off))[i]);
            }
        }
        int head = 0; uint64_t ms = (now_ns() - g_t0_ns) / 1000000;
        for (int i = 0; i < WATCH_SLOTS; i++) {
            watch_ent *e = &g_watch_tab[i];
            uint64_t va = atomic_load(&e->va), n = atomic_load(&e->n);
            if (!va || n == e->shown) continue;
            if (!head++) fprintf(g_watch_f, "t=%llu%s\n", (unsigned long long)ms, peek);
            char a[160], b[160]; nxrt_symbolize(va, a, sizeof a); nxrt_symbolize(atomic_load(&e->lr), b, sizeof b);
            fprintf(g_watch_f, "  %s <- %s +%llu (%llu)\n", a, b, (unsigned long long)(n - e->shown), (unsigned long long)n);
            e->shown = n;
        }
        if (!head && peek[0]) fprintf(g_watch_f, "t=%llu%s\n", (unsigned long long)ms, peek);
        fflush(g_watch_f);
    }
    return 0;
}
static void nxrt_watch_init(void) {
    const char *e = getenv("NXRT_WATCH"), *all = getenv("NXRT_WATCH_ALL");
    if (!(e && *e) && !(all && *all == '1')) return;
    if (!g_table) return;
    for (const char *s = e ? e : ""; *s && g_nwatch < WATCH_MAX;) {
        char *end; uint64_t va = watch_parse(s, &end);
        if (end == s) break;
        if (va >= rt_code_base && (va - rt_code_base) >> 2 < rt_code_words) g_watch_va[g_nwatch++] = va;
        s = *end == ',' ? end + 1 : end;
    }
    const char *pk = getenv("NXRT_WATCH_PEEK");
    if (pk && *pk) {
        char *end; g_peek_slot = watch_parse(pk, &end);
        if (*end == ':') { g_peek_off = strtoull(end + 1, &end, 0); if (*end == ':') g_peek_len = (int)strtol(end + 1, NULL, 0); }
        if (g_peek_len > 32) g_peek_len = 32;
    }
    g_watch_bits = VirtualAlloc(NULL, rt_code_words / 8 + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g_watch_tab = VirtualAlloc(NULL, sizeof *g_watch_tab * WATCH_SLOTS, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    char p[MAX_PATH + 16]; snprintf(p, sizeof p, "%s.watch.txt", nxrt_log_path());
    g_watch_f = fopen(p, "w");
    if (all && *all == '1') g_cnt = VirtualAlloc(NULL, rt_code_words * 4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_watch_bits || !g_watch_tab || !g_watch_f || (all && *all == '1' && !g_cnt)) { LOG("[RED] NXRT_WATCH: cannot set up (%s)", p); g_nwatch = 0; g_cnt = NULL; if (g_watch_f) fclose(g_watch_f); g_watch_f = NULL; return; }
    for (int i = 0; i < g_nwatch; i++) { uint64_t k = (g_watch_va[i] - rt_code_base) >> 2; g_watch_bits[k >> 3] |= (uint8_t)(1u << (k & 7)); }
    LOG("NXRT_WATCH: %d functions, callers counted into %s%s", g_nwatch, p, g_cnt ? "; every function's entries counted (snapshots on <log>.snap)" : "");
    CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
    rt_trace_on = 1;
}

void nxrt_trace_init(void) {
    nxrt_watch_init();
    const char *e = getenv("NXRT_TRACE");
    if (!e || !*e || !g_table) return;
    const char *c = strstr(e, "calls:"), *f = strstr(e, "first:");
    if (c) g_trace_calls = strtoll(c + 6, NULL, 0);
    if (f) g_trace_first = strtoll(f + 6, NULL, 0);
    if (!c && !f) g_trace_calls = strtoll(e, NULL, 0);
    if (g_trace_first > 0) g_seen = VirtualAlloc(NULL, rt_code_words / 8 + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    char p[MAX_PATH + 16]; snprintf(p, sizeof p, "%s.trace.txt", nxrt_log_path());
    g_trace = fopen(p, "w");
    if (!g_trace) { LOG("[RED] NXRT_TRACE: cannot write %s", p); return; }
    LOG("NXRT_TRACE: %lld calls, %lld first entries into %s", (long long)g_trace_calls, (long long)g_trace_first, p);
    rt_trace_on = 1;
}
void rt_trace_entry(NxwGuestCpu *cpu, uint64_t va) {
    if (g_watch_f) {
        uint64_t k = (va - rt_code_base) >> 2;
        if (va >= rt_code_base && k < rt_code_words) {
            if (g_cnt) atomic_fetch_add_explicit(&g_cnt[k], 1, memory_order_relaxed);
            if (g_watch_bits[k >> 3] & (1u << (k & 7))) { watch_count(va, cpu->x[30]); if (atomic_load_explicit(&g_eval, memory_order_relaxed)) watch_eval(); }
        }
        if (!g_trace) return;
    }
    int first = 0;
    if (g_seen && va >= rt_code_base && (va - rt_code_base) >> 2 < rt_code_words) {
        uint64_t i = (va - rt_code_base) >> 2; uint8_t bit = (uint8_t)(1u << (i & 7));
        first = !(atomic_fetch_or(&g_seen[i >> 3], bit) & bit);
    }
    int64_t c = atomic_load(&g_trace_calls) > 0 ? atomic_fetch_sub(&g_trace_calls, 1) : 0;
    int64_t f = first && atomic_load(&g_trace_first) > 0 ? atomic_fetch_sub(&g_trace_first, 1) : 0;
    if (c <= 0 && f <= 0) {
        if (atomic_load(&g_trace_calls) <= 0 && atomic_load(&g_trace_first) <= 0 && !g_watch_f) rt_trace_on = 0;
        return;
    }
    char nm[160], lr[160];
    nxrt_symbolize(va, nm, sizeof nm); nxrt_symbolize(cpu->x[30], lr, sizeof lr);
    AcquireSRWLockExclusive(&g_trace_lk);
    fprintf(g_trace, "%c%llu t%lu %s x0=%llx x1=%llx x2=%llx sp=%llx lr=%s ms=%llu\n", f > 0 ? 'F' : 'C', (unsigned long long)g_trace_seq++,
            GetCurrentThreadId(), nm, (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1], (unsigned long long)cpu->x[2],
            (unsigned long long)cpu->sp, lr, (unsigned long long)((now_ns() - g_t0_ns) / 1000000));
    fflush(g_trace);
    ReleaseSRWLockExclusive(&g_trace_lk);
}
