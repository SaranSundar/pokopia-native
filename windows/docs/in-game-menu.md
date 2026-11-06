# The in-game settings menu

Status, 2026-11-06: on the `gfx` branch, local only. Windows (and, being SDL3 and Vulkan only, the Linux port's runtime as it is).

**The approved design is the first version's look** (08772b7), which the user tried and wants kept exactly, plus the label changes below: a white
panel (`C_PANEL` RGBA 250, 248, 255) with a lavender edge, "Settings" at the top left, yellow pill tabs at the top right (`C_SEL` RGBA 255, 222, 70 for
the active one, `C_TAB` for the other), rows with the label on the left and a dropdown field on the right (`C_PILL` lavender, the selected row's field
yellow, the row itself soft yellow), greyed rows in `C_OFF`, the description and the key hints at the bottom, a Close button; the Controls tab in two
columns, each action's key in a pill. Commit e98e444 recorded a coloured mockup (‹ › buttons, key chips, a dashed info box) as "the design the user
approved"; that was only a sketch, and this section replaces that wording. From the user's feedback on the first version (2026-11-06): **every size the menu shows is the size the game really draws**, Console mode without
sizes, a plain-words line about the picture and the window, Upscaling and Sharpness greyed with the reason when unused, and an FPS counter.

**F1** (or a controller's **Minus + Plus**, SDL's Back + Start, together) opens a settings panel over the running game; the same again, Esc / B, or
the Close button closes it. While it is open the game gets no input (what was held is let go, the mouse is freed from mouse look; key-ups still go
through, so nothing stays held), and it keeps running behind the panel. Two tabs:

- **Graphics**: Resolution, Display mode, Window size, Console mode, Upscaling, Sharpness, Frame rate, Vsync, FPS counter: the launcher's Display tab.
- **Controls**: the keyboard's key for each of the 21 actions (the launcher's Controls tab): choose an action, press its key; Reset to defaults.

Every change takes effect at once, without a restart, except **Console mode**, which the menu saves for the next start and says so (switched live,
player 1's controller would turn into the handheld one under the game: `hle_hid.c connected_id`). Each change is saved at once to the files the
launcher uses, `display.json` and `controls.json` in `%APPDATA%\Pokopia Native` (`NXRT_SETTINGS_DIR`, which `play.ps1` sets), so the launcher shows the
menu's choices next time and the other way round.

## Resolution: the size the game draws its picture at

"The size the game draws its picture at. Bigger looks sharper but works your graphics card harder. Default matches your monitor."

Pokopia draws its own picture at 1920x1080 **in both console modes**: with `DM_OPMODE=0` (handheld) its window crop is still 1920x1080, its
screen-sized targets are 1920x1080, and a frame at the same factor shows the same detail as docked (measured 2026-11-06, run h1). The request
assumed 1280x720 handheld (from the first menu's own "Handheld (1280x720, lighter)" label, which was wrong); a list built on that would show sizes
the game does not draw, so there is one list, the same in both modes, in the menu and the launcher:

| Resolution |
|---|
| Default: your monitor (W x H) |
| 1920x1080 (the game's own) |
| 2560x1440 |
| 3840x2160 |

Resolution is the real size the game draws at, never less than the game's own.

- **Default** is the monitor's height, at least the game's own and at most 2160, shown with the size it draws at ("Default: your monitor
  (2560x1440)"). It is the default: the game starts there unless the player picked something else. The monitor is the one the window is on
  (SDL's desktop mode times its pixel density); an ultrawide counts by its height (the picture is 16:9).
- On a graphics card with 4 GB of its own memory or less (the largest device-local Vulkan heap; D-1's RTX 3050 Laptop has 4 GB), Default stops at
  2560x1440, and the log says so once ("Default resolution kept to 2560x1440"). The launcher's Display tab shows the same size, from the display
  adapters' memory in the registry (one under 1 GB is an integrated GPU, which Vulkan gives shared memory instead). `NXRT_VK_VRAM_MB` pretends a size
  in both. A size picked from the list is drawn as picked.
- The factor F-5 draws at (docs/f5-resolution.md) is the chosen height over the game's own (1080): 2560x1440 is 1.333x, 3840x2160 2x. Were a game
  to draw handheld at 720, `base_h` in `front/menu.c` (and `$base` in `play.ps1`) is the one place to change.
- `display.json` keeps `"resolution": "default"` or `"<w>x<h>"`, the size as saved. A size not in the list (2880x1620 and 5760x3240 from the first
  menu) keeps its string and means that real size (up to 3240 lines); one below the game's own (1280x720 when docked) shows as and draws the game's
  own. A bare factor from before the sizes (1, 1.5, 2, 3) is read by the launcher as the size it meant docked (1.5 → 2880x1620).
- The launcher hands it to the game as `NXRT_VK_RESOLUTION`; `NXRT_VK_SCALE` (a factor) still works and wins, for tests.
- Changed in the game, the factor changes at the start of the next submission: every render target the F-5 rule takes is made again at the new size,
  its content scaled over (a blit), and those made later follow (`vk_tex.c vkt_apply_scale`).

## The picture and the window

Under the rows, only when it applies:

- the picture larger than the window: "Your window (1280x720) is smaller than the picture, so you won't see the extra detail. Make the window bigger
  or use full screen."
- the picture smaller than the window: "The picture is stretched to fill your window. Upscaling and Sharpness choose how that looks."

Whenever the picture is at least as big as the window, **Upscaling** and **Sharpness** are greyed with "Not used: the picture isn't stretched" in
their fields and cannot be opened. The launcher's Display tab shows the same line and greys the same two, from its window size (or the monitor in full
screen) and its Resolution.

## The other rows

| Row | Choices | What changes, and when |
|---|---|---|
| Display mode | Window, Full screen | at once (F11 and Alt+Enter still switch too; the menu follows them) |
| Window size | 1280x720 … 3840x2160 | at once (in a window only; greyed in full screen) |
| Console mode | Docked (TV), Handheld | at the next start; the row says "Handheld from the next start" while the saved mode differs from the running one. "Docked is the game's TV mode, handheld its portable mode; both draw 1920x1080 here. Changes at the next start; Resolution keeps its size." (The requested wording said docked draws 1920x1080 and handheld 1280x720: not so in this game, see Resolution.) |
| Upscaling | Sharp, Standard (softer) | from the next present (only used when the picture is stretched) |
| Sharpness | 0–10 | from the next present (Sharp only; only used when the picture is stretched) |
| Frame rate | 60 fps, 30 fps | from the next frame (docs/frame-rate-30.md); the game keeps its speed |
| Vsync | On, Off | the swapchain is made again with FIFO or mailbox |
| FPS counter | Off, Corner, Detailed | at once: a small white pill at the top left with the frames per second; Detailed adds the frame time and the size the game draws at ("60 fps \| 16.7 ms \| 3840x2160"). `display.json` "fps_counter", `NXRT_FPS_COUNTER` |

## Using it

| | Keyboard | Controller | Mouse |
|---|---|---|---|
| open / close | F1 | Minus + Plus | Close button |
| choose a row | arrows (or W / S) | D-pad, left stick | point |
| change | Enter / Space opens the list; left / right step through it | A opens the list; D-pad left / right step | click the value, click a choice |
| tabs | Q / E, Page Up / Down, Tab | L / R | click the tab |
| back | Esc closes a list, then the menu | B | right click |

A and B go by the controller's labels (SDL's button labels), so a Nintendo-layout controller confirms with its own A. To set a key: choose the action,
then press the key; Esc keeps the old one. The keys the launcher refuses are refused here too (Esc, F11, F1, Alt, the Windows keys). A key another
action had: the two swap (that action takes this one's old key).

The minus button reaches the game for the moment before plus completes the combination (the game may react to it); F1 does not.

## Checks (2026-11-06)

Scripted runs of 2.0.1 on copies of the save (`_local\gfx\gfxtest.ps1`, the menu driven by `DM_CTL`, settings in the run's folder; `-Restart`
starts through `play.ps1 -Ask` with a run's saved settings, as the installed game starts), RTX 4070 Ti, a 3840x2160 monitor:

- **Docked, from a settings file from before** (`"resolution": 1.5`, a 1920x1080 window): the launcher read it as 2880x1620, the game drew at
  1.5x, the menu's Resolution row said 2880x1620, the line under the rows said the window (1920x1080) is smaller than the picture, and Upscaling and
  Sharpness were greyed "Not used: the picture isn't stretched". Then live: Default (2x, "Default: your monitor (3840x2160)"), the FPS counter
  Detailed, 1920x1080 (1x), a 2560x1440 window (the stretched line, Upscaling and Sharpness back), Console mode Handheld ("Handheld from the next
  start"); each in effect at once and saved.
- **Handheld, after a restart from those saved settings**: the game came up handheld, at 1920x1080 (1x), in the 2560x1440 window, the counter on
  ("60 fps | 16.7 ms | 1920x1080"); Console mode read "Handheld"; the Resolution list the same four entries; Default 2x live.
- The game's presented picture at 1.5x was 2880x1620 in both modes (WindowCrop 1920x1080 handheld too), which is how the 1280x720 assumption was
  found wrong (an earlier run with a 720 base drew 5760x3240 for "Default" in handheld).
- The first version's runs (08772b7's checks, validation on, both game versions, 0 errors in 9,000 frames each) cover the drawing and the live
  resolution change, which this round leaves as they were; this round adds no renderer code beyond drawing the counter when the menu is closed.
- `nxrt.pad_sdl` (real SDL events: F1, keys not reaching the game, a key taken from another action swaps, a key let go while open, Esc, Minus + Plus,
  B) and `scripts.play_defaults` (an old factor kept as its size, a size by name, the counter, an older size keeps its string): ok.

Not checked by script: real F1, keyboard, mouse and controller input (a script would take the focus from the person using this PC).

## How it is built

- `front/menu.c`: the settings, the input, the layout as a list of quads (rounded rectangles, glyphs), the saving. The window thread feeds it events
  (`menu_event`, first in `front_sdl_event`: what it takes the game never sees), the present thread lays it out (`menu_build`) and applies the
  choices; one mutex between them. Platform-neutral: SDL3 only.
- `vk/vk_menu.c`, `shaders/menu.vert/frag`: the quads drawn over the picture in the present pass, alpha blended, one instance each from a host-visible
  buffer per present slot; the font uploaded the first time the menu or the counter shows. Nothing is made before that.
- The font: Aileron Regular (CC0, the copy Pillow bundles) as a signed-distance atlas made once by `front/menu_font.py`, so the text stays sharp at
  any size. The panel follows the window (laid out for 1080 lines, scaled).
- What it calls: `vkt_request_scale` (resolution), `vkw_request_fullscreen` / `vkw_request_size`, `vkp_set_upscale`, `vkp_set_mode` (vsync),
  `vk_set_fps_target` (frame rate), `front_sdl_keymap_set` (keys, under their own lock).
- Tests: `DM_CTL` lines `menu open|close|tab <0|1>|set <row> <choice>|list <row>|key <action> <scancode>|reset` (rows: resolution, display, window,
  console, upscale, sharpness, fps, vsync, counter; choices count from 0 in the menu's order, the Resolution list's for the running mode; `list`
  opens a row's list as Enter would). `NXRT_VK_SHOT_WINDOW=1` makes each `shot` save the window as well (`.window.png`), menu included.
  `nxrt.pad_sdl` drives the menu with real SDL events; `scripts.play_defaults` checks the launcher's side.
