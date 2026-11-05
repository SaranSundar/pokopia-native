// Linux stand-in for prof.c's sampling profiler (docs/LINUX-PLAN.md): Linux has perf, which samples game.so by symbol already
// (`perf record -g -p <pid>`); the in-process sampler is Windows only.
#include "dm.h"

void nxrt_prof_start(double secs) { LOG("profiler: not built on Linux (asked for %.1f s): use perf record -p %ld", secs, (long)getpid()); }
void nxrt_prof_env(void) {
    const char *e = getenv("NXRT_PROF");
    if (e && *e) nxrt_prof_start(atof(strchr(e, ',') ? strchr(e, ',') + 1 : e));
}
