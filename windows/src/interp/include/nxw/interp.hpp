// The fallback interpreter (DESIGN D4): runs guest code with the same semantics the lifter inlines, until the guest function
// returns. Calls (BL, BLR) go through the D4 lookup, so translated code runs wherever it exists and a miss interprets the callee
// (rt_miss hands back nxw_interp_trampoline); RET returns to the host caller, exactly as a lifted function does.
// Uses the runtime's rt_* symbols (nxw/rt.h).
#pragma once
#include "nxw/guest_cpu.h"

#include <cstdint>

extern "C" {
// A NxwGuestFn: interprets from cpu->pc until the function returns. rt_miss returns it after setting cpu->pc.
void nxw_interp_trampoline(NxwGuestCpu* cpu);
// Interprets the function at entry until it returns
void nxw_interp_run(NxwGuestCpu* cpu, uint64_t entry);
// Site hooks (the runtime's interpreted functions with hooks, rt.c): called with x0-x30 right before an instruction in [lo, hi) runs; a register it
// writes takes effect. Null: none
typedef void (*NxwInterpSiteHook)(uint64_t pc, uint64_t* x);
void nxw_interp_set_site_hook(NxwInterpSiteHook hook, uint64_t lo, uint64_t hi);
}

namespace nxw::interp {

// Per-thread counters, for tests and the runtime's statistics
struct Stats {
    uint64_t instructions = 0, calls = 0, entries = 0;
};
Stats& stats();

}  // namespace nxw::interp
