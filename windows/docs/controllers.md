# Controllers: rebinding, player 1, more controllers

From Atraxia (Discord) and GitHub #34 (Steam Deck). Windows and Linux share all of it: `front/front_sdl.c`, `front/front_map.h`, `front/pad_slots.h`, the
in-game menu's Controls tab (`front/menu_controls.c`). The Mac and iPhone builds use nxnative's own front end. The installer's Switch 2 host patch gives them
the same `controls.json` format (its `dm/host/controls_map.h`) and an in-game Settings window of their own:
- the Mac: F1 or ⌘,, with keys, the controller map, player 1 and live graphics;
- the iPhone: a controller's Home button or holding the layout button, with the controller map, player 1 and live graphics.

They remember player 1 as `"player1_mac"` by name (no SDL GUIDs there) and keep the Windows `"player1"` fields they find.

## One map for every controller

- **What it is:** the controller button for each of the Switch's 16 buttons, in `controls.json` beside the keyboard's keys, as a `"pad"` object:
  `"pad": {"A": "south", "B": "east", ...}`. Inputs are named by their place (SDL's names: `south` is the bottom face button whatever is printed on it),
  plus `lefttrigger` / `righttrigger` and `none`.
- **Without a `"pad"` object** the game keeps the layout it always had: face buttons by place as on a Switch, or by printed letter with `DM_PAD_FACE=labels`.
  Reset to defaults removes the object.
- **Rules,** as for the keys: Guide (Home) can't be bound; an input asked for twice goes to the first button; when a button takes an input another one had,
  the two swap. A trigger counts as pressed past a quarter of its travel.
- **Where to change it:**
  - the game's F1 menu, Controls tab, Controller page: choose a Switch button, then press the controller button (or pull the trigger) for it;
  - the Windows launcher's Controls tab, Controller buttons: a dropdown for each button (the launcher can't read controllers, so there is no "press it").
- **Older files keep working:** the keyboard's parser skips the `"pad"` object (its values are text), and the launcher keeps the fields it doesn't change.

## Choosing player 1 (#34)

- **Before:** controllers took players in the order they connected. On the Deck the built-in controls and an external pad connect together, so which one was
  player 1 was luck.
- **Now:** the F1 menu's Controller page has a Player 1 row. Left / right step through the connected controllers; Enter, then any button on the controller
  wanted, picks that one. Whoever was player 1 takes the chosen controller's old place.
- **It is remembered** as `"player1"` (SDL's GUID) and `"player1_name"` in `controls.json`. When that controller connects later it takes player 1, whatever
  the order. The name is kept too because Steam's virtual controllers share one GUID.
- **The Windows launcher** shows which controller is player 1 and can forget it. With `DM_KBD_PLAYER=own` the keyboard keeps player 1; with `DM_PAD_MERGE=1`
  every controller is player 1 anyway.

## More controllers

- **The Steam Controller:** SDL's HIDAPI driver for it is switched on (`SDL_JOYSTICK_HIDAPI_STEAM=1`; an environment variable of that name still decides).
  Under Steam (Big Picture, the Deck) Steam Input turns controllers into virtual Xbox pads, which worked before. The Valve controller announced after it
  has no driver hint in SDL 3.4.18: it may need a newer SDL.
- **gamecontrollerdb.txt:** SDL_GameControllerDB's community list (zlib; `src/nxrt/front/third_party/SDL_GameControllerDB`, its README names the
  commit and checksums) is copied beside `nxrt.exe` by the build and the kit, and read at start; then a `gamecontrollerdb.txt` in the settings folder
  (a player's own lines win). The log says how many mappings each gave (on Windows: 573 mappings, by SDL's count, from the file's 869 Windows lines).
- **A device SDL has no mapping for** gets a guessed layout instead of being ignored: buttons 0-3 the face buttons, 4 and 5 the shoulders, 6 and 7 Back and
  Start, 8 and 9 the stick presses; axes 0-1 the left stick, 2-3 the right, 4-5 the triggers; hat 0 the D-pad. The log prints the mapping; the Controller
  page, or a line in `gamecontrollerdb.txt`, fixes what is wrong.

## Tests

- **`nxrt.input`:** the default map plays exactly as the old layout; a file's map with refusals (Guide, an unknown name, an input asked twice), `none`, the
  keyboard's numbers beside it ignored; written and read back the same; swaps; player 1 chosen (the two swap with their names; a holder that keeps
  player 1); the name cleaned for the file.
- **`nxrt.pad_sdl`** (SDL's virtual gamepads through the real front end): a second gamepad made player 1 on the Controller page (the first takes its
  place), the bottom button captured for A; the game then reads A from it, the old player 1 plays with the same map, B took A's old button (the two
  swapped), and `controls.json` holds the map and player 1. The Keyboard page's binding still works.
- **Virtual controllers:** `DM_PAD_VIRTUAL=<n>` attaches n of SDL's virtual gamepads ("Virtual pad 1"...), and DM_CTL `menu vpad <i> <input> <0|1>` holds
  or lets go one of their inputs. With the F1 menu's `menu ct ...` lines (`page`, `pad`, `padreset`, `p1`, `capture`) the whole path runs without hardware.
- **Checked here on 2026-11-07,** with two virtual pads:
  - a file's map loaded;
  - player 1 chosen in F1, saved and taken again at the next start;
  - a button and a trigger captured on the Controller page, with the swap;
  - the launcher's Controller page set a button, swapped, and kept the map and player 1 the game wrote;
  - the Keyboard page unchanged.
- **`nxrt.pad_sdl`** also checks the file: an entry for this platform that SDL's own list lacks is mapped once the front end has started.
- **Not checked:** real controllers (none here), the Steam Controller, a guessed layout on a real device, Linux and the Deck.
- **A limit:** two controllers of one model share SDL's GUID and name, so the one remembered as player 1 is whichever of them connects first.
