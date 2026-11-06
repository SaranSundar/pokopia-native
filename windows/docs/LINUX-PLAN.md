# Pokopia Native for Linux: plan

Draft 1, 2026-11-04, for review. Target: **pokopia-native 1.2.0** with a "for Linux" download beside the Windows and Mac ones, for desktop Linux
x86-64 and the **Steam Deck** (SteamOS 3, AMD RADV). Branch `linux` in the worktrees `windows-native-linux` and `installer-linux`.

## 1. Why this is mostly a platform port

The Windows build already has everything that is hard: the recompiler (nxload, nxanalyze, nxlift), the runtime (nxrt: HLE, Vulkan replay, SDL3
front end), run-time shaders (nxshader) and Link Play (src/coop). What ties it to Windows is small and well fenced (measured on `6f845f6`):

| Area | Windows today | Linux |
|---|---|---|
| The game binary | nxlift emits COFF objects, `lld-link` makes `game.dll` importing from `nxrt.exe`; a table of `fn - __ImageBase` RVAs | ELF objects (PIC), `ld.lld -shared` makes `game.so`; every lifted function hidden (direct calls, no PLT); the table entries self-relative (§3 L-D2) |
| The support routines in the game binary | `__chkstk`, `memcpy`/`memmove`/`memset` in the Microsoft x64 convention | the same three in the System V convention; no `__chkstk` |
| Host-call token thunks (`rt.c`) | `mov edx, k; jmp common` (2nd argument in rdx) | `mov esi, k; jmp common` (rsi), placed with `mmap` within 4 GB above `game.so` |
| POSIX subset for the ported nxnative files | `plat/posix_win.h`, `posix_fs_win.h` over Win32 | the real headers (nxnative was POSIX to begin with) |
| `plat_*` (clock, sleep, threads, priorities, CPU and OS info, entropy) | `plat_win32.c` | a new `plat_linux.c` (`clock_gettime`, `clock_nanosleep`, pthreads, `setpriority`, `/proc/cpuinfo`, `getrandom`) |
| Guest address space (`addrspace.c`) | `VirtualAlloc2` placeholders at fixed addresses, a pagefile-backed section for alias/stack/heap | `mmap(MAP_FIXED_NOREPLACE)` at the same addresses, a sparse `memfd` for alias/stack/heap mapped `MAP_SHARED` (real aliases, as on Windows) |
| Faults (`crash.c`, `nvn_dirty.c`, the heap's lazy commit) | vectored exception handler, `VirtualProtect` | `sigaction(SIGSEGV/SIGBUS, SA_SIGINFO | SA_ONSTACK)`, the write bit from the trap's error code, `mprotect` |
| `__try` / `__except` around guest reads (6 places in vk/ and audio) | SEH | a small `PLAT_TRY` / `PLAT_EXCEPT` pair: SEH on Windows, a per-thread `sigsetjmp` the fault handler returns to on Linux |
| Loading the game | `LoadLibrary`, `GetProcAddress` | `dlopen(RTLD_NOW)`, `dlsym`; nxrt exports the `rt_*` ABI with `--export-dynamic-symbol` |
| Crash report, profiler symbols | `RtlVirtualUnwind`, DbgHelp, `SuspendThread` | frame-pointer walk + `dladdr`; the profiler's sampler later (diagnostic only) |
| Saves (`save_txn.c`) | `MoveFileEx` + `FlushFileBuffers` | `rename` + `fsync` on file and folder (nxnative's own idea, without the APFS clone) |
| Child processes (`nxs_rt.c`: on-demand shaders; `nxlift`: the link) | `CreateProcessW` below normal priority | `posix_spawn` + `setpriority` |
| Hashes and entropy (`sha256.cpp`, `hle_nvn.c`, `hle_os.c`) | BCrypt | a portable SHA-256 (nxload has the FIPS 180-4 vectors), `getrandom` |
| Vulkan | `vulkan-1.dll`, `VK_USE_PLATFORM_WIN32_KHR` | `libvulkan.so.1`, the surface from SDL3 (X11, Wayland, Gamescope) |
| SDL3 | `SDL3.dll` (official VC package) | `libSDL3.so.0` built from the official 3.4.18 source release (zlib licence), shipped beside nxrt (`$ORIGIN` rpath) |
| Folders | `%APPDATA%\Pokopia Native\<version>\save`, `%LOCALAPPDATA%` | `$XDG_DATA_HOME/pokopia-native/<version>/save`, `$XDG_CACHE_HOME/pokopia-native/<version>` |
| Link Play discovery | Windows DNS-SD (`DnsServiceRegister/Browse`) | none at first: **join by address** (and internet play, already portable); Avahi later |
| Launcher | `play.ps1` + its Link Play window (WinForms) | `play.sh` (bash; `play.json` read with python3) + a Link Play choice with `zenity`/`kdialog` when present, else switches |
| The builder | `nxs2.py build --windows` | `nxs2.py build --linux`: the same stages, Linux tools from the kit |

The game's own code and the HLE layer do not change: lifted functions stay `void fn(GuestCpu *)` in the host's own C convention on both systems,
and the HLE adapters are generated C, compiled for the host. `long` is 64-bit on Linux, as nxnative was written for.

## 2. Milestones

Each milestone says what it proves. I report at each one and wait for an OK before the next big step.

| # | Milestone | Done when | Proves |
|---|---|---|---|
| L0 | Plan and environment | this plan approved; WSL2 set up by the user (§4); the toolchain unpacked in WSL | we can build and test Linux on this PC without admin rights on our side |
| L1 | The tools on Linux | nxload, a64, nxanalyze, sem, interp, s2tex, coop, nxlift build in WSL with LLVM 23.1.4's Linux release; their tests pass; `program.db` from Linux nxanalyze equals the Windows one byte for byte | the recompiler side is portable |
| L2 | **The game as a Linux shared object** | `nxlift --so game.so` (ELF, PIC, hidden lifted functions, self-relative table) for 2.0.1; the same function count as Windows' `game.dll` (200,414); its undefined symbols are exactly the `rt_*` ABI plus the support routines; a stand-in executable `dlopen`s it and resolves the table | the recompile produces a Linux shared object |
| L3 | nxrt headless on Linux | `plat_linux.c`, `addrspace` on `mmap`/`memfd`, signals, `PLAT_TRY`, saves, `dlopen`, SysV thunks; nxrt's unit tests pass (addrspace, svc, threads, adapters, gamedll, boot order, nvn, audio, save, nvn_rec, save_rt, coop); `nxrt --gfx null` runs 2.0.1 from sdk init to the title's logic, as M3/M4 did on Windows | the game's own code runs on Linux |
| L4 | **The title on software Vulkan** | the Vulkan backend on `libvulkan.so.1` with an SDL3 window under WSLg; nxshader through .NET on Linux and ampere-lower built for Linux; Mesa lavapipe draws the title; a fixed-clock screenshot (`DM_FIXED_TIME`) compared with Windows' (PSNR, as M5-C did with the Mac) | the renderer works on a second, non-NVIDIA Vulkan driver |
| L5 | Playable in WSL, and the tester beta | input (SDL3), sound (PipeWire/PulseAudio through WSLg), saves in and out; the field reached on lavapipe (slowly); `nxs2.py setup/build --linux`, `play.sh`, the kit; **the beta zip, the tester guide and the `report` script** | the whole build-from-dump flow on Linux, ready for Steam Deck volunteers |
| L6 | Steam Deck, through volunteers | testers' reports and recordings: build time, title, field, speed on RADV, controls through Steam Input, Desktop Mode and Game Mode (a non-Steam game), 1280×800; fixes | real AMD hardware and SteamOS |
| L7 | Release 1.2.0 | "for Linux" zip; README "Which download do I need?" with the Linux and Steam Deck rows; release notes; source snapshot; the user's release-candidate test | the release |

This session's scope is L0 to L5 (the beta in the user's hands). L6 depends on volunteers; L7 is the user's release.

## 3. Decisions

**L-D1 · One tree.** Linux lives in this repository beside Windows; platform code goes into files of its own (`plat_linux.c`, the Linux half of
`addrspace.c`, `crash.c`, `rt.c`'s thunks) instead of `#ifdef`s through the HLE. The Windows build must stay byte-identical in behaviour: every
step re-runs the Windows ctest before it is committed.

**L-D2 · `game.so`.** ELF shared object, small code model, PIC. Every lifted function is hidden and `dso_local`, so calls between them are direct
(`R_X86_64_PLT32` resolved at link time, no PLT, no symbol interposition); only `nxw_game_*` is exported. `ld.lld -shared -z now -z noexecstack
--hash-style=gnu -nostdlib`, undefined symbols allowed only for the `rt_*` ABI, which nxrt's executable exports. ELF cannot express
`fn - image base` in a 32-bit field, so a Linux table entry holds `fn - &entry.rva` (`R_X86_64_PC32`) and sets `flags` bit 0
(`NXW_GAME_FN_SELFREL`); nxrt converts it to the image-relative RVA the indirect-branch table already uses. `NXW_ABI_VERSION` is unchanged for
Windows.

**L-D3 · The address space.** The same fixed addresses as Windows (tokens `0x70FFF00000`, modules `0x7100000000`, the guest block
`0x7200000000` + `mem_reserve`): Linux puts the executable near `0x5555…`, libraries and `mmap` near `0x7f…` (128 TB), so 450 GB is normally free.
Reserved `PROT_NONE` with `MAP_FIXED_NOREPLACE` in a constructor that runs before `main`; a clear message if anything is in the way. Alias, stack and
heap are one sparse `memfd` (no memory used until written) mapped `MAP_SHARED` at fixed offsets, so `svcMapMemory` makes a real alias as on Windows;
commit is `mprotect`, decommit `fallocate(PUNCH_HOLE)`.

**L-D4 · Faults.** One `SIGSEGV`/`SIGBUS` handler on an alternate signal stack per thread runs the existing fault-hook chain (dirty tracking, lazy
commit), then a `PLAT_TRY` recovery point if the thread has one, then the crash report. The write bit comes from the page-fault error code
(`REG_ERR` bit 1).

**L-D5 · Threads and timing.** pthreads with the stack size nxrt asks for; Horizon priorities map to `nice` values per thread, only ever lower than
the process (never raised; unprivileged anyway); sleep with `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` and a small timer slack.

**L-D6 · Shaders on Linux.** The translator is the same .NET IL the Mac kit carries (`host/dmsh_xlate_bin/dmsh_xlate.dll`), run with `dotnet`;
ampere-lower is built with its own `build.sh` (as on the Mac) for the kit, so players need no compiler. SPIRV-Tools (`spirv-opt`, `spirv-val`) come
from the Vulkan SDK for Linux. *Decision for the user (later):* ship a self-contained .NET translator (≈70 MB, no .NET install, friendlier on a
Steam Deck) or ask players to install the .NET 10 runtime with Microsoft's `dotnet-install.sh` (no root needed).

**L-D7 · Portable binaries.** The kit's tools are built on **Ubuntu 22.04 (glibc 2.35)** so they run on SteamOS 3, Debian 12, Ubuntu 22.04+, Fedora
and Arch; libstdc++ and libgcc linked statically; SDL3 loads X11/Wayland/PipeWire/PulseAudio at run time as it always does; the Vulkan loader is the
system's. Minimum CPU stays x86-64-v3 (the Deck's Zen 2 has AVX2 and FMA3).

**L-D8 · Folders and saves.** XDG folders as in §1; the save layout and the commit protocol are the Windows ones, so saves cross between Windows,
Linux, Mac and iPhone unchanged; `nxs2.py import-save` works the same.

**L-D9 · Link Play.** The coop module's POSIX path; DNS-SD is "join by address" at first (the Link Play choice offers Host / Join address /
Internet); hosting needs the firewall to allow the Link Play port (docs for `ufw`/`firewalld`; SteamOS has no firewall by default). Avahi
(D-Bus) for "on this network" later.

**L-D10 · Gentle on this PC.** WSL capped by `.wslconfig` (§4); builds at `nice 19` with **at most `-j 2`** while the Spectator session runs;
lavapipe limited to 2 threads (`LP_NUM_THREADS=2`); at most one game window of mine at a time.

## 4. The Linux environment on this PC (needs the user)

WSL is not installed and installing it needs administrator rights, which I must not use. **The user runs, in an administrator terminal:**

```
wsl --install -d Ubuntu-22.04
```

Windows may ask for a restart; then the Ubuntu window asks for a Linux user name and password (any; never shared with me). **Proposed
`%UserProfile%\.wslconfig`** (a user file I can write once the user agrees), so WSL can never load every core of this PC:

```
[wsl2]
processors=4
memory=12GB
```

Inside WSL (`sudo` is the user's, so the user runs this one line too; I give the exact command when we get there): build tools (`clang` comes
from the LLVM tarball; `cmake`, `ninja-build`, `python3`, `git`), Vulkan headers and loader, `glslang-tools`, Mesa's lavapipe
(`mesa-vulkan-drivers`), SDL3's build dependencies (X11, Wayland, PipeWire/PulseAudio, udev headers), the .NET 10 runtime. Unprivileged downloads into
my own folders (LLVM 23.1.4's Linux release, the Vulkan SDK's SPIRV-Tools, SDL3's source release) are checksum-verified and logged in
PROVENANCE.

The sources stay in the Windows worktree (one source of truth, committed from Windows); WSL builds them from `/mnt/c/...` into build folders on its
own disk (`~/pkn-linux/build`). Game-derived data in WSL lives in `~/pkn-linux/_local/` under the same rule as `_local\`: never in git, never
uploaded. The game's files are read from their Windows folders and only copied, never changed.

## 5. The Steam Deck beta (L5)

A zip for volunteers from the user's Reddit, built in WSL:
- the kit (the builder, prebuilt Linux tools, `play.sh`), **no game files**;
- `TESTING.md`: Desktop Mode, unzip, `setup`, `build <their own dump>`, play, then Game Mode as a non-Steam game; how to screen-record the build
  output and the game (OBS from Discover, or Spectacle on Plasma 6);
- `report.sh`: one archive with logs, the build's stage logs, system facts (CPU, GPU, driver, Mesa version, kernel, distro, SteamOS version, RAM,
  display server) and `vulkaninfo --summary`; it **never** includes saves, game files, shader files, the dump path, the user name or home paths
  (paths rewritten to `~`, a final scan refuses the archive if anything matches), and it shows the tester the file list before writing.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Something already mapped in the guest range on some distro (unusual `mmap_min_addr`/ASLR settings, a preloaded overlay like the Steam overlay) | reserve in a constructor before libraries start threads; the error names the owner from `/proc/self/maps`; same fallback idea as Windows |
| `vm.max_map_count` (65,530 on many distros) exhausted by write-protect splits | merge-friendly protection runs; log the count; SteamOS and Fedora/Ubuntu 24.04+ already raise it; doc line for others |
| Signal-handler work (dirty tracking) is slower than Windows' vectored handler | measure in L4/L5; batch protection changes as M6 did for `VirtualQuery` |
| lavapipe misses a feature the replay needs | fall back where Windows already has fallbacks; the real target, RADV, has them all |
| Gamescope / Game Mode quirks (window size, cursor capture, focus) | Desktop Mode first in the guide; Game Mode fixes from L6 reports |
| Testers' dumps or Python setups vary | `nxs2.py doctor --linux` with plain answers; the report script captures it |
| The kit's tools on an older glibc | built on Ubuntu 22.04; the doctor prints `ldd --version` |

## 7. Sources

Logged in `docs/PROVENANCE.md` as they are consulted. Expected: the Linux man pages and POSIX (mmap, memfd_create, fallocate, sigaction,
sigaltstack, ucontext, sigsetjmp, posix_spawn, clock_nanosleep, getrandom, dlopen/dladdr/dl_iterate_phdr), the System V AMD64 psABI, the ELF gABI
and x86-64 relocation types, lld's ELF options, the Vulkan specification (the `VK_KHR_xlib/wayland_surface` names SDL asks for), SDL3's public
headers and build documentation, the XDG Base Directory specification, Steam's documentation for non-Steam games. Not opened, as before:
pokopia-recomp, nxrecomp, yuzu, Eden, Eden-NS2 (the `Eden-NS2-main*` and `fixed-build*` folders), suyu, Citron, Sudachi, QEMU, Unicorn,
`nvn_deswizzle.c`, `nvn_deswizzle_gpu.m`, `deswz.metal`, ampere-lower's sources (built with its own `build.sh`, never read).

## 8. Progress

| Milestone | State (2026-11-04) |
|---|---|
| L0 | done: WSL2 Ubuntu 22.04 (4 processors, 12 GB), the toolchain in `~/pkn-linux` |
| L1 | done: everything builds on Linux; `program.db` byte-identical to the Windows tool's; ctest 45 pass / 3 skip (with `nxrt.text_input`); the Windows build of this branch passes all 49 of its tests (2 skip) |
| L2 | done: `game.so` for 2.0.1, 200,414 functions, 405 MB, 0 ops without semantics |
| L3 | done: `nxrt --gfx null` runs 2.0.1 at 59.9 presents/s |
| L4 | done: lavapipe draws the title headless at 1080p (PSNR 28.5 dB against the Mac's shot at present 1200); nxshader under Linux .NET gives SPIR-V byte-identical to Windows' (3,967 of 3,967; the `.res.json` differ only in their `source` notes) |
| L5 | the beta zip, `docs/LINUX.md` and `report.sh` are made and tested in WSL as a tester runs them: unzip, venv, `doctor`, `setup --no-ios`, `build --linux` from the dump (33 min at one worker with the lift's objects cached), the launcher in a WSLg window (the loading screen and title at 7–12 presents/s at 960×540, shaders translated on demand), `import-save`, `report.sh` (no names, saves, game files or shaders). Found and fixed on the way: nxrt's run path, the translator DLL's build path, setup's translator folder, lavapipe's crash on reads past a buffer (robust access on CPU drivers). Not reached on lavapipe: the field and the naming box (the box's code is tested with a stand-in zenity) |
| L6 | waiting for Steam Deck testers |
| L7 | not started |

### The AppImage (2026-11-05, beta 1.2.3)

Two beta testers asked for an AppImage, so the builder now also comes as one file: `Pokopia-Native-Builder-<version>-x86_64.AppImage`
(the installer's `linux/appimage/`: `make_appimage.sh`, `AppRun`, the desktop entry, `make_icon.py`, `scan_payload.py` with `allow.tsv`). It holds
python-build-standalone's CPython 3.12.16 with Tk 9.0, lz4, fonttools, pillow and zstandard; the .NET 10.0.14 runtime; the "for Linux" download;
and nxnative set up by the download's own setup, without its git history (`nxnative/.bundled`; setup then has nothing to do). Packed with
appimagetool and the static type-2 runtime (no libfuse2). The first start copies it to `~/.local/share/pokopia-native/builder/<version>`, because
games built with it run its Python and .NET from there: play.json names both, and play.sh exports `DOTNET_ROOT` and takes the launcher's
`POKOPIA_PY`. The builder window (`nxs2_gui.py`) now works on Linux: the Linux game as the output, Play through the build's launcher, Import save.

Tested in WSL (Ubuntu 22.04, WSLg, lavapipe): the AppImage starts through FUSE and with `--appimage-extract-and-run`; `doctor` all ok through its
own Python and .NET; `setup` has nothing to do; a newer build of the same version replaces the unpacked copy; `report <build folder>` writes the
archive. The window, driven by a script as a player would use it: dump and build folder filled in, Build (7:58, mostly cached), Play (the game
ran 75 s in a WSLg window, play.json pointing at the AppImage's Python and .NET). Privacy: the contents are scanned before packing (no private
data, no game files, no video or debug symbols); the zip passes `--strict`.

**Untested, needs a real Steam Deck (or another Linux desktop):** double-clicking it in Dolphin (Desktop Mode) and FUSE on SteamOS; the window
under a real X11 or XWayland desktop (WSLg gives no pixels back, so the window's own picture switch `NXS2_GUI_SHOT` was checked only for its error);
the copy's 1 GB on the Deck's disk; the zenity unpacking progress and the name box (WSL has no zenity); Game Mode with the launcher added to Steam;
RADV. **Known:** the bundled Tk draws with the X server's core fonts only (the bitmap "fixed" face, no bold): the window works but looks plain.

### 1.3.0 (2026-11-06, open beta)

Main 432544e merged (3904b7b): the in-game settings menu, the Resolution setting, Story co-op, Quantum's CPU work, the wider ballot fix. On Linux:
`play.sh` applies `display.json` at every start with `--config` or `--ask` (as `play.ps1`), hands the menu its folder (`NXRT_SETTINGS_DIR`,
`~/.config/pokopia-native`) and has `--story`; `vk_tex.c`'s page hash reads guest memory under `plat_guard`; and the subgroup-ballot rewrite
(`vk_shader.c`, docs/ballot-hang.md) applies only on NVIDIA's GPUs, whose hang it is (`NXRT_VK_BALLOT_FIX=1` or `0` still decides): main's
78a368c, on Windows too.

Why the last one: in WSL, lavapipe (Mesa 23.2.1, LLVM 15) crashed 84–137 s into every start at 2560x1440 (8 of 8; also at 1.5x, and at 1x one
start in two) inside the JIT code of one rewritten compute shader, reading past a heap block; the Khronos validation layer saw nothing. With
`NXRT_VK_BALLOT_FIX=0` it ran (2 of 2); Quantum's three switches made no difference. 1.2.6 rewrote one shader (d2c50cca); 1.3.0's new shapes are
the ones it crashes in. And the rewrite's lane bit (`1 << lane`, unmasked) assumes 32-lane subgroups, which RADV's compute (64) is not.

Checked in WSL (Ubuntu 22.04, lavapipe at 2 threads), on copies of the VM's test save: ctest 54 (51 pass, 3 skip; `nxrt.pad_sdl`, the menu's
input through SDL events, and `nxrt.vk_scale` among them); the AppImage's doctor and build (15 min, mostly cached); the menu draws over the game
(Graphics tab, the Resolution list "Default: your monitor (3840x2160)" on WSLg's 4K monitor, 1920x1080, 2560x1440, 3840x2160; the FPS counter);
real keys through X11's XTEST (a click first, for the keyboard): F1 opens it, Esc closes it, F1 opens and closes it again; the Resolution reaches
the game from the variable, from `display.json` and live from the menu (render targets re-made at once, saved, read again at the next start); with
the gate, the game's first screen (the language choice) at ~240 s. With the rewrite off or gated the crash got rare but not gone: at 2560x1440 1 start
in 5 still crashed (149 s, the same JIT code), at 1080 none in 3 (200-260 s): something else reads past a heap block too. **Not checked:** the field
(lavapipe draws 2 fps), a real controller's Minus + Plus, any real GPU.
