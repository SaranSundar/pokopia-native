# pokopia-native 1.1.2

---

**Pokopia runs natively on Windows too**, Link Play works between our builds, and the builder works when your build folder is on another drive. **1.1.2** fixes new games on Windows (the name prompt); everything from 1.1.1 and 1.1.0 below still applies. One release, one download per computer:

## Which download do I need?

| I have… | and want to… | Download |
|---|---|---|
| a Windows PC | play on Windows | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Windows PC | build the iPhone app | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Mac | play on the Mac | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |
| a Mac | build the iPhone app | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |

- **Co-op needs the same pokopia-native version on every device.** 1.1.0, 1.1.1 and 1.1.2 count as the same: the game's network code did not change between them.
- **Each player needs their own save.** The game refuses a second copy of the same save.

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump. Please do not share build folders, apps, IPAs, `game.dll`, shader files or saves.

## New in 1.1.2

- **Fixed, Windows: new games got stuck at "What's your name?".** The Windows game never showed a keyboard when the game asked for text, so a new game could not get past Tangrowth's name question. A small text box now opens over the game: type the name and press Enter (Esc cancels). Mac and iPhone were not affected.
- **Fixed, Windows: freezes on a first play-through.** The first time the game drew something new, the graphics driver built it on the spot, freezing the game for up to half a second. The builder now ships a list of the game's graphics pipelines (2.0.1: the title, a new game and the first towns), and the game builds them on background threads while it starts. In our test with an empty cache, the pipelines built in the middle of play went from 213 to 1, and the opening area ran at a steady 60 fps.
- **Getting it:** unzip 1.1.2, run **Setup** once, then **Build** with "Windows: play on this PC" ticked, using the same build folder: only the Windows game's runtime and its pipeline list are replaced, nothing is recompiled. The Mac and iPhone apps you already built stay as they are.

## New in 1.1.1

- **Fixed, Windows: hundreds of terminal windows during the shader step.** Building from the builder window could open a new console window for every tool the shader step ran and freeze the PC. The build now runs in one hidden console that all its tools share.
- **New: `.nup` dumps.** A `.nup` file (an open container for already-decrypted dumps) works as your dump: **.nup file…** in the window, or `nxs2.py build <file>.nup`. The builder unpacks the update it needs into the build folder once (about 7 GB more) and builds exactly as from a dump folder. It needs Python's `zstandard` module: `python -m pip install zstandard`.

## New in 1.1.0

### The Windows game

The "for Windows PCs" download builds **Pokopia for Windows** from your dump: the game's arm64 code is recompiled for your PC (our own recompiler, through LLVM) and runs on our own runtime with a Vulkan renderer. No emulator.

- Both game versions, **1.0.5 and 2.0.1**.
- **60 fps** in the field on the test PC; mouse look written straight into the game's camera, as on the Mac; controllers (Xbox, PlayStation, Switch Pro) as Switch controllers, with rumble and motion; SDL3 sound.
- **Your save**: `%APPDATA%\Pokopia Native\<version>\save`; **Import save…** (or `nxs2.py import-save`) brings yours from Eden, the iPhone or the Mac. Windows saves load on the Mac and the iPhone.
- **Run-time shaders** are recorded and translated on the spot the first time the game makes them; no refresh step.
- **No installer, no administrator rights, no compiler**: the download brings its own tools in `windows\bin` (their notices in `windows\bin\THIRD-PARTY-NOTICES.txt`). It needs Windows 10/11 64-bit, an AVX2 CPU, a Vulkan 1.3 GPU, Python 3.12, the .NET 10 runtime and git, and about 12 GB per game version. The build took 20 minutes on the test PC (4 workers; longer on smaller PCs), at idle priority. Details: [docs/WINDOWS.md](https://github.com/xFractals/pokopia-native/blob/main/docs/WINDOWS.md).
- The source is in `windows/` (MIT).

### Link Play between our builds

The game's **Link Play → Local connection** works between our builds: Windows, Mac and iPhone in any mix, up to four players, without Nintendo's servers. ("Online connection" in the same menu is Nintendo's service and does nothing here.)

- **On the same Wi-Fi:** turn on **Link Play on this network** in the iPhone or Mac launcher; on Windows a window before the game starts asks how you want to play. Hosts find each other by themselves.
- **Over the internet** (opt-in): the host forwards port **47600 (TCP and UDP)** on its router and shares its address and a **room code**; visitors type both in (iPhone / Mac: **Join address**, **Internet play**, **Room code** in the launcher; Windows: "Visit a host over the internet" in that window, while a Windows host gets its code in a window with a **Copy** button). Every connection proves the room code and every message is protected against changes; the traffic is not encrypted.
- **Tested:** a Windows PC hosting with an iPhone visiting over home Wi-Fi, and over the internet with the phone on mobile data; Windows and Mac together; four players; players leaving and coming back.
- Windows asks once to allow the game on networks (allow **Private**), and the network must be Private; the iPhone and the Mac ask once for local network access.
- Steps and tips (Tailscale when you cannot open a port): the README's [Playing together](https://github.com/xFractals/pokopia-native#playing-together-link-play).

### Builder

- **The build folder on another drive** than the tool now works (nxnative's `DM_GAMES_DIR` is split on the system's path separator, not on `:`).
- **Windows without Apple's tools**: `nxs2.py setup --no-ios` (or Setup with "iPhone IPA" unticked) sets up the Windows game only.
- The Windows game's recordings of the run-time shaders also finish the iPhone app of the same build folder (`nxs2.py refresh` picks them up).
- `nxs2.py --version`; the window's title shows the version.
- **Windows: when the builder window does not open**, `Pokopia Native Builder.cmd` now says why and waits (no Python, a Python older than 3.12, the Microsoft Store's "python" placeholder, Python without Tk), and writes `builder-log.txt` beside itself for a report.

## Known issues

- The Windows game was developed and tested on one PC (Windows 11, NVIDIA). AMD and Intel GPUs are untested; please report how it runs.
- Physical controllers on Windows were tested with SDL's virtual pads only; `-PadCheck` logs everything for a report.
- The first start on Windows draws some effects late while the run-time shaders are translated (about 150, once).
- **Windows: areas past the first towns** still build their graphics the first time you visit them (short freezes, once: they are kept for later starts). The list grows with each update. Game version 1.0.5 has no list yet.
- The Expansion Pass (DLC) is not supported yet.
- Link Play over the internet needs the host's router to forward a port; there is no matchmaking server. Hosting from an iPhone over the internet is untested, as are Tailscale-style virtual networks.
- The iPhone app reads its settings when it starts: after changing Link Play settings, close it fully and open it again.
- **Internet play must match on every device**: a device with Internet play on does not see a host on the same Wi-Fi that has it off ("destination not found"). For play on one network, leave it off everywhere.

## Upgrading

- **From 1.1.0 or 1.1.1:** see "Getting it" above. On Windows, Setup once and Build again (nothing is recompiled); Mac and iPhone builds need nothing.
- **From 1.0.x, Mac / iPhone:** unzip the new download, run Setup once in it, build again with your dump: only what changed is redone, and your recorded shaders stay. Install the new iPhone app **over** the old one (never delete the app first: that deletes its save); the game files on the phone only need copying again if you moved to another update of the game. Mac saves live outside the app. Steps: the README's "Updating from an older version".
- **From 1.0.x, Windows (iPhone builder):** the same. The Windows game is new: tick "Windows: play on this PC".
