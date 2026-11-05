/* The platform layer of nxrt: everything that talks to Win32 apart from the address space (host/addrspace.c) and the
   POSIX subset the ported nxnative files use (posix_win.h). Clock, sleep, safe reads, threads, system information. */
#ifndef NXRT_PLAT_H
#define NXRT_PLAT_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clock: QueryPerformanceCounter in nanoseconds (monotonic, invariant and coherent across cores on Windows 10+) */
uint64_t plat_now_ns(void);
uint64_t plat_qpc(void);
uint64_t plat_qpc_freq(void);
uint64_t plat_realtime_ns(void);   /* since 1970, GetSystemTimePreciseAsFileTime */

/* Sleep with sub-millisecond resolution (a high-resolution waitable timer per thread) and yield */
void plat_sleep_ns(uint64_t ns);
void plat_yield(void);
/* timeBeginPeriod(1) for the process, so condition-variable timeouts are millisecond-accurate */
void plat_timer_resolution(void);

/* Reads memory that may not be mapped: 0 when all n bytes were read, -1 otherwise (never faults) */
int plat_read(uint64_t addr, void *out, size_t n);

/* Threads */
typedef unsigned (*plat_thread_fn)(void *arg);
/* A detached thread with a stack reservation of stack_reserve bytes; returns the thread id, 0 on failure */
uint32_t plat_thread_start(plat_thread_fn fn, void *arg, size_t stack_reserve);
uint32_t plat_thread_id(void);
void plat_thread_name(const char *name);
/* Horizon priority (0 highest .. 63 lowest) to a Windows priority, gently (DESIGN D9: never above ABOVE_NORMAL) */
int plat_priority_for(int horizon_prio);
void plat_thread_set_priority(uint32_t tid, int horizon_prio);
double plat_thread_cpu_seconds(uint32_t tid);
_Noreturn void plat_thread_exit(void);

/* System information for the log banner */
void plat_cpu_brand(char *out, size_t n);
int plat_cpu_x86_64_v3(void);      /* AVX2 + FMA3 + BMI1/2 (DESIGN §9.1) */
unsigned plat_cpu_count(void);
void plat_windows_version(char *out, size_t n);
uint64_t plat_ram_bytes(void);

/* Entropy (BCryptGenRandom) */
void plat_random(void *out, size_t n);

#ifndef _WIN32
/* Linux (plat_linux.c, docs/LINUX-PLAN.md) */
void plat_thread_lower(int nice_offset);   /* the calling thread a few nice steps below the process, for good */
void *plat_exe_image(size_t *size);        /* this executable's load address and the extent of its segments (tests attach it as a code image) */

/* A recovery point around reads of memory that may be unmapped (Windows: __try / __except). After the fault hooks decline a fault, crash.c's
   handler pops the innermost guard and siglongjmps to it:
       plat_guard g; PLAT_GUARD_PUSH(&g);
       if (sigsetjmp(g.jb, 0)) return -1;      // faulted (the guard is popped already)
       ...;  PLAT_GUARD_POP(&g);  return 0; */
#include <setjmp.h>
typedef struct plat_guard { sigjmp_buf jb; struct plat_guard *prev; } plat_guard;
extern __thread plat_guard *plat_guard_top;
#define PLAT_GUARD_PUSH(g) ((g)->prev = plat_guard_top, plat_guard_top = (g))
#define PLAT_GUARD_POP(g) (plat_guard_top = (g)->prev)
#endif

#ifdef __cplusplus
}
#endif
#endif
