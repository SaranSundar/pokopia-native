# Split-screen co-op: two players on one PC (prototype, 2026-11-08)

Branch `splitscreen` (from main 1c244a3), for 1.3.6. Windows; the pieces are nxrt's and the launcher's, so Linux can follow.

## How it works

Two games side by side, joined by Link Play over this PC (loopback, the same way as two PCs on a network):

- **Window 1** hosts with the player's own save. It plays the keyboard, the mouse and the first controller.
- **Window 2** visits with the **split-screen save** and plays the second controller. It is muted, never takes the keyboard focus, and closes when
  window 1 closes.
- **The split-screen save** is made from the player's save the first time (`scripts/splitsave.ps1`) and then kept in its own folder (`<save> (split
  screen)`), so the second player keeps what they pick up. The player's save is only read. The game refuses a second copy of a save in its own session
  (error 9005, "a user with the same user ID is already in the session"); what it compares is the 16-byte user ID in `S00\SYDAT.BIN` at 0x61, a file
  without a checksum ([coop-step1.md](coop-step1.md#why-one-save-is-refused)). The split save is a copy with only that ID changed (SHA-256 of a fixed text
  and the old ID: made again from the same save it is the same second player).
- **Interact everywhere** is on in both: in a story town the visitor can do little without it. `-Story` turns Story co-op on too.

## Starting it

In the launcher window: Link Play tab → "Split screen: two players on this PC" (with "Story co-op" if wanted) → Play. play.ps1 then hands over to
split.ps1 with the same paths. From a command line:

```
powershell -ExecutionPolicy Bypass -File scripts\split.ps1 -Config <play.json>          # the installed game's paths
powershell -ExecutionPolicy Bypass -File scripts\split.ps1 -ExeFS ... -Dll ... -RomFS ... -Save ... -Exe ... -Shaders ...   # or given one by one
```

Options: `-Story` (Story co-op), `-Keyboard` (player 1 on the keyboard and mouse only, player 2 the first controller), `-Layout stack` (above each other
instead of side by side), `-Sound2` (window 2 with sound), `-Port` (default 47660), `-SplitSave <folder>`, `-DryRun` (prints both command lines, makes
nothing). Then in the games: window 1, PC → Link Play → Invite Others to Visit → Local connection; window 2, PC → Link Play → Visit a Friend → Local
connection → the host's town.

nxrt's switches behind it (`split.ps1` sets them; `play.ps1` passes them on):

| switch | does |
|---|---|
| `DM_SPLIT=1` / `2` | this game is window 1 or 2: borderless, its half of the main display's usable area at 16:9 and centred (vk_window.c); size and fullscreen requests (F11, Alt+Enter, the Display tab) ignored; window 2 is not focusable |
| `DM_SPLIT_PAD=<n>` | the controller in place n (1-based, in connection order; the one remembered as player 1 comes first) is this game's player 1; 0 none. Default: the window's number. Other controllers do nothing here; controllers play while the window is out of focus (front_sdl.c) |
| `DM_SPLIT_LAYOUT=stack` | top and bottom halves |

Window 2 takes no keyboard or mouse input. In split mode play.ps1 starts the game with its log console minimized.

## Tested (2026-11-08, this PC)

- `scripts.split_save` (15 checks, stand-in saves): only the ID's 16 bytes differ, every other file is byte-for-byte the same, the player's save is
  unchanged, made once then kept (the second player's changes stay), the same ID when made again, refusals (no system save yet, the same folder, a plain
  copy, a folder holding something else, a file without the game's magic).
- `nxrt.pad_sdl_split` (virtual controllers, DM_SPLIT=2): only the second controller plays, as player 1, also out of focus; the first, the keyboard and the
  mouse do nothing; detached, nothing stays pressed. `nxrt.pad_sdl` (one window as before) still passes.
- **The game**, copies of a Level 2 save (test copies only): `split.ps1 -Story` made the split save, both windows opened side by side (each 1920x1080 on a
  3840x2160 display at 150%), window 1 hosted, window 2 found it ("Demomax, Withered Wasteland") and **arrived** (no 9005); both screens show both
  characters. Closing window 1 closed window 2 within a second.
- **Not tested:** two real controllers (none could be connected here: the controller split is covered only by the virtual test), sound from window 1
  with window 2 muted (the switches are play.ps1's own -Silent), a long session, the second player's save after a session.

## Cost (this PC: Core i9-13900KF, 24 cores / 32 threads; RTX 4070 Ti; 32 GB; both games in town, 1920x1080 windows)

| | each game | both |
|---|---|---|
| frame rate | 59.9 per second, p99 19 ms | both steady |
| CPU | about 2.5 cores busy (8% of 32 threads) | about 5 cores |
| GPU (3D engine) | 13-14% | 27% |
| memory | about 5 GB (2.1-2.2 GB private, the rest the mapped game files) | free memory went from 11.8 GB to 1.1 GB |

What a PC needs, by these numbers (only this PC was measured): memory is the limit. **32 GB recommended, 16 GB is too tight** (two games take about
10-11 GB before Windows and a browser). A CPU with 8 or more cores; a graphics card that runs one game comfortably at 60, since it renders twice
(lowering the game resolution in the Display tab, or the console mode "handheld", 720p, halves the GPU's share).

## Limits (by design, as with any Link Play)

- The second player is a **visitor**: the host's town, the host's story. Story co-op's limits apply (docs/story-coop.md): what the visitor does in the
  world counts for the host, story steps only through the host (the story-host branch fixes the host's own story while a session is open).
- The second player starts as a **copy of the first character**: the same name and look (both are "Demomax" in the test). Changing them needs the
  compressed CMNDAT.BIN (not done).
- Both players go through the game's own Link Play menus each time (host, then visit). Automating that is open.
- Window 2's first start copies window 1's shader cache, so it does not compile from nothing.

## What's left

1. Two real controllers on real hardware (Xbox through XInput, a DualSense or Switch Pro through SDL's HIDAPI in two processes at once: the HID ones may
   fight over lights and rumble).
2. F1 → Link Play: "Start split screen" from inside the game (window 1 would have to take its half while running). The launcher's choice is done
   (tried as a dry run from a remembered choice; the window itself not clicked through).
3. The second character's name and look (CMNDAT.BIN), so the two can be told apart.
4. Automating the Link Play menus (host and visit) at start.
5. Linux (the same switches; split.ps1's part in play.sh), Mac and iPhone (no second instance on an iPhone).
6. Measure on a 16 GB PC and a mid-range graphics card.
