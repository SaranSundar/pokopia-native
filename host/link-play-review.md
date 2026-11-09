# Link Play on the local network: changes for review

Not released and not in the README yet. What changed in this repository for Link Play ("Visit a Friend" / "Invite Others" → **Local
connection**) between the iPhone app, the Mac app and the Windows build, on one Wi-Fi network. The design, the tests and the test steps are in
the Windows port's `docs/coop-step1c.md`.

## Files

- `host/nxnative-switch2.patch` (34 → 46 files; the 32 sections Link Play does not touch are unchanged byte for byte):
  - new: `dm/host/coop.h`, `coop_int.h`, `hle_coop_ldn.c`, `hle_coop_vnet.c`, `hle_coop_mpnet.c`, `hle_coop_plat.c`, `hle_coop_disc.c`. The co-op
    module, our own MIT code, the same files the Windows build uses;
  - `dm/host/hle_offline.c`: `nn::ldn` goes to the module while Link Play is on (else cut, as before); the game's identity for the join check is
    main's build ID and the cheats that are on;
  - `dm/host/hle_net.c`: the game's UDP sockets become the module's virtual sockets while Link Play is on;
  - `dm/host/hle_misc.c`: parental controls answer "allowed" only while Link Play is on;
  - `dm/games/settings.tsv`: the launcher's **Link Play on this network** switch (`DM_COOP` = `auto` / `off`, off by default), iPhone and Mac;
  - `dm/ios/project.yml`, `dm/mac/pack_app.py`: `NSLocalNetworkUsageDescription` ("Link Play uses your local network to find and visit other
    players' towns.") and `NSBonjourServices` (`_pokopia-ldn._udp`);
  - `docs/env_switches.tsv`: `DM_COOP`, `DM_COOP_DISCOVERY`, `DM_COOP_TCP`, `DM_UID`.
- `host/windows/build_app.patch`: the same two Info.plist keys in IPAs built on Windows.
- `host/UPSTREAM.md`: the patch's description.

## What a player sees

- Off by default. With the switch on, the first Link Play use asks for local network access once.
- Hosting and visiting work as on the console: the visitor's "Local connection" list shows towns hosted on the same network (found with
  DNS-SD/Bonjour). Where a router blocks that, `DM_COOP=join:<host's IPv4>` in `Documents/env.txt` joins by address.
- Only devices on the local network can connect (private and link-local addresses); the host listens only while it hosts; both sides must run the
  same game build and the same cheats.
- Each player needs their own save: the game refuses a visitor whose save is a copy of the host's.

## Checked

- Built on the Mac with `nxs2.py setup` and `nxs2.py build … --ipa` (the module builds without warnings); two Mac instances found each other
  and the visitor arrived in the host's town. nxnative's `test_launcher_settings` and `test_lint_c` pass; `test_env_switches` and `test_docs` fail
  as they did before this change.
- Not yet: the Windows builder with the new patch (setup re-applies it on the next run), and the iPhone itself.

## Addendum: internet play (Windows port's step 1d)

Also not released. Link Play between players on different networks, an explicit opt-in under the same switch; the design, the threat model and
the tests are in the Windows port's `docs/coop-step1d.md`.

- `host/nxnative-switch2.patch` (46 → 54 files; the 38 sections internet play does not touch are unchanged byte for byte, `dm/games/game.py` and
  the texture sections among them; the new patch applies to a clean c1df4dd and reproduces the tree):
  - new: `dm/host/hle_coop_auth.c` (SHA-256, HMAC-SHA256 and HKDF, checked against FIPS 180-4, RFC 4231 and RFC 5869; the room code); the module
    files `coop.h`, `coop_int.h`, `hle_coop_ldn.c`, `hle_coop_mpnet.c`, `hle_coop_disc.c` updated (`hle_coop_vnet.c`, `hle_coop_plat.c` and the
    `hle_offline.c` / `hle_net.c` / `hle_misc.c` routing unchanged);
  - `dm/games/settings.tsv`: under **Link Play on this network**, **Internet play** (`DM_COOP_INTERNET`, off), **Room code** (`DM_COOP_ROOM`) and
    **Relay address** (`DM_COOP_RELAY`), both empty; the last two are drawn only while internet play is on;
  - the launcher's new `text` kind for those two (launcher.json schema 2): `dm/games/settings.py`, `docs/launcher.schema.json`, `docs/frontends.md`,
    `dm/ios/Sources/LauncherRow.swift` (a text field, shared by the iPhone and the Mac), `LauncherSettings.swift`, `dm/mac/launcher.c` and
    `dm/mac/pack_app.py` (a saved text is used when it is printable ASCII within its length), `dm/tools/tests/test_launcher_settings.py`;
  - `docs/env_switches.tsv`: `DM_COOP_INTERNET`, `DM_COOP_RELAY`, `DM_COOP_ROOM`; `DM_COOP`'s row mentions that `join:` takes any address then.
- `host/windows/build_app.patch`: a text field in the iPhone launcher the Windows builder compiles (`win/ios/Launcher.m`, the port's UIKit launcher,
  which drew any kind it did not know as a switch).
- `host/UPSTREAM.md`: the patch's description.

What a player sees: nothing changes with internet play off (the default). On, every player enters the host's room code; a host without one gets a
new code each session, written to the log. The traffic is authenticated (a wrong code or a changed message is refused) but **not encrypted**.
With a relay address, the host and the visitors all connect out to that relay (one the players run themselves; there is no public one).

Checked on the Mac in a fresh tree (`~/c1d`): nxnative's `test_launcher_settings` (16), `test_launcher_storage`, `test_lint_c` and
`test_nsp_mac_app` (44) pass; `test_env_switches` fails as before, on the Switch 2 patch's own switches; `LauncherRow.swift` typechecks for macOS
and iOS 26, `dm/mac/launcher.c` and the patched `win/ios/Launcher.m` compile without warnings (macOS SDK, iOS 26 SDK). Not built into an app or
an IPA, and the text field has not been seen on a screen.
