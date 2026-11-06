# Co-op plan

2026-11-04. What we want, in what order, and where the line is. The evidence behind it is [research/06](../research/06-multiplayer.md).
**Schedule (updated 2026-11-03, the user's decision):** step 1 starts now, alongside the Windows port. The Android port and the remaining
steps follow the order below. **The Windows port is part of the co-op plan:**
- the shared module lives in this repository (`src/coop/`) and is built and unit-tested on Windows from the start;
- the Windows runtime's hook files (`src/nxrt/hle/hle_offline.c`, `hle_net.c`, `hle_account.c`, `hle_misc.c`, ported from nxnative's)
  get wired to it as soon as the port can reach the Link Play menu (M6, input);
- if step 1 finishes before the Windows port is playable, the Windows hookup becomes the next co-op step.

## Goals

1. **Link Play "Local Connection" across every native build.** Mac, iPhone, Windows, Android, and later Linux: any device can host,
   any device can join (an iPhone joining a Windows host, Android with a Mac, and so on). Everyone must run the same game version;
   the connection checks it.
2. **Palette Town multiplayer.**
   - As a Link Play visitor, Palette Town is where visitors can build (the game allows terrain changes there only), so it comes with
     Link Play.
   - **GameShare** (2 players, Palette Town only) is the game's own two-player system: through `nn::streamplay` the host renders a
     second view for the guest and reads the guest's controller. Across devices that means streaming the guest's view and sending
     back their input.
3. **Two-player story co-op** (a mod, uncertain):
   - **Route A:** lift the Link Play visitor restrictions (terrain changes outside Palette Town and the other rules). Both devices
     run the game; the game's networking already syncs visitors. Story progress stays the host's.
     - **Measured target (research/06 §3.1):** the game has its own switch for this, the host's **Spectator Mode** setting ("certain moves
       and items can't be used"). It is fixed when the session is created.
     - **The lock:** in Withered Wasteland at Level 2, Off is greyed out and locked On. The first experiment is to find where Off unlocks,
       or let the host choose it, then see what visitors can do.
   - **Route B:** extend GameShare's two-player system beyond Palette Town, so a second player is in the host's story world. Closer
     to true co-op; story scripts, cutscenes and quests likely assume one player.

## Order

| Step | What | Size |
|---|---|---|
| 0 | ~~Measure Link Play on a save at Environment Level 2+ with the instrumented Mac host (`DM_MPTRACE=1 DM_MPFAKE=1`)~~ **done 2026-11-03** (research/06 §3.1): hosting and scanning work on a lone node | about a day |
| 1 | ~~Link Play between two game instances on the Mac (loopback, join by port), with the shared module in `src/coop/`, unit-tested on Windows and the Mac~~ **done 2026-11-04**: with two different saves the visitor arrives in the host's town and movement syncs (docs/coop-step1.md) | ~1–2 weeks |
| 1b | ~~Windows hookup: the Windows runtime's hooks call the shared module; Windows ↔ Mac join~~ **done 2026-11-04** (docs/coop-step1b.md): two Windows instances meet over loopback, and through an SSH tunnel (~145 ms) a Windows visitor arrives in a Mac host's town and a Mac visitor in a Windows host's; movement syncs. Joining is still loopback only | after M6, or next if step 1 finishes first |
| 1c | Every platform (iPhone, Android, later Linux), LAN discovery, join-by-address over the internet, the optional relay. **First part built 2026-11-04** (docs/coop-step1c.md): LAN joining protected to loopback / private / link-local peers with a build and mods check, DNS-SD (`_pokopia-ldn._udp`) with `DM_COOP=auto`, join by LAN address; two Windows instances meet over this PC's LAN address; pokopia-native's patch carries the module and the iPhone's Link Play switch; an unsigned IPA waits for **the user's PC ↔ iPhone test**. Still open: that test, Android, Linux (internet join and the relay: 1d) | 5–8 weeks in all with step 1 |
| 1d | **Internet play and the relay built 2026-11-04** (docs/coop-step1d.md): an explicit opt-in (`DM_COOP_INTERNET=1`) where every link proves a 16-symbol room code (80 bits; HMAC-SHA256 challenge-response) and every frame and datagram carries a tag with replay counters, **not encrypted** (the user's decision: a library such as Monocypher); `tools/coop-relay`, a relay players run themselves (it routes, cannot forge, can read); through an SSH tunnel a Windows visitor arrives in a Mac host's town through the relay (round trip ~156 ms); **four players** in one Mac session (everyone sees everyone, one leaving disturbs nobody, the game's own kick works); pokopia-native's patch gains the launcher's Internet play, Room code and Relay address rows. Still open: encryption, the real internet (routers, NAT, loss), the iPhone's launcher on a screen, Route A | part of 1c's 5–8 weeks |
| 2 | Route A experiment: lift visitor restrictions. **First part built 2026-11-04** (docs/coop-interact.md): "Interact everywhere", an experimental opt-in (`DM_COOP_INTERACT`, the launcher's switch, the Windows Link Play window's checkbox) that gives story-area sessions Palette Town's Spectator-Off permissions through one hook in the game's permission mask; part of the mods fingerprint. Two Windows instances: a visitor in Withered Wasteland waters, picks up and places parts, and the host's challenges advance; both saves load afterwards. Still open: the join notice's text, more actions and towns, two visitors, a real iPhone. **Story co-op phase 1, 2026-11-05** (docs/story-coop.md): an opt-in `DM_COOP_STORY` lets a visitor talk to story NPCs (one site in the A button's talk handler); the conversations read the host's requests, but their outcome stays in the visitor's game, since the netcode carries no story messages. Next: the beats that already count, then a host-applied relay for one request | small once step 1 works |
| 3 | GameShare for Palette Town, with the own-build rule | larger (video streaming per platform) |
| 4 | Route B spike: GameShare beyond Palette Town | uncertain |

## Design (research/06 §5)

One shared C module in the HLE layer every port uses:
- `ldn_hle`: the `nn::ldn` state machine (the game uses 17 functions and polls state);
- `vnet`: a virtual 169.254.S.0/24 network for the game's UDP sockets (port 49530, a /24 broadcast), broadcasts fanned out to peers;
- `mpnet`: our own transport, a star around the host: a TCP control link per player (join, leave, session updates, keepalive) and UDP
  for the game's datagrams; no ENet (docs/coop-step1.md);
- discovery by Bonjour/DNS-SD on the LAN, join-by-address as a fallback, and an optional self-hosted relay;
- a compatibility key from build IDs and active cheats, so mismatched games can't join.

**Required first:**
- a per-install account user ID (today every install has the same one, and the game rejects a duplicate player);
- the parental-control communication check answering "allowed". Confirmed in step 0: without it the Link Play tile does nothing.

**Measured in step 0:**
- **Sockets first:** the game opens and binds its UDP socket (port 49530) before creating the network.
- **The network config:**
  - `NodeCountMax` is the host's Number of Players setting;
  - `LocalCommunicationVersion` is 301 on 2.0.1;
  - the Link Code travels inside the game's advertise data, which the emulation passes through untouched.
- **A lone host only listens.**

**Measured in step 1: every player needs their own save.**
- **The rule:** the game refuses a visitor whose save carries the host's player ID (error 9005, "a user with the same user ID is already in the session"). The ID is a 16-byte value in `S00/SYDAT.BIN`, and a separately started save has its own.
- **The consequence:** a save synced between one player's devices (say, a Mac and an iPhone) can't join itself. Two people with their own saves can play.
- **The account user ID doesn't matter to this game**, so the "per-install `Uid`" fix above isn't needed for Pokopia (still harmless).
- **Retail never uses broadcast-relay mode:** all traffic is unicast to port 49530.

## Boundaries

- Never Nintendo's servers: NPLN, Cloud Islands, online Link Play, accounts, tokens, GameChat, anti-cheat.
- GameShare only when **the guest also runs their own build** (everyone owns a copy, as with Link Play).
- No public relay; Ryujinx's relay terms limit it to Ryujinx clients, so we never use it. A relay players host themselves is fine.
- Never publish the game's built-in LDN passphrase; the design does not need it.
- Cheats: refuse sessions whose mod fingerprints differ, and warn when cheats are on.
