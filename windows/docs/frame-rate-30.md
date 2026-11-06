# The 30 fps setting

Status, 2026-11-05: works. The launcher's Display tab has offered "Frame rate: 30 fps" (`DM_FPS=30`) since the Display tab came, but nothing in the
runtime read it: `gfx_fps_target()` answered 30 and no one asked (found by Quantum in his 1.2.4 package; the 1.2.6 notes list it as a known issue).

## How nxnative does it

nxnative (MIT, `dm/gfx/nvn/nvn_mtl.m` `mtl_fps_target`, `dm/gfx/nvn/hle_nvn.c` `nvn_game_fps` / `fps_present_cap` / `present_itv`): a game with a frame
step or an engine limiter the host can write (`hle_fpslock.c`) is told the cap; any other game, Pokopia among them (its profile names no `fps_target`
and it has neither), gets the display link at 30 Hz ("power-saving mode"). The game's acquires then wait for the link's ticks, so it presents 30 frames a
second, and the game's own pacing decides whether its world still runs at the right speed.

## Here

The emulated display (`hle_nvn.c`, `nxrt_nvn_display`, `present_itv`) shows each frame for the game's present interval in vsyncs; under the 30 fps
cap that interval is at least 2. A frame then stays on screen two vsyncs, the swap chain's textures are free half as often, and `WindowAcquireTexture`
holds the game at 30 a second, as the Mac's 30 Hz link does. The present period handed to the Vulkan present thread follows (33.3 ms). A game that asks
for a longer interval itself keeps it. Without `DM_FPS=30` nothing changes.

## Tests

2.0.1, a copy of the play save, the clock fixed, the same scripted walk (3 s forward) and camera turn (1 s) at 60 and at 30:

| | 60 fps | 30 fps |
|---|---|---|
| presents / s in the field | 56.9–59.9 | 30.00–30.01 |
| present interval p50 / p99 | 16.6 / 22–65 ms | 33.2 / 37–39 ms |
| vsyncs that showed no new frame | 0–16 per 300 frames | 0 |
| shown after the present | 14–28 ms | 52–57 ms |
| after the walk and the turn | | the same place and view as at 60 |

Pokopia's movement and camera are frame-time based, so at 30 the world runs at the right speed. The picture reaches the screen about a frame later at
30 (the swap chain of three lets the game run up to two frames ahead).
