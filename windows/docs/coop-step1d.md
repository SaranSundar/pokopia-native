# Co-op step 1d: internet play, the relay, four players

2026-11-04. The next part of step 1 in [COOP-PLAN](COOP-PLAN.md): Link Play between players on different networks, as an explicit opt-in, with a
room code that every link must prove and a relay players can run themselves; and the first session with four players. Built on
[coop-step1c.md](coop-step1c.md); design background in [research/06](../research/06-multiplayer.md) §5.4–5.7. In the working trees for review:
nothing is committed in either repository, nothing is published, no relay runs anywhere.

## Result

| | status |
|---|---|
| Internet play by address, off by default: `DM_COOP_INTERNET=1` + `DM_COOP_ROOM`, `join:` to any address, hosting for links from anywhere | done (`src/coop/`); LAN mode unchanged (step 1c's 1,327 checks pass as they were) |
| Room-code authentication: a challenge-response proof both ways, per-link keys, a tag on every frame and datagram, replay counters, limits on failed proofs | done (coop protocol 3, `coop_auth.c`); **no encryption** (the user's decision, [below](#decisions-for-the-user)) |
| SHA-256, HMAC-SHA256, HKDF-SHA256 written here, checked against the published vectors | **15 of 15** match: FIPS 180-4's SHA-256 examples (5), RFC 4231's HMAC cases 1–7, RFC 5869's HKDF A.1–A.3 |
| A self-hostable relay, `tools/coop-relay` (C11, no dependencies) | done: rooms by a 16-byte id derived from the code, pairing, a byte copy it does not parse, limits on rooms, links, addresses, bytes, rates and time; rooms expire |
| Unit tests | **1,798 checks pass** on Windows (clang 23.1.4, `build\coop`, 2 jobs, low priority) and on the Mac (Apple clang 21, plain `cc`); ctest `build\coop` 6/6, `build\c1d` 9/9 |
| Real latency through the SSH tunnel to the Mac, internet mode | by address: **500 of 500** echoes, round trip median **156 ms**; through the relay on the Mac's 127.0.0.1: 500 of 500, median **156 ms**; a wrong code refused in 438 ms |
| The game, internet mode, through the tunnel and the relay | a **Windows** visitor (`build\c1d` nxrt) arrives in a **Mac** host's town; movement syncs both ways |
| Four players in one session (Mac research host, LAN mode on loopback) | host + 3 visitors with four different saves: everyone sees everyone, movement syncs station to station, one visitor leaving disturbs nobody, the game's own **Kick a player** works |
| pokopia-native's patch | 46 → 54 sections, the other 38 byte for byte (`dm/games/game.py` and the texture sections among them); the launcher's **Internet play**, **Room code**, **Relay address** rows and a `text` kind for them; nxnative's launcher tests pass |
| Route A (Spectator Mode Off) | [not reached](#route-a-not-reached) |

## Playing over the internet

Everyone needs the same build of this app, the same game version and the same cheats (as on the LAN), and **their own save**. Internet play is an
explicit switch; with it on, every link must prove the room code, LAN visitors too.

**1. The room code.** On Windows (and Linux) the in-game menu's **Link Play** tab (F1) shows it with a **Copy** button, the addresses to give and
who is connected (docs/in-game-menu.md). The host's log (Windows: the run's log; iPhone: `Documents/log.txt`) shows a new code each session:
`internet play: this session's room code is XXXX-XXXX-XXXX-XXXX; visitors need it`. To keep one code, set it (`DM_COOP_ROOM`, or the launcher's
**Room code**) on the host too. The code is 16 letters and digits; case, dashes and spaces don't matter, and O / I / L read as 0 / 1. Use a code
the module made (`coop_room_new`): a code a person makes up is easy to guess. A code the host was given (DM_COOP_ROOM, the launchers) is logged by its first group only (`7K3M-****-****-****`): players attach their logs to bug reports.

**2a. Port forwarding** (the host's router forwards a port to it):

- Host: on the router, forward TCP **and** UDP 47600 to the host's LAN address (192.168.x.y). Then `DM_COOP=auto`, `DM_COOP_INTERNET=1`. On
  Windows the first hosting asks about the firewall: allow **Private networks** only (the port forward arrives on the LAN interface).
- Visitors: `DM_COOP=join:<the host's public IPv4>` (`:<port>` if not 47600), `DM_COOP_INTERNET=1`, `DM_COOP_ROOM=<the code>`.
- With only TCP forwarded, the game's datagrams stay on the control link (slower on a lossy line, but it works).

**2b. A relay** (nobody forwards anything; everyone connects out):

- Someone the players trust runs `coop-relay --listen <an address everyone can reach>:47610` on a machine with a reachable address. By default it
  listens on 127.0.0.1 only. The relay cannot change or forge game messages, but **it can read them** (nothing is encrypted).
- Host and visitors: `DM_COOP=auto`, `DM_COOP_INTERNET=1`, `DM_COOP_ROOM=<the code>`, `DM_COOP_RELAY=<the relay's IPv4>[:<port>]`. Hosting opens
  the room at the relay (nothing listens on the host's device, so there is no firewall prompt); the visitor's **Local connection** list shows the
  room's town.

**On the iPhone and the Mac app:** the launcher's **Link Play on this network** switch, then **Internet play** under it, then **Room code** and
**Relay address** under that (empty by default). Join by address on the iPhone stays in `Documents/env.txt` (`DM_COOP=join:<IPv4>`, with
`DM_COOP_INTERNET=1` and `DM_COOP_ROOM=…`).

**On Windows:** environment variables before the game starts, e.g. in PowerShell `$env:DM_COOP = "auto"; $env:DM_COOP_INTERNET = "1";
$env:DM_COOP_ROOM = "<code>"; $env:DM_COOP_RELAY = "<relay>"`, then play as usual (`play.ps1` passes them on; it has no flags for them yet).

## Configuration

| setting | what it does |
|---|---|
| `DM_COOP_INTERNET=1` | internet play: `auto` hosts take links from any address, `join:` takes any address, every link proves the room code (coop protocol 3). Unset, `0` or `off`: step 1c exactly; `DM_COOP_ROOM` / `DM_COOP_RELAY` are then ignored with a log line |
| `DM_COOP_ROOM=<code>` | the room code, 16 Crockford base32 symbols (80 bits). A visitor must give it (`join:` without it is a configuration error); a host without it gets a new one, logged |
| `DM_COOP_RELAY=<IPv4>[:<port>]` | a relay (port 47610 by default); needs `DM_COOP=auto`. DNS-SD is off with a relay |
| `DM_COOP=host:<port>` + `DM_COOP_INTERNET=1` | listens on 127.0.0.1 with the room code: an SSH tunnel's end, as the tests below use |
| launcher rows (iPhone, Mac) | **Internet play** (`DM_COOP_INTERNET`, shown while Link Play is on), **Room code** (`DM_COOP_ROOM`, at most 19 characters) and **Relay address** (`DM_COOP_RELAY`, at most 21), both shown while internet play is on |

Room codes, relay addresses and keys are never sent to anyone but the peers; the log shows the code (it is what the host gives its visitors) but no
key. Hostnames are not taken, only IPv4 addresses.

## Design

```
 station                                  (relay, optional: routes, never parses)                      host
 ── TCP connect ─────────────────────────────────────────────────────────────────────────────────────▶
 [relay: JOIN room id ──▶ relay ──VISITOR id,cookie──▶ host; host dials ATTACH id,cookie ──▶ relay pairs ──OK──▶ station]
 HELLO  magic, protocol 3, platform, Uid, nonce_s (16) ─────────────────────────────────────────────▶  failure budget left?
 ◀──────────────────────────────────────────────────────────────── AUTH_CHAL  magic, 3, nonce_h (16)
 AUTH_PROOF  HMAC(k_auth, "PKP3 station proof" ‖ nonce_s ‖ nonce_h ‖ platform ‖ Uid) ───────────────▶  checks (constant time)
 ◀───────────────────────────────────── AUTH_OK  HMAC(k_auth, "PKP3 host proof" ‖ nonce_s ‖ nonce_h)  (or AUTH_FAIL, closed)
 both: link keys = HKDF(salt nonce_s ‖ nonce_h, k_auth): tcp s2h, tcp h2s, udp s2h, udp h2s
 ◀═══════════ every later frame: length, type, body, tag = HMAC(key, frame count ‖ length ‖ type ‖ body)[0:16] ════════════▶
 ◀═══════════ every datagram: "PCU3", type, token, sequence number, body, tag = HMAC(key, header ‖ body)[0:16] ═════════════▶
```

### Keys (coop_auth.c)

- `prk = HKDF-Extract(salt "pokopia-coop room v1", the 16 symbols)`; `k_auth = HKDF-Expand(prk, "auth", 32)`;
  `room id = HKDF-Expand(prk, "relay room id", 16)`. The room id is all a relay learns; it is one-way from the code.
- Per link: `HKDF-Extract(salt nonce_s ‖ nonce_h, k_auth)`, expanded into four keys, one per direction and path, so a frame cannot be reflected
  back or replayed as a datagram, and a recorded session's tags fit no other session (fresh nonces on both sides).
- The station proves first and the host answers only after checking it: a stranger who dials a host learns nothing to guess the code from.
- Keys live in the node and its links and are wiped (`cwipe`) when the node goes; nothing is written to disk.

### Tags and replays

- **Frames (TCP):** a 16-byte tag (HMAC-SHA256 cut to 128 bits, as RFC 4868 does) over the frame's count in its direction (never sent: both ends
  count), its length, type and body. A changed, dropped, repeated or reordered frame fails the next check and closes the link (`bad_tag`).
- **Datagrams (UDP):** the header carries a 64-bit sequence number; the tag covers header and body. A window of 64 below the highest number seen
  refuses repeats and old numbers (`replays`); a changed one is dropped (`bad_tag`). A tagged datagram from a new address moves the link there (a
  NAT that changed its mapping), since nobody without the key can make one. The UDP endpoint no longer has to share the control link's address.
- **Costs:** 16 bytes a frame, 24 a datagram, two SHA-256 passes over each; the game sends about 50 datagrams a second per player.

### Who may connect

| | LAN (step 1c) | internet play |
|---|---|---|
| source addresses | loopback, RFC 1918, 169.254/16 | any valid unicast (the room code decides) |
| `join:` targets | the same | any valid unicast |
| proof | none (protocol 2) | the room code, both ways (protocol 3); protocol 2 and 3 refuse each other with a log line |
| failed proofs | — | 6 a minute per address, 30 a minute in all (links through a relay share only the second); past either, new links close before the challenge |
| a station whose code a host refused | — | not dialled again for 30 s (one failure, not one a second) |
| HELLO to READY | 3 s | 6 s (a relay adds a pairing and round trips); a Scan waits up to 1.5 s for links and answers |

Every limit of step 1c (frame sizes, rates, buffers, NetworkInfo checks) stays and now runs on authenticated input only, except HELLO and the proof.

### The relay (tools/coop-relay)

- **Why C:** the unit tests run it in the test process (two and four players, its limits), it builds with `cc` alone on a Mac, Linux box or
  Windows PC, and it is small enough to read in one sitting (coop_relay.c ~430 lines, main.c ~100). Python would need an interpreter on the relay's
  machine and could not share the C tests.
- **Protocol** (coop_relay.h): a link's first 32 bytes say what it is — `HOST` (opens the room for its id), `JOIN` (a visitor asks for the room)
  or `ATTACH` (the host's link for one waiting visitor, naming its id and the relay's random cookie). The relay answers in 16-byte messages (`OK`,
  `VISITOR`, `GONE`, `ERR <code>`, `PONG`), then copies bytes between the paired links without reading them.
- **Untrusted:** it never sees the code or a key, only the room id. Inside a pair everything is the players' own protocol 3, so the relay cannot
  forge, change, drop or replay a game message without the link closing — but it can read them (no encryption), and it can refuse service.
- **Limits** (defaults, all flags): 64 rooms, 7 visitors a room, 256 links, 24 links per address, 10 new links a second per address (burst 20),
  60 lookups of absent rooms a minute per address, the first message within 5 s, a visitor paired within 10 s, a host's control link silent for 30 s
  closes its room (hosts ping every 5 s), a room lives at most 12 h and carries at most 4 GiB, 256 KiB a second per link (burst twice). Reading
  stops while a reader is 256 KiB behind (back-pressure, not loss).
- **Only for the players:** listens on 127.0.0.1 unless told otherwise; prints a warning on another address. No disk, no accounts, no logging of
  payloads; the log names rooms by their id's first 4 bytes.
- **Through a relay, game datagrams travel on the TCP link** (the relay copies streams only). The host dials one link per visitor; a node that
  only initialized Link Play (the menu open, nothing chosen) does not ask the relay for a room.

## Threat model

| who | can | cannot |
|---|---|---|
| a stranger on the internet who finds the host's port (port forwarding) | connect, send a HELLO, fail the proof (6 a minute from one address, 30 in all); fill the failure budget so honest visitors wait up to a minute (denial of service) | join, scan the host's NetworkInfo, send a datagram the game sees, learn anything to guess the code from |
| someone on the path (shared Wi-Fi, an ISP, a VPN) | read every message, including towns, names and positions in the game's traffic; record a handshake and guess the code offline; drop traffic | change, inject, drop or replay a message without detection; reuse a recorded session |
| the relay's operator | everything an on-path observer can; refuse rooms; read the room id (one-way from the code) | the same as on-path: no forging, no reading the code |
| a player who has the code | join and play as on the LAN (Spectator Mode and the game's own rules apply); cheat on its own side | — (the room code is the trust boundary; the mods fingerprint is self-reported, not anti-cheat) |

Not protected: **confidentiality** (the user's choice for now), **offline guessing** of the code from a recorded handshake (a PAKE such as CPace
or SPAKE2 would remove it; 80-bit codes make it impractical, below), denial of service, and traffic analysis. The game's own parsers still see
every datagram from a member, as on a console.

### The room code's entropy

- 16 symbols of Crockford's base32 (32 symbols: digits and letters without I, L, O, U), each from 5 bits of the system's random source
  (`rand_s` on Windows, `arc4random_buf` on Apple and Android, `/dev/urandom` elsewhere): **80 bits**. The test draws 1,000 codes: all
  different, every symbol 451–548 times of 16,000 (500 expected).
- **Online:** at most 30 wrong proofs a minute reach a host, so finding a code takes about 2^79 / 30 a minute ≈ 4 × 10^16 years.
- **Offline** (a recorded handshake, or the relay's room id): each guess costs a few SHA-256 compressions; at 10^10 guesses a second (a large
  GPU) 2^79 takes about 2 million years.
- A shorter or chosen code changes both numbers: 8 symbols (40 bits) fall offline in about a minute on that GPU. So codes stay 16 generated
  symbols unless a PAKE comes with the encryption library.

## Tests

### Unit (PC and Mac)

```
build\coop (clang 23.1.4, 2 jobs, low priority), coop_tests:
== crypto: SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 4231), HKDF-SHA256 (RFC 5869), room codes, tags, the replay window
   published vectors: SHA-256 5, HMAC-SHA256 7, HKDF-SHA256 3; 15 of 15 match
   1000 new room codes: all valid, all different; each symbol drawn 451-528 times of 16000 (500 expected)
== inet: internet play's room-code handshake, tags and limits (loopback)
   a host and two stations with the room code, game datagrams over UDP
   datagrams: a repeat refused (replays 1), a changed one and a renumbered one refused (bad tags 2); the link goes on
   a host and two stations with the room code, game datagrams over the control links
   proxy, AUTH_OK changed (the host's proof): station links 0, bad tags 0, failed proofs 1, networks listed 0
   proxy, WELCOME changed: station links 0, bad tags 1, failed proofs 0, networks listed 0
   proxy, WELCOME dropped: station links 0, bad tags 1, failed proofs 0, networks listed 0
   proxy, the first answer repeated: station links 0, bad tags 1, failed proofs 0, networks listed 1
   proxy, nothing changed: station links 1, bad tags 0, failed proofs 0, networks listed 1
   failure budget: 9 wrong proofs from one address, 6 challenged and refused, 3 closed before the challenge
== relay: tools/coop-relay in this process (loopback only)
   a host and visitors through the relay: two players, then four, one leaving
   visitor 1 joined through the relay 204 ms after it started
   relay: 1 rooms, 4 visitors paired, 16258 bytes copied, 2 lookups not found
   the relay's limits
   40 links at once from one address: 19 refused at once
   a 64 KiB room: closed after 81920 of 98304 bytes
   a link at 32 KiB/s: 64 KiB arrived intact in 1015 ms
1798 checks, 0 failed

ctest --test-dir build\coop: coop.loopback, coop.lan, coop.hostile, coop.crypto, coop.inet, coop.relay: 6/6 passed (70 s)
ctest --test-dir build\c1d -R coop: nxrt.coop_off, nxrt.coop_host, nxrt.coop_join and the six above: 9/9 passed
The Mac (~/c1d/test, cc -std=c11 -Wall -Wextra, no warnings): 1798 checks, 0 failed; the relay builds alone (cc main.c coop_relay.c), no warnings
```

- **crypto:** the vectors byte at a time and in one call; a prepared key and a message in two pieces; the padding edges (55, 56, 63, 64, 65, 119,
  120, 128 bytes, cross-checked with Python's hashlib); HKDF's 255-block limit; room codes (normalizing, refusing 15 or 17 symbols, U, other
  characters); keys (the same code the same keys, another code others); proofs binding both nonces and the HELLO; link keys pairing up across the
  two ends and differing by direction and path; a tag changing with every body byte and the frame count; the replay window.
- **inet:** configuration (26 cases: public `join:` refused without internet play, a visitor without a code refused, codes, relays, `on`/`yes`);
  sessions over UDP and over the control links with station-to-station datagrams and a broadcast; datagrams made white-box with a station's own
  key (accepted once, then refused as a repeat; a changed byte; an old number with a tag that no longer fits); a wrong code (refused once, the
  station waits 30 s); a LAN node and an internet node refusing each other; a man-in-the-middle proxy changing the host's proof, changing a
  tagged frame, dropping one, repeating one; the failure budget, then a correct station joining after 10.5 s.
- **relay:** a host and one, then three visitors through the in-process relay; datagrams between all of them through the host; one leaving; a
  node that only initialized does not ask the relay; a wrong code finds no room (the host never hears of it); a right room with a wrong proof
  fails at the host; the room closing with its host. Limits: rooms, a held room, visitors, a wrong cookie, a visitor never attached (timeout and
  `GONE` to the host), lookups a minute, the first message (nothing in time, not ours), new links a second, a room's age, its byte budget, a
  link's rate.

### Through the SSH tunnel to the Mac (real latency)

`coop_tests probe` is one node from the same environment the games read (`DM_COOP*`); the visitor sends 500 datagrams 20 ms apart and times
the echoes. Everything listened on 127.0.0.1; the tunnels were `ssh -N -L 127.0.0.1:<port>:127.0.0.1:<port>` from the PC; every tunnel, probe
and relay was stopped afterwards. The tunnel carries TCP only, so the game's datagrams used the control link (UDP to the tunnel's end finds
nothing and the station stays on TCP, as designed).

| path | found / joined | echoes | round trip min / median / p95 / max |
|---|---|---|---|
| A. by address: PC visitor → tunnel → Mac host (`host:47731`, internet play) | 562 ms / 719 ms | 500 / 500 | 140 / 156 / 157 / 172 ms |
| A, wrong code | refused after 438 ms ("host … refused the room code") | — | — |
| B. relay on the Mac (127.0.0.1:47741), Mac host beside it, PC visitor through the tunnel | 781 ms / 938 ms | 500 / 500 | 140 / 156 / 157 / 188 ms |
| C. the same relay, PC host through the tunnel, Mac visitor beside it | 761 ms / 916 ms | 500 / 500 | 150 / 170 / 187 / 193 ms |

The tunnel's own round trip is about 140 ms (step 1b measured ~145); the authenticated path adds nothing measurable, the relay on the far end
none either. C's extra ~10 ms is the Windows probe host's echo loop (15.6 ms sleep granularity). The relay copied 111 KB for B and C together.
Logs: `_local\c1d\logs\tA-*.log`, `tB-*.log`, `tC-*.log` (not in git).

### The game: Windows visitor, Mac host, internet play through the relay

- Mac: `coop-relay --listen 127.0.0.1:47741`; the research host (`~/mp/nxnative-mp`, rebuilt by `apply_mp_patch.py` with this module) on a copy
  of `~/mp/saves/level2-1103`, `DM_COOP=auto DM_COOP_INTERNET=1 DM_COOP_ROOM=<a test code> DM_COOP_RELAY=127.0.0.1:47741`.
- PC: the tunnel to the Mac's 47741; `build\c1d\bin\nxrt.exe` through `_local\c1d\run.ps1` on a copy of
  `save-sync\backups\20261104-001844-player2-level2`, the same three settings, `DM_FIXED_TIME=1793793600`, `TZ=UTC`, driven by `_local\coop1b\k.py`.

| | |
|---|---|
| host | "hosting through the relay 127.0.0.1:47741 (nothing listens on this device)"; "the relay opened the room (id fc44249f...)"; NodeCountMax 4 |
| visitor, Link Play → Visit a Friend → Local connection | 0.6 s after browsing: "host link up: 127.0.0.1:47741 (the relay's room), room code proven"; "Scan: 1 network(s) found"; the host's town listed (`gv1\gv_list`) |
| visitor, the town chosen | "Connect … -> 0, joined"; host: "station link 0 up through the relay (visitor 1), room code proven", "node 1 joins", ACCEPT after the game read the NetworkInfo |
| arrival | the visitor's screen shows both characters (`gv1\gv_arr`); the visitor walks off and back, the host's view shows it beside the host (`c1dih_gh_back`); the host stands and moves, the visitor sees it (`gv1\gv_move2`) |
| relay | 1.54 MB copied in about 2.5 minutes of play (all game datagrams on the TCP link); no refusals, no bad input |

### Four players (Mac, LAN mode on loopback)

The research host rebuilt with this module (`apply_mp_patch.py` now copies `coop_auth.c` too); four instances on the M4 through `~/mp/ia.sh`
(runs `c1d4h`, `c1d4v1`, `c1d4v2`, `c1d4v3` under `~/mp/runs/`), `DM_COOP=host:47650` / `join:127.0.0.1:47650`, distinct `DM_UID`s, the fixed clock.

| player | save (a copy each run) |
|---|---|
| host | `~/mp/saves/level2-1103` |
| visitor 1 | `~/mp/saves/player2-level2` |
| visitor 2 | `~/mp/saves-derived/level2-1103-uid9b` (step 1's derived copy: one bit of the player ID flipped) |
| visitor 3 | `~/mp/saves-derived/player2-level2-uid70b` (new: a copy of `player2-level2` with one bit at `S00/SYDAT.BIN` +0x70 flipped; `cmp` shows that one byte; the original was only read) |

| step | result |
|---|---|
| hosting | Number of Players **Four** (the default), Spectator Mode On (locked, as in step 0); "CreateNetwork: NodeCountMax 4" |
| visitor 1, 2, 3 in turn | each list shows the host's town with "Visitors 0/3", "1/3" (visitor 1 named), "2/3"; host: "node 1 joins … 2 of 4", "node 2 joins … 3 of 4", "node 3 joins … 4 of 4 nodes", all "datagrams over UDP" |
| seeing each other | after visitor 2 arrives, the host's and visitor 2's screens both show all three (`c1d4h_h_arr3`, `c1d4v2_v2_arr2`); visitor 1's screen shows "… has come to visit!" as visitor 3 arrives (`c1d4v1_v1_arr4`) |
| movement | visitors 1 and 3 walk; the host's and visitor 2's screens show both at their new places (`c1d4h_h_move`, `c1d4v2_v2_move`): station-to-station traffic through the host |
| one leaves | visitor 2's process ends: host "station link 1 down", "node 2 left", "3 of 4 nodes"; visitors 1 and 3 walk on, the host's and visitor 3's screens follow (`c1d4h_h_left`, `c1d4v3_v3_left`) |
| the game's own kick | the host's menu lists **Connected Players** and offers **(+) Manage Connected Players → Kick a player → Who would you like to send home? → Are you sure?** Yes: host "node 3 left"; visitor 3's own `nn::ldn` goes Disconnect → CloseStation → Finalize and it travels home (`c1d4v3_v3_kicked`). The game does not call `nn::ldn::Reject` for this: it tells the visitor in its own session messages and the visitor leaves |

- Four instances made the Mac's frames long (170–550 ms, all four rendering); presses of 150 ms were lost, so the drive used 0.4–0.5 s holds. Under
  that load one ACCEPT went out after `ACCEPT_WAIT_MS` (300 ms) "without the game reading the NetworkInfo"; that visitor arrived normally.
- Screenshots: `_local\c1d\shots\` (game-derived, not in git; they show the saves' character names, which stay out of this document).

### Firewall and processes

- The tests and probes bound 127.0.0.1 only; the game visitor and a relay host only dial out. No Windows Security Alert appeared, and
  `Get-NetFirewallApplicationFilter` (read only) shows no rules for `build\coop\coop_tests.exe`, `build\c1d\bin\nxrt.exe` or
  `build\coop-relay\coop-relay.exe`. Nothing was changed in the firewall or the network profile (still Public).
- Stopped afterwards: both SSH tunnels (only the processes started for this step), the Mac's relays, probes and the five game instances; the PC's
  game window. No relay runs anywhere.

## pokopia-native (installer, not committed)

- `host/nxnative-switch2.patch`: regenerated from the committed patch (b9d02f5) with `apply_coop.py` + `make_patch.py` in a fresh tree on the
  Mac (`~/c1d/nx`: c1df4dd + the committed patch). First the unchanged pipeline reproduced the committed patch byte for byte; then 38 of its 46
  sections stayed byte-identical (`dm/games/game.py`, every texture section), 8 changed (`settings.tsv`, `coop.h`, `coop_int.h`,
  `hle_coop_{disc,ldn,mpnet}.c`, `pack_app.py`, `env_switches.tsv`), 8 are new (`hle_coop_auth.c`, `settings.py`, `LauncherRow.swift`,
  `LauncherSettings.swift`, `dm/mac/launcher.c`, `test_launcher_settings.py`, `frontends.md`, `launcher.schema.json`). The new patch applies to a
  clean c1df4dd and reproduces the tree exactly; its module files are byte-identical to `src/coop`.
- **The launcher's `text` kind** (launcher.json schema 2, as nxnative's own contract requires for a new kind): `settings.py` writes `length`; the
  schema, `frontends.md` (the kind, the saved-choice rule) and `test_launcher_settings.py` (schema 2, the co-op rows, malformed text rows) follow;
  `LauncherRow.swift` draws a text field (printable ASCII up to the length; capitals and no autocorrection on the iPhone) for the iPhone and the
  Mac; `LauncherSettings.swift` and `dm/mac/launcher.c` accept a saved text on the same rule, `pack_app.py` writes it into `DMSettings`.
- `host/windows/build_app.patch`: a text field in `win/ios/Launcher.m`, the Windows builder's UIKit launcher, which drew any unknown kind as a
  switch. The patch applies to the fork's `win/` at 269d61f.
- `host/UPSTREAM.md`, `docs/link-play-review.md` (an addendum).
- **Checked:** nxnative's `test_launcher_settings` (16), `test_launcher_storage`, `test_lint_c`, `test_nsp_mac_app` (44) pass;
  `test_env_switches` fails as before on the Switch 2 patch's own switches (none of the co-op ones); `LauncherRow.swift` + `LauncherSettings.swift`
  typecheck for macOS 26 (with `dm/mac/Launcher.swift`) and iOS 26; `launcher.c` and the patched `Launcher.m` compile without warnings. Not built
  into an app or an IPA; the text field has not been seen on a screen.

## Real devices (2026-11-04) and the fixes for pokopia-native 1.1.0

- **Home Wi-Fi:** the user's PC hosting, their iPhone (the IPA from pokopia-native main) visiting: works. On the way it did not:
  the PC advertised `a=172.20.10.2`, the address of the iPhone's own USB link (Personal Hotspot, "Ethernet 4", a Public network)
  because it routed from there while the phone was plugged in, so the phone dialled an address Windows Firewall blocks
  ("destination couldn't be found"). The PC's Ethernet had also been Public. Unplugged and Private, it worked.
- **The internet:** the PC hosting at home behind the router's port forward (47600 TCP and UDP), the iPhone on mobile data,
  `DM_COOP=join:<the home address>`, internet play and the room code: **works** (room code proven, datagrams over UDP through
  the carrier's NAT; the host learns the station's UDP endpoint from its UHELLO). Lessons: the game's menu item is **Local
  connection** (Online connection is Nintendo's service and returns to the menu); the router's NAT/Gaming page needs the
  service assigned to the right device; the iPhone app reads `env.txt` only at launch.
- **Fixed (windows-native 643d861):** the host lists every LAN address (TXT `a=` the best, `x=` the rest; Public networks
  last, private before link-local, the route's own first among equals; the Windows A record is the best); a visitor takes the
  first on one of its own subnets and tries the others when a dial fails. The Public hint names only the LAN adapters. The
  launcher's **Join address** (`DM_COOP_JOIN`) replaces env.txt for joining by address. `play.ps1` gains `-LinkPlay`, `-Join`,
  `-Internet`, `-Room`, `-Relay`, and (an installer build, or `-Ask`) a window before the game starts: play alone, on this network,
  host or visit over the internet, remembered in `%APPDATA%\Pokopia Native\linkplay.json` ("Don't ask again"; Shift brings it back).
  A host over the internet gets its room code in a window with a Copy button.

## Route A: not reached

The static analysis of what greys out Spectator Mode's Off option was only begun (read only, a few minutes with step 1b's string dump
`_local/mp/main-201-strings.txt`); the four-player test and the launcher work took the time. What that look found, as leads, not answers:

- main 2.0.1 has no "Spectator" (or similar) text: the settings screen's words and probably its rows come from data in the RomFS, so the lock is
  either a data table's flag or code that reads one;
- the save's reflected field names include `m_OnlineSettings` (beside `m_Dummy00`), a likely home of the host's Link Play settings (Display Link
  Code, Number of Players, Spectator Mode). The next step is to find the code that reads that structure and the screen's enable check, then test
  the three candidates (Environment Level, the town, a story flag) by comparing saves at different points.

research/06 §3.1 still holds what is measured: Off is drawn disabled in Withered Wasteland at Level 2, and the setting is fixed from CreateNetwork on.
No game code was changed and no patch exists.

## Not done, not verified

- **No encryption:** anyone on the path, and a relay's operator, can read the game's traffic. The integrity layer stays when encryption comes.
- **No PAKE:** a recorded handshake allows offline guessing of the code (80 bits make it impractical; shorter codes would not).
- **The real internet:** only an SSH tunnel to the rented Mac. Not tried: two home routers, port forwarding on a real router, carrier NAT,
  mobile networks, packet loss (through a relay, game datagrams ride TCP: head-of-line stalls on a lossy line are untested), a NAT changing its
  mapping mid-session (the code accepts it; not exercised).
- **No hole punching, no UPnP:** port forwarding or a relay only. Hostnames and IPv6 are not taken.
- **Denial of service:** the global failure budget (30 a minute) lets a stranger delay honest visitors by up to a minute at a time; the relay's
  limits are per address.
- **The relay never ran on a reachable address** (by rule) and has no TLS or authentication of its own; its rooms are first come, first served.
- **iPhone / Mac launcher:** the rows and the text field are compiled and tested by nxnative's tests, not seen on a screen or built into an app;
  an iPhone host that leaves the code empty finds the new code only in `Documents/log.txt`.
- Four players ran in LAN mode on one Mac; internet play ran with two (Windows ↔ Mac). Four through a relay, and Windows in a four-player
  session, were not run.
- Windows: no `play.ps1` flags for internet play (environment variables work); the main `build\Release` does not have this module until it is rebuilt.
- nxnative's player documentation (`docs/settings.md`) does not list the new rows; its `test_env_switches` / `test_docs` fail as before.

## Decisions for the user

- **Encryption:** Monocypher (CC0 / BSD-2, one C file) for XChaCha20-Poly1305 with keys from the same handshake, and ideally a PAKE or an X25519
  exchange for forward secrecy; or libsodium (ISC, larger). Until then internet play is authenticated but readable.
- **Relay hosting:** whether to run a relay at all, where, and who may use it (it can read the traffic without encryption); whether the release
  ships `coop-relay` (Windows, Mac, Linux builds) with instructions, or only the source.
- **Room-code UX:** 16 generated symbols (80 bits) as now, or shorter codes once a PAKE exists; whether the launcher should make and show a code
  for a host (now: the log), and whether a host may type its own.
- **The iPhone / Mac wording:** "Internet play" — "Play with someone on another network: both enter the same room code. Not encrypted.";
  "Room code" — "The host's code, XXXX-XXXX-XXXX-XXXX. A host without one gets a new code each time."; "Relay address" — "A relay you trust, as
  IPv4[:port]. Empty: visitors connect to the host's address."
- Whether internet play should also require the code from LAN visitors (it does: one switch, one rule), and the relay's default port 47610.

## Reproduce

```
rem PC: the module and its tests
start "" /b /low /wait cmake --build build\coop -j 2
ctest --test-dir build\coop
rem the relay alone
clang -std=c11 -O2 -Wall -Wextra -o build\coop-relay\coop-relay.exe tools\coop-relay\main.c tools\coop-relay\coop_relay.c -lws2_32
rem nxrt with this module (build\c1d; the generator check's nxnative path is pointed nowhere and that test is not run)
cmake -S . -B build\c1d -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DNXRT_SDL3_DIR=<work>/toolchains/SDL3-3.4.18 -DNXW_LLVM_DIR=<work>/toolchains/llvm-23.1.4 -DNXRT_NXNATIVE=<nowhere>
start "" /b /low /wait cmake --build build\c1d -j 2 --target nxrt nxrt-tests-coop coop_tests
ctest --test-dir build\c1d -R coop
```

```
# latency probes (A: by address; B: through a relay on the far end)
mac$ DM_COOP=host:47731 DM_COOP_INTERNET=1 DM_COOP_ROOM=<code> COOP_PROBE=host ./coop_tests probe
pc$  ssh -N -L 127.0.0.1:47731:127.0.0.1:47731 <mac>
pc$  DM_COOP=join:127.0.0.1:47731 DM_COOP_INTERNET=1 DM_COOP_ROOM=<code> COOP_PROBE=join coop_tests probe
mac$ coop-relay --listen 127.0.0.1:47741
mac$ DM_COOP=auto DM_COOP_INTERNET=1 DM_COOP_ROOM=<code> DM_COOP_RELAY=127.0.0.1:47741 COOP_PROBE=host ./coop_tests probe
pc$  ssh -N -L 127.0.0.1:47741:127.0.0.1:47741 <mac>
pc$  DM_COOP=auto DM_COOP_INTERNET=1 DM_COOP_ROOM=<code> DM_COOP_RELAY=127.0.0.1:47741 COOP_PROBE=join coop_tests probe

# the Mac's unit tests and the research host
cc -std=c11 -Wall -Wextra -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Isrc/coop -Itools/coop-relay \
   src/coop/coop_*.c src/coop/tests/coop_test.c tools/coop-relay/coop_relay.c -o coop_tests && ./coop_tests
cd ~/mp && python3 macsrc/apply_mp_patch.py ~/mp/nxnative-mp ~/mp/macsrc
cd ~/mp/nxnative-mp/dm/host && DM_GAMES_DIR=$HOME/installer-test/build201-clean/profile make GAME=0400A3A01EE4C000 dmhost

# pokopia-native's patch (a fresh tree: c1df4dd + the committed patch)
python3 src/coop/nxnative/apply_coop.py ~/c1d/nx
python3 src/coop/nxnative/make_patch.py ~/c1d/nx <installer>/host/nxnative-switch2.patch nxnative-switch2.patch.new
```
