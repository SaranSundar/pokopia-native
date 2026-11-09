# 03: Static recompiler design (AArch64 to x86-64) for Pokopia on Windows

Date: 2026-11-03. Scope: how to translate Pokopia's `main`, `sdk` and `subsdk0` ahead of time into x86-64 code that runs inside a
Windows runtime, plus the toolchain and runtime contract around it. Rendering is covered only briefly (section 4).

Clean-room note: for GPL projects (nxrecomp, pokopia-recomp, SwitchRecomp, suyu, Unicorn) only READMEs and docs were read. No GPL
source was opened. Rellume and Instrew are LGPL, so only their READMEs and papers were used. Code was read only from permissive
projects, and each one's licence is given where it is cited.

---

## 0. Recommendation in one page

- **Lifter:** C++20 against **LLVM 23.1.x**, pinned and statically linked. LLVM 23.1.0 shipped on 2026-08-25, and clang 23.1.4 is
  already installed on the build PC. Instruction semantics are written once, as C++ templates (Remill's approach). They are compiled
  three ways: to LLVM bitcode for the lifter, natively for a fallback interpreter, and natively on the Mac for tests against real
  hardware.
- **Granularity:** one guest function (one `.eh_frame` FDE) becomes one host function. BL becomes a direct host call, RET a host
  return, and B to another function's start a `musttail` call. No dispatcher loop runs on the hot path.
- **Runtime ABI:** every translated function has the signature `void fn(GuestCpu* noalias)`. Guest registers live in `GuestCpu`
  between calls and in SSA values inside a function.
- **Memory:** identity mapping, where a guest pointer is the host pointer. The guest regions are reserved at startup with
  `VirtualAlloc2` placeholders at fixed addresses below 2^39. Aliases use pagefile-backed sections mapped with `MapViewOfFile3`. This
  costs nothing per access and lets the nxnative HLE host (C code that assumes native pointers) be ported almost as-is.
- **Indirect branches:** a dense guest-to-host table (4 bytes per guest instruction slot, about 50 MB) handles BLR/BR. On a miss, an
  interpreter built from the same semantics runs the code, and the miss address is logged for the next build, as kirikae does. Jump
  tables become LLVM `switch` instructions.
- **Cross-module calls (PLT) are bound statically** at recompile time using rtld's lookup order. That affects 885 PLT slots in main
  and 9,683 in sdk.
- **Exceptions and longjmp:** the guest's own unwinder (libunwind and libc++abi in `sdk`) keeps running as translated code. Only its
  final "jump to context" step is replaced, with a host C++ throw that is caught by the translated function that owns the landing pad
  or setjmp point. nn::os fibers map one-to-one onto Windows fibers.
- **Floating point:** emulate exactly wherever results differ for ordinary inputs: FMIN/FMAX(NM), FCVTZ*, FRECPE/FRSQRTE (bit-exact,
  never RCPPS), FRECPS/FRSQRTS, and fused multiply-add. NaN payloads are not canonicalised in the default build. An "accurate NaN" flag
  exists for differential testing.
- **Atomics:** the binaries use LSE, not LL/SC (0 LDXR/STXR in main). LSE instructions map one-to-one onto LLVM atomics. Plain guest
  loads and stores are emitted as `volatile` (XenonRecomp's precedent), so LLVM cannot hoist relaxed-atomic spin loops.
- **Build:** about 190 partitions of about 1,000 functions each, compiled in parallel at low priority with a capped job count and a CPU
  rate cap, with a content-hash object cache. The result is linked with lld-link into a **freestanding game-code DLL**, which needs no
  MSVC CRT or Windows SDK libs. Expect about 30 to 60 P-core-minutes for a full -O2 build: roughly 10 to 15 minutes at 4 jobs.
- **Verification:** the Mac is the oracle. Use (a) per-instruction random-state vectors executed natively on the M4, (b) the lifted IR
  compiled for **arm64** and swapped into nxnative function by function, so failures can be bisected, and (c) function-entry trace
  diffs under nxnative's deterministic clock.

---

## 1. What the binaries actually contain (measured)

All numbers come from a read-only scan of the 2.0.1 update ExeFS (`update_exefs/exefs`): NSO headers, LZ4-decompressed segments, MOD0,
`.eh_frame_hdr` and `.eh_frame` (including CIE augmentations and LSDA pointers), the dynamic section (RELA, RELR, JUMP_SLOT), `.dynsym`,
and opcode-class counts over `.text`. Nothing was modified. The scan took under 2 seconds on one core.

The fuller census in `04-binary-census.md` (done separately) agrees wherever the two overlap. It also splits main's 12,944 BRs into
6,346 jump tables, 886 PLT stubs and 5,712 other.

| | main | sdk | subsdk0 |
|---|---|---|---|
| NSO flags | 0x3f (LZ4 and hash on all segments, **not** execute-only, not zstd) | 0x3f | 0x3f |
| `.text` size / 4-byte words | 41.2 MB / **10,298,656** | 8.1 MB / 2,023,164 | 0.72 MB / 181,208 |
| `.rodata` / `.data` / `.bss` | 11.4 / 2.8 / 8.7 MB | 6.2 / 0.39 / 2.0 MB | 0.43 / 0.09 / 0.06 MB |
| FDEs in `.eh_frame_hdr` | **147,875** (cover 98.4 % of `.text`) | 38,283 (96.1 %) | 1,909 (96.9 %) |
| Function size (FDE range) | median 84 B, p99 2,720 B, max 68,480 B; 58 over 16 KB | median 76 B, max 21,168 B | max 11,676 B |
| FDEs with an LSDA (landing pads) | **3,388** | 574 | 0 |
| CIE augmentation `B` (PAC B-key frames) | 83,354 | 20,812 | 1,276 |
| `.dynsym` | 1,052 (947 imports, only 16 defined functions) | 27,913 (**23,136 named functions**) | 1,682 |
| PLT slots (JUMP_SLOT) | 885 | 9,683 | – |
| RELATIVE/RELR code pointers | 246,274 into 92,327 targets, **92,241 of them FDE starts** | 12,696 into 10,323 targets (10,320 FDE starts) | 275 |
| BL / BLR / BR / RET / RETAB (RETAA: 0) | 427,774 / **136,621** / 12,944 / 66,666 / **79,219** | 94,803 / 15,723 / 12,644 / 14,164 / 22,631 | 7,368 / 1,341 / 952 / 1,143 / 1,337 |
| B: intra-function / to a function start (tail call) / to PLT / to mid-function | 222,024 / 29,127 / 14,220 / **0** | 48,368 / 5,053 / 5,802 / 0 | |
| Conditional branches leaving their function | **0** | 0 | |
| BL from FDE code landing mid-function | **0** (all non-FDE targets are PLT stubs) | 0 | |
| LDXR / STXR | **0 / 0** | 3 / 2 | 0 |
| CAS / LSE LD-ops / SWP / LDAPR / LDAR / STLR | 13,676 / 14,942 / 1,090 / 4,536 / 91 / 250 | 699 / 979 / 250 / 2,567 / 689 / 738 | |
| DMB (ISH / ISHLD) | 2,453 / 15 | 96 / 201 (+5 other) | 0 |
| SVC | 0 | 57 | 0 |
| MRS system registers | TPIDR_EL0 ×145 only | TPIDRRO_EL0 ×430, CNTPCT_EL0 ×43, FPCR ×7, FPSR ×8, CTR_EL0 ×2, TPIDR_EL0 ×2 | none |
| MSR | none | FPCR ×5, FPSR ×7, TPIDR_EL0 ×3, NZCV ×2 | none |
| PACIBSP · AUTIBSP (B key only; the A-key forms: 0 in main) | 83,986 · 32,096 | 20,847 · 4,716 (+34 XPACLRI) | 1,276 · 352 |
| BRAA/BLRAA/LDRAA (authenticated branches/loads) | 0 | 0 | 0 |
| TBL/TBX · FRECPE/FRSQRTE · FRECPS/FRSQRTS | 2,704 · **784** · 1,568 | 181 · 10 · 20 | 0 |
| Scalar FMADD family · vector FMLA/FMLS | **53,607** · 2,613 | 2,628 · 597 | |
| Scalar FMIN/FMAX(NM) · scalar FCVTZS/FCVTZU | 4,506 · 6,474 | 333 · 634 | |
| LD3/ST3/LD4/ST4 · SDOT/UDOT · FP16 arithmetic/conversion | 3 · 0 · 0 | 604 · 0 · 0 | |
| Padding: zero words · NOPs | 99,916 · 109,060 | 39,825 · 24,522 | |

What this changes in the brief:

1. **The work is about 12.5 million instruction words, not 7 million.** The 29 MB figure for `main` is the LZ4-compressed size; its
   `.text` alone is 41 MB. Roughly 12.2 M of the words are real instructions after removing zero and NOP padding.
2. **The ISA is ARMv8.2-A plus two ARMv8.3 features of the A78C:** PAuth with the B key (79k `RETAB`, which is *not* a hint-space instruction)
   and LRCPC (`LDAPR`). There is no dot product and no FP16 arithmetic, as far as the encodings checked show. LSE is used everywhere.
3. **`.eh_frame` is a near-complete function map.** No function is entered mid-body by BL or B, there is no hot/cold splitting, and
   conditional branches never leave a function. This validates the "one FDE = one host function" model.
4. **C++ exceptions, setjmp/longjmp and fibers are all in play.** main imports `__cxa_throw`, `__cxa_begin_catch`,
   `_Unwind_Resume`, `__gxx_personality_v0`, `setjmp`, `longjmp` and `nn::async::detail::FiberTaskCoordinatorBase`; 3,962 functions
   carry landing pads.
5. `main` never touches FPCR, FPSR or the counters directly. All of that sits in `sdk`, which is fully symbolised (23k function names).

---

## 2. Survey

| Project | Licence (code readable?) | Approach | Maturity | Reuse / learn |
|---|---|---|---|---|
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | MIT (yes) | MIPS to C, one literal statement per instruction (`ctx->r4 = ADD32(ctx->r4, 0X20)`). `jal` becomes a direct call; `jalr` becomes `LOOKUP_FUNC(reg)(rdram, ctx)`; recognised jump tables become `switch`. Needs an ELF for symbols; TOML stubs and patches; relocatable overlays via `RELOC_HI16/LO16` macros. | Mature; powers several shipped ports | Context-struct ABI, config-driven stubs and overrides, "single file output" for patched functions |
| [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) | MIT (yes) | PPC to C++ compiled by Clang 18+. Functions come from XEX `.pdata` plus `bl` scanning. Switch tables are detected by XenonAnalyse, which its README says is tuned for Sonic Unleashed and not fully generic. Indirect calls use a table placed after the image: address × 2 indexes an 8-byte host pointer. **All memory accesses are `volatile`**; setjmp/longjmp redirect to native code; mid-asm hooks; registers can be turned into locals (`non_volatile_as_local` etc.). Exceptions are not supported. | Shipped (Unleashed Recompiled) | Dense lookup table; the volatile-memory precedent; register-as-local optimisations (the README credits them with about 20 MB less code and several ms off frame times) |
| [ReXGlue](https://github.com/rexglue/rexglue-sdk) | BSD-3-Clause (yes) | Xbox 360 PPC to C++23. A 7-stage analysis (Register, Scan, Discover, GapFill, Discover, Merge, Validate) with **authority levels** per boundary source (PDATA, CONFIG, IMPORT, GAP_FILL, VTABLE). RTTI vtable scanner; SEH and C++ EH tables mined for handlers. Xenia-derived runtime with a `FunctionDispatcher`. The 4 GB guest is a file mapping at a fixed host address (`0x100000000`), using base+offset. ([pipeline](https://github.com/rexglue/rexglue-sdk/wiki/Codegen-Pipeline-Overview), [memory](https://github.com/rexglue/rexglue-sdk/wiki/Memory)) | v0.3, experimental; community ports exist | The discovery pipeline design (authority levels, gap fill, validation of every `bl` target) |
| [Remill](https://github.com/lifting-bits/remill) | Apache-2.0 (yes) | Semantic lifter to LLVM bitcode. Semantics are C++ functions (`DEF_SEM`/`DEF_ISEL`) compiled to bitcode; lifted blocks take `(State*, pc, Memory*)`, and a threaded `Memory*` orders memory operations ([life of an instruction](https://github.com/lifting-bits/remill/blob/master/docs/LIFE_OF_AN_INSTRUCTION.md)). Supports AArch64, x86/AVX-512 and SPARC; LLVM 15+; Windows "experimental". | Maintained; analysis-oriented | **Adopt the semantics-in-C++ design.** Its AArch64 semantics can seed ours (keep the NOTICE), but its ABI is built for analysis, not speed, and v8.1+ coverage needs checking |
| [McSema](https://github.com/lifting-bits/mcsema) | AGPL-3.0 (docs only); archived Aug 2022 | Whole-binary lifting: IDA-based CFG recovery, then Remill, then recompile | Archived | Lesson: CFG recovery quality is the bottleneck. Here `.eh_frame` and the relocations remove most of that risk |
| [Rellume](https://github.com/aengelke/rellume) / [Instrew](https://github.com/aengelke/instrew) | LGPL-2.1+ (docs only under our rules) | Lifts x86-64, AArch64 and RV64 to LLVM IR, aiming for fast lifted code; functions operate on a CPU-state struct and can be wrapped for any calling convention. Instrew (VEE'20) uses LLVM as a dynamic binary translator. | Research-grade, maintained | Confirms the performance model: CPU struct, register caching in SSA, LLVM -O2 cleaning up |
| [Dynarmic](https://github.com/lioncash/dynarmic) | 0BSD (yes); upstream archived Mar 2024; forks [azahar-emu](https://github.com/azahar-emu/dynarmic) and [Vita3K](https://github.com/Vita3K/dynarmic) are active and 0BSD | A32/A64 JIT with x64 and arm64 hosts. IR with get/set elimination, RSB emulation, a fast dispatcher. Accuracy toggles: `Unsafe_UnfuseFMA`, `Unsafe_ReducedErrorFP`, `Unsafe_InaccurateNaN`, `Unsafe_IgnoreStandardFPCRValue`, `Unsafe_IgnoreGlobalMonitor` ([flags](https://github.com/lioncash/dynarmic/blob/master/src/dynarmic/interface/optimization_flags.h)) | Mature for v8.0. **Its A64 decoder has CAS/LDADD/SWP/LDAPR commented out**, so it cannot be a drop-in fallback for this game | x64 FP fixup sequences (software DAZ, FMA NaN selection), the decoder bit-pattern table (`a64.inc`), the accuracy-flag taxonomy. 0BSD needs no attribution |
| Ryujinx ARMeilleure ([Ryubing fork](https://git.ryujinx.app/projects/Ryubing); the GitHub original is [DMCA-blocked](https://github.com/github/dmca/blob/master/2025/03/2025-03-06-nintendo.md)) | MIT (yes) | A64 to custom IR to x86-64/arm64 JIT, with a PTC disk cache. Exclusive monitor: a per-thread address (64-byte granule) and value, with STXR done as a CAS against the saved value (`InstEmitMemoryExHelper.cs`). FRECPE fast path: RCPPS rounded to 8 mantissa bits (close, not exact), otherwise `SoftFloat32.FPRecipEstimate`. SSE/AVX lowering of NEON with soft fallbacks. | Mature for Switch 1 (ARMv8.0). **The decoder has no LSE, LDAPR or dot product** | Port `SoftFloat*` (ARM pseudocode in C#) and the NEON-to-SSE/AVX lowering ideas; the monitor design for the 5 LL/SC sites |
| [FEX](https://github.com/FEX-Emu/FEX) | MIT (yes) | x86 on ARM64. Host-library forwarding via generated thunks ([ThunkLibs](https://github.com/FEX-Emu/FEX/blob/main/ThunkLibs/README.md)), an experimental code cache, per-app memory-model toggles | Mature | Thunk generation (the template for HLE marshalling); [allocator discipline](https://github.com/FEX-Emu/FEX/blob/main/docs/ProgrammingConcerns.md) to keep host allocations out of guest ranges, which is exactly the identity-mapping problem |
| [Unicorn](https://github.com/unicorn-engine/unicorn) | GPL-2.0 (QEMU-based; docs only) | Emulator library | Mature | Not needed: the Mac is a better oracle. nxrecomp, kirikae and LAS Recomp use it for tests or jump tables |
| [kirikae](https://github.com/Cor3Duo/kirikae) | MIT OR Apache-2.0 per README; GitHub detects Apache-2.0 (yes) | **Switch static recompiler in Rust.** A64 to a small IR; `ops.rs` semantics shared by the interpreter and a Cranelift backend (1,024 functions per object, compiled in parallel). Discovery from module entries, relocations, dynsym, BL, ADR and ADRP+ADD, **plus interpreter-recorded profiles**. Dispatcher with interpreter fallback. Reserves the 39-bit space and computes `base + (addr & mask)`. One host thread per guest thread; HLE trampolines at guest addresses; Maxwell to SPIR-V on Vulkan ([ARCHITECTURE.md](https://github.com/Cor3Duo/kirikae/blob/main/ARCHITECTURE.md)) | Early preview (repo created 2026-10-23); Switch 1, no updates or DLC | Closest permissive prior art: profile feedback, interpreter fallback, a shared semantics source |
| [LAS Recomp](https://github.com/peiikai/LASRecomp) and [letsgo-recomp](https://github.com/raulzrrs/letsgo-recomp) | MIT per README (GitHub: NOASSERTION) | AArch64 to C (Capstone disassembly; jump tables resolved by executing code snippets under Unicorn), built by `zig cc`; `rt_dispatch` plus `musttail` for indirect and tail jumps. Needs AVX2 and FMA. Let's Go: one-time recompile of 22 minutes on a 20-thread CPU, about 5.2 GB RAM during setup | Shipping community ports (Link's Awakening, Let's Go, Sword/Shield) | Proof that C emission plus a dispatcher works at Switch scale; the build-time data point |
| [nxrecomp](https://github.com/drakolordx7/nxrecomp) and [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp) | **GPL-3.0 (docs only)** | AArch64 to C++, validated instruction by instruction against Unicorn; runtime with memory map, loader, threads, Horizon SVC and IPC, NVN to Vulkan; Maxwell to SPIR-V, ASTC to BC7. pokopia-recomp runs **Pokopia 2.0.1a on Windows**: its README gives about 7 minutes of game compilation, 1 to 2 hours of shader cache building, Vulkan 1.3 and 16 GB RAM | Early; Pokopia playable per its README | Proves feasibility for this exact game. Clean-room: nothing may be taken from it |
| [SwitchRecomp](https://github.com/kaiyao7/SwitchRecomp) | GPL-3.0 (docs only) | Decoder to typed IR to C11; by its own account it runs no commercial game | Research skeleton | Nothing beyond its testing discipline |
| suyu 0.0.4 "recompiler mode" ([news](https://www.digitalcitizen.life/suyu-0-0-4-adds-static-recompilation-to-improve-nintendo-switch-emulation-on-pc/)) | GPL-3.0 (docs only) | AOT AArch64 to x86-64 combined with emulator HLE | Archived | Avoid |
| [nxnative](https://github.com/seedds/nxnative) | MIT (yes) | Native execution on Apple arm64: NSO rewriter, Horizon HLE host, NVN to Metal; shaders through Ryujinx's translator to SPIR-V then SPIRV-Cross. Docs cover spin-loop rewriting, exclusive-store contention, and a `MapMemory` that copies bytes rather than sharing pages ([development.md](https://github.com/seedds/nxnative/blob/main/docs/development.md)) | In use | **The HLE host to port**, and the oracle |
| Others | [llvm-mctoll](https://github.com/microsoft/llvm-mctoll) (archived; x86-64/ARM32 only); [RetDec](https://github.com/avast/retdec) (MIT; decompiler-grade arm64 lifting); [Ghidra](https://github.com/NationalSecurityAgency/ghidra) (Apache-2.0) with [Ghidra-Switch-Loader](https://github.com/Adubbz/Ghidra-Switch-Loader) (ISC); [Capstone](https://github.com/capstone-engine/capstone) (BSD-3-Clause); [sail-arm](https://github.com/rems-project/sail-arm) (BSD-3-Clause-Clear, executable Armv8.5/9.x model); [isa-tools/ASLi](https://github.com/alastairreid/isa-tools) (BSD-3-Clause) | | | Cross-checking analysis; an executable spec for corner cases |

Takeaways:

- Every shipping recompiler uses a context-struct ABI, direct calls for direct branches, and a lookup for indirect ones.
- None of the permissive A64 JITs supports ARMv8.1+ atomics, so LSE, LRCPC and PAuth semantics must be written from the Arm ARM.
- Two GPL projects already run Switch titles (one of them Pokopia) on Windows. That proves feasibility, and it raises the bar for
  keeping this work clean.

---

## 3. Problems, options and recommendations

### 3.1 Function discovery

**Sources, in order of authority:**

1. **`.eh_frame_hdr` FDEs.** MOD0 points at `.eh_frame_hdr` (MOD0 + 0x10/0x14). They give a start *and a size* for 188,067 functions
   covering 96 to 98 % of `.text`. Treat as authoritative boundaries.
2. **Dynamic relocations.** RELATIVE plus RELR give every code pointer stored in data: vtables, callback tables, `.init_array`. In main,
   99.9 % of the 92,327 targets are FDE starts. The 86 exceptions must be inspected; `0x0` and `0x8` are module-base references, not
   code. GLOB_DAT and ABS64 relocations give cross-module function pointers.
3. **PLT stubs.** These are the non-FDE targets at the end of `.text`. Recognise the `adrp x16; ldr x17,[x16,#..]; add x16,..; br x17`
   pattern and bind each stub through its JUMP_SLOT.
4. **Direct BL/B targets.** Here they add nothing beyond 1 to 3, which is a good consistency check.
5. **ADR/ADRP+ADD address computations** landing in `.text` (callbacks passed in registers).
6. **Module entry, `DT_INIT_ARRAY`/`DT_FINI_ARRAY`** and the dynsym (sdk: 23k names, which drive HLE binding).
7. **Gap fill.** Main has 28,682 gaps holding 63,391 non-zero words, including the PLT. Decode them, then promote sequences that are
   reachable or that look like functions (ReXGlue's GAP_FILL authority).
8. **Profiles:** addresses the runtime's fallback interpreter had to run, plus entry traces from nxnative on the Mac.

**Data in code** is a non-issue for Clang AArch64 output: jump tables and literals live in `.rodata`, and the 99,916 zero words and
109,060 NOPs in main are alignment padding. The NSO is not execute-only (flag bit 6 is clear). Still, the decoder must treat any
undecodable word as "stop and trap" rather than guessing.

**Non-returning calls:** when the last instruction of an FDE range is a BL, the callee does not return (abort, `__cxa_throw` and so
on). Emit `unreachable` after such a call rather than falling into the next function.

**Recommendation:** model this on ReXGlue's pipeline (seed with authority levels, discover, gap fill, merge, validate), seeded by FDEs
and relocations. Fail the build if any BL or B target is unresolved. Expect almost no heuristics to be needed for this game.

### 3.2 Control flow

**BL/RET as host call/return, not a dispatcher loop.** Host call and return keep the return-stack predictor, allow direct calls (all
427k BLs in main are statically known), and let LLVM optimise each function. A dispatcher loop is robust but costs an indirect jump per
transfer. Dynarmic needs a whole RSB-emulation layer just to approximate host returns. Use the dispatcher only inside the fallback
interpreter.

- **Return safety net (debug builds):** the translated RET stores `x30` to `cpu->pc`, and each call site checks it against its
  expected return address. On a mismatch it calls `rt_unexpected_return`. Release builds drop the check.
- **Tail calls:** `B <function start>` (29,127 in main) and B to PLT (14,220) become
  `musttail call void @callee(ptr %cpu)` after flushing registers. Every translated function has the same prototype, so `musttail` is
  always legal and deep tail recursion cannot grow the host stack. `BR Xn` that is not a jump table is an indirect tail call: lookup,
  then `musttail`. LAS Recomp uses `__attribute__((musttail))` the same way.
- **Jump tables:** about half of main's 12,944 BRs (6,346, per the census) are jump tables. Clang's AArch64 shape is
  `cmp wI,#N; b.hi default; adrp/add xT,table; ldrb|ldrh|ldrsw wE,[xT,xI,...]; adr xB,anchor; add xB,xB,wE,sxt?#2; br xB`, with
  LLVM's AArch64 jump-table compression producing 1-, 2- or 4-byte entries scaled by 4. Resolve it by backward slicing inside the
  block plus the dominating bound check. Validate that every target lies inside the same FDE, then emit an LLVM `switch`. For anything
  unusual, run the slice through the semantics interpreter at build time (LAS Recomp does this with Unicorn; we have our own
  interpreter), and record unresolved BRs for manual TOML hints. An unresolved intra-function BR at runtime goes to the interpreter,
  never to a guess.
- **PLT and imports:** bind each JUMP_SLOT statically, reproducing rtld's lookup order (main first, then subsdk*, then sdk, so main's
  16 exported functions can interpose). The call becomes a direct call to the translated function or HLE thunk. This removes an
  indirect lookup from about 150k call and tail-call sites in main and sdk.
- **PAC:** `PACIBSP`/`AUTIBSP`/`XPACLRI` (and the A-key forms, if they ever appear) are no-ops, and `RETAB`/`RETAA` are RET. Nothing in the binaries uses
  authenticated branches or loads. Since nothing is ever signed, the guest unwinder's "negate RA state" handling (`B` augmentation)
  stays consistent.

**C++ exceptions** (3,962 functions with landing pads; a few are probably cleanup-only) — options:

- (a) Replace the guest EH runtime with host EH. This needs a guest personality and LSDA parser on the host anyway, and it is invasive.
- (b) **Keep the guest unwinder.** Translated code keeps guest memory and the guest stack exact, and `GuestCpu` is exact at every
  call, so libunwind's phase 1 and phase 2 walk guest frames through `.eh_frame` correctly. Only the final "restore registers and
  branch to the landing pad" primitive (libunwind's `Registers_arm64::jumpto`, which is not exported) has to be HLE'd. It copies the
  context into `GuestCpu`, sets `unwind_pc`/`unwind_sp`, and throws a host C++ exception `GuestNonLocal`. A translated function that
  owns landing pads emits its call sites as `invoke` with a `catchswitch`. The handler checks that the target SP equals the SP saved
  at the call site and that the target PC is one of its landing pads. If so it does `catchret` and branches to the landing-pad block,
  which reloads every register from `cpu`. Otherwise it rethrows. Intermediate host frames need no cleanups because they hold no host
  resources.
  - The non-throwing path stays zero-cost.
  - A throw costs microseconds, which is fine for game exceptions.
  - XenonRecomp does not support exceptions; ReXGlue at least discovers SEH and C++ EH handlers.
- **Recommendation: (b).** Landing-pad addresses come from the LSDA call-site tables (`.gcc_except_table`), reached through FDE
  augmentation `L`.

**setjmp/longjmp** (imported by main; libpng in sdk uses them too): keep the guest `setjmp` (it only stores registers). Mark each
`BL setjmp` return address as a resume point of the caller. HLE `longjmp` restores registers from the guest `jmp_buf` and makes the
same `GuestNonLocal` transfer as above, so one mechanism serves both EH and longjmp. XenonRecomp's alternative is to redirect to host
`setjmp`/`longjmp` (`setjmp_address`/`longjmp_address` in its TOML). That also works in LLVM (`returns_twice`), and it is a fallback if
the unified path proves awkward.

**Fibers and coroutines:** main uses `nn::async`'s fiber coordinator, which runs on sdk's `nn::os::SwitchToFiber`. Because translated
frames live on the *host* stack, switching only the guest SP is wrong; the host stack must switch as well.

- Map each guest `FiberType` to a Windows fiber (`CreateFiberEx`, about 1 MB reserve). `ConvertThreadToFiber` each guest thread.
- HLE the sdk's low-level fiber switch: save and restore the guest register state in the fiber record, then `SwitchToFiber`.
- Windows fibers update the TEB stack bounds that SEH and `__chkstk` depend on. A hand-written stack switch would not.
- Static analysis flags every function that loads SP from memory or rewrites x30 outside the normal prologue/epilogue pattern. That
  list (expected to be under 20, all in sdk: jumpto, longjmp, fiber switch, thread entry) must be fully HLE'd or verified.

**Stack placement:** guest stacks stay in guest memory, exactly where the game or nn::os allocates them, so the game's
stack-address arithmetic and cross-thread pointers to locals keep working. Host stacks are separate: 8 MB reserve per guest thread and
about 1 MB per fiber. Guest stack usage is therefore identical to native, so a translated game cannot overflow a guest stack that was
big enough on hardware.

### 3.3 Guest memory

**Can Windows reserve 0x70_0000_0000 to 0x80_0000_0000?** Yes. x64 user space on Windows 10/11 is 128 TB, so 448 to 512 GB is ordinary
user space. `VirtualAlloc2(process, base, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, …)` with a base that is a
multiple of the 64 KB allocation granularity reserves it if it is free. Placeholders can be split and replaced, and
`MapViewOfFile3(…, MEM_REPLACE_PLACEHOLDER, …)` waives the 64 KB alignment rule, so views can be placed at 4 KB granularity, which
matches Horizon's page size ([VirtualAlloc2](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2),
[MapViewOfFile3](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3)).

**The catch is ASLR.** High-entropy bottom-up randomisation varies the start of bottom-up allocations across a 1 TB window
([MSRC](https://msrc.microsoft.com/blog/2013/12/software-defense-mitigating-common-exploitation-techniques)), so host
heaps, stacks and driver allocations can land anywhere in [0, about 1 TB), which includes the guest range. Mitigations, in order:

1. Link the runtime exe with `/HIGHENTROPYVA:NO`. Image ASLR stays on, but bottom-up allocations fall back to the low few GB.
2. Reserve all guest regions as placeholders in a **TLS callback** of the exe, before the CRT and any DLLs start allocating.
3. As belt and braces, a launcher can `CreateProcess(CREATE_SUSPENDED)`, check with `VirtualQueryEx`, reserve into the child with
   `VirtualAlloc2(hChild, …)`, then resume it.

FEX documents the same class of problem: host allocations stealing guest ranges.

**Identity mapping compared with base+offset:**

| | Identity (guest VA == host VA) | Base+offset | Base+(addr&mask) (kirikae) |
|---|---|---|---|
| Per access | nothing | 1 add (often folded into the x86 addressing mode) plus a register tied up for the base in every function | add + AND; breaks address-mode folding |
| HLE boundary | pass pointers through | translate every pointer, including inside structures | same |
| Porting nxnative's host | near-verbatim | rewrite pointer handling | rewrite |
| Wild guest pointer | can hit host memory | stays in the guest window | stays in the window |
| Address choice | fixed at recompile time (modules) | anywhere | anywhere |

Base+offset is typically a single-digit-percent cost per access in published JIT experience. The bigger cost is engineering: every
HLE function and every NVN structure walk must translate pointers. **Recommendation: identity mapping.**

**Address plan (proposal):**

- Modules are laid out contiguously as Horizon's loader would, starting at `0x71_0000_0000`. That base matches common Switch
  reverse-engineering databases, so main+offset annotations line up. It is a single build constant, `GUEST_MODULE_BASE`.
- Heap, alias, stack and TLS regions are reserved as placeholders in the rest of [0x72_0000_0000, 0x80_0000_0000) and committed on
  demand as `svcSetHeapSize` grows. Commit charge is counted at commit time, not at reservation.
- Everything stays below 2^39, so any code that assumes 39-bit pointers still works.
- Guest `.text` is mapped read-only and *non-executable*. The interpreter reads it; translated code lives in the DLL. The null page is
  already unmapped on Windows, so null dereferences still fault.

**Aliasing** (svcMapMemory, shared and transfer memory, code memory): back guest RAM with pagefile-backed sections
(`CreateFileMapping(INVALID_HANDLE_VALUE, …, SEC_RESERVE)`) and map the same pages at two addresses with `MapViewOfFile3`. nxnative's
Mac implementation copies bytes instead. That is acceptable for a first port of its code, but true aliasing is the correct end state.

### 3.4 NEON/AdvSIMD on x86

**Target x86-64-v3 (AVX2, FMA3, F16C, BMI2) as the baseline.**

- The i9-13900KF has no AVX-512 (it is fused off on Raptor Lake), and v3 covers nearly all gaming PCs.
- FMA3 is not optional for exactness. 53,607 scalar FMADD-family instructions and 2,613 vector FMLA must be single-rounding, and
  `llvm.fma` without FMA hardware becomes a slow libcall.
- Because the code is compiled on the player's PC (bring your own game), the recompiler can target the player's CPU: v3 by default,
  v4 when AVX-512 is present (Zen 4/5), and a slow v2 path otherwise. LAS Recomp simply requires AVX2+FMA.

**Lower through generic LLVM vector IR wherever the semantics match**, because LLVM's x86 backend picks good sequences and the same IR
compiles for arm64 on the Mac test rig (section 5.9):

- Lane arithmetic, logic, compares (producing masks), `select`, and immediate shifts.
- ZIP/UZP/TRN/EXT/REV/DUP/INS as `shufflevector`.
- Saturating add/sub via `llvm.{s,u}{add,sub}.sat`, which become PADDS/PADDUS for 8- and 16-bit lanes.
- `llvm.abs`, `llvm.{s,u}{min,max}`, `llvm.ctpop` (CNT), `llvm.ctlz` (CLZ), `llvm.bitreverse` (RBIT), `llvm.vector.reduce.*`
  (ADDV/UMAXV and so on).
- Widening and narrowing via `sext`/`zext`/`trunc`; saturating narrows as clamp then `trunc`, which becomes PACKSS/PACKUS.

**Hard cases, with fixed helper sequences:**

- **TBL/TBX** (2,704 in main): `pshufb(table, paddusb(idx, 0x70))` zeroes indices ≥ 16 in one extra op. 2- to 4-register tables use
  one PSHUFB per register plus range masks and blends; TBX keeps the destination lanes that were out of range.
- **USHL/SSHL/URSHL** (per-lane signed shift amounts): AVX2's `vpsllv/vpsrlv/vpsrav` cover 32- and 64-bit lanes, with negative and
  ≥ width amounts handled explicitly. 8- and 16-bit lanes need widening. LLVM's generic `shl` is poison for amount ≥ width, so it must
  never be emitted unguarded.
- **SQDMULH/SQRDMULH:** PMULHRSW plus a saturation fixup for the −32768 × −32768 case (16-bit); 32-bit lanes are emulated.
- **LD2/LD3/LD4 and ST*n*** (3 in main, 604 in sdk): loads plus shuffles. These are slow on x86 but rare here.
- **FP vector conversions:** see 3.5. AVX2 has no unsigned conversions; LLVM expands them.
- **Crypto:** AESE = `aesenclast(x ^ k, 0)`; AESMC = `aesenc(aesdeclast(x, 0), 0)`; AESIMC maps directly; PMULL.1Q = PCLMULQDQ.
  The SDK's `nn::crypto` users can also be HLE'd.
- **Dot product and FP16:** none found, so there is no AVX-VNNI dependency (Raptor Lake's AVX-VNNI is unsigned×signed only).

### 3.5 Floating point

| Issue | ARM | x86 | Recommendation |
|---|---|---|---|
| Rounding mode | FPCR.RMode, changed only via MSR | MXCSR.RC | Only sdk writes FPCR (5 MSR sites; main has none). Compile everything assuming the SDK's default FPCR, which must be measured (open question 1). The MSR helper updates MXCSR. A debug assert checks that the assumed FPCR matches at runtime. LLVM's default FP environment is fine; `constrained` intrinsics are needed only for functions found to change modes. |
| Flush-to-zero | FPCR.FZ flushes denormal inputs and outputs | MXCSR FTZ (outputs) plus DAZ (inputs) | If FZ=1, set FTZ\|DAZ on every guest thread. The Win64 ABI keeps MXCSR control bits non-volatile. One known difference, noted in Dynarmic's x64 backend: x64 rounds and then flushes, AArch64 flushes and then rounds, which shows only when a result would round to the smallest normal. |
| Default NaN | 0x7FC00000; with FPCR.DN every NaN result is the default NaN | QNaN indefinite 0xFFC00000 (sign bit set) | The default build does not canonicalise. An `--accurate-nan` build (like Dynarmic's `Unsafe_InaccurateNaN` toggle) canonicalises results per ARM rules, for differential testing. |
| NaN propagation | First SNaN, otherwise first QNaN, in operand order | Usually the first source operand; for `vfmadd231` the first NaN among operands 2, 3, 1 regardless of signalling (per Dynarmic) | As above; the accurate build fixes it. Inf×0 + QNaN in FMADD gives the default NaN on ARM. |
| FMIN/FMAX | NaN-propagating, −0 < +0 | MINPS/MAXPS return the 2nd operand on NaN and ignore the sign of zero | `llvm.minimum/maximum` (4,506 scalar sites in main) |
| FMINNM/FMAXNM | Quiet NaN ignored, −0 < +0 | – | `llvm.minimumnum/maximumnum` (IEEE 754-2019, in LLVM since 2024); sNaN inputs differ, which is negligible |
| FCVTZS/FCVTZU | Saturate, NaN gives 0 | CVTT* gives 0x80000000 | `llvm.fptosi.sat/fptoui.sat` (exact match; 6,474 sites). FCVTN*/FCVTA* = `roundeven`/`round` then `.sat`. FRINT{N,A,M,P,Z,X,I} = `roundeven`/`round`/`floor`/`ceil`/`trunc`/`rint`/`nearbyint` |
| FRECPE/FRSQRTE | Architecturally exact 8-bit estimates (Arm ARM pseudocode `RecipEstimate`/`RecipSqrtEstimate`) | RCPPS/RSQRTPS are about 12-bit and **differ between Intel and AMD** ([rr/Pernosco write-up](https://robert.ocallahan.org/2021/09/emulating-amd-rsqrtss-etc-on-intel.html)) | **Bit-exact emulation:** a 256-entry table plus exponent handling, vectorised with a gather or a PSHUFB split (784 sites in main). Never RCPPS: results would differ between players' machines and from the Switch. ARMeilleure's FastFP path (RCPPS rounded to 8 bits) is a known approximation, and Dynarmic labels such shortcuts `Unsafe_ReducedErrorFP`. |
| FRECPS/FRSQRTS | Fused; special cases (±inf, ±0) give 2.0 / 1.5 | – | `llvm.fma` plus a select for the special case (1,568 sites) |
| FMA | FMADD/FMLA fused | FMA3 | `llvm.fma`. Never add `contract` flags, so a separate FMUL+FADD is never fused. |
| FP16 | – | F16C | None found. If any turns up: F16C for conversions, and arithmetic in float32 then rounded (exact for + − × ÷ √, since 24 ≥ 2·11+2). |
| FPSR flags (IOC…QC) | Sticky | MXCSR flags | Only sdk reads FPSR (8 sites). Compute QC and the exception flags only in functions whose results reach an FPSR read; otherwise drop them. |
| FCMP | NZCV unordered = 0011 | – | `fcmp` to NZCV mapping in the semantics library |

FP accuracy is the main determinism risk for physics and animation. With exact FRECPE, exact FMA and exact min/max/convert, the
remaining differences are NaN payloads and the denormal boundary case. Both can be measured by the accurate build.

### 3.6 Atomics and memory order

| Guest | LLVM | x86 |
|---|---|---|
| CAS{,A,L,AL} (13,676 in main) | `cmpxchg` with matching orderings; Ws = old value | `lock cmpxchg` |
| CASP (0 sites) | `cmpxchg i128` | `lock cmpxchg16b` |
| LDADD/LDCLR/LDEOR/LDSET/LD{S,U}{MAX,MIN} (14,942) | `atomicrmw add/and(~)/xor/or/max/min/umax/umin` | `lock xadd`, or a `lock cmpxchg` loop when the old value is used; `lock and/or/xor` for ST* forms |
| SWP (1,090) | `atomicrmw xchg` | `xchg` |
| LDAPR (4,536) | `load atomic acquire` | `mov` |
| LDAR (91) | `load atomic seq_cst` | `mov` |
| STLR (250) | `store atomic seq_cst` (conservative: an STLR followed by an LDAR must not be reordered, and x86 TSO allows store-to-load reordering) | `xchg` |
| DMB ISH (2,453) / ISHLD (15) / ISHST | `fence seq_cst` / `fence acquire` / `fence release` | `mfence` (or `lock or`) / compiler barrier / compiler barrier |
| LDXR/STXR (5, all in sdk) | Idiom-match the LL/SC loop into one `atomicrmw` or `cmpxchg`; otherwise ARMeilleure-style emulation: a per-thread monitor of (address & ~63, value), STXR as a CAS against the saved value, CLREX and context switch clear it | – |

**The ABA caveat** of CAS-based monitors (A to B to A between LDXR and STXR succeeds where hardware would fail) is accepted by QEMU,
Dynarmic and Ryujinx. With 5 sites it is a non-issue here.

**Plain loads and stores:** x86's TSO is stronger than ARM's model, so plain `mov` is safe *at the hardware level*. The risk is the
compiler. C++ `memory_order_relaxed` atomics compile to plain LDR/STR on AArch64. If the lifter emits ordinary LLVM loads, LLVM may
legally hoist a load out of a spin loop or merge two loads, turning a correct guest spin-wait into an infinite loop. LLVM's own
[atomics guide](https://llvm.org/docs/Atomics.html) states that "Unordered loads/stores can be moved out of a loop", and treats
monotonic accesses as a read plus a write of the location for optimisation purposes.

- **Recommendation:** emit every guest memory access as `volatile` with its proven alignment (align 1 otherwise), as XenonRecomp does
  for exactly this reason.
- This costs little: lifted code's big optimisation wins are in `GuestCpu` register traffic, not in guest memory.
- Later, test-gated relaxations: SP-relative accesses (thread-private) as normal loads, and loads in provably non-looping code.
- [Lasagne](https://pldi22.sigplan.org/details/pldi-2022-pldi/52/Lasagne-A-Static-Binary-Translator-for-Weak-Memory-Model-Architectures)
  (PLDI'22) gives the formal treatment for an LLVM-based static translator (in the opposite, strong-to-weak direction) and skips fences
  for stack accesses, the same exemption.

**Spin loops:** nxnative found guest spin loops that burn host CPU and rewrites them through a gate (`dm_spin_wait`, about 2 ms of
polling, then sleeping). Apply the same rule (a loop path with no store, call, SVC, system-register access or event wait) at lift time,
inserting `_mm_pause` and a backoff helper on the back edge. YIELD and WFE (9 in sdk) become `_mm_pause`/`SwitchToThread`.

### 3.7 System registers, SVCs and HLE

- **TPIDR_EL0** (145 reads in main, written by sdk) and **TPIDRRO_EL0** (430 reads in sdk; points at the 0x200-byte Horizon TLS block
  that the runtime allocates in guest memory per thread) are `GuestCpu` fields. MRS and MSR become plain loads and stores.
- **CNTPCT_EL0** (43 reads, sdk only; no CNTVCT or CNTFRQ reads anywhere) is computed inline: `rdtsc`, then a 64×64 to 128-bit
  multiply and a shift, at the tick rate the SDK assumes (19.2 MHz on Switch 1; Switch 2 is open question 2). Do not use
  QueryPerformanceCounter, which typically runs at 10 MHz (coarser than the guest tick) and costs about 20 ns.
- **CTR_EL0** (2 reads) returns a constant. DC/IC maintenance instructions are no-ops. NZCV MSR (2) is trivial. FPCR and FPSR go
  through helpers (3.5).
- **SVC #imm** (57 sites, all in sdk) becomes `rt_svc(cpu, imm)`, which reads arguments from `cpu->x[]` exactly as nxnative's
  `hle_svc.c` does today.
- **HLE functions:** the sdk exports 23,136 named functions, so HLE binds by mangled name: `nvn*`, `nn::os` sync, fs, hid and so on,
  matching what nxnative hooks (`hle_nvn.c`, checked by `nso_syms.py`).
  - An HLE function has the same `void(GuestCpu*)` signature and is entered from the dispatch table and from statically bound calls.
  - Marshalling follows AAPCS64: integer and pointer arguments in x0–x7, floats and doubles (including HFAs) in v0–v7, structs up to
    16 bytes in register pairs, larger structs by reference, indirect results via x8, and stack arguments read at `cpu->sp`.
  - Generate the marshalling with C++ templates over the host function's own signature, e.g. `HLE(nvnDeviceInitialize)`, rather than
    hand-written wrappers. FEX's thunk generator is the heavier, Clang-tooling version of the same idea.
  - With identity mapping, pointers pass through unchanged.
- **Host-to-guest calls** (thread entry, callbacks, `atexit`, sort comparators passed to HLE code):
  `guest_call(cpu, fn, args…)` sets the argument registers, sets x30 to a `RETURN_TO_HOST` sentinel, calls through the table and
  returns `cpu->x[0]`. HLE code that guest callbacks can unwind through must be built with `/EHsc`.

### 3.8 Scale: compile time, partitioning, gentle load

**Calibration.** On the build PC, clang 23.1.4 compiled a synthetic C file shaped like context-struct lifter output: 1,000 functions ×
70 guest-instruction equivalents with ALU, flag, memory, branch, call, indirect-call and FMA mix, cross-function inlining off. Single
thread, x86-64-v3:

| | Time | Peak RAM | Per guest instruction |
|---|---|---|---|
| -O1 | 5.3 s | – | about 76 µs |
| -O2 | 6.5 s | 316 MB (includes the C front end) | about 93 µs |

Extrapolated to 12.4 M instructions, that is about 19 core-minutes at -O2 for synthetic code. Real lifted IR will be heavier (NEON
expansion, EH, more flags), so plan on **30 to 60 P-core-minutes for a full -O2 build** and about 20 % less at -O1. E-cores run at
roughly 0.6× P-core speed. Allowing cross-function inlining made the same file take more than 10 minutes, so **inlining across guest
functions must stay off** (or limited to proven-tiny leaves) until there is a reason.

Points of comparison: pokopia-recomp reports about 7 minutes of game compilation, and Let's Go Recomp reports 22 minutes on a
20-thread CPU with about 5.2 GB RAM.

**Partitioning:**

- About 1,000 functions per translation unit, ordered by address (kirikae uses 1024 per object), giving about 190 objects.
- Each of the 62 functions over 16 KB (58 in main, 4 in sdk) gets its own TU, at a lower optimisation level above a size threshold,
  so superlinear passes cannot stall the build.
- Partitions are keyed by address range so that a local change rebuilds only its partition.
- Expect objects totalling about 250 MB (the synthetic test produced about 20 bytes of object per guest instruction) and a 150 to
  250 MB DLL.

**Cache:** an object cache keyed by SHA-256 of (partition's guest bytes after relocation, lifter git hash, LLVM version, options,
HLE binding table). A game update moves almost every address, so it triggers one full rebuild; iterations on the lifter or runtime
rebuild only what changed. Iterations on the runtime alone (HLE, renderer) never recompile game code, because game code lives in its
own DLL.

**Gentle mode (the default on the build PC):**

- `--jobs 3`, `BELOW_NORMAL_PRIORITY_CLASS`, and the whole build inside a **Job Object with
  `JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP` at about 40 %**
  ([docs](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_cpu_rate_control_information)).
- Optional affinity to the E-cores (logical CPUs 16–31 on a 13900KF).
- `-O1` for development builds; `-O2` for release.
- Expected wall-clock: about 15 to 30 minutes for a full release build, minutes for incremental ones.
- **Offload:** LLVM is a cross-compiler, so the M4 Mac can produce the x86-64 COFF objects (`--target=x86_64-pc-windows-msvc`), which
  need no Windows SDK. Only the light final link runs on the PC.
- Separately, the stability problem on this CPU is the known Raptor Lake Vmin-shift issue. Intel's fixes are microcode 0x12B and 0x12F
  delivered via BIOS, plus the "Intel Default Settings" profile
  ([Tom's Hardware](https://www.tomshardware.com/pc-components/cpus/raptor-lake-instability-saga-continues-as-intel-releases-0x12f-update-to-fix-vmin-instability)).
  A BIOS update is worth checking, but the build should not depend on it.

### 3.9 Correctness tooling

1. **Decoder cross-check:** decode all 12.5 M words with our decoder and with LLVM's AArch64 MC disassembler, which is linked in
   anyway. Any disagreement fails the build. This takes seconds.
2. **Instruction oracle on the Mac's arm64 chip:**
   - Generate random register and memory states and edge values (NaN, ±0, denormals, ±inf, saturation boundaries, TBL indices ≥ 16,
     all FPCR modes).
   - Run each instruction natively on the M4 through a tiny `MAP_JIT` harness, and through the semantics library compiled natively
     on the Mac. Record the vectors.
   - Replay the vectors on the PC through the lifted x86-64 path: an AOT test binary, or LLVM ORC in-process.
   - The M4 implements everything the A78C binary uses (LSE, LRCPC, PAuth). FRECPE/FRSQRTE are architecturally exact. Keep
     `FPCR.AH = 0` so Apple's alternate FP mode is off.
   - This replaces Unicorn, which nxrecomp and kirikae use, with ground truth.
3. **Swap-in testing on the Mac (the main tool):** because the lifter emits target-independent IR (generic LLVM vectors plus a small
   set of helper intrinsics with arm64 implementations), the *same* lifted IR compiles for `arm64-apple-macos`.
   - nxnative already patches the game's code, so it can redirect any chosen subset of guest functions to their lifted versions
     through small native-to-`GuestCpu` entry and exit thunks.
   - Run the game on the Mac with 1 %, 10 %, then 100 % of functions swapped, and bisect any divergence down to one function.
   - This tests the lifter's semantics, the control-flow model, the EH, longjmp and fiber bridges, and the HLE contract on the
     platform where the oracle lives.
   - x86-specific risk is then confined to LLVM's x86 backend and the handful of x86 helper sequences, which step 2 covers.
4. **Trace diff:** an optional instrumentation pass logs (thread, function, hash of x0–x7 and SP) at function entry. nxnative logs the
   same through its rewriter. Run under `DM_DETCLOCK`-style deterministic time with recorded inputs, then diff to the first divergence.
5. **Per-function micro-execution (later):** run leaf functions (median 84 bytes; many have no calls) on random inputs against mock
   memory, natively and lifted, and compare the write sets.
6. **Runtime checks and forensics:**
   - Debug builds add the RET/return-address check, SP alignment and guard asserts, and an FPCR-assumption assert.
   - Every table miss and interpreter entry is logged and feeds the next build's profile.
   - A vectored exception handler maps host RIP to guest PC (a side table of host function ranges) and walks the guest FP chain and
     FDEs to print a symbolised guest backtrace, as kirikae's `fault.rs` does.
   - Emit CodeView line info whose "source file" is a generated disassembly listing (one line per guest instruction). Visual Studio,
     WinDbg and profilers then show and step AArch64 instructions. [Tracy](https://github.com/wolfpld/tracy) (BSD-3-Clause) handles
     frame profiling.

---

## 4. Vulkan renderers for NVN/Maxwell-class GPUs (permissive)

- **Ryujinx** ([Ryubing](https://git.ryujinx.app/projects/Ryubing), MIT):
  - `Ryujinx.Graphics.Vulkan` is the backend.
  - `Ryujinx.Graphics.Gpu` covers Maxwell 3D, compute, DMA and 2D engines, plus texture and buffer caches.
  - `Ryujinx.Graphics.Shader` is the SASS to SPIR-V/GLSL translator.
  - nxnative already uses that translator to produce **SPIR-V**, so a Windows Vulkan backend can consume the same SPIR-V directly and
    drop the SPIRV-Cross-to-MSL step.
- **kirikae** (MIT/Apache-2.0): Maxwell decoder to SPIR-V, NVN pushbuffer to Vulkan with an on-disk pipeline cache and hash-gated
  uploads. It is starting host-side NVN (HLE NVN over Vulkan), the same direction nxnative took with NVN on Metal.
- **NVIDIA [open-gpu-doc](https://github.com/NVIDIA/open-gpu-doc)** (MIT): official 3D-class method headers from Fermi through
  **Ampere** (`clb197.h` for Maxwell, `clc697.h`/`clc797.h` for Ampere, plus texture/SPH variants), the MME macro-method docs and the
  Shader Program Header. This is primary documentation for Switch 2's Ampere-class GPU.
- **[envytools](https://github.com/envytools/envytools)** (MIT): reverse-engineered NVIDIA ISA and register docs.
- **Mesa NVK/NAK** ([docs](https://docs.mesa3d.org/drivers/nvk.html), MIT): an NVIDIA Vulkan driver whose NAK compiler encodes
  Turing/Ampere-era SASS. Read it to understand Ampere instruction semantics and hardware state; it is not a renderer to embed.
- **[CuAssembler](https://github.com/cloudcores/CuAssembler)** (MIT): Ampere (SM86) SASS encodings.
- GPL renderers (yuzu, Eden, suyu, the nxrecomp front end) are off-limits.

---

## 5. Recommended architecture

### 5.1 Components and data flow

```
user's dump: exefs/{main, sdk, subsdk0, rtld, main.npdm}  (+ romfs)
      │
      ▼
 nxload    NSO+LZ4, MOD0, dynamic, RELA/RELR/JUMP_SLOT/GLOB_DAT, dynsym, npdm;
           rtld-equivalent symbol binding (main → subsdk* → sdk). Shared by recompiler and runtime.
      │
      ▼
 nxanalyze discovery (3.1) → per-function CFG → jump tables → LSDA landing pads, setjmp sites
           → special functions (SP/LR rewriters) → spin loops → FPCR/FPSR users → HLE binding
           → program.db  (+ per-build TOML: hints, HLE overrides, patches; + profiles from runtime and Mac traces)
      │
      ▼
 nxlift    per partition (~1,000 functions): A64 decode → semantics (C++ templates as bitcode) → LLVM IR
  (×N)     → O1/O2 pipeline → COFF x86-64 object   [content-hash cache; gentle job control; optional Mac offload]
      │
      ▼
 lld-link  → game_<buildid>.dll (freestanding: no CRT; imports only from nxrt.exe)
             + tables: function list, landing pads/resume points, guest→host map seed, metadata
      │
      ▼
 nxrt.exe  loader (maps NSO segments at fixed guest VAs; does rtld's relocation work),
           memory (placeholders, sections, aliasing), threads (host thread per guest thread),
           fibers (Windows fibers), SVC layer + HLE services (ported from nxnative dm/host),
           dispatch table + fallback interpreter (same semantics library), EH/longjmp bridge,
           timer, VEH crash handler, NVN→Vulkan, audio, input
```

`rtld` is not recompiled; nxload and nxrt do its job.

### 5.2 Language and LLVM

- **C++20** for nxload, nxanalyze, nxlift and nxrt. Reasons:
  - LLVM's C++ API is the complete one.
  - The semantics library must compile to bitcode (Clang), natively on Windows (interpreter) and natively on macOS arm64 (oracle and
    swap-in tests).
  - The nxnative host to be ported is C.
- Python is only for offline tools; the measurements in section 1 were taken with a few small numpy scripts.
- **LLVM 23.1.x**, pinned (LLVM 23.1.0 shipped 2026-08-25; [Phoronix](https://phoronix.com/news/LLVM-23.1-Released)); bump once a
  year at most. Link LLVM statically into the recompiler: Apache-2.0 WITH LLVM-exception allows redistribution, so players get a
  single `nxrecomp.exe` plus `lld-link.exe`.
- Decoder: generate it from a bit-pattern table seeded by Dynarmic's `a64.inc` (0BSD, which already lists the v8.1+ patterns as
  commented entries). Check it against LLVM MC in CI (3.9 step 1).
- Emitting C/C++ instead of IR (N64Recomp, XenonRecomp, LAS Recomp, nxrecomp) remains a valid fallback design. IR is preferred for
  direct control of `musttail`, `invoke`, atomics, `noalias`, debug locations, preserve_none experiments, and for reusing the lifter in
  an ORC JIT for tests.

### 5.3 Runtime ABI (game DLL ↔ nxrt)

```cpp
// abi.h — shared by lifter (as an LLVM struct type) and runtime. Field offsets are the ABI; bump ABI_VERSION on change.
struct alignas(64) GuestCpu {
    uint64_t x[31];                 // X0..X30
    uint64_t sp;
    uint64_t pc;                    // valid only when control is in the runtime
    uint8_t  n, z, c, v;            // NZCV unpacked (SSA-friendly)
    uint32_t fpcr, fpsr;
    alignas(16) uint8_t q[32][16];  // V0..V31
    uint64_t tpidr_el0, tpidrro_el0;
    uint64_t excl_addr; alignas(16) uint8_t excl_val[16];   // the 5 LL/SC sites
    uint64_t unwind_pc, unwind_sp;  // pending non-local transfer (EH resume / longjmp)
    struct HostThread* thread;      // runtime-private from here on
};
using GuestFn = void (*)(GuestCpu* __restrict);              // every translated and every HLE function

// Imported by the game DLL from nxrt.exe
extern "C" GuestFn  rt_miss(GuestCpu*, uint64_t target);     // table miss: HLE stub, interpreter trampoline, or fatal
extern "C" void     rt_svc(GuestCpu*, uint32_t imm);
extern "C" uint64_t rt_mrs(GuestCpu*, uint32_t sysreg);      // rare registers; hot ones are inline
extern "C" void     rt_msr(GuestCpu*, uint32_t sysreg, uint64_t v);
extern "C" [[noreturn]] void rt_nonlocal(GuestCpu*);         // throws GuestNonLocal{unwind_pc, unwind_sp}
extern "C" [[noreturn]] void rt_trap(GuestCpu*, uint64_t pc, uint32_t why);  // UDF/BRK/unreachable/undecodable
extern "C" const uint32_t* rt_code_table;                    // guest word index → host RVA (0 = miss)
extern "C" uint64_t rt_code_base, rt_code_words;
// Exported by the game DLL: function list (guest VA, RVA, flags), resume-point list, build id, assumed FPCR, ABI_VERSION.
```

Conventions:

- **Calling convention:** default Win64 C convention to start with. Then measure LLVM's `preserve_none` (x86-64 and AArch64; Win64
  support must be verified) for guest-to-guest calls, which removes callee-save spills in every translated function.
- **Register caching:** inside a function, guest registers are allocas promoted to SSA. They are loaded lazily and stored back only
  when dirty, at every exit: call, tail call, return, SVC, helper, or `invoke`.
- **"Trust AAPCS64" option:** keep x19–x29, SP and d8–d15 in SSA across calls instead of reloading them. Enable it per function only
  after differential tests pass. It is the analogue of XenonRecomp's `non_volatile_as_local`.
- **Flags:** NZCV as four `i1` SSA values, materialised only when live out.
- **Guest memory:** `volatile` loads and stores through `inttoptr(guest_va)` (identity mapping); LSE as atomics (3.6).
- **Debug info:** a `DILocation` per guest instruction (3.9 step 6).

### 5.4 Indirect-branch strategy

**Fast path, inline at each of the about 150k BLR sites:**

```
i = (target - rt_code_base) >> 2;  f = (i < rt_code_words) ? rt_code_table[i] : 0;
fn = f ? (GuestFn)(dll_base + f) : rt_miss(cpu, target);  fn(cpu);   // or musttail for BR tail calls
```

That is about 5 instructions plus a BTB-predicted indirect call.

**The table:** one 4-byte RVA per guest instruction slot across all modules. That is 12.5 M slots, about 50 MB reserved, filled at
startup from the DLL's function list (188k entries) and committed only where touched. A dense table is needed because 59k functions in
main are only 4-byte aligned. XenonRecomp uses the same idea with 8-byte entries. If memory matters, a rank-indexed bitmap plus values
(about 2 MB) or a minimal perfect hash (about 1.5 MB) are drop-in replacements at a few extra instructions.

**Misses:** `rt_miss` checks, in order:

1. HLE stubs and the `RETURN_TO_HOST` sentinel.
2. Any address inside a loaded module: the **fallback interpreter**, built from the same semantics library, runs until the guest
   function returns. BLs to translated functions call straight back into the DLL. The address is appended to `profile.txt` and becomes
   a function in the next build (kirikae's loop).
3. Otherwise fatal, with a guest backtrace.

**Optional later:** profile-guided devirtualisation (`if (t == hot_target) direct_call; else lookup`) on hot virtual call sites.

### 5.5 Memory strategy

Covered in 3.3. In short:

- Identity mapping. `/HIGHENTROPYVA:NO`, plus TLS-callback placeholder reservation of the module, heap, alias, stack and TLS regions
  below 2^39 at build-constant bases (modules at `0x71_0000_0000`).
- Pagefile-backed sections for aliasable RAM, 4 KB-granular placeholder views, lazy commit.
- Guest `.text` read-only and non-executable.
- An optional launcher with suspended-child reservation for robustness.

### 5.6 Build pipeline

```
nxrecomp build --dump <dir> --out <dir> [--opt 2] [--jobs 3] [--cpu-cap 40] [--affinity ecores] [--profile profile.txt]
  1. verify build ids (main/sdk/subsdk0) against the per-build profile TOML; refuse unknown builds
  2. analyze  (single thread, ~1 min)            → program.db + report (unresolved items fail the build)
  3. lift+opt+codegen partitions (gentle, cached) → obj/*.obj
  4. lld-link /DLL /NODEFAULTLIB /NOENTRY        → game_<buildid>.dll  (+ optional PDB for dev builds)
  5. write manifest (ABI version, LLVM version, options, hashes)
```

- Developer flow: edits to nxrt or HLE never trigger steps 2 to 4.
- Player flow: the installer runs this once per game build on the player's PC, targeting the player's CPU (3.4). The dump is never
  redistributed, which matches how pokopia-recomp and LAS Recomp ship.
- Provide `memcpy`, `memset` and `__chkstk` to the freestanding DLL from nxrt, or define them in it.

### 5.7 Phased plan (indicative)

1. **nxload + nxanalyze + decoder cross-check.** Reproduce section 1's numbers. Zero unresolved BL/B; jump-table resolution rate
   reported; special-function list reviewed.
2. **Integer and control-flow lifting + interpreter + Mac swap-in harness.** sdk init through `nnMain` with a headless nxrt on
   Windows; first swap-in runs on the Mac.
3. **NEON/FP (exact paths), EH/longjmp/fiber bridges, the HLE marshalling layer, porting nxnative's host.** Boot to title with a null
   renderer; 100 % swap-in on the Mac matches native.
4. **Vulkan NVN backend; performance work** (trust-AAPCS64, preserve_none, devirtualisation, relaxing volatile on stack accesses),
   each change gated by the differential tests.

---

## 6. Main risks

1. **Legal and clean-room exposure.**
   - A GPL project already recompiles this exact game. Keep a written provenance log and never open nxrecomp, pokopia-recomp, yuzu,
     Eden, suyu or Unicorn sources.
   - Game code must never be redistributed: compile on the player's PC.
   - The climate is hostile: Ryujinx's GitHub is DMCA-blocked, and press reports a further DMCA wave in August 2026.
2. **Non-local control flow** (EH, longjmp, fibers, the sdk's context-switch primitives) is the most intricate part. A bug shows up as
   rare crashes far from the cause. Mitigation: the special-function inventory, the unified `GuestNonLocal` mechanism, and swap-in tests.
3. **Floating-point determinism:** wrong FRECPE, min/max or FMA behaviour causes divergence in physics and animation that is hard to
   attribute. Mitigation: the exact paths, the `--accurate-nan` build, and the hardware oracle.
4. **Compiler-level memory-model bugs** if guest accesses are ever made non-volatile without proof, for example hoisted relaxed-atomic
   spin loops.
5. **Performance:** `GuestCpu` traffic at the 428k call sites in main, 137k virtual calls through the table, and `volatile` limiting
   optimisation. Expect a slower starting point than native, recovered by the measured optimisations in 5.3 and 5.4.
6. **Compile time and stability of the build PC:** mitigated by gentle mode, caching and Mac offload. Players with 4-core PCs may wait
   20 minutes or more on first install.
7. **Address-space collisions:** mitigated by `/HIGHENTROPYVA:NO`, early reservation and the launcher.
8. **Coverage gaps** (code reached only through computed addresses): mitigated by the interpreter fallback and profiles. Must be
   measured.
9. **Game updates:** a new build means a full rebuild and fresh per-build profiles and patches; HLE bindings by name survive.
10. **Scope:** the renderer (NVN2 on Vulkan, Ampere shaders) is likely the larger project and is not covered here.

---

## 7. Open questions

1. **Which FPCR does the SDK install for game threads (FZ, DN, RMode)?** Read the 5 `MSR FPCR` sites in sdk, or log FPCR on the Mac
   inside nxnative. This decides MXCSR setup and the static FP assumptions.
2. **Which tick frequency does the SDK assume for CNTPCT on Switch 2?** There are no CNTFRQ reads, so it is a constant in sdk
   (nn::os). What does nxnative return on the Mac?
3. **How often are exceptions thrown at runtime?** Count `__cxa_throw` calls on the Mac. Are the 3,962 LSDA functions mostly
   cleanup-only? Where are setjmp/longjmp used in main?
4. **The complete list of sdk functions that rewrite SP or x30** (jumpto, longjmp, fiber switch, thread entry), and whether main or
   subsdk0 contain any hand-written context switches.
5. **Static coverage:** what fraction of executed function entries (from a long nxnative trace on the Mac) is outside the discovered
   set? Target 0.
6. **Jump-table resolution rate** with the pattern matcher alone. `04-binary-census.md` finds 6,346 jump tables in main and 697 in
   sdk. What are main's 5,712 "other" BRs: indirect tail calls, or patterns the census missed?
7. **HLE boundary per subsystem:** which sdk subsystems are HLE'd at function level (NVN, nn::os sync, fs, hid, audio) and which are
   recompiled with SVC-level HLE, mirroring what nxnative does today.
8. **Does any code depend on 39-bit pointers, tagged top bytes or specific region bases?** Test on the Mac by moving nxnative's heap
   base.
9. **Aliasing:** true page aliasing from day one, or port nxnative's copy-based `MapMemory` first?
10. **Build distribution:** ship clang/lld inside the installer (about 100 to 150 MB), or link LLVM statically into one
    `nxrecomp.exe`? Is an AVX2-less fallback build worth supporting?
11. **Performance budget:** what frame-time target on what PC, and is the trust-AAPCS64 optimisation needed to meet it?
12. **kirikae as a base:** is there value in reusing parts of it (Rust, Cranelift, permissive) rather than only borrowing its ideas?
    The LLVM-based design above is recommended, but kirikae's loader and service code may save time.

---

## 8. Sources

Recompilers and lifters:
- N64Recomp (MIT): https://github.com/N64Recomp/N64Recomp
- XenonRecomp (MIT): https://github.com/hedge-dev/XenonRecomp
- ReXGlue SDK (BSD-3-Clause): https://github.com/rexglue/rexglue-sdk, [Codegen pipeline](https://github.com/rexglue/rexglue-sdk/wiki/Codegen-Pipeline-Overview), [Memory](https://github.com/rexglue/rexglue-sdk/wiki/Memory)
- Remill (Apache-2.0): https://github.com/lifting-bits/remill, [Life of an instruction](https://github.com/lifting-bits/remill/blob/master/docs/LIFE_OF_AN_INSTRUCTION.md)
- McSema (AGPL-3.0, archived): https://github.com/lifting-bits/mcsema
- Rellume (LGPL-2.1+): https://github.com/aengelke/rellume; Instrew (LGPL-2.1): https://github.com/aengelke/instrew; Engelke & Schulz, *Instrew: Leveraging LLVM for High Performance Dynamic Binary Instrumentation*, VEE 2020
- Dynarmic (0BSD): https://github.com/lioncash/dynarmic (archived), [optimization_flags.h](https://github.com/lioncash/dynarmic/blob/master/src/dynarmic/interface/optimization_flags.h), [emit_x64_floating_point.cpp](https://github.com/lioncash/dynarmic/blob/master/src/dynarmic/backend/x64/emit_x64_floating_point.cpp), [A64 decoder table](https://github.com/lioncash/dynarmic/blob/master/src/dynarmic/frontend/A64/decoder/a64.inc); forks https://github.com/azahar-emu/dynarmic, https://github.com/Vita3K/dynarmic
- Ryujinx / Ryubing (MIT): https://git.ryujinx.app/projects/Ryubing (`src/ARMeilleure/Instructions/InstEmitMemoryExHelper.cs`, `InstEmitSimdArithmetic.cs`, `Decoders/OpCodeTable.cs`, `Instructions/SoftFloat/`); DMCA notice: https://github.com/github/dmca/blob/master/2025/03/2025-03-06-nintendo.md
- FEX (MIT): https://github.com/FEX-Emu/FEX, [ThunkLibs](https://github.com/FEX-Emu/FEX/blob/main/ThunkLibs/README.md), [ProgrammingConcerns.md](https://github.com/FEX-Emu/FEX/blob/main/docs/ProgrammingConcerns.md)
- Unicorn (GPL-2.0): https://github.com/unicorn-engine/unicorn
- kirikae (MIT OR Apache-2.0): https://github.com/Cor3Duo/kirikae, [ARCHITECTURE.md](https://github.com/Cor3Duo/kirikae/blob/main/ARCHITECTURE.md)
- LAS Recomp (MIT per README): https://github.com/peiikai/LASRecomp; letsgo-recomp (MIT per README): https://github.com/raulzrrs/letsgo-recomp
- nxrecomp (GPL-3.0, README only): https://github.com/drakolordx7/nxrecomp; pokopia-recomp (GPL-3.0, README only): https://github.com/drakolordx7/pokopia-recomp
- SwitchRecomp (GPL-3.0, README only): https://github.com/kaiyao7/SwitchRecomp
- suyu recompiler mode (GPL-3.0, news only): https://www.digitalcitizen.life/suyu-0-0-4-adds-static-recompilation-to-improve-nintendo-switch-emulation-on-pc/
- nxnative (MIT): https://github.com/seedds/nxnative, [docs/development.md](https://github.com/seedds/nxnative/blob/main/docs/development.md)
- llvm-mctoll (archived): https://github.com/microsoft/llvm-mctoll; RetDec (MIT): https://github.com/avast/retdec; Ghidra (Apache-2.0): https://github.com/NationalSecurityAgency/ghidra; Ghidra-Switch-Loader (ISC): https://github.com/Adubbz/Ghidra-Switch-Loader; Capstone (BSD-3-Clause): https://github.com/capstone-engine/capstone

Specifications, platform and compiler docs:
- Switchbrew NSO format: https://switchbrew.org/wiki/NSO
- Arm Architecture Reference Manual (A-profile; `RecipEstimate`, `FPProcessNaNs`, `FPDefaultNaN`): https://developer.arm.com/documentation/ddi0487/latest
- sail-arm (BSD-3-Clause-Clear): https://github.com/rems-project/sail-arm; isa-tools / ASLi (BSD-3-Clause): https://github.com/alastairreid/isa-tools
- Intel SDM (RCPPS precision, QNaN floating-point indefinite): https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- RCPPS/RSQRTPS differ between Intel and AMD: https://robert.ocallahan.org/2021/09/emulating-amd-rsqrtss-etc-on-intel.html
- VirtualAlloc2: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2; MapViewOfFile3: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3
- Windows ASLR entropy (MSRC): https://msrc.microsoft.com/blog/2013/12/software-defense-mitigating-common-exploitation-techniques
- Job Object CPU rate control: https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_cpu_rate_control_information
- LLVM atomics guide: https://llvm.org/docs/Atomics.html; LangRef (`preserve_nonecc`, `musttail`, `llvm.fptosi.sat`, `llvm.minimum`): https://llvm.org/docs/LangRef.html; `minimumnum`/`maximumnum`: https://github.com/llvm/llvm-project/commit/fb9e685fc41b
- LLVM 23.1 release: https://phoronix.com/news/LLVM-23.1-Released
- Lasagne (PLDI 2022, DOI 10.1145/3519939.3523719): https://pldi22.sigplan.org/details/pldi-2022-pldi/52/Lasagne-A-Static-Binary-Translator-for-Weak-Memory-Model-Architectures
- Raptor Lake Vmin shift microcode: https://www.tomshardware.com/pc-components/cpus/raptor-lake-instability-saga-continues-as-intel-releases-0x12f-update-to-fix-vmin-instability

GPU references:
- NVIDIA open-gpu-doc (MIT): https://github.com/NVIDIA/open-gpu-doc
- envytools (MIT): https://github.com/envytools/envytools
- Mesa NVK (MIT): https://docs.mesa3d.org/drivers/nvk.html
- CuAssembler (MIT): https://github.com/cloudcores/CuAssembler
- Tracy (BSD-3-Clause): https://github.com/wolfpld/tracy
