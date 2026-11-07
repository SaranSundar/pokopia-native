# M6 phase A: Pokopia 2.0.1 playable on Windows (input, gameplay, sound, saves)

Status, 2026-11-04: **2.0.1 plays on Windows.** From the title the game takes A, loads the save, walks, turns its camera by the mouse (direct look), opens its
menu and saves and quits back to the title, at **59–60 presents/s in gameplay** (25 before this phase), with sound from the sound card. Saves written on
Windows load on the Mac host at the same spot, and the other way round. In the working tree for review (nothing committed).

| | |
|---|---|
| New | `src/nxrt/front/`: `front_sdl.c/.h` (keyboard, mouse look, controllers on the window thread), `front_map.h` (the mapping, pure), `pad_slots.h` (nxnative's player slots), `audio_sdl.c` (the SDL3 sound card); `src/nxrt/host/prof.c` (a sampling profiler), `host/ctl_gfx.h`; `scripts/play.ps1`, `play.cmd`; 4 tests |
| Changed | `host/main.c` (`hid_keys_init` is called), `hle/hle_hid.c` (DM_CTL `shot` / `prof`, its file read in binary), `host/ctl_standins.c` (the renderer's DM_CTL hooks), `hle/audio_silent.c` / `audio_out.h` / `hle_audio.c` (the device backend), `vk/vk_window.c` (controllers, the front end), `vk/gfx_vk.c`, `vk/vk_shot.c` (DM_CTL screenshots, depth dumps), `vk/vk_replay.c` (replay fixes, §3), `vk/vk_mem.c`, `host/hle_svc.c`, `host/crash.c`, `gfx/nvn_dirty.c` (performance, §4), `vk/vk_tex.c`, `vk/vk_fn.h` |
| Tests | 4 new: `nxrt.input`, `nxrt.pad_sdl`, `nxrt.audio_sdl`, `nxrt.save_rt`; the suite is 39 tests (34 + these 4 + co-op's `coop.loopback`), all pass with `NXW_DUMP` set |
| Contract | unchanged (`gfx_backend.h`, `nvn_obj.h`, `dmsh_abi.h`) |

## 1. Input

**Scripted input (DM_KEYS, DM_CTL, DM_REC).** `hid_keys_init` (hle_hid.c) had no caller on Windows; `nxrt_load_program` now calls it after the modules are loaded,
as nxnative's `main.c` does (look_cam matches main's build ID). DM_CTL's file is read in binary mode, so a CRLF file keeps its byte offsets. Its renderer
commands go to the backend chosen at run time through `dm_gfx_ctl` (host/ctl_gfx.h): `shot <path>` (the next presented frame as PNG), `texdump` (the next
frame's images beside the log, depth images too, with their raw values) and `passdump <n>`; `prof <seconds>` starts the profiler (§4). With DM_CTL the same
scripts drive Windows and the Mac host.

**The SDL3 front end (DESIGN D14).** It runs on the window's thread, which now also starts SDL's controller subsystem. Behaviour follows nxnative's Mac front end:
- **Keyboard**, by key position (SDL scancodes), player 1 through `dm_input_set`: the Mac's layout (table in §7). Since 1.2.0 a map can replace it:
  `DM_KEYMAP=<file>` names `"<action>": <SDL scancode>` pairs (actions: the 16 buttons by name, `StickUp/Left/Down/Right`, `Shake`; `front_map.h`
  `front_keymap_parse`: Esc, F11 and a key used twice are refused, an action whose default key another took has none). `play.ps1`'s window writes it from
  its Controls tab (`%APPDATA%\Pokopia Native\controls.json`, scan codes turned into the same positions), and the log lists the map in use. Windows only;
  the Mac and the iPhone keep the layout.
- **Mouse look:** a click on the picture captures the mouse (SDL relative mode). Its movement goes to `dm_look_add_at` with the event's age, so Pokopia's
  profile entry `look_cam` writes the camera directly, as on the Mac. The default is 0.05° per mouse count (raw input, no pointer acceleration); `DM_LOOK_SENS` and
  `DM_LOOK_SCALE` override it. While captured, left is A, right is B and middle is the right stick's press. Esc or losing focus releases the mouse. Without pointer
  look the left button is the touch screen.
  While the character moves sideways (the left stick held left or right), the game's follow camera sets the yaw each frame and a written turn is lost (#31), so the
  mouse turns the camera through the right stick then, as a controller does (`DM_LOOK_STRAFE=0`: written then too).
- **Controllers:** each is a player of its own. The first is player 1 through `dm_pad_set_player(0)` = `dm_pad_set`, ORed with the keyboard. `DM_PAD_MERGE=1`
  makes every controller player 1 and `DM_KBD_PLAYER=own` keeps player 1 for the keyboard alone (nxnative's `pad_slots.h`). Mapping:
  - Face buttons by position, as on a Switch (right A, bottom B, top X, left Y). SDL3 names buttons by position, so a Nintendo pad maps name for name (as
    `pad_apple.h` has it) and an Xbox or PlayStation pad by position. `DM_PAD_FACE=labels` maps an Xbox pad's printed letters instead.
  - Shoulders are L / R, triggers ZL / ZR past a quarter of their travel, Start is +, Back is −, plus both sticks and their presses and the D-pad.
  - Sticks are turned to y-up and go through a radial inner dead zone (0.08) and an outer one (0.92: research/01 #18, a DualSense that never reaches full
    tilt); `DM_PAD_INNER` / `DM_PAD_OUTER` set them.
  - Home and Capture have no function.
- **Focus:** out of focus nothing reaches the game; what was held is released (front_mac.m `input_release`). F11 and Alt+Enter toggle fullscreen.

**Verified:**
- `nxrt.input` checks the game's reads (GetNpadStates) for: DM_KEYS frame steps and seconds steps; DM_CTL with CRLF lines, holds, a partial line, `shot`'s
  path; the front-end paths ORed, the press latch and a second player; pointer look on the stick path; the whole mapping.
- `nxrt.pad_sdl` drives `front_sdl.c` with **SDL virtual gamepads** (player 1, player 2 on No2, disconnects), keyboard events, mouse capture and look, and focus.
- In the running game, **real keyboard and mouse input** (Windows SendInput, sent only while the game's window is in front, `_local/m6/sendkeys.ps1`) works
  end to end:
  - J at the title loads the save;
  - S walks the player toward the camera;
  - a click captures the mouse, 800 counts turn the camera 40° through look_cam, and Esc releases it (log: "mouse look on" / "off").
- **Not verified with physical controllers** (none is attached to this PC): the gamepad path is covered by the virtual pads only.

## 2. Gameplay against the Mac

**Method.** `_local/m6/m6drive.py` drives both hosts the same way, by what is on screen rather than by time. It takes DM_CTL screenshots and compares fixed
regions with reference pictures: the title's logo, then the field HUD's tool widget. The steps are: wait for the title, A, wait for the field, then the plan
(stand, `LDown~2`, `LLeft~1.5`, `look 40 0`, `look -40 -10`, the menu, B or Save & Quit), with a screenshot after each step. Both hosts use a fresh copy of the
user's Level 2 save and `DM_FIXED_TIME=1793793600` (`TZ=UTC` on both). The Mac boots `~/installer-test/build201-clean` read only from `~/m6` (`m6run.sh`).
`cmp.py` gives PSNR and the pairs.

| Moment (`_local/m6/cmp-final2/pair-*.png`) | PSNR | Notes |
|---|---:|---|
| title | 49.4 dB | |
| g0: standing where the save puts the player | 24.5 dB | same place and camera; clouds' shadows (below), NPC and PC-screen animation |
| g1–g4: after the walk and the two look turns | 14–17 dB | same framing; the clouds, animation and a small difference in where the walk ends move everything |
| g5: the menu | 17.9 dB (menu panel alone 31.5 dB) | 11/4 12:00 PM on both |
| HUD tool widget at g0 | 33.1 dB | |
| a save reloaded after a 60° direct-look turn (§6) | 28.3 / 28.7 dB | same position and camera turn on both hosts |

**The large shadow differences are clouds, not a replay bug.** The ground's big soft shadows drift by themselves while the player stands still (Windows
screenshots 10 s apart). At the same moment the shadow map matches the Mac's (texraw against the new depth dump: the same picture, mean difference 0.8 % of
the depth range). The G-buffer targets match too (albedo, normals, AO), as checked against the Mac's DM_CTL texdump. The cloud shadow is a separate texture
whose phase depends on time since loading, which differs between the hosts. Earlier walk comparisons at 25 presents/s (before §4) ended the walk short; at
59 presents/s the walk ends where the Mac's does.

## 3. Replay fixes found in gameplay

| Found | Fix |
|---|---|
| 4-byte counter of a gameplay compute shader at an offset Vulkan cannot bind (`minStorageBufferOffsetAlignment`): not bound, its writes lost | dispatches bind such a storage buffer through a copy in an aligned place (`R0.bounce`): copied in before the dispatch, back after it, then written back to guest memory |
| Shadow samplers of lights without shadows bound to the game's 1x1 / 8x8 default colour textures: depth comparison on a colour image, undefined in Vulkan (VUID-vkCmdDrawIndexed-None-06479) | a 1x1 depth image holding the texture's first red value stands in, as the hardware compares with red (values from storage) |
| Indirect quads not drawn | drawn as indexed quads with the arguments guest memory holds when recorded (as the indexed indirect count already was) |
| An sRGB view of an image with storage use (VUID-VkImageViewCreateInfo-usage-02275) | the view's usage leaves storage out where its format has none |
| Timestamp query slots reset on the host while still in use during loading (VUID-vkResetQueryPool-firstQuery-02741) | the reset is recorded (`vkCmdResetQueryPool`) |
| Texture dumps capped at 96 images, none for depth | 1,024; depth images as grey PNG plus their raw values |

**The M5-C gaps:**
- **Pipeline compilation:** still not in the background. With the cache kept between runs (`play.ps1` keeps it) gameplay has no compile hitches. On a cold cache
  the first minute has single stalls up to ~2 s (the replay's log: `worst` pipeline creation time).
- **Dual-source blending, the alpha test, partially masked clears, GPU→storage texture copies:** none was hit from the title to walking, the menu and saving
  (each logs once when hit).
- **The validation error at loading:** a 6-layer image (a cube map) is sampled at its first use while still UNDEFINED (VUID-vkCmdDraw-None-09600, 6 layers,
  once at the switch into the field). Not fixed: its first transition is not recorded before the submission that samples it.

Validation in gameplay after the fixes (title → field → walk → menu, `NXRT_VK_VALIDATION=1`): only that error. "A 3D texture read as another type" also
remains, once: the game binds its 8x8 default 2D texture where a shader declares 3D; it reads 0, as Metal cannot do better either.

## 4. Performance

| | before (M5-C replay) | after |
|---|---|---|
| Field, standing and walking | 24.2–26.9 presents/s, p95 65–69 ms | **59.2–59.6 presents/s**, p50 16.6 ms, p95 17.4–18.5 ms |
| Menu open | 12.4–12.8 presents/s | 57–60 presents/s |
| Title → field (loading after A) | 37 s | 21–25 s (the Mac host: 24–32 s) |
| Whole process | | 2.4 cores on average over a 130 s run with loading (317 CPU-seconds) |
| Render thread ('SynergisticDispatcherTask') | 97 % of a core, 70 % of it in `NtQueryVirtualMemory` | 66–84 %, spread over the driver, uploads and the replay |
| GPU per frame (RTX 4070 Ti) | 3.9–7.7 ms | 2.1–2.6 ms |
| Mac host, same moments (M1 shared with two other game instances) | ~30 frames/s (26.9–32.4) | |

**Profiling.** A sampling profiler was added for this (`host/prof.c`). DM_CTL `prof <s>` or `NXRT_PROF=<start>,<s>` samples the busiest threads at 1 kHz
and logs where their time goes: by module, and by function. Inside the game DLL the function is the guest function (`main+0x...`); inside nxrt it comes from
`nxrt.pdb` (configure with `-DNXRT_PDB=ON`). It found two problems:
1. `vk_mem.c`'s `pool_sync` called `VirtualQuery` for every run of pages it uploaded, and the dirty tracker's protections split the address space into many
   regions. Pages seen committed are now remembered per pool.
2. The heap's lazy-commit fault hook ran a `VirtualQuery` on every write fault of the dirty tracker ahead of it. It now asks the tracker first
   (`dm_fault_wp_register`). The tracker's protection changes went from 8–20 ms to 1.2 ms a frame.

The game thread and `KIDSTaskScheduler0` spend most of their time in the game's own waits (sleep / yield), so gameplay is now bound by the 60 Hz present.
One experiment was reverted: letting pages written every two or three frames become "hot" (left writable, copied at every use) tripled the uploads and
cost 37 presents/s.

## 5. Sound

`front/audio_sdl.c` is the device backend of `audio_out.h`:
- **How it pulls:** it opens the default playback device with SDL_OpenAudioDeviceStream (48 kHz PCM16, the output's channels). SDL's device thread asks for
  what the card is about to play, and the stream's get-callback pulls exactly those whole frames from hle_audio.c. Blocks are therefore released, and the
  game's audio threads woken, at the sound card's own rate.
- **Stop:** clears the run flag, pauses the device and takes the stream's lock once (the callback runs under it), so no pull runs or will run afterwards.
- **Fallback:** the silent sink stays when there is no SDL3.dll or no device, or when the device will not open or start. hle_audio.c's watchdog also hands
  over when the device stops calling back.
- **Default:** `DM_AUDIO=1` selects the card; the default stays silent, as on the Mac. `play.ps1` sets it (and `-Silent` turns it off again).

| Check | Result |
|---|---|
| `nxrt.audio_sdl` (SDL's `dummy` driver as a fake card) | 47,773 frames/s pulled, whole frames, none after stop, restartable; through hle_audio.c with 6 × 5 ms blocks queued: 48,051 frames/s released in order, 190 device callbacks |
| The game on this PC's card (WASAPI, DELL S2817Q over NVIDIA HDMI, 48 kHz stereo, 480 frames a period) | 100 callbacks/s, 480 frames each, 4.0 blocks queued on average, **0 underruns** over 60 s at the title and in gameplay |
| What the game plays (`DM_AUDIO_DUMP`) | silence while loading, then the title music (RMS 1,500–1,800, peaks ~8,700); effects and gameplay music play through the same output |

## 6. Saves

- **In-game save:** Save & Quit (the menu's −, then Yes) writes through hle_fs.c into the work folder. Commit goes through `save_txn.c`:
  "save commit #1 … (clone + swap 31 ms)", nothing left beside the save, back at the title.
- **The written save:** the same layout and sizes as the Level 2 backup. B00/*, G00/SCSHDAT.BIN and S00/SYDAT.BIN are byte-identical; G00/CMNDAT.BIN and
  G00/STGDAT01.BIN changed. The Mac's own Save & Quit gives the same picture (the same two files changed).
- **Windows → Mac:** the Mac host loads the Windows-saved save (`~/m6/saves/win-m6-1`) into the field. Its screenshots match Windows' reload of the same save:
  25.9 dB standing, 28.7 dB after a 60° look turn, same spot, same camera.
- **Mac → Windows:** the save from the Mac's Save & Quit loads on Windows. Against the Mac's reload: 25.0 / 28.3 dB, the same spot.
- **`nxrt.save_rt`** drives nn::fs as the game does: mount, write in place, create, read back, commit, delete, commit again. Before Commit the committed folder
  is untouched; after it, it holds exactly the work directory, the untouched files byte for byte.

## 7. Playing

```
scripts\play.cmd -ExeFS <ExeFS folder> -Dll <game.dll> -RomFS <RomFS.img> -Save <save folder> [-Work <folder>] [-Silent] [-Fullscreen] [-Window 1600x900]
```
- **The save:** the game writes to `-Save`, through `<Save>.work`. Use a copy, never your only save.
- **`-Work`:** holds the shader / pipeline cache (kept between runs), temp files and logs. The default is `%LOCALAPPDATA%\pokopia-native`.
- **Runs:** nxrt at below-normal priority, with the sound card on.
- **This PC:** `_local\play-local.ps1` (git-ignored) fills in the paths and plays on a copy of the Level 2 backup, made once into `_local\play\save`.
  `-Fresh` makes a new copy.

| Switch | Keyboard / mouse | Controller |
|---|---|---|
| Left stick (move) | W A S D | left stick |
| A / B / X / Y | J / K / U / I (mouse left / right while captured) | right / bottom / top / left face button |
| L / R, ZL / ZR | Q / E, Z / C | shoulders, triggers |
| + / − (menu, Save & Quit) | Return / Tab | Start / Back (Menu / View, Options / Create) |
| D-pad | arrow keys | D-pad |
| Camera | mouse: click the picture to capture, Esc releases | right stick |
| Stick presses | F / G (middle mouse = right stick press) | stick clicks |
| Shake | Space | |
| Fullscreen | F11 or Alt+Enter | |

## 8. Not done, and phase B

**Not done or not verified in phase A:**
- Physical controllers: SDL virtual pads only. Not covered: rumble (`hle_haptics.c` is not wired to SDL), motion (gyro), per-pad calibration beyond the two
  dead zones, the Joy-Con sideways case.
- The UNDEFINED cube map at the switch into the field (§3); the 3D/2D default texture.
- Background pipeline compilation (cold-cache hitches in the first minute).
- Dual-source blending, the alpha test, partial clears and GPU→storage copies: not hit, so still unimplemented.
- The indirect-quad draws use the arguments as they are when recorded. A GPU-written count shows the previous frame's value.
- 1.0.5 (out of scope).

**Phase B:**
1. **1.0.5:** its own lift (`nxlift` over the 1.0.5 ExeFS, the profile's 1.0.5 look_cam entry exists), boot and title checks on the Mac, then this phase's
   gameplay drive.
2. **Controllers in full:**
   - test real Xbox, DualSense and Switch Pro pads;
   - rumble through SDL_RumbleGamepad (`hle_haptics.c`);
   - gyro through SDL's gamepad sensors into `dm_motion_set_hid`;
   - per-pad calibration and settings in the launcher.
3. **Renderer:**
   - background pipeline compilation (graphics pipeline libraries) or a shipped warm cache;
   - the first-use transition of never-initialised images;
   - dual-source and alpha-test variants when a scene needs them;
   - GPU→storage copies read back.
4. **Performance beyond 60 Hz playability:**
   - the replay's descriptor churn (≈5,500 descriptor sets a frame: push descriptors or descriptor buffers);
   - write-fault and upload costs (≈460 faults, 6.6 MB a frame);
   - the recompiled code's own speed (DESIGN §7: register traffic at call sites, `volatile` loads), measured with `prof`.
5. **M7:** the Windows target in the installer and its window, the controls in the launcher, a release build of the tools.

## 9. Reproducing

```
scripts\build.cmd                                  (4 jobs, low priority; -DNXRT_PDB=ON once for the profiler's host names)
ctest --test-dir build\Release                     (NXW_DUMP=<2.0.1 ExeFS>)
_local\m6-run.ps1 -Name <run> -Seconds 150 -Ctl -Cache _local\m6-cache -Env @{ DM_FIXED_TIME = "1793793600"; TZ = "UTC" }   (a copy of the Level 2 backup)
python -I _local\m6\m6drive.py --ctl _local\nxrt-run\<run>\ctl.txt --out _local\nxrt-run\<run>\shots --ref _local\m6\ref [--plan walk|save|load|perf|dump|drift]
Mac: cd ~/m6 && ./m6run.sh <tag> 300 TZ=UTC &  python3 m6drive.py --ctl ~/m6/runs/<tag>/ctl.txt --out ~/m6/runs/<tag>/shots --ref ~/m6/ref
python -I _local\m6\cmp.py <mac shots> <windows shots> <out>
```

Sources consulted are logged in `docs/PROVENANCE.md` (2026-11-04, M6 phase A).
