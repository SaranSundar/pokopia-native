# Pokopia Native for Windows: design

Draft 1, 2026-11-03, for review. Built on four research reports in `research\`:
[01 their issues](research/01-their-issues.md), [02 nxnative portability](research/02-nxnative-portability.md),
[03 recompiler design](research/03-recompiler-design.md), [04 binary census](research/04-binary-census.md).
Section numbers like "03 §5.3" point into them.

## 1. Goal

Pokémon Pokopia **1.0.5 and 2.0.1** running natively on **Windows 10/11 x86-64 PCs** with a Vulkan 1.3 GPU, built on the
player's own PC from their own dump by our installer, with everything the Mac and iPhone builds already have: direct mouse and
touch look, correct icons, the run-time shaders, both game versions, save sync.

Not now: Windows on ARM, Linux and Steam Deck (but nothing may rule out Wine/Proton later), other games, and any distribution of
game code (the game is only ever compiled on the player's PC).

## 2. Ground rules

1. **Clean-room.** No code, and no reading of code, from pokopia-recomp, nxrecomp, yuzu, Eden, suyu, Citron, Sudachi or Unicorn.
   Their issues, docs and release notes are fine. Permissive projects may be studied and used with credit: LLVM (Apache-2.0 with
   LLVM exception), Ryujinx (MIT; its MIT code now lives at git.ryujinx.app), Dynarmic (0BSD), Remill (Apache-2.0), XenonRecomp
   and N64Recomp (MIT), kirikae (MIT/Apache-2.0). We keep a short provenance log of what was consulted.
2. **MIT**, like pokopia-native and nxnative.
3. **The Mac is the oracle.** On the Mac the game's real arm64 code runs natively under nxnative, so every part of the
   recompiler can be checked against the Mac's own run, one function at a time.
4. **Gentle builds** on this PC until its BIOS has Intel's microcode fix (0x12B or newer): capped jobs, low priority, or
   cross-compilation on the Mac.

## 3. What we are dealing with (measured, 04)

| | main | sdk | subsdk0 |
|---|---:|---:|---:|
| code | 39.3 MiB | 7.7 MiB | 0.7 MiB |
| instructions | 10.3 M | 2.0 M | 0.18 M |
| functions (`.eh_frame`) | 147,875 | 38,283 | 1,909 |
| code covered by `.eh_frame` | 98.4 % | 96.1 % | 96.9 % |

What makes this tractable: `.eh_frame` is a near-complete function map. No branch enters another function mid-body. 99.9 % of
stored code pointers point at function starts. main uses LSE atomics, not load-exclusive/store-exclusive (0 LDXR/STXR). NEON is
only 1.3 % of instructions, with no SVE, dot product or FP16 arithmetic. main never touches FPCR, counters or system calls: all 57
SVCs live in sdk.

What makes it hard:
- **Pointers everywhere:** 136,621 indirect calls in main (95 % through C++ vtables), and 60 % of functions are reached only
  through pointers.
- **Flags:** 476k flag-setting instructions.
- **Floating point:** 74k fused multiply-adds, and ARM's reciprocal estimates (FRECPE/FRSQRTE) must be bit-exact.
- **Non-local control flow:** live C++ exceptions (3,388–3,962 functions with handlers), setjmp/longjmp and fibers.
- **Return-address signing:** ARMv8.3 PAC with the B key (84k sites), plus RCpc loads (LDAPR). The first becomes no-ops; the
  second becomes plain loads on x86.

## 4. Architecture

```
player's dump (exefs + romfs, 1.0.5 or 2.0.1; verified by build ID)
   │
   ├─ nxload     NSO/LZ4, MOD0, relocations (incl. RELR), symbols; does rtld's binding. Shared by recompiler and runtime.
   ├─ nxanalyze  functions from .eh_frame (+ BL targets, relocations, prologue scan for libcurl), CFGs, jump tables,
   │             landing pads, setjmp sites, SP/LR-rewriting functions, spin loops, HLE bindings → program.db
   ├─ nxlift     A64 → LLVM IR from one semantics library → x86-64 objects, ~190 partitions, cached, gentle
   └─ lld-link   → game_<buildid>.dll  (freestanding; imports only from nxrt)
                                   │
nxrt.exe (the runtime) ────────────┘
   loader      maps the modules at the game's fixed addresses, applies relocations
   memory      identity-mapped guest address space (guest pointer = host pointer), aliases via mapped sections
   cpu         GuestCpu, indirect-branch table, fallback interpreter (same semantics library), EH/longjmp bridge, fibers
   horizon     nxnative's dm/host ported to Win32: SVCs, threads and sync, clocks, HLE services (by name), file system, saves
   gpu         nxnative's NVN recording layer (hle_nvn.c, unchanged) + a new Vulkan replay backend
   shaders     our translator (ampere-lower → Maxwell → dmsh_xlate → SPIR-V), on demand with a persistent cache
   platform    SDL3 window, input (direct look), audio; crash reports and logs
```

## 5. Decisions

Each decision gives the choice, then the reason and its source.

**D1 · Recompiler: an LLVM IR lifter in C++20, LLVM 23.1 pinned.** The instruction semantics are written once as C++ templates
and compiled three ways: to bitcode for the lifter, natively for the fallback interpreter, and natively on the Mac for oracle tests.
*Reason:* direct control over `musttail`, `invoke`, atomics and debug locations, and one source of truth for semantics (03 §5.2).
Emitting C instead (the N64Recomp style) stays the fallback. The decoder is generated from a pattern table seeded by Dynarmic's
0BSD list and checked against LLVM's own disassembler, which already agrees with the census word for word (04 Method).

**D2 · One guest function becomes one host function, `void fn(GuestCpu*)`.** BL becomes a call, RET a return, and a tail branch a
`musttail` call. There is no dispatcher loop. Cross-module calls through stubs are bound at build time.
*One change to 03's sketch:* `GuestCpu` **starts with nxnative's 800-byte register frame verbatim** (x0–x30, q0–q31,
nzcv/fpsr/fpcr, sp; 02 fact 3), so `svc_dispatch`, the observation hooks and `GetThreadContext3` take it unchanged. Recompiler
fields (TLS registers, the exclusive monitor, pending unwind) follow the frame. Inside a function, flags are four SSA bits; they are
packed into the frame only at exits.

**D3 · Memory: identity mapping at the game's own addresses.** Use `VirtualAlloc2` placeholders below 2^39, link with
`/HIGHENTROPYVA:NO`, reserve early, and offer a launcher fallback. Aliases are pagefile-backed sections mapped with
`MapViewOfFile3`. *Reason:* no cost per access, and nxnative's host code, which assumes native pointers, ports almost as-is (02
fact 1, 03 §3.3). The layout copies nxnative's (one 11 GB block: alias, stack, heap). Windows' 4 KB pages remove the Mac's
16 KB-page copying.

**D4 · Indirect branches: a dense table, an interpreter fallback and a profile loop.** One 4-byte entry per guest instruction slot
(about 50 MB reserved, committed only where touched). A miss runs the interpreter and logs the address, so the next build compiles
it. Jump tables become `switch`. *Reason:* 150k indirect call sites and 59k functions only 4-byte aligned (03 §5.4).

**D5 · Exceptions, longjmp and fibers.** The game's own unwinder (libunwind and libc++abi, in sdk) runs as translated code. Only
its final "jump to the landing pad" step becomes a host C++ throw, caught by the translated function that owns the landing pad.
`nn::os` fibers become Windows fibers. *Reason:* we never re-implement C++ unwinding (03 §3.2). This is the highest-risk area and
gets its own inventory and tests first.

**D6 · Floating point exact where it matters; require AVX2 and FMA3.** Exact implementations of FRECPE/FRSQRTE (ARM's tables;
never x86's RCPPS), FRECPS/FRSQRTS, FMIN/FMAX(NM), saturating conversions and fused multiply-add. *Minimum CPU: x86-64-v3*
(AVX2 + FMA3: Intel Haswell 2013 and AMD Excavator 2015 onwards). That makes FMA a single instruction and halves the vector
lowering work. An "accurate NaN" build exists for testing (03 §3.5, 04 §5).

**D7 · Atomics and memory.** LSE instructions map onto LLVM atomics. Guest loads and stores start as `volatile`, as XenonRecomp
does, so the compiler cannot hoist spin loops, and are relaxed later only where proven safe. The 5 load-exclusive/store-exclusive
sites in sdk take a slow path (03 §3.6).

**D8 · System services: reuse nxnative's HLE layer.** It replaces SDK functions by name: 848 forced replacements, 4,096 named
stubs, and 734 NVN entry points behind `nvnBootstrapLoader`. The roughly 1,336 signatures are already written in the source, so
we **generate** the adapters between recompiled code and host C from them instead of writing them by hand. Also needed:
- a `guest_call` API, because the host calls into game code too (thread entry, init, the audio renderer, NVN callbacks);
- fixes for 94 uses of `long`, which is 32-bit on Windows;
- care with libc passthroughs (`strtol`, `struct tm`) (02 facts 4–6).

**D9 · Threads.** One guest thread is one Windows thread. nxnative's single-lock Horizon sync logic ports as is. WFE, YIELD and the
49 spin loops yield to the host, and there is no hard core pinning: their tracker shows starvation on busy PCs (01 lesson 11).
Pausing a thread and crash backtraces use safepoints and a host-to-guest address map instead of real registers.

**D10 · Renderer: keep NVN recording, write a Vulkan replay.** `hle_nvn.c` (2,652 lines, no Metal) stays. A new backend behind
nxnative's `mtl_*` interface replaces `nvn_mtl.m`, whose roughly 75 % Metal-specific code is rewritten, not translated. Rules we
already know:
- **GPU memory:** device-local copies of guest pools, updated through nxnative's write-protect dirty tracking. Do not import a
  giant host heap into Vulkan, which breaks on some drivers and under Wine (01 lesson 10, 02 fact 9).
- **Textures:** share by storage, shape, format and usage. Textures the CPU writes (including Pokopia's IMAGE-flagged BC1/BC7
  icons) are re-read from storage. That is our icon fix, kept from day one.
- **Format fallbacks** (D24S8) apply to every view of an image. **ASTC** is transcoded to BC7 with a cache, since desktop GPUs
  cannot sample ASTC (01 lessons 8, 17, 18).
- **Resolution independence** from day one: test at 1080p, 1440p, 4K and ultrawide (01 lesson 9).
- **Undefined output:** vertex outputs a shader never writes start at (0,0,0,1) (01 lesson 3).
- **Pipeline cache** written incrementally, not only at exit (01 lesson 16).

**D11 · Shaders: our pipeline, stopping at SPIR-V, translated on demand.** Keep `.spv` before the Metal-only edits (02 fact 10).
Translate any shader the game creates when it first appears, into a persistent cache keyed by shader hash plus translator version
and options, so a translator fix invalidates exactly what it should. Ship the known run-time keys (154 on 2.0.1, 140 on 1.0.5) to
prepare at first launch. A missing shader is a loud log line, never a silently skipped draw. The `.H0_NH1` swizzle fix and our
other translator fixes come along automatically (01 lessons 1, 2, 4).

**D12 · The build on the player's PC.** One `nxrecomp.exe` with LLVM linked in statically, plus `lld-link`; no external
toolchain to fail opaquely. The defaults:
- jobs sized from free RAM and cores, at low priority;
- an object cache;
- Unicode-safe paths;
- the compiler's real errors shown;
- every build stamped with recompiler, runtime and option hashes, so an update rebuilds what it must.

The game is identified by build ID, a "check my dump" mode gives plain answers, and folders and NUP files are accepted. Expect
about 10–15 minutes at 4 jobs for a full build (03 §3.8). It all goes behind a new Windows target in our existing builder and
window (01 lessons 5–7).

**D13 · Saves.** nxnative's transactional save (APFS clone and atomic swap) becomes, on NTFS: write a new folder, flush it, then
swap with `MoveFileEx` plus a commit marker that a startup check uses to finish or roll back. It is verified with kill tests.
The save keeps the game's own `B00`/`G00`/`S00` layout, so the save sync tool only needs a third location next to the phone and
Eden.

**D14 · Platform.** SDL3 for the window, input and audio. Direct look comes over with the per-build `look_cam` profile entries.
DualSense outer dead zone and per-pad calibration come from day one (01 lesson 13). Logs print GPU, driver, build ID, missing
shaders and resolution at start; a crash always writes a report; and no alarming stub banners (01 lesson 15).

**D15 · Correctness: three checks against the Mac.**
1. Random-state instruction vectors run on the M4 and through our semantics.
2. Lifted code compiled for **arm64** and swapped into nxnative on the Mac, function by function, to bisect any divergence.
3. Function-entry trace diffs under nxnative's deterministic clock.

The interpreter and the recompiled code must agree too (03 §3.9).

## 6. Milestones

| # | Milestone | Done when |
|---|---|---|
| M1 | Research and design | this document approved |
| M2 | Foundations: nxload, nxanalyze, decoder | the census numbers are reproduced by our own tools; decoder matches LLVM on every word; zero unresolved direct branches; inventory of special functions (SP/LR rewriters, context switches); open questions 1–6 answered with Mac traces |
| M3 | CPU core | integer and control-flow lifting, the interpreter and the Mac swap-in harness work; on Windows, a headless runtime runs sdk init to `nnMain` and the game's first frame request |
| M4 | Complete CPU and system | NEON and FP exact paths, EH/longjmp/fibers, generated HLE adapters, nxnative host ported; the game reaches its title screen logic with a null renderer; 100 % of functions pass swap-in on the Mac |
| M5 | Vulkan renderer | the title screen is drawn, then gameplay; textures and ASTC, shaders on demand, resolution-independent |
| M6 | Playable | input with direct look, audio, saves, both versions, performance work (each optimisation gated by the differential tests) |
| M7 | Release | Windows target in our builder and window, docs, a CI build of the tools, a public release |

Honest scale: M2–M4 is the long stretch. This is months of work in total. M3's "the game's own code runs on Windows" is the first
big proof point, M5's title screen is the first one you can see, and M2 alone is about a week or two.

## 7. Risks

| Risk | Mitigation |
|---|---|
| Non-local control flow (exceptions, longjmp, fibers, sdk context switches) causes rare crashes far from the cause | Inventory in M2; one `GuestNonLocal` mechanism; swap-in tests |
| FP divergence in physics and animation | Exact paths, accurate-NaN build, hardware oracle |
| GPU memory on discrete GPUs (copying and sync cost, driver differences) | Device-local copies plus dirty tracking; test on NVIDIA first, AMD before release, Wine later |
| Rebuilding the renderer's accumulated per-game fixes | The Metal backend is the reference; the Mac renders the same frames for comparison |
| Generated ABI adapters silently wrong | Generated from one signature list and checked on the Mac by swap-in |
| Performance: register traffic at about 430k call sites, `volatile` | Measured optimisations later (trusting AAPCS64 callee-saves, `preserve_none`, devirtualising hot calls) |
| First build time on weak PCs | Cache, gentle defaults, a clear progress display |
| Legal climate (DMCA activity around Switch projects) | Clean-room log, MIT, never ship game code, build from the player's own dump only |

## 8. Open questions (M2's answers; details in [docs/M2-REPORT.md](docs/M2-REPORT.md))

1. **FPCR in game threads:** always 0 (round to nearest, no flush-to-zero, no default NaN), and nothing sets it in play. MXCSR stays
   at Windows' default; `msr fpcr` is a slow helper. *(research/05; the value the real kernel starts threads with is unconfirmed.)*
2. **Counter frequency:** 31.25 MHz, an SDK constant; nothing reads CNTFRQ. The timer is QueryPerformanceCounter scaled to it.
3. **Exceptions, setjmp, longjmp:** none ran from boot to gameplay. main calls setjmp and longjmp once each (in the bundled gRPC
   code). D5 moves off the M3/M4 critical path, with "report and stop" hooks first.
4. **SP and x30 rewriters:** only four, all in sdk: `nn::os::detail::DispatchFiberContext`, `nn::os::detail::UserExceptionHandler`,
   `longjmp` and libunwind's `jumpto`. All four are replaced by host code (D5); main and subsdk0 have none.
5. **Coverage:** every function start the Mac executed is in nxanalyze's set, and no executed code fell outside the known functions
   apart from PLT stubs.
6. **main's other indirect branches:** all 5,843 are indirect tail calls (5,414 through vtables). None is a computed goto or a jump
   into another function.
7. **HLE boundary:** by name, as nxnative does it. main sends 332 imports to host code (1,233 call sites through its PLT) and binds
   613 to guest code. 1,078 indirect calls reach host code through slots filled at run time (NVN entry points and others); these
   need guest-visible addresses that map to the generated adapters (D8).
8. **39-bit pointers and fixed region bases:** still open, for M3, by moving nxnative's heap base on the Mac.

## 9. Decisions (approved 2026-11-03)

1. **Minimum CPU: AVX2 + FMA3.**
2. **A private, local repository** under `windows-native\` until M4 boots the game.
3. **The PC's BIOS update** (Intel microcode 0x12B or newer) before M3's heavy compiling; until then compiles stay gentle or run on
   the Mac.
4. **kirikae:** study its ideas, borrow code only where it clearly saves time.
