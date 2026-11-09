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
through, so nothing stays held), and it keeps running behind the panel. Three tabs:

- **Graphics**: Resolution, Display mode, Window size, Console mode, Upscaling, Sharpness, Anti-aliasing, Frame rate, Vsync, FPS counter, Wide view: the launcher's Display tab.
- **Controls** (`menu_controls.c`), two pages:
  - **Keyboard:** the keyboard's key for each of the 21 actions (the launcher's Controls tab): choose an action, press its key; Reset to defaults.
  - **Controller:** which controller is player 1, kept for next time; the controller button for each of the 16 Switch buttons, one map for every
    controller: choose a button, press the controller button for it; Reset to defaults. See [controllers.md](controllers.md).
- **Link Play**: how this game plays Link Play, what to give a friend, the room code with Copy buttons, who is in the session ([below](#the-link-play-tab)).

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
| Anti-aliasing | Off, FXAA, SMAA | from the next frame: smooths the 3D picture's edges before the HUD is drawn (docs/anti-aliasing.md). SMAA by default. `display.json` "antialiasing", `NXRT_VK_AA` |
| Frame rate | 60 fps, 30 fps | from the next frame (docs/frame-rate-30.md); the game keeps its speed |
| Vsync | On, Off | the swapchain is made again with FIFO or mailbox |
| FPS counter | Off, Corner, Detailed | at once: a small white pill at the top left with the frames per second; Detailed adds the frame time and the size the game draws at ("60 fps \| 16.7 ms \| 3840x2160"). `display.json` "fps_counter", `NXRT_FPS_COUNTER` |
| Wide view | Off, On (experimental) | from the next start: on a screen wider than 16:9 the view fills it, the 2D stays in the middle 16:9 (docs/wide-view.md). `display.json` "wide_view", `NXRT_VK_WIDE` |

## The Link Play tab

Branch `linkplay-ui`, 2026-11-07. What Link Play is doing in this game, read from the coop node every half second (`coop_get_info`, one snapshot under
the node's lock) and put in plain words by `front/linkplay.c`:

- **How you play**: alone (Link Play off for this game), on this network, hosting over the internet or a VPN, visiting an address (on this network
  or over the internet), or a test link on this PC (`play.ps1 -CoopHost` / `-CoopJoin`).
- **What is happening**: not in a session (with where to go: in the game, Link Play, then Local connection), opening your town, your town is open
  (and how many players), looking for towns (how many found, or the address), visiting the host's town by its name, stopped with an error.
- **Give your friends**: this PC's addresses that fit how you play. On this network the home-network ones (192.168.x and the like; link-local ones
  are left out; when the PC has a VPN's address the note says VPNs need internet play). Over the internet a VPN's address as well, named (Radmin
  VPN's 26.x, Hamachi's 25.x, Tailscale's or carrier NAT's 100.64/10), and a public one. When none of them reaches a friend over the internet, the
  note says to give the router's internet address (search "what is my IP") and forward the port. A port other than 47600 (the system picked another)
  goes with each address.
- **Port forwarding** while hosting over the internet (docs/coop-nat.md): the router is asked to forward the port (UPnP, NAT-PMP or PCP); once it
  does, its internet address comes first in the list and in Copy invite, else the note says what stands in the way (carrier-grade NAT, a second
  router, the port taken, no answer) and what to do.
- **Room code** (internet play only) with **Copy**, and **Copy invite**: one line with the address (a VPN's first, then a public one, else "my
  internet address") and the code, to paste in a chat. Both go to the system clipboard; the log says what was copied, never the code itself.
- **Players**: the session's players by the names the game gives them, the host and you marked, with the real address where this device knows it
  (a host knows each visitor's, a visitor the host's). The menu's font is ASCII, so other letters show as `?` (José is "Jos?").

Up / down (or left / right) choose a button, Enter or A copies; the mouse clicks them.

### Change how you play

**Change how you play** (on the tab) shows the Link Play settings, the same ones the launcher's window keeps in `linkplay.json`:

| Row | Choices |
|---|---|
| How to play | Alone; On this network (the same Wi-Fi); Host over the internet or a VPN; Visit a host over the internet or a VPN (Linux adds Visit an address on this network: it has no DNS-SD) |
| Host's address | visiting only: Enter, then type it (digits, `.` and `:`), Ctrl+V pastes, Enter keeps it, Esc puts the old one back |
| Room code | visiting over the internet: the host's code, typed or pasted; hosting: a new code each time, or keep the last one |
| Interact everywhere, Story co-op | On, Off (greyed for a game version without them) |

**Apply** checks the settings the way the coop module will read them (an address it wouldn't take, such as a VPN's on this network, is refused with
the reason), saves them to `linkplay.json` (in the launcher's names: play.ps1's `alone / lan / host / visit`, play.sh's `alone / host / join /
inet-host / inet-visit`; the keys it doesn't know, such as `ask`, kept), and applies what it can. A line under the buttons says which:

- **Now**: a different way among the Link Play ways, another address or code, while the game's Link Play is closed (nn::ldn not initialized: before
  Link Play → Local connection, or after leaving it). The menu sets `DM_COOP`, `DM_COOP_JOIN`, `DM_COOP_INTERNET` and `DM_COOP_ROOM` as the launcher
  would and makes the coop node again (`hle_offline.c dm_coop_remake`; the old one stays allocated, a game thread may still hold it).
- **Once Link Play closes**: the same while it is open: leave Local connection and press Apply again.
- **From the next start**: Link Play on or off (from or to Alone: whether the game may use Link Play at all is asked at its start), Interact everywhere
  and Story co-op (their hooks go in as the game DLL loads, and every player's mods fingerprint must match). A way and the mods changed together: the
  way now, the mods at the next start.

The launcher reads `linkplay.json` when it starts the installer way (`-Config`, or `-Ask`): with "Don't ask again" the game starts the saved way, else
the window opens on it. Defaults are unchanged: Alone until someone chooses otherwise (a default of "On this network" would bring up Windows Firewall's
question for everyone).

`DM_CTL` `menu tab 2` shows the tab; `menu lp status|settings|mode <launcher name>|join <address>|room <code>|keep 0|1|interact 0|1|story 0|1|apply|copy
room|invite` drive it (tests). Tests: `coop.loopback` and `coop.inet` check the snapshot (names, addresses, room code, states); `nxrt.linkplay` the words for
each way of playing and the settings (the file as PowerShell 5 writes it with its UTF-8 mark, merging, the environment, when a change applies);
`nxrt.coop_remake` the node made again while Link Play is closed and not while it is open; `scripts.play_defaults` play.ps1 starting the way the game
saved (`-Ask`, dry run).

Seen in the game (2.0.1, one window, 2026-11-07): the status view (alone on this network: the PC's addresses, no room code, no players), the settings
view, and Apply to "Visit a host over the internet or a VPN" with an address and a code typed in lower case with spaces: the coop node made again (its
log line), `linkplay.json` written in play.ps1's form, and the status view showing the visit with the code (upper case, dashes) and Copy.

### The Mac and the iPhone

They have no F1 menu. For the same, their launcher (nxnative's settings rows from `apply_coop.py`) would need:

- one "How to play" choice in place of the separate Link Play, Join address and Internet play switches (the same four ways);
- the room code shown in the launcher with a Copy button when hosting over the internet: today the Mac host reads it in the log, the iPhone host never
  sees it; and a "keep my room code" choice;
- what to give a friend (this device's addresses, a VPN's named) and, during a session, who is connected: a small status page or notification;
- the same checks and words when an address won't do (a VPN's address on this network);
- Interact everywhere and Story co-op greyed for a game version without them (the builder only builds versions that have them today).

## Using it

| | Keyboard | Controller | Mouse |
|---|---|---|---|
| open / close | F1 | Minus + Plus | Close button |
| choose a row | arrows (or W / S) | D-pad, left stick | point |
| change | Enter / Space opens the list; left / right step through it | A opens the list; D-pad left / right step | click the value, click a choice |
| tabs | Q / E, Page Up / Down, Tab | L / R | click the tab |
| back | Esc closes a list, then the menu | B | right click |

A and B go by the controller's labels (SDL's button labels), so a Nintendo-layout controller confirms with its own A. To set a key: choose the action,
then press the key; Esc keeps the old one. On the Controller page: choose the Switch button, then press the controller button or pull the trigger for it
(Esc, or 8 seconds, keeps the old one); on the Player 1 row, left / right step through the controllers, Enter waits for a button on the one wanted. The keys the launcher refuses are refused here too (Esc, F11, F1, Alt, the Windows keys). A key another
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

- `front/menu_linkplay.c` (the Link Play tab's drawing and input), `front/linkplay.c` (its words) and `front/linkplay_prefs.c` (linkplay.json, the
  environment of each way, when a change applies; both without SDL, `nxrt.linkplay` tests them) are files of their own; `front/menu_int.h` gives them the
  menu's colours and drawing calls. The clipboard is written on the window thread (an SDL user event), and so are the keys typed into a field (under
  the tab's own lock). Applying now: `hle_offline.c dm_coop_remake`, which calls `coop_env_make` (a node from `DM_COOP*` each call).
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
  console, upscale, sharpness, fps, vsync, counter, wide; choices count from 0 in the menu's order, the Resolution list's for the running mode; `list`
  opens a row's list as Enter would). The Controls tab's Controller page: `menu ct page <0|1>|pad <button> <input>|padreset|p1 <controller>|capture <button,
  -1 player 1>`; with `DM_PAD_VIRTUAL=<n>`, `menu vpad <i> <input> <0|1>` presses a virtual controller's input (controllers.md).
  `NXRT_VK_SHOT_WINDOW=1` makes each `shot` save the window as well (`.window.png`), menu included.
  `nxrt.pad_sdl` drives the menu with real SDL events; `scripts.play_defaults` checks the launcher's side.
