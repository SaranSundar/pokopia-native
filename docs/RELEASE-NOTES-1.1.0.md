# pokopia-native 1.1.0 (draft, not published)

*Draft for the release page. Fill in the co-op section and the checksums on release day (§ "Release day" at the end); the
rest is written against the release candidate.*

---

**Pokopia now runs natively on Windows too**, Link Play works between our builds, and the builder works when your build
folder is on another drive. One release, one download per computer:

## Which download do I need?

| I have… | and want to… | Download |
|---|---|---|
| a Windows PC | play on Windows | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Windows PC | build the iPhone app | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Mac | play on the Mac | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |
| a Mac | build the iPhone app | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |

- **Co-op needs the same pokopia-native version on every device.** Update every device to 1.1.0 before playing together.
- **Each player needs their own save.** The game refuses a second copy of the same save.

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your
computer from your dump. Please do not share build folders, apps, IPAs, `game.dll`, shader files or saves.

## New

### The Windows game

The "for Windows PCs" download builds **Pokopia for Windows** from your dump: the game's arm64 code is recompiled for your
PC (our own recompiler, through LLVM) and runs on our own runtime with a Vulkan renderer. No emulator.

- Both game versions, **1.0.5 and 2.0.1**.
- **60 fps** in the field on the test PC; mouse look written straight into the game's camera, as on the Mac;
  controllers (Xbox, PlayStation, Switch Pro) as Switch controllers, with rumble and motion; SDL3 sound.
- **Your save**: `%APPDATA%\Pokopia Native\<version>\save`; **Import save…** (or `nxs2.py import-save`) brings yours from
  the iPhone or the Mac. Windows saves load on the Mac and the iPhone.
- **Run-time shaders** are recorded and translated on the spot the first time the game makes them; no refresh step.
- **No installer, no administrator rights, no compiler**: the download brings its own tools in `windows\bin` (their
  notices in `windows\bin\THIRD-PARTY-NOTICES.txt`). It needs Windows 10/11 64-bit, an AVX2 CPU, a Vulkan 1.3 GPU,
  Python 3.12, the .NET 10 runtime and git, and about 12 GB per game version. The build took 20 minutes on the test PC (4 workers; longer on smaller PCs), at idle
  priority. Details: [docs/WINDOWS.md](docs/WINDOWS.md).
- The source is in `windows/` (MIT).

### Link Play between our builds

The game's **Link Play → Local connection** now works between our builds: Windows, Mac and iPhone in any mix, up to four
players, without Nintendo's servers. ("Online connection" in the same menu is Nintendo's service and does nothing here.)

- **On the same Wi-Fi:** turn on **Link Play on this network** in the iPhone or Mac launcher; on Windows a window before
  the game starts asks how you want to play. Hosts find each other by themselves.
- **Over the internet** (opt-in): the host forwards port **47600 (TCP and UDP)** on its router and shares its address and
  a **room code**; visitors type both in (iPhone / Mac: **Join address**, **Internet play**, **Room code** in the launcher;
  Windows: "Visit a host over the internet" in that window, while a Windows host gets its code in a window with a
  **Copy** button). Every connection proves the room code and every message is protected against changes; the traffic is
  not encrypted.
- **Tested:** a Windows PC hosting with an iPhone visiting over home Wi-Fi, and over the internet with the phone on mobile
  data; Windows and Mac together; four players; players leaving and coming back.
- Windows asks once to allow the game on networks (allow **Private**), and the network must be Private; the iPhone and
  the Mac ask once for local network access.
- Steps and tips (Tailscale when you cannot open a port): the README's
  [Playing together](https://github.com/xFractals/pokopia-native#playing-together-link-play).

### Builder

- **The build folder on another drive** than the tool now works (nxnative's `DM_GAMES_DIR` is split on the system's
  path separator, not on `:`).
- **Windows without Apple's tools**: `nxs2.py setup --no-ios` (or Setup with "iPhone IPA" unticked) sets up the
  Windows game only.
- The Windows game's recordings of the run-time shaders also finish the iPhone app of the same build folder
  (`nxs2.py refresh` picks them up).
- `nxs2.py --version`; the window's title shows the version.
- **Windows: when the builder window does not open**, `Pokopia Native Builder.cmd` now says why and waits (no Python, a
  Python older than 3.12, the Microsoft Store's "python" placeholder, Python without Tk), and writes `builder-log.txt`
  beside itself for a report.

## Known issues

- The Windows game was developed and tested on one PC (Windows 11, NVIDIA). AMD and Intel GPUs are untested; please
  report how it runs.
- Physical controllers on Windows were tested with SDL's virtual pads only; `-PadCheck` logs everything for a report.
- The first start on Windows draws some effects late while the run-time shaders are translated (about 150, once).
- Link Play over the internet needs the host's router to forward a port; there is no matchmaking server. Hosting from an
  iPhone over the internet is untested, as are Tailscale-style virtual networks.
- The iPhone app reads its settings when it starts: after changing Link Play settings, close it fully and open it again.
- **Internet play must match on every device**: a device with Internet play on does not see a host on the same Wi-Fi
  that has it off ("destination not found"). For play on one network, leave it off everywhere.

## Upgrading

- **Mac / iPhone:** unzip the new download, run Setup once in it, build again with your dump: only what changed is
  redone, and your recorded shaders stay. Install the new iPhone app **over** the old one (never delete the app first:
  that deletes its save); the game files on the phone only need copying again if you moved to another update of the
  game. Mac saves live outside the app. Steps: the README's "Updating from an older version".
- **Windows (iPhone builder from 1.0.x):** the same. The Windows game is new: tick "Windows: play on this PC".

## Release day (remove this section from the published notes)

1. Co-op has landed in `main` and the release candidate passed the user's end-to-end test (Mac app, iPhone app from a Mac
   and from Windows, Windows game, Link Play between them).
2. `windows/` refreshed: `python tools/snapshot_windows.py <windows-native checkout>` (the audit's fixes run again; it
   stops on anything that must not be published).
3. The Windows tools built from that source with the static C runtime, the tests passed; then
   `python tools/package_release.py` → `dist/1.1.0/`. Check `SHA256SUMS.txt` and `CONTENTS.txt`.
4. Commit, tag `v1.1.0` and push (the user, as xFractals), create the release with both zips (asset names exactly
   `Pokopia-Native-for-Windows.zip` and `Pokopia-Native-for-Mac.zip`, so the README's links work), paste these notes with
   the checksums.
