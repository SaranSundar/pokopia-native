# Quantum's CPU work, measured piece by piece

Status, 2026-11-06: three pieces taken (commits by Quantumenis, ported by xFractals), four left out. Quantum agreed to the MIT licence and to being
credited as Quantumenis (2026-11-06).

Quantum (Discord: Quantumenis) sent changes on top of our 1.2.5 (`<work>\reference\quantum-1.2.5`). The CPU work was ported here one piece at
a time, each with an off switch, and kept only when the same scripted run showed a gain on both game versions. The game on Windows is CPU-bound
(about 3 ms of GPU a frame against 7–8 ms of replay), so the measure is the replay's time per frame.

**The run** (`_local\gfx\perf.ps1`): a copy of the play save (2.0.1) or of the 1.0.5 save, the clock fixed (`DM_FIXED_TIME`, `TZ=UTC`), losttest's
presses into the field, no screenshots, 7,800 presents, a warm shader and pipeline cache; the statistics of frames 4,500–7,800 (the field). RTX 4070 Ti,
i9-13900KF. "Replay" is `submit+present ms a frame` of the `[nvn] frame pacing` lines: the time the game thread spends recording a frame's NVN work
into Vulkan. The frame rate was 59.9 in every run (the PC is fast enough for 60 either way); a slower CPU gains frames where this one gains headroom.

| | Replay 2.0.1 | Replay 1.0.5 | Off switch |
|---|---|---|---|
| before (gfx at e653b2b) | 7.48 / 7.79 ms | 7.53 ms | |
| + texture re-uploads skipped by page hash (37a92f7) | 6.37 ms | 6.31 ms | `NXRT_TEX_HASH=0`: 7.87 ms |
| + dynamic state recorded only when it changes (7c3a9a5) | 5.31 ms | 5.23 ms | `NXRT_VK_STATE_CACHE=0`: 6.43 ms |
| + descriptor sets reused within a submission (889e5e4) | 5.00 ms | 4.92 ms | `NXRT_VK_SET_REUSE=0`: 5.35 ms |

About a third less replay time in all. What each did:

- **Page hash** (`vk_tex.c`): 2 textures re-uploaded every frame in the field (6,600 over the run, 24.7 GB) were rewritten with the same bytes; now 0,
  for 0.03 ms of hashing a frame. Changed from his version: the pages are hashed before the upload reads them, not after (a CPU write in between would
  otherwise leave a stale image that later checks call unchanged).
- **Dynamic state** (`vk_replay.c`): about 25 `vkCmdSet*` calls a draw over 2,300 draws a frame, now only the changed ones; vertex buffers bound only
  when they differ. The F-5 depth-bias scaling goes through it.
- **Set reuse** (`vk_replay.c`): 5,237 descriptor sets allocated and written a frame → 1,585 (3,631 bound again).

**Left out** (no gain on this run, or a risk with none):

| Piece | Result | Why left out |
|---|---|---|
| `VirtualProtect` split (`nvn_dirty.c`) | pool uploads 6.5 MB a frame and faults unchanged; the region-by-region retry runs, but those pages were not being re-sent here | no gain |
| hot pages: 3-of-8 detection and a hash at each use (`vk_mem.c`) | uploads 6.5 → 1.6 MB a frame, but 9.7 s of hashing over the run: replay 6.37 → 7.56–7.71 ms | slower |
| staging ring 128–256 MB (`vk_mem.c`) | 6.46 ms against 6.37 ms; overflows about the same | no gain (`NXRT_VK_STAGING_MB` already sets it) |
| `vkm_use` memo and texel-view cache (`vk_replay.c`) | 4.94 ms against 5.01 ms with it off on 2.0.1 | within the noise, and a range written twice within a submission would keep its first bytes |

Not taken at all (the brief): parallel graphics, frame generation, the F1 overlay, NIS, the 120 Hz frame limit, PGO, his debug switches.

**Checks:** both versions in the field, bag and menu at 1x and 2x, and Vulkan validation at 2x (see the commit messages); every off switch
restores the old numbers.
