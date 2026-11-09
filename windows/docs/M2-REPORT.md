# Milestone 2 report: foundations

2026-11-03. M2's goal (DESIGN §6): our own tools load and analyse the game's code with no gaps, and the design's open questions are
answered. Done, with every acceptance check met. Details per component: [m2-nxload.md](m2-nxload.md), [m2-a64.md](m2-a64.md),
[m2-nxanalyze.md](m2-nxanalyze.md), [../research/05-mac-traces.md](../research/05-mac-traces.md).

## What exists now

| Component | Commit | What it does | Checked by |
|---|---|---|---|
| Build | `cbd1541` | clang 23.1 with Visual Studio's C++ library, CMake and Ninja; 4 jobs at low priority by default | every build since |
| `nxload` | `6f8987d` | NSO with our own LZ4 decoder, MOD0, dynamic section, symbols, RELA/RELR/PLT, `.eh_frame`, npdm and the SVC mask; links and relocates the four modules at fixed bases in 0.42 s | reproduces every census number on 2.0.1; loads 1.0.5 |
| `a64` | `ca2b1ee` | AArch64 decoder generated from one table (765 encodings, 63 aliases), about 7 ns per word, with typed fields and an LLVM-style printer | **all 12,505,492 words of 2.0.1 and all of 1.0.5's main identical to llvm-objdump 23.1.4**; about 60M synthetic words too |
| Mac traces | `7ff0873` | instrumented copy of nxnative running the real game on the M4 | three sessions from boot to gameplay |
| `nxanalyze` | `608f4af` | functions, CFGs, jump tables, landing pads, special functions, spin loops, HLE binding → `program.db` in 2.5 s | 0 unresolved direct branches; 100 % of the Mac's executed functions found |

About 11,000 lines of C++20 in all, with 6 test suites (synthetic data, plus dump and trace checks that skip without the files).
Nothing from the game is in the repository: everything derived from it stays in `_local\`.

## Numbers that matter for M3

- **Functions (2.0.1):** main 149,688, sdk 47,994, subsdk0 2,732. They come from FDEs plus BL and tail targets, relocations,
  exports and PLT stubs. Only 927 functions in main have neither an FDE nor a PLT stub (the bundled libcurl), found through BL
  targets and a prologue scan.
- **Jump tables:** all 6,172 relative tables in main are resolved: 5,999 with a proven bound, 55 by a mask, and 118 read up to the
  next table. Those 118 need a trapping default in the lifter. There are also 43 absolute tables of function pointers.
- **Indirect branches:** every remaining BR in main (5,843) is an indirect tail call, 5,414 of them through vtables, plus 136k
  indirect calls. All need the D4 lookup table, and nothing jumps into the middle of a function.
- **Landing pads:** main has 8,934 in 3,388 functions and sdk 1,522. No catch clause names a type, and 3,537 of main's 3,854
  catch-all pads are clang's `noexcept` terminate pads.
- **Special functions:** only four in the whole program rewrite SP or x30, all in sdk and all replaced by host code (DESIGN §8.4).
- **Spin loops:** main has 95, sdk 10, subsdk0 6. They become host yields.
- **Host calls:** 332 imports of main go to nxnative's host replacements (1,233 call sites), and 1,078 indirect calls reach host code
  through slots filled at run time.
- **Runtime facts from the Mac's native run:** FPCR is 0, the counter runs at 31.25 MHz, and no exception, setjmp or longjmp runs in normal
  play (DESIGN §8).
- **Both versions:** 1.0.5 and 2.0.1 share identical sdk, subsdk0 and rtld; only main differs (10.05M against 10.30M instructions).
  Both pass every check.

## What changed in the plan

- **D5 (exceptions, longjmp, fibers) moves off the critical path.** None of them runs in normal play. M3 and M4 start with "report
  and stop" hooks, and the four sdk context functions are replaced by host code, as nxnative already does on the Mac.
- **D8 gains an item:** guest-visible addresses for host functions, so the 1,078 run-time-filled indirect calls (NVN entry points
  and others) can reach the generated adapters through the D4 table.
- **The lifter's jump-table rule:** decoded tables become `switch`; the 118 tables bounded only by the next table get a trapping
  default that logs, until the data flow can prove their size.

## Found along the way

- **A bug in nxnative, the Mac and iPhone build:** its rewriter's spin-loop mask also matches LDAPR, so 18 spin loops in main that
  poll with LDAPR never get a yield. That can waste a core while the game waits, which matters most on iPhone. It's a small fix for
  pokopia-native, separate from this project.
- **Binding rule (nxload):** under the standard rule, 60 of sdk's own exports bind to copies in main (C++ type information,
  `__cxa_pure_virtual`). That is the same rule nxnative uses on the Mac, but worth a trace check in M3.

## Still open for M3

1. Do any pointers depend on 39-bit addresses or fixed region bases? (Move nxnative's heap base on the Mac.)
2. Should a forced host replacement also redirect direct calls inside sdk itself? This is policy, decided while writing the
   adapters.
3. libcurl's function boundaries are inferred, because the Mac never ran it. It gets the interpreter fallback if anything is missed.

## M3: the CPU core (next)

Goal: the game's own code runs on Windows up to its first frame request, with no graphics yet.

1. **Semantics library and lifter.** Instruction semantics in C++ templates, compiled to LLVM bitcode, lifted per function from
   `program.db` into LLVM IR, then optimised and compiled to x86-64 objects in partitions with a cache. Integer, branches, memory and
   LSE first; NEON and FP with the exact paths next.
2. **Fallback interpreter** from the same semantics, for table misses.
3. **Mac swap-in harness:** the same lifted code compiled for arm64 and swapped into nxnative function by function, so every
   function is checked against the real one.
4. **Headless runtime on Windows:** nxload's images mapped at the game's addresses, nxnative's host layer (SVCs, threads, HLE)
   ported to Win32, a null renderer, and the generated adapters for host calls.
5. **Exit:** sdk's init runs, `nnMain` starts, and the game requests its first frame.

**M3 needs LLVM's development libraries,** which the installed LLVM lacks: only the tools are installed, not the C++ API the lifter
links against. The official build is `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz` from the llvm-project GitHub releases
(roughly 1 GB). It matches the installed 23.1.4 and avoids building LLVM from source, which would be a multi-hour all-core job on
this PC. The Mac needs the arm64 macOS build of the same release for the swap-in harness.
