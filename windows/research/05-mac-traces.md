# 05 — Mac traces: FPCR, the counter, exceptions and setjmp, coverage

Answers to DESIGN.md §8 open questions 1, 2, 3 and 5, measured on the rented Mac (Apple M4), where Pokopia 2.0.1's own arm64
code runs natively under nxnative with our Switch 2 patch. Every number below comes from the scripts in `research/mac-traces/`
(see **Files**). Build: `~/installer-test/build201-clean` (Pokopia 2.0.1, main `76a489f3bd3642fa…`, sdk `cc7da00d672399ef…`,
subsdk0 `9646a69cb1024ba9…`: the same modules as the census in 04), used read-only. Sources consulted: nxnative's host (MIT, ours)
and the game's own code; nothing external.

## Answers

| # | Question | Answer |
|---|---|---|
| 1 | FPCR in game threads | **Always 0**: round to nearest, FZ = 0, DN = 0, AHP = 0, FZ16 = 0, no trap enables. 20.6–21.2 M gate entries on 65 guest threads, 28 k sampled thread states, 56 k first executions and 150 k probe hits per session saw no other value. **No code sets it**: none of sdk's 5 `msr fpcr` and 7 `mrs fpcr` sites ran (nor the 15 FPSR sites). The value is the thread's initial FPCR: macOS's 0 here, the kernel's initial value on the Switch 2 (not observable on the Mac; the SDK's own `fesetenv(FE_DFL_ENV)` writes 0). |
| 2 | Counter frequency | The SDK assumes **31,250,000 Hz**: `nn::os::GetSystemTickFrequency()` is the constant `0x1dcd650` (sdk+0x148d80), `ConvertToTimeSpan` works in 32 ns ticks, and nothing reads `CNTFRQ_EL0`. nxnative's handler returns `guest_ns × 31,250,000 / 10⁹` (measured 31,249,864–31,249,987 ticks/s). Called from the host before the game ran, `GetSystemTickFrequency()` and `nnosGetSystemTickFrequency()` both return 31,250,000 and `ConvertToTimeSpan(31,250,000 ticks)` returns 1,000,000,000 ns. 7 of the 43 rewritten reads run, about 16,000 reads/s, 91 % of them in `nn::os::GetSystemTick()`. main calls `GetSystemTickFrequency()` about 870 times/s. |
| 3 | Exceptions, setjmp/longjmp | **None.** In three sessions (boot, title, save load, 90 s of menus and play): 0 `__cxa_throw`, 0 `__cxa_rethrow`, 0 `_Unwind_RaiseException`, 0 `_Unwind_Resume`, 0 personality calls, 0 `setjmp`, 0 `longjmp`. Statically, main calls setjmp and longjmp from one site each, both in one decoder of the network stack (gRPC region); sdk calls them from its PNG/JPEG codecs, `pthread_create`/`pthread_exit` and the TrueType interpreter. None of those callers ran. |
| 5 | Coverage | main ran **36,439 of 147,875** FDE starts (24.6 %; union of two routes 36,955), sdk 582 of 38,283, subsdk0 3. **Outside the FDEs, only PLT stubs ran**: every one of the 106,709 nonzero words outside FDEs carried a trap, and only 1,912 PLT-stub words (478 stubs) fired; 0 words of libcurl or of sdk's no-FDE helpers. All 96,000 logged br/blr targets (19,570 distinct site–target pairs over 17,300 sites) were FDE starts, labels in the branching function, PLT-to-host or host code: **0 entries into the middle of another function**. Sampling: 99.1 % of guest-text samples inside FDEs, 0.9 % in PLT stubs, 0 elsewhere. Target 0 is met, PLT stubs aside. |

## Sessions and method

**Session.** `tracedrive.py` (from `~/setup/menudrive.py`) boots a copy of the run-4 save with scripted input through `DM_CTL`:
- **boot**: 40 s to the title screen;
- **title**: 8 × A, 4 s apart, through the title and the save load (42 s);
- **menus**: bag, the Plus menu, the Minus screen (about 30 s);
- **play**: walking, running, the water gun, camera turns, interacting (about 60 s).

Each step's time goes to `phases.tsv`. All runs reached gameplay (screenshots checked).

| run | instruments | route | length |
|---|---|---|---|
| `pilot` | all of the below | a | 170 s |
| `fullb` | all of the below | b (other menus and moves) | 174 s |
| `light` | gates, probes, sampler (no coverage traps) | a | 170 s |

**Instruments.** One new file in the copy, `hle_trace.c`, plus three one-line hooks (`nxnative-trace.diff`). All of it is off unless
`DM_TRACE` is set.

| instrument | how | what it gives |
|---|---|---|
| gate hook | nxnative's gate (`entry.S`) already saves the guest's FPCR at +784 of its frame; `gate_dispatch` passes it on | FPCR at every SVC, counter read and spin wait, per guest thread; calls per gate |
| probes | `brk #0xf0e2` at a function entry or instruction; the displaced instruction runs from a slot near the module that branches back (`b`, `bl`, `adr` and `adrp` are emulated) | every call of setjmp, longjmp, the C++ ABI and unwinder entries, `GetSystemTickFrequency`, and every FPCR/FPSR access in sdk, with lr, the frame's return address, FPCR and one argument |
| coverage traps | one-shot `brk #0xf0e1` on all 188,041 FDE starts and on 95,040 nonzero text words outside FDEs (every one that is not a br/blr or a probe site), set before the game runs; the first execution logs the word and writes the original back through a writable alias of the code | exact sets of executed function entries and of executed code outside FDEs |
| br/blr targets | `brk #0xf0e3` on all 180,195 br/blr; the first 8 executions of each are emulated in the handler and logged, then the instruction goes back | indirect branch targets |
| sampler | a host thread suspends every thread, reads pc (and FPCR when the pc is in guest code), resumes it; 1 kHz requested, about 690 Hz per thread achieved | where guest time goes, FPCR per thread |

**Effect on the game.** Frame time p50 16.5–16.8 ms and p95 17.0–18.3 ms at every 300-frame report in all three runs. An
uninstrumented run of the same build (`~/setup/menudrive/m1`) shows the same. The coverage traps cost about 56,000 first-execution
signals plus 96,000 emulated branches per run, most of them during boot.

**Why not lldb.** lldb would take every one of the host's own Mach exceptions (write-protect dirty tracking, BRK probes) and stop
all threads at each breakpoint hit. `GetSystemTickFrequency` alone is called 870 times/s. The in-process traps cost a signal on one
thread instead.

## 1. FPCR

| source (run `fullb`; the others agree) | events | FPCR values seen |
|---|---:|---|
| gates: every svc, counter read and spin wait | 21,191,860 on 65 guest threads | 0 |
| first execution of each traced word | 55,734 | 0 |
| sampler, threads stopped in guest code | 27,662 on 38 threads | 0 |
| probe hits | 155,696 | 0 |

Every FPCR and FPSR access in sdk was probed. None ran in any session:

| sites | functions | executions |
|---|---|---:|
| `mrs fpcr` ×3, `msr fpcr` ×3, `mrs fpsr` ×3, `msr fpsr` ×3 at sdk+0x152b54…0x153060 | `nn::os::detail::UserExceptionHandler` (assembly without an FDE: saves and restores a user exception context) | 0 |
| `mrs fpcr` 0x7040b0, 0x7040bc, 0x704114; `msr fpcr` 0x7040c8, 0x70413c | `fegetround`, `__fesetround`, `fegetenv`, `fesetenv` | 0 |
| `mrs/msr fpsr` 0x7040d8…0x704140 | `fetestexcept`, `feclearexcept`, `feraiseexcept`, `fegetenv`, `fesetenv` | 0 |
| `mrs fpcr` 0x791e50, `mrs/msr fpsr` 0x791e84 / 0x791e8c | unexported helper (reads the rounding mode through a table, `FLT_ROUNDS`-like) | 0 |

main never touches FPCR or FPSR (census 04). So every game thread runs its whole life on its initial FPCR:
- **On the Mac** that is macOS's 0. nxnative's gate saves the guest's FPCR on every host entry and restores it on the way out, so
  host code cannot leak another value into a guest thread.
- **On the Switch 2** it is the kernel's initial value, which this method cannot see. The SDK's C library calls 0 its default
  environment: `fesetenv(FE_DFL_ENV)` writes FPCR = 0 and FPSR = 0. The game plays correctly with 0.

Confirming the value from a permissive reference logged in PROVENANCE is the one
open item.

## 2. Counter frequency

**What the SDK assumes:**
- `nn::os::GetSystemTickFrequency()` is `mov w0, #0xd650; movk w0, #0x1dc, lsl #16; ret` = **31,250,000**.
- `nnosGetSystemTickFrequency` branches to it.
- `TickManager::ConvertToTimeSpan` multiplies by 32 ns and saturates.
- No module reads `CNTFRQ_EL0` (census 04).

**What nxnative returns.** The rewriter turns each of sdk's 43 `mrs xN, cntpct_el0` into a gate. `gate_dispatch` (hle.c) waits
while the app is in the background (`guest_clock_read_wait`), then writes `guest_ticks()` into xN:
- `guest_ticks() = guest_ns × hz / 10⁹`, computed in 128 bits;
- `hz` is 31,250,000 unless `DM_TICK_HZ` sets another value;
- `guest_ns` is `mach_absolute_time` in ns minus the time the clock spent frozen, or a per-present step under `DM_DETCLOCK`.

`svc 0x1E GetSystemTick` returns the same, but this SDK has no such SVC site.

**Measured:**
- The handler's rate is 31,249,864–31,249,987 ticks/s (0.2 s windows).
- Calling the guest functions from the host before the game ran:
  - `GetSystemTickFrequency()` returns 31,250,000;
  - `nnosGetSystemTickFrequency()` returns 31,250,000;
  - `ConvertToTimeSpan(31,250,000 ticks)` returns 10⁹ ns.

| counter-read site (run `fullb`) | function | reads | per s |
|---|---|---:|---:|
| sdk+0x148d50 | `nn::os::GetSystemTick()` | 2,577,643 | 14,832 |
| sdk+0x14a3b4 | `nn::os::detail::TimeoutHelper::GetLeftTimeOnTarget()` | 86,296 | 497 |
| sdk+0x144ac8, 0x144b00 | `nn::os::WaitTimerEvent` | 103,523 | 596 |
| sdk+0x1512c0 | `InterProcessEventImplByHorizon::TimedWait` | 34,525 | 199 |
| sdk+0x144120 | `nn::os::TimedWaitEvent` | 17,263 | 99 |
| sdk+0x1447ac | `nn::os::StartPeriodicTimerEvent` | 1 | 0 |

The other 36 sites (message queues, semaphores, condition variables, multi-wait, `GetSystemTickOrdered`) never ran. In all,
15,800–16,200 counter reads/s: about 7,800/s at boot, 15,900/s through the title and load, 19,000–22,000/s in menus and play.

main calls `GetSystemTickFrequency()` 144–156 k times per session (about 870/s) from 12 call sites on 3 threads. The busiest, `main+0x882c60`, accounts for 62–65 %. The game takes its frequency from the SDK. nxnative's earlier 19.2 MHz counter ran Pokopia's clock at
61.4 % speed, which is 19.2 / 31.25 (its comment in `hle_os.c`).

## 3. Exceptions and setjmp/longjmp

**Dynamic.** In all three sessions:

| probe | calls |
|---|---:|
| `__cxa_throw`, `__cxa_rethrow`, `__cxa_allocate_exception`, `__cxa_begin_catch` | 0 |
| `_Unwind_RaiseException`, `_Unwind_Resume`, `_Unwind_ForcedUnwind`, `__gxx_personality_v0` | 0 |
| `setjmp` (= `_setjmp`, `__setjmp`, sdk+0x7044f4), `longjmp` (= `_longjmp`, sdk+0x7044b4) | 0 |

There are no `sigsetjmp` or `siglongjmp` in this SDK. Of the functions that carry an LSDA (handlers or cleanups), main ran 56 of
3,388 and sdk 39 of 574, all on their normal paths.

**Static.** Who could call them: BL or B to the importing PLT stub, or to the module's own definition. The last column is how
many of the calling functions ran in a session.

| module | callee | sites | calling functions | ran |
|---|---|---:|---:|---:|
| main | `setjmp` | 1 | 1 (main+0x15744e0) | 0 |
| main | `longjmp` | 1 | 1 (main+0x1574f00) | 0 |
| main | `__cxa_throw` / `__cxa_rethrow` | 5 / 8 | 5 / 6 | 0 / 0 |
| main | `_Unwind_Resume` (cleanup pads) | 2,802 | 1,811 | 46 |
| sdk | `setjmp` | 10 | 10 | 0 |
| sdk | `longjmp` | 7 | 7 | 0 |
| sdk | `__cxa_throw` / `__cxa_rethrow` | 22 / 62 | 22 / 62 | 0 / 1 |
| sdk | `_Unwind_RaiseException` / `_Unwind_Resume` | 4 / 711 | 4 / 475 | 0 / 36 |

- **main's pair** sits just before gRPC's `client_channel.cc` code. main+0x15744e0 builds a small context, calls setjmp on it,
  and runs a parser; main+0x1574f00 is `longjmp(ctx, 1)`. That is the shape of a protobuf decoder's error escape. Offline, it
  never runs.
- **sdk's setjmp callers:**
  - `nn::image` JPEG decode and encode (libjpeg error recovery);
  - PNG decode and encode, and libpng's `png_safe_execute`, `png_create_png_struct` and `png_free_jmpbuf`;
  - `pthread_create`'s thread start (sdk+0x154ce0), which `pthread_exit` longjmps back to;
  - the TrueType interpreter `fnt_Execute`.
- **sdk's longjmp callers:** `nn::image::detail::HandleLibjpegError` and `HandleError`, `png_safe_error`, `pthread_exit`, and three
  `fnt_*` error paths.

## 5. Coverage

**Function entries** (first execution of each FDE start):

| module | FDEs | executed (`pilot`) | % | first run: boot | title + load | menus | play |
|---|---:|---:|---:|---:|---:|---:|---:|
| main | 147,875 | 36,439 | 24.6 | 29,732 | 5,397 | 669 | 641 |
| sdk | 38,283 | 582 | 1.5 | 511 | 26 | 0 | 45 |
| subsdk0 | 1,909 | 3 | 0.2 | 3 | 0 | 0 | 0 |

`GetSystemTickFrequency`, which carried a probe instead of a coverage trap, also ran.

Across the two routes (`compare.py`), main ran 36,439 and 36,420 entries: 35,904 in both and 36,955 (25.0 %) in either. sdk ran the
same 582 in both. Four fifths of what runs, runs during boot.

sdk's share is small for two reasons:
- nxnative takes over 477 functions by name (forced takeovers, mostly sdk's service layer, D8), so their guest bodies never run;
- the movie module (subsdk0) is not used.

**Code outside the FDEs.** Every nonzero word outside FDEs carried a trap: main 63,388, sdk 40,029, subsdk0 3,292. Of these, 11,657
are br/blr (emulated, as below), 12 are sdk's FPCR/FPSR probe sites, and the rest one-shot traps. The first three header words of each
module are excluded. Executed:

| module | words executed outside FDEs | in PLT stubs | anything else |
|---|---:|---:|---:|
| main | 912 (228 of 885 stubs) | 912 | **0** (libcurl's 59,818 instructions never ran) |
| sdk | 992 (248 stubs) | 992 | **0** (its 9 small no-FDE regions never ran) |
| subsdk0 | 8 (2 stubs) | 8 | **0** |

**Indirect branch targets** (the first 8 executions of every executed br/blr; run `pilot`; `fullb` within 1 %):

| branch | target | executions logged | sites |
|---|---|---:|---:|
| blr | an FDE start | 81,383 | 14,327 |
| blr | host code (NVN and HLE procedures handed to the game) | 3,840 | 1,077 |
| br | an FDE start (tail call) | 5,107 | 841 |
| br | a label in the same function (jump tables) | 5,598 | 923 |
| br | host code (PLT stubs bound to HLE) | 498 | 133 |
| br/blr | **inside another function, or outside FDEs** | **0** | **0** |

**Samples** (pc of every thread, about 690 Hz):

| run | guest-text samples | inside an FDE | in PLT stubs | elsewhere outside FDEs |
|---|---:|---:|---:|---:|
| `pilot` | 25,512 | 99.12 % | 0.88 % | 0 |
| `fullb` | 25,690 | 99.14 % | 0.86 % | 0 |
| `light` | 25,804 | 99.06 % | 0.94 % | 0 |

Another 2,000 samples per run (7–8 % of guest samples) fell in nxnative's patch areas: the rewriter's relocated copies of x18, TLS, svc and counter
sites. Mapped back through the rewriter's branches, every one belongs to a rewritten site inside an FDE (runs `fullb` and `light`).

The hottest function, main+0x1365360, takes 12 % of guest-text samples; `memcpy` and `memset` follow.

## Limits

- **FPCR.** The Mac shows that nothing writes FPCR. It cannot show what Horizon starts a thread with (see §1).
- **Scope.**
  - Three sessions of about 170 s on one save.
  - Offline: no network, no saving, no photo or album, no corrupt PNG/JPEG.
  - Exceptions, setjmp/longjmp, the counter's 36 unused sites and three quarters of main can still run elsewhere. The static
    tables above bound where.
- **Coverage traps** prove that each trapped word ran at least once:
  - every FDE start;
  - every nonzero word outside FDEs. The non-PLT result outside FDEs is exhaustive for these sessions.

  They say nothing about which blocks inside a function ran.
- **Indirect targets** are the first 8 per site: 19,570 distinct site–target pairs. A polymorphic site with more targets shows
  only some of them.
- **Sampling** cannot prove 0. With about 25,600 guest samples per run and none in a category, that category is under about 0.012 %
  of guest time (rule of three, 95 %), per run.
- **The host is nxnative.**
  - 477 functions (forced takeovers) are replaced by host code, and their guest bodies never run. The Windows runtime keeps the same HLE layer
    (D8), so this matches the port, not the console.
  - Rewritten sites execute from patch areas, mapped back by the nearest rewriter branch (within 4 KB).
- **Perturbation.**
  - Traps fire mostly once per word.
  - Probe hits cost a signal each: about 870/s for `GetSystemTickFrequency`, under 1 % of a core.
  - The sampler's thread suspensions are microseconds long.
  - Frame times matched the uninstrumented run, and `light` (no coverage traps) agrees with `pilot` within 1–3 % on every count.

## Implications for the recompiler

1. **MXCSR.** Guest FPCR is 0, so a guest thread needs MXCSR **0x1F80**: round to nearest, all exceptions masked, FTZ = 0,
   DAZ = 0.
   - That is Windows' default for new threads, and its control bits are nonvolatile in the x64 ABI, so no per-thread setup is
     needed.
   - Denormals must be kept: no DAZ/FTZ anywhere in lifted code, which also suits D6's exact FMA and estimate paths.
   - DN = 0 means NaN payloads propagate. x86 differs from ARM in operand priority and in the default NaN (0xFFC00000 vs
     0x7FC00000). That stays an accurate-NaN-build question (D6), not an MXCSR one.
   - Keep FPCR and FPSR as GuestCpu fields (they are in nxnative's frame). Lower `msr fpcr` to a slow helper that maps
     RMode → RC, FZ → FTZ + DAZ and AHP/DN to flags for the exact helpers. Lower `mrs fpcr` to a field read. None runs in play.
   - Nothing reads FPSR, so there is no need to model the cumulative exception flags in lifted code. `fetestexcept` can read
     MXCSR's status bits.
   - In debug builds, check MXCSR against 0x1F80 when HLE calls return, as nxnative's gate restores FPCR. Drivers and libraries
     that set FTZ/DAZ on the calling thread would otherwise leak into the game.
2. **Timer.** Lower `mrs cntpct_el0` (and `cntvct_el0`) to a runtime call: `QueryPerformanceCounter` scaled to 31.25 MHz with a
   128-bit multiply-divide, or a precomputed 64.64 factor.
   - Never use 19.2 MHz.
   - QPC is invariant and coherent across cores on Windows 10 and later, and 16,000 reads/s cost well under 1 ms/s.
   - `GetSystemTickFrequency` is ordinary recompiled code returning 31,250,000, so the game stays consistent by construction.
   - Port nxnative's freeze (background) and `DM_DETCLOCK` (deterministic per-present clock). D15's trace diffs need the latter.
   - If `cntfrq_el0` ever appears, answer 31,250,000.
3. **Exception and longjmp bridge.** No throw, unwind or longjmp ran in normal offline play, so D5 can move off the M3/M4
   critical path.
   - First version: hooks at `__cxa_throw`, `__cxa_rethrow`, `_Unwind_RaiseException`, `_Unwind_ForcedUnwind` and `longjmp` that
     write a report and stop, so an occurrence in the field is loud.
   - Landing pads (1,811 cleanup functions in main) compile as blocks reachable only from the bridge.
   - Build the real bridge before release, tested against the static inventory: the image codecs on corrupt data,
     `pthread_create`/`pthread_exit`, the TrueType interpreter, the network stack's decoder, and sdk's 84 throw/rethrow sites.
4. **Coverage confidence.** The `.eh_frame` map plus PLT stubs and jump-table labels holds every executed instruction and every
   observed branch target.
   - D4's table can be built from FDE starts, jump-table labels, PLT stubs and host thunks. The interpreter fallback stays as a
     safety net but should not fire in normal play.
   - New: **1,077 blr sites and 133 br sites reach host code.** These are NVN procedures from `DeviceGetProcAddress` and imports
     bound to HLE. On the Mac guest and host share an ABI. On Windows every host function handed to the game needs a
     guest-visible address that the dispatcher maps to its generated adapter (D8), not an interpreter miss.
   - The 36,955 functions that ran are the first set for D15's swap-in tests. The other 75 % are still compiled, statically.
5. **Side observation for D9.** The game makes about 105,000 SVCs/s on average, about 280,000/s during boot.
   - `SleepThread` (yield) leads with 13.1 M per session.
   - Next come `ArbitrateUnlock`, `WaitProcessWideKeyAtomic` and `SignalProcessWideKey`, about 1.6 M each.
   - The Windows SVC path and yield must be cheap.

## Files

`research/mac-traces/` (copies of what ran on the Mac in `~/setup/traces/`):

| file | does |
|---|---|
| `make_inputs.py` | PC: the census FDE ranges as offset lists (`_local/mac-traces/fde_<module>.txt`), the only thing copied to the Mac |
| `hle_trace.c` | the trace module of the host copy (gates, probes, coverage traps, br/blr targets, sampler, writer) |
| `apply_trace_patch.py`, `nxnative-trace.diff` | copies `hle_trace.c` into a nxnative copy and adds the three hooks; the resulting diff |
| `tracedrive.py` | menudrive with tracing: run folder `~/setup/traces/<tag>/`, `phases.tsv`, flush at the end |
| `run_session.sh` | the sessions: `run_session.sh <tag> full|light|bare`, `SCRIPT=b` for route b |
| `analyze.py` | PC: one run against the census map and the NSOs (symbols, PLT stubs, static call sites) → `summary.md`, `executed_<module>.txt` |
| `compare.py` | PC: executed entries per run, in all runs, in any run |

To reproduce:
1. **PC:** `py -3 -I make_inputs.py`, then copy `fde_*.txt` to `~/setup/traces/input/`.
2. **Mac:** `rsync -a --exclude '*.o' ~/nxnative/ ~/nxnative-trace/`, then `python3 apply_trace_patch.py ~/nxnative-trace hle_trace.c`, then
   build with the `make` line in `apply_trace_patch.py`, then `zsh run_session.sh pilot full`, `SCRIPT=b zsh run_session.sh fullb full`
   and `zsh run_session.sh light light`.
3. **PC:** copy `<run>/{trace,phases.tsv,host.log}` to `_local/mac-traces/<run>/`, then run `analyze.py _local/mac-traces/<run>` and
   `compare.py`.

`pilot` ran before patch-area sampling was added to `hle_trace.c`; it is otherwise the same build.

Data, never committed:
- Mac: `~/setup/traces/{pilot,fullb,light}/` (logs, screenshots, the saves' copies);
- PC: `_local/mac-traces/<run>/` (traces, `summary.md`, executed-entry lists) and `_local/mac-traces/rewriter/` (the rewriter's
  site tables, offsets only).
