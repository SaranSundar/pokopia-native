# Co-op: "Interact everywhere" (story co-op, step 1)

2026-11-04. The first step of story co-op (COOP-PLAN Route A): an **experimental, opt-in** setting that lets Link Play players use
moves and items and build in the story areas, where the game otherwise forces Spectator Mode on everyone. It is off by default,
and every player in a session must have it on: a session refuses a player whose setting differs.

| | Status |
|---|---|
| Windows | built and tested: two instances on this PC (loopback), host and visitor in Withered Wasteland |
| Mac | built by pokopia-native's builder with the new patch and gate; tested with Windows both ways ([the Mac](#the-mac)) |
| iPhone | an unsigned IPA from the same build (154 of 154 run-time shaders), for the user to sign and test |
| 1.0.5 | not supported: the setting does nothing there and does not enter the fingerprint |

## What the game does

Nintendo's rule: Spectator Mode, where "certain moves and items can't be used", can only be turned off in Palette Town. Elsewhere
the host's settings show it locked On, and the join notice says everyone will be restricted.

- **The check is a permission mask, read inline.** The game keeps 36 permission bits at `GameMgr+0x8c` (GameMgr = `*(main+0x3561710)`;
  bits 32–35 are the byte at `+0x90` that research/06 called the role bits: online, host, visitor). Gameplay tests the bits directly
  (no function call), which is why the static pass did not find "the" check and why the visitor bit has thousands of readers.
- **One function builds the mask** (`main+0xb0f60`): when hosting starts, when a town loads (from the session's parameters) and when
  the session ends. Measured patterns (bits 0–31):

| case | pattern |
|---|---|
| offline (any town) | all set but 31 |
| online, a story area (stage type 1: stages 1–10, read from the game's own data at run time) | **bits 23–26 and 31: Spectator** |
| online, Palette Town, Spectator Mode On | the same Spectator pattern |
| online, Palette Town, Spectator Mode Off | bits 1–28 and 30: the normal one (`main+0xb7eb0`'s table) |

  In a story area the session's own Spectator flag is always clear (hosting computes it for Palette Town only), yet the restricted
  pattern is used whenever the player is online. Bit 31 is set only in the Spectator pattern.
- **How it was found:** one measured run on Windows (below, "Tools"): with the host online in Withered Wasteland, the HUD's Water Gun
  is crossed out and ZR says "You can't use that move now."; offline it works. No watched predicate had a caller that appeared only
  for the blocked press; the mask builder runs exactly when hosting starts, and its patterns read back from memory.

## The change: one site

- **Where:** in the mask builder's story-area online block, the instruction `movk x13, #1, lsl #32` (`main+0xb1030`, 2.0.1). There x13
  holds bits 0–31 of the pattern, the movk adds bit 32 (online), and x22 is the session's Spectator flag.
- **What:** while the setting is on and the flag is clear, x13 becomes the normal pattern (`0x5ffffffe`). The mask is then exactly
  Palette Town's with Spectator Mode Off (measured: `fe ff ff 5f 03` for the host, `fe ff ff 5f 05` for a visitor).
- **Not touched:** Palette Town and the other type-3 towns (13, 17), offline play, the host's Spectator setting, the session flag
  and the advertise data. The feasibility study's patch set P1 (the settings row, the flag plumbing) turned out unnecessary.

The shared part is `src/coop/coop_interact.c` (`coop.h`): the site table (build, function, site, the instruction word checked before
attaching), the callback `coop_interact_site(id, x0..x30)`, `coop_interact_on()` (`DM_COOP_INTERACT=1`) and `coop_mods_add()`.

## How each platform reaches the site

- **Windows (nxrt):** the translated mask builder's entry is overwritten with a jump to the interpreter (`rt.c interpret_function`), and
  the interpreter calls a site hook right before the instruction at the site (`nxw_interp_set_site_hook`, `src/interp`). No relift:
  `game.dll` is unchanged. The function runs only on session starts and town loads. nxrt checks main's build and the instruction word
  first, and logs `DM_COOP_INTERACT: … runs in the interpreter with a hook at main+0xb1030`.
- **Mac and iPhone (nxnative):** an observation gate, which changes no instruction: pokopia-native's profile gains a `patches.py`
  (`OBS = {2.0.1 main: [(0xb1030, 27 | 1 << 8, 'f2c0002d', …)]}`), the rewriter moves the instruction into a stub that first calls the
  host, and the co-op patch adds `OBS_COOP_INTERACT = 27` to `dm.h` and attaches the callback (`ldn_glue.c`, a constructor). The
  gate is built in at build time and decided at run time, so it works on the iPhone, whose code pages are signed and never written.
  The callback reads `DM_COOP_INTERACT` when it fires (the iPhone's launcher sets its variables after the host's constructors run).
- **Both log once** when the hook lifts a restriction: `[coop] Interact everywhere: this story area's Link Play session has Palette
  Town's permissions`.

## The setting

- **Windows:** the Link Play window (before the game starts) has **Interact everywhere (experimental)** under the ways to play,
  remembered in `linkplay.json` with the rest; `play.ps1 -Interact` from a terminal (with a Link Play switch).
- **iPhone and Mac:** the launcher's **Interact everywhere** row under Link Play (`DM_COOP_INTERACT`, a toggle, off, shown with Link Play
  on); `docs/env_switches.tsv` documents it.
- **The fingerprint:** with the setting on, a device's mods fingerprint gains "Interact everywhere" (FNV-1a over the name, as nxnative's
  cheat names: `fd0b8d7d8a364d88` alone). Hosts whose fingerprint differs are hidden from scans, and a joiner's is checked again.

## Tests (Windows, two instances over loopback)

Fresh copies of the two co-op saves (host: the Level 2 save; visitor: player 2's), the fixed clock, below-normal priority.

| | result |
|---|---|
| host alone, hosting in Withered Wasteland | Water Gun no longer crossed out; ZR waters |
| visitor joins (both on) | accepted; the visitor's mask is the normal one |
| visitor's Water Gun on dry ground | grass grows; the host sees it |
| visitor picks up a placed part (Y), then puts it back ("Spit Out") | both work (a Brick border piece) |
| progress | the visitor's watering made challenge steps; its sender ran, and ~4 s later the host ran `RequestChallengeProgress`, which advanced the host's challenge manager |
| visitor goes home, both save and quit | clean |
| saves against their copies | only the live slot changed (`G00/CMNDAT.BIN`, `G00/STGDAT01.BIN`); the backup slot and `S00/SYDAT.BIN` identical |
| both saves loaded alone | both reach the field; the host's world keeps the visitor's grass |
| host off, visitor on | the host is hidden from the visitor's scan ("its mods or cheats differs"); the game says the destination cannot be found |
| logs | only the renderer warnings every co-op run has |
| unit tests | `coop.interact` (sites by build, the callback, the fingerprint as nxnative computes it); `scripts.play_defaults` (`-Interact`) |

## The Mac

Built on the rented Mac by the builder from pokopia-native 1.1.1 plus these changes (`~/spec`, a clone of step 1c's build folder; the
recorded run-time shaders added back with `nxs2.py refresh`, 154 of 154). The rewriter placed one observation gate in main (`obs 1`).
Two cross-platform sessions through an SSH tunnel (`-L` / `-R` 47600, `DM_COOP_TCP=1`), fixed clock, copies of the same two saves:

| | result |
|---|---|
| Mac host, Withered Wasteland, hosting | the gate fired (the log line above); fingerprint `fd0b8d7d8a364d88`, the same as Windows; the Water Gun works |
| Windows visitor joins it | joined; its own hook fired; its Water Gun grows grass, and the Mac host sees it |
| Windows host, Mac visitor joins it | joined; the Mac visitor's gate fired; its Water Gun grows grass, and the Windows host sees it |
| nxnative's own checks in the patched tree | `test_launcher_settings` (with a new case for the row) and `test_lint_c` OK |
| the co-op module on the Mac (cc, -Wall -Wextra) | no warnings; 1786 checks, 0 failed (with `interact`) |

## Not done, and known gaps

- **Cosmetic:** the visitor's join notices still say everyone will be in Spectator Mode, and the host's settings row still shows
  Spectator Mode On, greyed. Their decisions are branches, which a gate cannot change.
- **The Link Play menu** still treats the host as a spectator in story areas (`main+0xe7a878`, UI only): "Get Friends' Stuff" was greyed.
- **Items go home:** leaving, a visitor's bag goes to its own PC ("All the items you have will be kept in the PC"): in a story area a
  visitor can now carry the host's parts home, as in Palette Town. Not tested: whether story objects can be picked up.
- **Not tested:** other moves (Level 2 has only Water Gun), crafting, NPC requests, cutscenes and story triggers caused by a visitor,
  an Environment Level up during a session, a town change with visitors, more than one visitor, the iPhone, the Mac app's launcher
  row on screen (its `launcher.json` lists it).

## Tools (Windows, for this kind of search)

nxrt's entry hook (lifted code calls `rt_trace_entry` while `rt_trace_on`) gained, in `rt.c`:

- `NXRT_WATCH=main+0x…,…`: entries of those functions counted per caller, the changes written once a second to `<log>.watch.txt`;
  `NXRT_WATCH_PEEK=<main+off>:<off2>:<len>` adds bytes at `*(main+off)+off2` to each second's line (the mask, here);
- `NXRT_WATCH_ALL=1`: every function's entries counted; a file `<log>.snap` holding a name writes `<log>.snap-<name>.txt` (two snapshots
  around an action, diffed, are what ran during it);
- `<log>.eval` with lines `<fn> <first> <last>`: the next watched entry on a game thread calls `fn` with x0 = first..last and writes the
  results to `<log>.eval.txt` (the stage types were read this way).

They cost nothing unless set.

## Reproduce

```
scripts\build.cmd -j 2
ctest --test-dir build\Release -R "coop|scripts"
scripts\play.cmd ... -Save <host save copy> -Work <host work> -CoopHost 47600 -Interact
scripts\play.cmd ... -Save <visitor save copy> -Work <visitor work> -CoopJoin 127.0.0.1:47600 -Interact
```

In the game: the host hosts from the Pokémon Center PC in a story area (Link Play → Invite Others to Visit → Local connection), the
visitor joins; the visitor's Water Gun works where it did not.
