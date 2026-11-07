# M5-C: the replay (the game's own frames on Vulkan)

Status, 2026-11-03: **the title screen is drawn and matches the Mac's; in the working tree for review (nothing committed).** With Pokopia
2.0.1's M3 `game.dll`, `nxrt --gfx vulkan` replays every NVN command list the game submits (about 130 draws, 29 passes, 5 dispatches and
12 clears a frame at the title, none skipped) and presents the result at **58.8–60.1 presents/s (about 59.6 on average)**. Against the Mac host at the same
present and the same clock the picture differs only where the title animates (PSNR 32.5 dB, mean absolute difference 2.1 of 255).

| | |
|---|---|
| Code | `src/nxrt/vk/`: `vk_replay.c/.h` (record walk, passes, draws, dispatches, copies, clears, queries), `vk_pipe.c/.h` (state translation, pipelines), `vk_shader.c/.h` (modules, reflection, binding table, SPIR-V edits), `vk_shot.c/.h` (screenshots, PNG), `vk_json.c/.h`; about 2,650 lines of C, 265 of tests |
| Changed in `vk/` | `gfx_vk.c` (the `gfx_*` calls into the replay, per-use sync by default), `vk_tex.c/.h` (views, initial content, linear CPU-written textures, readback of a region), `vk_dev.c`, `vk_internal.h`, `vk_fn.h` (`VK_EXT_vertex_input_dynamic_state`, draw / query / dynamic-state entry points), `vk_submit.c/.h` (`vks_recording`), `CMakeLists.txt` |
| Outside `vk/` | `src/nxshader/` (`nxs_spv.py` pass `fetch1d_tbuf`, `nxs_refl.py` `fetch1d_vars`, `nxshader.py` publishes `c1` and the fetch1d variables); the cache under `_local/nxshader/cache` republished (3,967 shaders, all pass `spirv-val`) |
| Tests | 1 new (`nxrt.vk_replay`, 52 checks); the suite is 34 tests, all pass (the 5 dump tests with `NXW_DUMP` set to the 2.0.1 ExeFS) |
| Contract | unchanged (`gfx_backend.h`, `nvn_obj.h`, `dmsh_abi.h`) |

## 1. What is implemented

Behaviour follows nxnative's Metal replay (`dm/gfx/nvn/nvn_mtl.m`) where Pokopia needs it; its TBDR-specific work (pass merging, load/store
lookahead, deferred clears) has no counterpart.

**Records.** `vkr_exec` walks a command handle (`JUMP`, `CALL` to depth 8, a 16M-record budget, malformed records logged and stopped). NVN
state belongs to a queue: one binding state per queue (`vkr_queue_activate` / `_forget`), Metal-like defaults.

**Render targets and passes.** Dynamic rendering, `LOAD`/`STORE`, images resting in `GENERAL`; attachment views with identity swizzle,
duplicate slots skipped, render area clipped to the smallest attachment, attachment-less passes at the viewport. A pass ends at a copy,
clear, dispatch or target change. Clears are mini-passes with `loadOp CLEAR` over the scissor rectangle and view layers; a partial channel or
stencil mask is logged once and cleared fully.

**Pipelines (`vk_pipe.c`).** Keyed by shaders, attachment formats, samples and what the device cannot make dynamic. With extended dynamic
state 1/2/3 and `VK_EXT_vertex_input_dynamic_state` (all present on the RTX 4070 Ti) the state is set per draw: viewport and scissor (the
viewport flipped for the upper-left window origin, depth `[0,1]` or `[-1,1]`), cull, front face (against the queue's origin), depth and
stencil (both faces), blend enable / equation / write mask per target (advanced Porter–Duff modes as fixed functions, a missing second
source as zero, no blending on integer targets, targets a shader does not write masked), logic op, alpha to coverage, polygon mode, depth
clamp, topology (quads and line loops expanded), patch points, vertex input. Pipelines go through the persistent pipeline cache of track B
(`vk_pcache.c`), created synchronously at first use: no graphics pipeline libraries or background compilation yet (§6).

**Shaders and descriptors (`vk_shader.c`).** nxshader's SPIR-V and reflection by key (`gfx/nxs_rt.h`); one descriptor set per stage
(VS 0, FS 1, GS 2, TCS 3, TES 4; compute 0) by rewriting `DescriptorSet` decorations. Binding kinds as `nxs_refl.py` names them: support
buffer (Ryujinx's layout, render scale 1), constant buffers c0–c17 (unaligned ranges copied into a per-submission arena), the shader's own
c1 (from the reflection, in a host-visible bank), driver segments c0/c2 (`dmsh_abi.h`: buffer tables, per-draw words, the base-instance row
at c0+0x640, the Switch 2 packed compute bank), storage buffers (written back to guest memory at completion), combined image samplers (view
type fitted to the declaration, the texture's own sampler, a slot or an ID), buffer textures (texel buffer views over the pool mirror),
storage images. A sampled or texel-buffer view takes the numeric class the shader declares when the bits allow (`vkr_fmt_as_ct`: 32-bit
components between float / uint / int, 8- and 16-bit integers between uint and int). Vertex inputs whose format is of another class than
the shader's declaration get a variant whose inputs are declared in the format's class and bit-cast back (Maxwell hands the shader register
bits). `gl_PerVertex`'s unused `CullDistance` member is removed when clip + cull exceed the device limit (8 + 1 > 8 here).

**Commands.** Draws (arrays, indexed u8/u16/u32, instanced, base vertex / instance, indirect and indirect-count multi-draw, quads through a
static index buffer or expanded), dispatches (direct and indirect, group counts in c0), buffer copies, fills and inline updates, buffer ↔
texture copies (with the copy-stride state; storage-backed textures also get the bytes in their storage), texture ↔ texture copies
(exact, scaled and mirrored blits, MSAA resolves), texture clears, occlusion queries with real sample counts and timestamps / counters,
conditional rendering, barriers.

**Memory and sync.** Per-use by default (track B §7): every range a draw, dispatch or copy binds is brought up to date through the dirty
tracker (`NXRT_VK_SYNC=all` restores the copy-everything mode). GPU writes into pool memory (storage buffers, texture → buffer copies,
fills, updates, query results) reach guest memory when the submission completes. Textures: a linear texture in a CPU-writable pool is read
from storage (Pokopia's 2x1 exposure texture, written by the CPU every frame); a GPU texture in a CPU_UNCACHED / CPU_NO_ACCESS or flushed
pool gets its storage as initial content (nxnative's `tex_init_fill`).

**Presentation and screenshots (`vk_shot.c`).** The game's presented texture goes through track B's scaled, letterboxed present.
Screenshots read the presented image back at the game's resolution (crop applied, rows turned for a lower-left origin) and write PNG:
`NXRT_VK_SHOT=<n>[,<n>…]` (present numbers), `NXRT_VK_SHOT_EVERY=<n>`, `NXRT_VK_SHOT_FILE=<path>` (a control file: when it exists the next
present is taken, to the path written in it, and the file is deleted), `NXRT_VK_SHOT_DIR`, `NXRT_VK_SHOT_WINDOW=1` (also the window as shown).

**Diagnostics.** `NXRT_VK_PASSDUMP=<a>[-<b>]` logs every pass, clear, draw (state, raw blend words, bindings with texture serials, the first
vertex's attribute bytes, the first 8 constants) and dispatch of those frames; `NXRT_VK_TEXDUMP=<n>` writes every image frame n drew into or
sampled (PNG; compressed ones as DDS). The replay logs per-300-frame statistics and the GPU time (timestamps around each submission).
`NXRT_VK_SKIP=<key>[,<key>…]` leaves out every draw with one of those shaders in any stage, and every dispatch of one (keys of 16 hex digits,
any case, separated by commas, semicolons or spaces); each key's first skip is logged, and the replay line counts them. It is read again at
each frame when it changes, so DM_CTL's `env NXRT_VK_SKIP=…` switches suspects while the game runs (D-7: a tester on another GPU finds the
draw that goes wrong). Leaving out a full-screen pass freezes what it writes, not the picture's other parts.

## 2. The title screen against the Mac

**Method.** Both hosts boot a fresh copy of `_local\saves\run4\save` (Mac: `~/m5c/titleshot.py`, a no-input copy of `~/setup/menudrive.py`
run from `~/m5c`, booting `~/installer-test/build201-clean` read only) and capture their own presented image at presents 1,800 and 2,400.
**The clock must be fixed:** Pokopia's title follows the time of day. The first comparisons (Windows at 22:30 local, Mac at 07:06) differed
in the background (dark grey-green against bright yellow-green, a lavender photo frame): the game had bound a multiply layer for the evening
(raw blend word `0x485020c08`, ZERO / SRC_COLOR) where the morning draws an additive glow (`0x4820a0828`). The BC7 background texture,
dumped and decoded, is the Mac's colour exactly. With `DM_FIXED_TIME=1793793600` (2026-11-04 12:00 UTC) on both hosts (and `TZ=UTC` on
Windows) the pictures agree.

| Files (`_local\m5c-compare\noon\`) | |
|---|---|
| `mac-1800.png`, `mac-2400.png` | the Mac host's presented image |
| `win-1800-final.png` (the final build), `win-1800.png`, `win-2400.png` | Windows |
| `pair-1800.png`, `pair-2400.png` | Mac, Windows, difference (×4) side by side |
| `diff-1800.png`, `diff-2400.png` | the per-pixel largest channel difference ×4 |
| `crop-photo-1800.png`, `crop-topleft.png` | details: the photo, the light rays |
| `..\pair-1800-m5c14.png` | the clock mismatch: Mac morning, Windows evening |

**Result (present 1,800, final build): PSNR 32.5 dB, mean absolute difference 2.1, 2.1 % of pixels off by more than 32, 0.02 % by more
than 64.** Layout, logo, "Ver. 2.0.1", "Settings", the copyright lines, the stump, the photo and its frame, and the background match pixel for
pixel where nothing moves (e.g. (1700, 200): Mac 191/219/121, Windows 192/219/121). What differs is animated: the light rays at the top left
and the bokeh at the bottom right (both present on both hosts, at another phase), and "PRESS A BUTTON", which blinks (shown in the Mac's
1,800, not in that Windows frame). Windows also reaches the title some 100–300 presents later than the Mac (its loading is slower), so the
same present number is a different moment of the animation.

**Bugs found on the way, fixed:** buffer textures read as 2D fetches by the translator (always zero) → nxshader's `fetch1d_tbuf` pass;
RGBA16I attributes into uint inputs zero-extended → vertex input variants; the CPU-written 2x1 exposure texture treated as GPU-owned (black or
over-exposed UI, missing text) → linear CPU-written textures from storage; textures without initial content → `tex_init_fill`; view
formats of another class than the shader's (validation) → `vkr_fmt_as_ct`; 9 clip + cull distances (validation) → the cull member dropped.

## 3. Validation

`NXRT_VK_VALIDATION=1`, 90 s (the title from about present 1,400 to 4,200): **one error**, once, during the switch from loading to the title:
`VUID-vkCmdDraw-None-09600`, an image sampled as `GENERAL` while still `UNDEFINED` at submission (an image created and sampled before its
first layout transition was recorded; which one is not identified yet). Fixed since the first validation run: `VUID-vkCmdDraw-format-07753`
and `VUID-vkCmdDispatch-format-07753` (texel buffer views `R32_UINT` read as float), `VUID-RuntimeSpirv-maxCombinedClipAndCullDistances-00710`.
The test `nxrt.vk_replay` runs with validation on: 0 errors.

## 4. Performance at the title (RTX 4070 Ti, i9-13900KF, 1920x1080 game image in a 1280x720 window, FIFO)

| | |
|---|---|
| Presents | 58.8–60.1 per second in 300-frame windows over frames 3,600–7,500 (about 59.6 on average); interval p50 16.5–17.1 ms, p95 17.6–22.8 ms, p99 17.8–34.7 ms, occasional single intervals up to 88 ms; none dropped |
| GPU | 1.8–2.35 ms per frame (11 submissions a frame, about 0.2 ms each; timestamps around each submission's replay) |
| CPU | 1.50 cores for the whole process (45.1 s of CPU in 30 s, below-normal priority); the game thread's submit + present 4.1–5.2 ms a frame |
| Work | 125–136 draws, 29 passes, 5 dispatches, 12 clears, about 270 descriptor sets a frame |
| Pipelines | 40 graphics + 11 compute for boot and title, 400 ms to create on a cold cache, the slowest 141 ms (a hitch at its first use) |

## 5. Gameplay

Not reached, by design of this milestone's input: on Windows nothing feeds the controller yet (`hle_hid.c`'s scripted input, `DM_KEYS` /
`DM_CTL`, is not started: `hid_keys_init` has no caller, M6). With neutral input the game stays on the title, waiting for A; a 130 s run
(7,500 presents) stayed there at 59–60 presents/s with no replay warnings and no growth in the per-frame work.

## 6. Remaining gaps

- **Pipeline hitches**: no graphics pipeline libraries or background compilation; a new pipeline stalls the frame on a cold cache (141 ms
  worst at boot). The persistent cache removes them on later runs.
- **Dual-source blending**: a second source counts as zero (logged once); no dual-source variant of the fragment shaders yet. Not used at
  the title.
- **Alpha test** (`ColorState` alpha function): not applied (logged once; nxnative uses shader variants). Not used at the title.
- **A GPU texture copied into a storage-backed texture**: the next upload from storage drops it (logged once; nxnative reads it back).
- **Partial clears**: a colour clear with some channels masked, or a masked stencil clear, clears everything (logged once).
- The one validation error of §3.
- Input (M6), so gameplay is untested.

## 7. Reproducing

```
scripts\build.cmd                         (-j 4, low priority)
ctest --test-dir build\Release            (NXW_DUMP=<2.0.1 ExeFS> for the dump tests)
_local\m5c-run.ps1 -Name <run> -Seconds 60 -Env @{ NXRT_VK_SHOT = "1800,2400"; DM_FIXED_TIME = "1793793600"; TZ = "UTC" }
    = build\Release\bin\nxrt.exe <ExeFS> --dll _local\nxlift\m3\game.dll --romfs <RomFS.img> --save <a copy of run4\save> --temp <dir> --log <dir> --gfx vulkan
    NXRT_VK_VALIDATION=1  NXRT_VK_PASSDUMP=<a>[-<b>]  NXRT_VK_TEXDUMP=<n>  NXRT_VK_SYNC=all  NXRT_VK_SHOT_FILE=<control file>
Mac: cd ~/m5c && python3 titleshot.py ~/installer-test/build201-clean <tag> 70 1800,2400 DM_FIXED_TIME=1793793600
```

Sources consulted are logged in `docs/PROVENANCE.md` (2026-11-03, M5-C).
