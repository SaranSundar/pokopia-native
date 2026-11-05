/* Linux build only (docs/LINUX-PLAN.md): the Win32 subset nxrt's own code uses, so the Windows-written runtime compiles unchanged.
   Locks, condition variables, events, semaphores, threads, sleeps, the performance counter, the last error, and virtual memory
   (VirtualAlloc / VirtualFree / VirtualProtect over mmap and mprotect). Implemented in plat/win32_linux.c over pthreads and futexes.
   Anything else (VirtualQuery, structured exceptions, wide-character paths, processes, DLLs) is deliberately missing: each use is ported
   where it is, so the compiler finds every one. This folder is on the include path of the Linux build only. */
#ifndef NXRT_LINUX_WINDOWS_H
#define NXRT_LINUX_WINDOWS_H
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include "plat.h"   /* plat_guard (the Linux stand-in for __try), the clock, threads */

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE, *HMODULE, *LPVOID, *PVOID;
typedef const void *LPCVOID;
typedef uint32_t DWORD, UINT, ULONG;
typedef int32_t LONG;
typedef int BOOL;
typedef uint64_t ULONG64, DWORD64, ULONGLONG;
typedef int64_t LONGLONG;
typedef size_t SIZE_T;
typedef unsigned char BYTE, UCHAR, *PUCHAR;
typedef union { struct { uint32_t LowPart; int32_t HighPart; }; int64_t QuadPart; } LARGE_INTEGER;
#define WINAPI
#define CALLBACK
#define __stdcall
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define ERROR_TIMEOUT 1460u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_PARAMETER 87u

/* The last error is the errno of the failing call (or one of the ERROR_* above), per thread; unsigned long, as nxrt's logs print it with %lu */
unsigned long GetLastError(void);
void SetLastError(DWORD e);

/* ---- SRW locks: pthread read-write locks (glibc prefers readers, so a thread may take a shared hold it already has) ---- */
typedef struct { pthread_rwlock_t l; } SRWLOCK;
#define SRWLOCK_INIT {PTHREAD_RWLOCK_INITIALIZER}
static inline void InitializeSRWLock(SRWLOCK *s) { pthread_rwlock_init(&s->l, NULL); }
static inline void AcquireSRWLockExclusive(SRWLOCK *s) { pthread_rwlock_wrlock(&s->l); }
static inline void AcquireSRWLockShared(SRWLOCK *s) { pthread_rwlock_rdlock(&s->l); }
static inline void ReleaseSRWLockExclusive(SRWLOCK *s) { pthread_rwlock_unlock(&s->l); }
static inline void ReleaseSRWLockShared(SRWLOCK *s) { pthread_rwlock_unlock(&s->l); }
static inline BOOL TryAcquireSRWLockExclusive(SRWLOCK *s) { return pthread_rwlock_trywrlock(&s->l) == 0; }
static inline BOOL TryAcquireSRWLockShared(SRWLOCK *s) { return pthread_rwlock_tryrdlock(&s->l) == 0; }

/* ---- Condition variables: a futex on a wake counter, usable with an SRW lock held either way ---- */
typedef struct { uint32_t seq; } CONDITION_VARIABLE;   /* read and written with __atomic builtins (C and C++) */
#define CONDITION_VARIABLE_INIT {0}
#define CONDITION_VARIABLE_LOCKMODE_SHARED 1u
static inline void InitializeConditionVariable(CONDITION_VARIABLE *c) { c->seq = 0; }
BOOL SleepConditionVariableSRW(CONDITION_VARIABLE *c, SRWLOCK *l, DWORD ms, ULONG flags);   /* FALSE + ERROR_TIMEOUT on a timeout */
void WakeConditionVariable(CONDITION_VARIABLE *c);
void WakeAllConditionVariable(CONDITION_VARIABLE *c);

/* ---- One-time initialisation: the callback runs once; a FALSE return lets the next caller try again ---- */
typedef struct { pthread_mutex_t mu; int done; } INIT_ONCE, *PINIT_ONCE;
#define INIT_ONCE_STATIC_INIT {PTHREAD_MUTEX_INITIALIZER, 0}
typedef BOOL (*PINIT_ONCE_FN)(PINIT_ONCE o, PVOID param, PVOID *ctx);
static inline BOOL InitOnceExecuteOnce(PINIT_ONCE o, PINIT_ONCE_FN fn, PVOID param, PVOID *ctx) {
    if (__atomic_load_n(&o->done, __ATOMIC_ACQUIRE)) return TRUE;
    pthread_mutex_lock(&o->mu);
    BOOL ok = TRUE;
    if (!o->done && (ok = fn(o, param, ctx))) __atomic_store_n(&o->done, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&o->mu);
    return ok;
}

/* ---- Kernel objects: events, semaphores, threads (a HANDLE points to one; CloseHandle frees it, detaching a thread) ---- */
HANDLE CreateEventW(void *sa, BOOL manual_reset, BOOL initial, const void *name);
#define CreateEventA CreateEventW
#define CreateEvent CreateEventW
BOOL SetEvent(HANDLE h);
BOOL ResetEvent(HANDLE h);
HANDLE CreateSemaphoreW(void *sa, LONG initial, LONG max, const void *name);
#define CreateSemaphoreA CreateSemaphoreW
#define CreateSemaphore CreateSemaphoreW
BOOL ReleaseSemaphore(HANDLE h, LONG n, LONG *previous);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
BOOL CloseHandle(HANDLE h);

typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
HANDLE CreateThread(void *sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, DWORD *tid);
uintptr_t _beginthreadex(void *sa, unsigned stack, unsigned (*fn)(void *), void *arg, unsigned flags, unsigned *tid);
HANDLE GetCurrentThread(void);   /* a pseudo handle */
DWORD GetCurrentThreadId(void);
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
/* Lowering works (a nice offset); raising is ignored, as an unprivileged Linux process cannot raise a thread's priority */
BOOL SetThreadPriority(HANDLE h, int prio);

void Sleep(DWORD ms);
BOOL SwitchToThread(void);
BOOL QueryPerformanceCounter(LARGE_INTEGER *t);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f);

/* ---- Virtual memory over mmap: a reservation is PROT_NONE and MAP_NORESERVE; committing makes the pages accessible (demand-zero);
   MEM_RELEASE needs the reservation's base and frees all of it, as on Windows (the layer remembers each reservation's size) ---- */
#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_DECOMMIT 0x00004000u
#define MEM_RELEASE 0x00008000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_GUARD 0x100u
int win32_prot(DWORD page_prot);   /* PAGE_* to PROT_* */
LPVOID VirtualAlloc(LPVOID addr, SIZE_T n, DWORD type, DWORD prot);
BOOL VirtualFree(LPVOID addr, SIZE_T n, DWORD type);
BOOL VirtualProtect(LPVOID addr, SIZE_T n, DWORD prot, DWORD *old);
/* x86-64 keeps instruction fetches coherent with stores: nothing to flush after code is patched */
static inline BOOL FlushInstructionCache(HANDLE p, const void *addr, SIZE_T n) { (void)p; (void)addr; (void)n; return TRUE; }

/* ---- Processes, waitable timers ---- */
#define ExitProcess(code) (fflush(NULL), _exit(code))   /* every thread ends at once, as on Windows: no atexit handlers */
#define TerminateProcess(h, code) ((void)(h), _exit(code))   /* nxrt only ever terminates itself */
HANDLE GetCurrentProcess(void);
DWORD GetCurrentProcessId(void);
DWORD GetModuleFileNameA(HMODULE m, char *out, DWORD n);   /* m = NULL: this executable (/proc/self/exe) */
/* Child processes, for the tests that start themselves again: the command line is split as Windows' C runtime splits it (quotes, spaces) */
typedef struct { DWORD cb; DWORD dwFlags; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOA;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;
#define CREATE_SUSPENDED 0x4u   /* not supported: CreateProcessA fails with it */
BOOL CreateProcessA(const char *app, char *cmdline, void *psa, void *tsa, BOOL inherit, DWORD flags, void *env, const char *cwd,
                    STARTUPINFOA *si, PROCESS_INFORMATION *pi);
BOOL GetExitCodeProcess(HANDLE h, DWORD *code);   /* the exit status, or 128 + the signal */
#define ZeroMemory(p, n) memset((p), 0, (n))
#ifndef _MSC_VER
#define __declspec(x) __attribute__((visibility("default")))   /* dllexport / dllimport: a symbol the other side binds at load */
#endif
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2u
#define TIMER_ALL_ACCESS 0x1F0003u
HANDLE CreateWaitableTimerExW(void *sa, const void *name, DWORD flags, DWORD access);
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, void *fn, void *arg, BOOL resume);   /* due < 0: relative, 100 ns units */
static inline int _putenv_s(const char *n, const char *v) { return setenv(n, v, 1); }
void SetThreadDescription(HANDLE h, const wchar_t *name);   /* the calling thread only, as nxrt uses it */

/* ---- Paths and files. wchar_t is UTF-32 here; nxrt's own Windows paths use backslashes, which become slashes where a wide path turns into a
   Linux one (a backslash is never part of the names nxrt builds) ---- */
#define MAX_PATH 260
#define CP_UTF8 65001u
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_SHARE_READ 1u
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define MOVEFILE_REPLACE_EXISTING 1u
#define MOVEFILE_WRITE_THROUGH 8u
#define ERROR_ALREADY_EXISTS 183u
int MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, wchar_t *w, int wn);
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *w, int n, char *s, int sn, const char *def, BOOL *used);
int win32_wpath(const wchar_t *w, char *out, size_t n);   /* UTF-8, backslashes as slashes; 0 = ok */
FILE *_wfopen(const wchar_t *path, const wchar_t *mode);
#define _snwprintf swprintf
BOOL CreateDirectoryW(const wchar_t *path, void *sa);
BOOL CreateDirectoryA(const char *path, void *sa);
DWORD GetFileAttributesW(const wchar_t *path);
DWORD GetFileAttributesA(const char *path);
BOOL DeleteFileW(const wchar_t *path);
BOOL DeleteFileA(const char *path);
BOOL MoveFileExW(const wchar_t *from, const wchar_t *to, DWORD flags);
BOOL MoveFileExA(const char *from, const char *to, DWORD flags);
HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share, void *sa, DWORD disposition, DWORD flags, HANDLE tmpl);
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *sa, DWORD disposition, DWORD flags, HANDLE tmpl);
BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *written, void *ov);
BOOL FlushFileBuffers(HANDLE h);
/* The per-user folders: LocalAppData is $XDG_CACHE_HOME (~/.cache), RoamingAppData $XDG_DATA_HOME (~/.local/share) */
typedef int32_t HRESULT;
typedef int KNOWNFOLDERID;
#define S_OK 0
extern const KNOWNFOLDERID FOLDERID_LocalAppData, FOLDERID_RoamingAppData;
HRESULT SHGetKnownFolderPathC(const KNOWNFOLDERID *id, DWORD flags, HANDLE token, wchar_t **out);
#define CoTaskMemFree free

/* ---- Directory listing (the tests): "<folder>\*" patterns only ---- */
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;   /* 100 ns since 1601, as on Windows */
typedef struct { DWORD dwFileAttributes; FILETIME ftLastWriteTime; char cFileName[MAX_PATH]; } WIN32_FIND_DATAA;
typedef struct { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;
typedef enum { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;
BOOL GetFileAttributesExW(const wchar_t *path, GET_FILEEX_INFO_LEVELS level, void *out);
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd);
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd);
BOOL FindClose(HANDLE h);
LONG CompareFileTime(const FILETIME *a, const FILETIME *b);
BOOL RemoveDirectoryA(const char *path);
DWORD GetTempPathA(DWORD n, char *out);   /* $TMPDIR or /tmp, with the trailing slash */
static inline BOOL SetEnvironmentVariableA(const char *n, const char *v) { return (v ? setenv(n, v, 1) : unsetenv(n)) == 0; }

/* ---- Interlocked operations ---- */
typedef int64_t LONG64;
static inline LONG InterlockedIncrement(volatile LONG *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedDecrement(volatile LONG *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline LONG64 InterlockedIncrement64(volatile LONG64 *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline LONG64 InterlockedAdd64(volatile LONG64 *p, LONG64 v) { return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST); }

/* ---- The Microsoft C runtime's names the Windows-written files use, as the POSIX functions they stand for ---- */
#include <strings.h>
#include <time.h>
#define _strdup strdup
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define strtok_s strtok_r
#define _tzset tzset
#define _tzname tzname
#define _mkgmtime timegm
static inline void *_aligned_malloc(size_t n, size_t align) {
    void *p = NULL;
    return posix_memalign(&p, align < sizeof(void *) ? sizeof(void *) : align, n ? n : 1) ? NULL : p;
}
#define _aligned_free free
static inline int localtime_s(struct tm *out, const time_t *t) { return localtime_r(t, out) ? 0 : EINVAL; }
static inline int gmtime_s(struct tm *out, const time_t *t) { return gmtime_r(t, out) ? 0 : EINVAL; }
static inline int _putenv(const char *kv) {   /* "NAME=value"; "NAME=" removes it, as on Windows */
    const char *eq = strchr(kv, '=');
    if (!eq) return -1;
    char n[256]; size_t k = (size_t)(eq - kv) < sizeof n - 1 ? (size_t)(eq - kv) : sizeof n - 1;
    memcpy(n, kv, k); n[k] = 0;
    return eq[1] ? setenv(n, eq + 1, 1) : unsetenv(n);
}

#ifdef __cplusplus
}
static inline HRESULT SHGetKnownFolderPath(const KNOWNFOLDERID &id, DWORD f, HANDLE t, wchar_t **o) { return SHGetKnownFolderPathC(&id, f, t, o); }
#else
#define SHGetKnownFolderPath SHGetKnownFolderPathC
#endif
#endif
