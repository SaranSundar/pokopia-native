# 1.3.0, the graphics update (Windows)

Status, 2026-11-06: on `main` at f5ddb0c (the `gfx` branch fast-forwarded), local only. This note covers what the Windows build gains over 1.2.6
(`main` at 6bbb3ec, from which 1.2.6 was packaged). Story co-op and the new builder are separate and not described here.

## What's new for players

- **A sharper picture.** A new **Resolution** setting draws the game itself at a larger size: the 3D scene, its lighting and effects and the HUD, not
  just the finished picture stretched to the window. The choices are the real sizes the game draws at:
  - **Default: your monitor**, the size the game starts at;
  - **1920x1080** (the game's own size, as in 1.2.6);
  - **2560x1440**;
  - **3840x2160**.

  The menu shows the size Default picked, for example "Default: your monitor (2560x1440)". On a graphics card with 4 GB of its own memory or less,
  Default stops at 2560x1440 so the card does not run out; 3840x2160 can still be picked by hand. Docked and handheld both draw 1920x1080 in Pokopia,
  so the list is the same in both modes.
- **Settings inside the game.** Press **F1** (or **Minus + Plus** together on a controller) for a settings window over the game:
  - **Graphics:** Resolution, Display mode (window or full screen), Window size, Console mode, Upscaling, Sharpness, Frame rate, Vsync, FPS counter.
  - **Controls:** the keyboard key for each of the 21 actions. Taking a key another action had swaps the two; Reset to defaults brings the usual keys
    back.

  Changes take effect at once, except Console mode, which applies from the next start. They are saved, and the launcher's Display and Controls tabs
  show the same settings. When the picture is at least as big as the window, Upscaling and Sharpness are greyed out with "Not used: the picture isn't
  stretched", and a line says when the window is too small to show the extra detail.
- **An FPS counter**: Off, Corner (frames per second in a small pill at the top left) or Detailed (adds the frame time and the size the game draws at).
- **30 fps works.** The Display tab's "30 fps" did nothing in 1.2.6 (a known issue). Each frame now stays on screen for two refreshes, and the world
  still moves at the right speed.
- **Less CPU work per frame.** The renderer's CPU time per frame is about a third lower: 7.5 ms → 5.0 ms in the field on our test PC (i9-13900KF).
  This comes from three changes by Quantumenis:
  - textures that are rewritten with the same bytes are not uploaded again;
  - draws only record the settings that changed;
  - identical resource bindings are reused within a frame.

  A fast PC was already at 60 fps; a slower one should gain frames.
- **A fix for a freeze on NVIDIA cards.** Players reported the whole game stopping with "device lost" on NVIDIA cards:
  - **RTX 50, #12:** fixed in 1.2.6.
  - **RTX 3050 Laptop, D-1:** the same kind of GPU hang in another compute shader. The fix now covers every shader of that kind in the game: 11
    shaders.
- **The launcher's Display tab** lists the same real sizes as the in-game menu, names the console modes "Docked (TV)" and "Handheld", and shows
  Default's size, including the 2560x1440 limit on 4 GB cards.

## Switches (environment variables)

Players do not need these; they are for testing and for comparing against 1.2.6. A switch given from outside wins over the saved settings.

| Switch | What it does |
|---|---|
| `NXRT_VK_RESOLUTION=default` or `<w>x<h>` | The size the game draws at, as the Resolution setting (the launcher sets it from `display.json`). |
| `NXRT_VK_SCALE=<factor>` | The same as a factor (1 to 4), for tests; wins over `NXRT_VK_RESOLUTION`. |
| `NXRT_VK_VRAM_MB=<MB>` | Pretends the graphics card has this much memory (tests the 4 GB limit on Default); also read by the launcher. |
| `DM_FPS=30` | 30 frames a second (the Frame rate setting). |
| `NXRT_FPS_COUNTER=corner` or `detailed` | The FPS counter. |
| `NXRT_TEX_HASH=0` | Turns off skipping unchanged texture uploads. |
| `NXRT_VK_STATE_CACHE=0` | Turns off recording only the changed draw settings. |
| `NXRT_VK_SET_REUSE=0` | Turns off reusing identical resource bindings. |
| `NXRT_VK_BALLOT_FIX=0` | Leaves the shaders of the NVIDIA freeze fix as translated (the 1.2.5 behaviour). |
| `NXRT_SETTINGS_DIR`, `POKOPIA_SETTINGS_DIR` | Another folder for `display.json` and `controls.json` (tests); default `%APPDATA%\Pokopia Native`. |
| `NXRT_VK_SHOT_WINDOW=1`, `DM_CTL` lines `menu ...` | Test tools: screenshots of the window with the menu, and the menu driven by script (docs/in-game-menu.md). |

The ones from 1.2.6 (`NXRT_VK_UPSCALE`, `NXRT_VK_SHARPNESS`, `NXRT_VK_PRESENT`, `NXRT_VK_WINDOW`, `NXRT_VK_FULLSCREEN`, `DM_OPMODE`, `DM_KEYMAP`) are
unchanged.

## What was checked, and on which GPU

Every run below was on one PC: **NVIDIA GeForce RTX 4070 Ti (12 GB), driver 616.92, a 3840x2160 monitor**, Windows 11, i9-13900KF. On that PC Default
is 3840x2160, so Default and 2x are the same size; 1x (1920x1080) and 1440 (2560x1440) were run as well. All game runs used copies of the saves.

- **Automated tests** (ctest, `build\pack`), on `gfx` before the merge:
  - 46 passed, 1 failed, 8 skipped. The failure was `nxrt.generated` (an HLE inventory with stale line numbers), fixed in f5ddb0c.
  - With the game's ExeFS given (`NXW_DUMP`), 4 of the dump tests pass; `nxlift.dump` skipped (no analysis database in that worktree).
  - On `main` after the merge (a fresh `build\pack`, `NXW_DUMP` set): 55 of 55 passed, none skipped. main has the analysis database, the texture
    corpus and the `build\Release` that the last skipped tests need.
- **The field route** (title → field, walking, the camera, the bag), Vulkan validation on:

  | Run | Version | Resolution | Validation errors |
  |---|---|---|---|
  | v201-default | 2.0.1 | Default = 3840x2160 | 1 (see below) |
  | v201-default2 | 2.0.1 | Default = 3840x2160 | 0 |
  | v201-1x | 2.0.1 | 1920x1080 | 0 |
  | v105-default | 1.0.5 | Default = 3840x2160 (menu opened over the field) | 0 |
  | v105-1x | 1.0.5 | 1920x1080 | 0 |

  Warnings: 12 in each run, all "a fragment output with no attachment to write to", as in 1.2.6.

  The one error ("an image used in GENERAL layout while still UNDEFINED", at the moment the field loads) appeared once in two identical runs. The same
  message is in an M5-C validation run from 2026-11-03, long before this update: an old, intermittent message at that one moment, not new here.
- **The in-game menu**, 2.0.1, validation on, 0 errors in each run:
  - every Graphics row changed while the game ran: each resolution, full screen and back, a window size, console mode, upscaling, sharpness, 30 and
    60 fps, vsync off and on, each FPS counter;
  - a key rebound, with the other action swapped;
  - the menu closed and reopened, and it remembered;
  - Reset to defaults.

  Then the game was started again from those saved settings, which gave handheld, 2560x1440, the 1600x900 window, 30 fps, the corner counter and the
  rebound key. It was started once more after switching back, which gave docked, Default, 60 fps and the default keys.
- **The 4 GB limit:** checked by pretending the card has 4096 MB. Default became 2560x1440, the log said so, and the menu showed "Default: your monitor
  (2560x1440)". Without the pretend size nothing changes. The launcher's check gives 1440 at 4096 MB and 2160 at 8192 MB and on this card; its Display
  tab was not opened on screen.
- **The NVIDIA freeze fix** (docs/ballot-hang.md):
  - the shader tests pass (14 of 14);
  - the 11 rewritten shaders pass the SPIR-V validator and differ from the translation only where intended;
  - the field route runs with them, pictures the same by eye as without the fix.
- **The CPU work** (docs/cpu-work.md): each piece measured on both versions, with an off switch that brings back the old numbers.
- Screenshots of the menu and the field: `_local\gfx\runs\release\<run>\shot-*.png` (game-derived, never in git).

## Updating a 1.2.6 build folder

Setup + Build with the 1.3.0 kit redoes only these steps. Measured on our PC on a copy of a 2.0.1 build folder that 1.2.6 made:

| Step | What happens | Time |
|---|---|---|
| Analysis (`program.db`) | Runs again: `nxanalyze.exe` and `hle_inventory.tsv` have new bytes. The tool differs only in its link time stamp, the inventory only in source line numbers. The new `program.db` is byte for byte 1.2.6's. | 3 s |
| game.dll | Rebuilt: `nxlift.exe` has a new link time stamp, so its stamp no longer matches. Every one of the 305 parts comes from the object cache (keyed by its code, which is unchanged); nothing is compiled, only lifted again to look the parts up, then linked. The result differs from 1.2.6's game.dll only in its time stamp. `nxrt.lib` is unchanged. | 6 min at 2 workers, idle priority (the first build took 33 min) |
| Shaders | Not translated again: nxshader and its ABI header are unchanged, and shaders already in the cache are kept. The freeze fix rewrites shaders as the game loads them, so 10 shaders get new pipelines, built once at first use. | none |
| Runtime and launcher | `nxrt.exe` and `play.ps1` copied (both changed). | seconds |

## Known limits

- **Only an RTX 4070 Ti ran it.** No RTX 3050 or other 4 GB card (the 4 GB limit was checked by pretending), no AMD, no Intel, no RTX 50.
- **The freeze fix for D-1 is expected to work, not proven.** It follows #12's bisect, where the same change stopped the RTX 50 freeze, and the same
  shader shape. The freeze itself was never reproduced here. Three rarer counters of other shapes are left as they were (docs/ballot-hang.md).
- **Larger sizes cost GPU time.** 3840x2160 is four times the pixels of 1920x1080; on a slower card pick a smaller size.
- **The launcher's 4 GB check** reads the graphics adapters' memory from the registry; on a PC with two dedicated cards it looks at the larger one.
- **Input by hand:** F1, the keyboard, the mouse and a controller in the menu were tried by the user on the first build. Tonight's checks drove the menu
  by script.
- **At 30 fps** the picture reaches the screen about a frame later than at 60.
- **Mac is not included**: this is the Windows build. The Linux beta is separate.

## Credits

- **Quantumenis** (https://github.com/Quantumenis):
  - the three CPU changes: the texture page hash, the draw state cache and the binding reuse, committed under his name;
  - finding that the 30 fps setting did nothing;
  - #12's bisect, which showed what the NVIDIA freeze fix had to change.
- The players who reported the freezes on Discord and on GitHub, with their logs.

## Commits since 1.2.6 (6bbb3ec..f5ddb0c)

| Commit | Change |
|---|---|
| d31c131 | Higher resolution: the game's render targets drawn 1.5x / 2x / 3x |
| 53318db | The 30 fps setting works |
| 717280e, e653b2b | Research note: DLSS / FSR 2 would not have their inputs |
| 37a92f7 | Texture re-uploads skipped when the written pages hold the same bytes (Quantumenis) |
| 7c3a9a5 | Draws record only the dynamic state that changed (Quantumenis) |
| 889e5e4 | Descriptor sets with the same content reused within a submission (Quantumenis) |
| 2f25adc | cpu-work.md: the CPU work measured piece by piece |
| cddd6d0 | Window screenshots on a thread of their own (tests) |
| 08772b7, e98e444, 23bc7ab, 09fec69, 09eaec6, eb3c1e4 | The in-game settings menu, real sizes, the FPS counter |
| 30c499c, cd9f4ae | The NVIDIA freeze after early returns (D-1): every counter of that kind made per lane |
| 997464d | Default resolution kept to 2560x1440 on cards with 4 GB or less |
| f5ddb0c | hle_inventory.tsv: line numbers after the 30 fps and menu changes |
