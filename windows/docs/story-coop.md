# Story co-op (shared campaign in the host's save)

2026-11-05. The step after "Interact everywhere" (docs/coop-interact.md): two players progress the story together in the host's world, the
host's save keeping the progress, on Windows, Mac and iPhone. Players asked for it (pokopia-native #24, "sync for all quest and story mode to
play with friends"). Feasibility pass 1 (static analysis, local notes) rated it: (b) a shared campaign in the host's save **plausible**, the
netcode being host-authoritative and already carrying terrain, items, NPC work, challenges and cutscenes; (c) every player's own save
progressing **not feasible** inside the game.

## In short: "Story co-op (experimental)"

An opt-in switch on top of Interact everywhere, for Pokopia 2.0.1 and 1.0.5 on Windows, Mac and iPhone (1.0.5 from 2026-11-06, below). Turn it on with the Windows launcher's
"Story co-op (experimental)" checkbox in the Link Play tab (`play.ps1 -Story`), the Mac and iPhone launcher's "Story co-op" row, or
`DM_COOP_STORY=1`. It turns Interact everywhere on with it. Every player in a session needs the same setting: it is part of the mods fingerprint
(`c75a2da0380038c1` with Interact everywhere), so a session refuses a player whose setting differs. Off by default.

What counts for the host when friends play in the host's town:

| | counts for the host | how |
|---|---|---|
| world work: terrain, watering, items, furniture, building kits a visitor places | yes | Interact everywhere; the game replicates it |
| challenges ("Place furniture…", "Build something…") | yes | the game's own visitor-to-host challenge messages |
| habitats and the Pokémon they bring | very likely, not tested end to end | the game's own visitor-to-host requests for habitat Pokémon |
| talking to the town's story characters | the visitor sees and reads them | Story co-op's one change: the visitor gets the conversation instead of the visitor's stub reply |
| story requests (Tangrowth's and the Pokémon's) | **no**: only the host's own actions complete them | the game has no message that carries a request's progress |

So friends can help build up the host's town and follow the story, but the host still has to do the story steps themselves. Saves stay
intact: after sessions with Story co-op on, only each save's live slot changed, and every save loaded on its own.

## Phase 1: what the game already does, and the smallest change

| | experiment | question |
|---|---|---|
| A | reference | what a story beat does when the host does it alone (Palette Town, where Spectator Mode is off natively, is not reached by either test save; the Interact everywhere mask *is* Palette Town's Spectator-Off mask, so the reference is the host's own play in Withered Wasteland) |
| B | visitor, Interact everywhere | each kind of story beat done by the visitor: the story NPC, a Pokémon's request, a habitat, a challenge, a cutscene, a level-up; what the host's save keeps, what desyncs or softlocks |
| C | instrumented | for each beat that fails in B, which "is visitor" check (GameMgr+0x90 bit 2, ~3.3k readers) or which host-side rule stops it |
| D | patch set P1 | the smallest change that lets the visitor's beats count as the host's, the host staying authoritative: an opt-in switch, "Story co-op (experimental)", in the mods fingerprint |
| E | progress test | the visitor completes a story step; the host's save afterwards, loaded alone (2.0.1; 1.0.5 if cheap) |

**Rig.** Free memory allows one game instance per machine: the host on the rented Mac (pokopia-native's build with Interact everywhere,
`DM_COOP=host`), the visitor on Windows (nxrt with the watch tools: `NXRT_WATCH`, `NXRT_WATCH_ALL` snapshots, `<log>.eval`), joined through an
SSH tunnel. Copies of the two co-op test saves (host: the Level 2 save; visitor: player 2's Level 2 save), the fixed clock, scripted input.

**Boundaries.** Test copies only; never Nintendo's servers; the game's LDN passphrase is never written down; players' character names stay out
of this document.

**Rig as run.** Memory on the PC allowed one game at a time, and another session's game took it twice, so the in-game tests ran as two
instances on the rented Mac (host: a copy of the Level 2 save; visitor: a copy of player 2's Level 2 save), both with Story co-op on. The Windows
visitor runs of B and C (Interact everywhere only) went through the SSH tunnel to a Mac host.

## Results (2026-11-05)

### A visitor's world work already counts (Interact everywhere)

From the Interact everywhere tests (docs/coop-interact.md) and the iPhone test of 2026-11-04: a visitor's terrain, item and furniture actions land
in the host's world, and the host's challenges count them ("Place furniture and decorations" advanced when an iPhone visitor placed a storage
box). The visitor's PC shows the host's challenge list and the game's Group Challenges. Netcode: RequestChallengeProgress, RequestNPCTask,
the block and part messages.

### Talking to NPCs: blocked by one check, lifted by P1

**B.** With Interact everywhere alone, the visitor gets the "Talk" prompt at Professor Tangrowth, but A does nothing.

**C.** The A button asks a chain of fifteen handlers (`main+0x4c3ec8`) what it does; `main+0x3180e0` dispatches the answer (`main+0x4bf7c0`). Snapshots of
every function's entries during the same A press, alone and as a visitor (`NXRT_WATCH_ALL`), showed the chain stopping early for the visitor: the talk
handler `main+0x4c024c` took every visitor press (6939 of 6939 calls; alone, 970 of 1914). For an online player who is not the host, it answers
with a visitor's reply (kinds 0xa/0xb, handed to `main+0x339580`: no conversation). Three NPC kinds (0x52, 0x72, 0xaf) are exempt. The deciding
check is IsHost at `main+0x4c06d4`, skipped on stage 0x11 and while the session flag GameMgr+0x11 is set.

**D (P1).** One site: at `main+0x4c06d8` (`orr w8, w25, w0`, word `2a000328`) IsHost's answer becomes yes, so a visitor takes the host's path.
Windows: nxrt interprets `main+0x4c024c` with a site hook (rt.c, like Interact everywhere). Mac/iPhone: an observation gate,
`(0x4c06d8, OBS_COOP_INTERACT | COOP_SITE_TALK << 8, '2a000328', …)` in the profile's `patches.py`. The switch is `DM_COOP_STORY=1` (play.ps1
`-Story`). It brings Interact everywhere with it, and it adds "Story co-op (experimental)" to the mods fingerprint: `c75a2da0380038c1`
with Interact everywhere, logged by the Mac hosts and checked in coop_test.

**1.0.5.** The talk handler is `main+0x469424`; the site sits at the same offset in it, `main+0x4698b0` (`orr w8, w25, w0`, word `2a000328`),
between the same calls (stage == 0x11 at `main+0x703d60`, IsHost at `main+0x9a07c`, both identical to 2.0.1's). The rest of the handler differs
only in struct offsets and its record's layout, which the site doesn't touch. The same table entries on both platforms (coop_interact.c,
patches.py). On Windows both 1.0.5 sites attach and the game plays normally with them (one player, 2026-11-06); a visitor's talk on 1.0.5 is untested
(it needs two 1.0.5 saves past the Link Play unlock, and 1.0.5 can't load 2.0.1 saves).

| step (Mac host + Mac visitor, Story co-op on both) | result |
|---|---|
| visitor talks to Tangrowth | his conversation opens; "Look at this!" opens the item picker with the visitor's own items; the conversation closes cleanly |
| host's request list | one story request open, Tangrowth's "Time to get chatty!" (bring the connected player to say hi); the visitor's list is empty |
| before the host took the request | the visitor gets an idle line |
| host talks to Tangrowth (takes the request) | the request text; the visitor's screen shows nothing of it |
| visitor talks to Tangrowth again | the same request text as the host's: the visitor's conversation follows the host's request state |
| visitor finishes that conversation | the request stays open on the host; talking again repeats it |
| host sees the visitor talk | a "…" bubble over the visitor |
| NPC positions | after the visitor's conversations Tangrowth stood in different places on the two screens (cause not confirmed) |

So P1 opens the story conversations to a visitor, and they read the host's state, but **a conversation's outcome stays in the visitor's game**.
The game's netcode has no message for it: there are messages for challenges, NPC work tasks, cutscenes (SkitPlay, SkitForceComplete),
first contacts and the world's blocks, parts and items, and none for requests, story flags or a conversation's result. In the game's design only
the host's own conversations move the story.

### E: saves

After the session the visitor went home and both players saved and quit. Against their copies only the live slot changed (`G00/CMNDAT.BIN`,
`G00/STGDAT01.BIN`); the backup slot and `S00/SYDAT.BIN` are identical. Both saves loaded alone reach the field. These tests ran on 2.0.1;
1.0.5 has the same site since 2026-11-06 (above), untested in a session.

### Not covered yet

- Windows in game with Story co-op: the site is in nxrt (unit tests, the same shared callback the Mac ran), but the PC's memory went to
  another session's game both times a run was due.
- Completing "Time to get chatty!" with the visitor beside the host (the game's own Link Play beat; Tangrowth wandered off twice), a habitat,
  a Pokémon's request with an item handed over, a level-up's cutscene seen by the visitor.

## Phase 2, step 1: what counts with Interact everywhere alone (2026-11-05)

Two Mac instances again, Interact everywhere on both (fingerprint `fd0b8d7d8a364d88`), Story co-op off.

| beat | result |
|---|---|
| world work (terrain, items, furniture) | counts for the host (earlier tests) |
| challenges | count for the host (earlier tests; RequestChallengeProgress) |
| "Time to get chatty!" ("Try talking to Professor Tangrowth!") | the host takes it; talking again, alone or with the visitor beside, repeats "Bring … here to say hi!" and the request stays open. The visitor's Talk prompt at Tangrowth does nothing, and neither player can talk to the other. On these saves nobody can complete it yet: it waits on a condition we have not found |
| habitats | not run: a habitat needs placed items on a 2×2 patch (watering only turns dirt into short grass), too slow to drive blind. Nintendo's Link Play page says visitors in story towns "can interact with Pokémon and habitats" and that progress goes to the host, and the netcode has visitor-to-host requests for the habitats' Pokémon (RequestCreateNPC, RequestNpcPopExecutorAccess, RequestRepopHomelessNpcForSearch), so habitats very likely count |

Answer: with Interact everywhere, a visitor's world work, challenges and (very likely) habitats already count for the host. Story requests are the
host's alone, and the only open one on our Level 2 saves cannot be completed by either player yet. Step 2 needs a request someone can complete:
a save further along, or the condition that finishes "Time to get chatty!". Every save we have stops at Level 2.

## Phase 2, step 2: a story request a visitor can work on (2026-11-06)

**Host save.** A copy of the user's own Windows play save, which reached Environment Level 3: `_local\story-coop\saves\play-l3-20261106` (on
the Mac `~/mp/saves/play-l3-20261106`), checked byte for byte against the original, which nothing here reads again. A save derived from it
after buying the kits (below) is `~/mp/saves/play-l3-kit-20261106`. Visitor: player 2's Level 2 save, as before.

**Level 3 confirmed.** Opening the PC plays the pending level-up: Withered Wasteland, Environment Level 3, new challenges ("Make habitats!",
"Build something!", "Build different structures!"), more shop items, daily challenges. "Time to get chatty!" is gone; Tangrowth's next
conversation gives the story request **"Build a leaf den!"** ("Buy a leaf den kit from the PC shop and build it!"). Buying the kits (a set of
three, 50 coins) also completed the challenge "Use Life Coins! #1".

**The build as a visitor** (two Mac instances, Interact everywhere only):

| step | result |
|---|---|
| visitor's PC in the host's town | shows the host's level and the visitor's own coins; its shop sends purchases to the visitor's own town, so a visitor cannot buy a kit here |
| host drops the kits (bag, Drop) | the whole stack lands as a sack; the visitor picks it up ("Leaf den kit ×3") |
| visitor places a kit | a construction site (signpost and plot) appears, in the host's world too; Y picks it back up |
| the site | wants materials (3 sturdy sticks, 3 leaves) and two Pokémon helpers, one with the Build specialty, then "Start building!" |
| host's request | still open while the site waits |

Not finished: leaves come from cutting trees and grass, and neither player's moves cut (both have Water Gun and Leafage); helpers have to be
brought to the site ("Make Request to All" called none), which may need talking to Pokémon, something a visitor can only do with Story co-op.

**Answer so far:** the next story step after Level 3 is a build, not a conversation. Most of it already works for a visitor with Interact
everywhere (receiving the kit, placing it, the site replicating). Whether the finished den completes the host's request is the open question.
If it does, build-type story steps need no relay at all, and the relay of step 2 is only for conversation-gated steps.

**The build with helpers (2026-11-06, night).** The user played the copy on to befriend Timburr and learn Cut (a fresh copy of that,
`_local\story-coop\saves\play-l3-timburr-20261106`, Mac `~/mp/saves/play-l3-timburr-20261106`; the user's own files untouched). Its requests:
"Build a leaf den!" and Scyther's "Time to get chatty!". Two Mac instances, Interact everywhere only:

| step | result |
|---|---|
| host drops sticks, leaves and kits | the visitor picks all three up |
| visitor places a kit and adds the materials | the site takes them (sturdy sticks 3/3, leaves 3/3), from the visitor's bag |
| helpers ("Bring Pokémon here!": one with Build, one more) | not reached. "Make Request to All", by the visitor, called no one in several minutes; Timburr, beside the host, offered no Talk prompt while it carried its log, and kept moving |
| both players save and quit | only the live slot changed in each save; both load on their own |

So step 2's question stays open: the next story step is a build, and everything up to the helpers works for a visitor; whether the finished
den completes the host's request needs the helpers. That is game play (bringing Pokémon to a site), quicker for a person than for scripted
input. If it does not count, the relay (below, step 2) is the way; it needs a request that can be completed, which this save now has.

## Phase 2 recommendation

1. **Finish B on the cheap first** (Interact everywhere, no new patches): habitats, Pokémon requests the visitor fulfils with items, a level-up,
   the "Time to get chatty!" beat, and whether SkitPlay shows the host's cutscenes to visitors. This tells which beats already count for the host.
2. **Then one spike, "the host talks for the visitor":** our co-op layer owns the transport (coop_mpnet carries the game's datagrams over its own
   link), so it can carry one message of our own: "visitor finished conversation X with NPC Y". The host's game applies it by calling the game's
   own completion for that request, so the host stays authoritative. On the visitor, the same conversation must not apply it locally. Start
   with one request (Tangrowth's), then decide whether it generalises. Medium to high effort, per-request risk.
3. **Keep the shared-world save as the fallback** (feasibility pass 1, (c)): the group keeps one world save and whoever hosts plays its story
   beats, with the others' world work counting through Interact everywhere. Low effort (save hand-over in the launcher), no new game patches, but
   only the host talks the story forward.

Story co-op's talk site stays on this branch until step 2 works: on its own it lets a visitor read the story but not move it, which would
confuse players. The installer side (the profile's gate line, the regenerated nxnative patch, a launcher row for iPhone and Mac) waits for the
same decision.
