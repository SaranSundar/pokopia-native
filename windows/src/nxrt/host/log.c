/* The run's log (research/01 lesson 15, DESIGN D14): one file per run, flushed every line, with a banner that triages itself:
   the GPU (none until the Vulkan renderer), the CPU and its x86-64-v3 support, the Windows build, the game's build ID and the reserved ranges. */
#include "dm.h"
#include "nxrt/runtime.h"

#include <stdarg.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <link.h>
#include <sys/types.h>
#endif

static FILE *g_log;
static char g_log_path[MAX_PATH];
static int g_quiet;
static SRWLOCK g_log_lock = SRWLOCK_INIT;

const char *nxrt_log_path(void) { return g_log_path; }

static void log_line(const char *prefix, const char *fmt, va_list ap) {
    char buf[4096];
    int n = snprintf(buf, sizeof buf, "[%9.3f] %s", (now_ns() - g_t0_ns) / 1e9, prefix);
    if (n < 0) n = 0;
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    size_t len = (size_t)n + (m < 0 ? 0 : (size_t)m);
    if (len > sizeof buf - 2) len = sizeof buf - 2;
    buf[len++] = '\n'; buf[len] = 0;
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log) { fwrite(buf, 1, len, g_log); fflush(g_log); }
    if (!g_quiet || !g_log) { fwrite(buf, 1, len, stderr); fflush(stderr); }
    ReleaseSRWLockExclusive(&g_log_lock);
}
void nxrt_logf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); log_line("", fmt, ap); va_end(ap); }

void crash_report(const char *why, const void *ctx);   /* crash.c */
_Noreturn void nxrt_die(const char *fmt, ...) {
    char msg[1024]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    nxrt_logf("FATAL %s", msg);
    crash_report(msg, NULL);
#ifdef _WIN32
    ExitProcess(3);
#else
    _exit(3);
#endif
}

/* The folder: o->log_dir, NXRT_LOG_DIR, .\_local\nxrt when it exists, else the current folder */
int nxrt_log_open(const nxrt_options *o) {
    if (g_log) return 0;
    if (!g_t0_ns) g_t0_ns = now_ns();
    g_quiet = o && o->quiet;
    const char *dir = o && o->log_dir ? o->log_dir : getenv("NXRT_LOG_DIR");
#ifdef _WIN32
    struct _stat st;
    if (!dir) dir = !_stat("_local\\nxrt", &st) && (st.st_mode & _S_IFDIR) ? "_local\\nxrt" : ".";
    _mkdir(dir);
    SYSTEMTIME t; GetLocalTime(&t);
    snprintf(g_log_path, sizeof g_log_path, "%s\\nxrt-%04u%02u%02u-%02u%02u%02u-%lu.log", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
#else
    struct stat st;
    if (!dir) dir = !stat("_local/nxrt", &st) && S_ISDIR(st.st_mode) ? "_local/nxrt" : ".";
    mkdir(dir, 0755);
    time_t now = time(NULL); struct tm t; localtime_r(&now, &t);
    snprintf(g_log_path, sizeof g_log_path, "%s/nxrt-%04d%02d%02d-%02d%02d%02d-%ld.log", dir, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (long)getpid());
#endif
    g_log = fopen(g_log_path, "w");
    if (!g_log) { fprintf(stderr, "[nxrt] cannot write the log %s\n", g_log_path); return -1; }
    return 0;
}

void nxrt_log_banner(void) {
    char cpu[64], win[128];
    plat_cpu_brand(cpu, sizeof cpu); plat_windows_version(win, sizeof win);
    LOG("nxrt v0 (runtime ABI %d, built %s %s), log %s", NXW_ABI_VERSION, __DATE__, __TIME__, g_log_path);
    LOG("GPU: the graphics backend's choice (--gfx vulkan: the [vk] lines below name the GPU and driver; --gfx null: none)");
    LOG("CPU: %s, %u logical processors, %.1f GB RAM, x86-64-v3 (AVX2+FMA3) %s", cpu, plat_cpu_count(), plat_ram_bytes() / 1073741824.0,
        plat_cpu_x86_64_v3() ? "yes" : "NO: below the minimum CPU (DESIGN §9.1)");
    LOG("OS: %s", win);
    LOG("game profile %s (%s), app pool %.2f GB", DM_GAME_ID, DM_GAME_NAME, DM_GAME_APP_POOL / 1073741824.0);
    for (int i = 0; i < NXRT_AS_COUNT; i++) {
        const nxrt_range *r = nxrt_as_range(i);
        if (r->reserved) LOG("reserved %#llx-%#llx %s (%.2f GB)", (unsigned long long)r->base, (unsigned long long)(r->base + r->size), r->name, r->size / 1073741824.0);
        else LOG("[RED] NOT reserved %#llx-%#llx %s: %s %#llx+%#llx (state %#x type %#x)%s%s", (unsigned long long)r->base, (unsigned long long)(r->base + r->size), r->name,
                 r->conflict_size ? "taken by" : "VirtualAlloc2 unavailable or refused;", (unsigned long long)r->conflict_base, (unsigned long long)r->conflict_size,
                 r->conflict_state, r->conflict_type, *r->conflict_owner ? " " : "", r->conflict_owner);
    }
    const nxrt_layout *l = nxrt_as_layout();
    LOG("guest block: alias %#llx+%#llx, stack %#llx+%#llx, heap %#llx+%#llx", (unsigned long long)l->alias, (unsigned long long)l->alias_size,
        (unsigned long long)l->stack, (unsigned long long)l->stack_size, (unsigned long long)l->heap, (unsigned long long)l->heap_size);
#ifdef _WIN32
    uint8_t *img = (uint8_t *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(img + ((IMAGE_DOS_HEADER *)img)->e_lfanew);
    LOG("executable at %p, high-entropy ASLR %s", (void *)img, nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA ? "ON (link with /HIGHENTROPYVA:NO)" : "off");
#else
    struct link_map *lm = _r_debug.r_map;
    FILE *mm = fopen("/proc/sys/vm/max_map_count", "r"); long maps = 0;
    if (mm) { if (fscanf(mm, "%ld", &maps) != 1) maps = 0; fclose(mm); }
    LOG("executable at %#llx, vm.max_map_count %ld%s", lm ? (unsigned long long)lm->l_addr : 0ull, maps,
        maps && maps < 262144 ? " (low: the write protection of GPU pools may run out of mappings; see docs/LINUX-PLAN.md)" : "");
#endif
}
