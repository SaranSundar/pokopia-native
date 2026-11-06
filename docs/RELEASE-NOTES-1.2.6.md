# pokopia-native 1.2.6

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)** for install help, bug reports, co-op partners and release news.

**Link Play visitors can now help in every town**: an experimental switch, **Interact everywhere**, lifts the game's Spectator Mode outside Palette Town. **1.2.6** draws the particle effects 1.2.4 had to skip, fixes starting the Windows game from folders with [ ] in their names or from a fresh download, adds a Pokopia Settings launcher, and fixes Refresh on exFAT drives on the Mac; everything from 1.2.5, 1.2.4, 1.2.3, 1.2.2, 1.2.1, 1.2.0 and 1.1.x below still applies. One release, one download per computer:

## Which download do I need?

| I have… | and want to… | Download |
|---|---|---|
| a Windows PC | play on Windows | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Windows PC | build the iPhone app | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Mac | play on the Mac | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |
| a Mac | build the iPhone app | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |

- **Co-op needs the same pokopia-native version on every device.** 1.2.6, 1.2.5, 1.2.4, 1.2.3, 1.2.2, 1.2.1 and 1.2.0 count as the same, and all still play with 1.1.x as long as Interact everywhere is off: the game's network code did not change. With it on, every device needs 1.2.x and the switch on.
- **Each player needs their own save.** The game refuses a second copy of the same save.

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump. Please do not share build folders, apps, IPAs, `game.dll`, shader files or saves.

## New in 1.2.6

- **Fixed, Windows: particle effects that 1.2.4 had to skip are drawn** ([#17](https://github.com/xFractals/pokopia-native/issues/17), [#20](https://github.com/xFractals/pokopia-native/issues/20)). Some effects draw as many copies of themselves as a number the graphics card writes just before. The game read that number on the computer before the card had written it, so the effect didn't show, or (in 1.2.3 and older) the card froze. Now the card reads the number itself when it draws, capped so it can never keep the card busy. One such effect is in the intro, in the cave after you choose your look.
- **Fixed, Windows: Play said the game's files were missing when a folder's name contains square brackets**, such as `Pokopia_v2.0.1a[Base_Game_and_Update]`. Spotted by Quantumenis in the Discord.
- **Fixed, Windows: "The operation was canceled by the user" when starting the game.** Windows marks everything unzipped from a download as coming from the internet, and then refused to start the game. Setup, Build and the launcher now take that mark off Pokopia Native's own files.
- **New, Windows: Pokopia Settings**, a launcher beside Play Pokopia in your build folder's `out` folder. It opens the window before the game (Link Play, Controls, Display) even after you ticked "Don't ask again". Holding Shift while starting still works too.
- **Fixed, Mac: Refresh and rebuilds failed on exFAT drives** ([#23](https://github.com/xFractals/pokopia-native/issues/23)) with "Operation not supported" once the app existed. The app is now replaced in a way those drives support. No shader is rebuilt.
- **Fixed, Mac: Build stopped with "translation chain tools do not match the lock file: the translator does not run"** ([#26](https://github.com/xFractals/pokopia-native/issues/26)) when .NET came from Homebrew's `dotnet` formula, even though Check tools found it. The builder now finds that .NET's runtime.
- **Mac download:** the shader translator no longer ships its debug-symbols file.
- **Getting it:** Windows: unzip 1.2.6, run **Setup** once, then **Build** with the same build folder: the runtime and the launchers are replaced, nothing is recompiled. Mac: unzip 1.2.6, run **Setup** once, then **Build** with the same build folder: no shader is rebuilt. iPhone: nothing changed.

## New in 1.2.5

- **Fixed, Windows: the game freezing on RTX 50-series graphics cards** ([#12](https://github.com/xFractals/pokopia-native/issues/12), [#21](https://github.com/xFractals/pokopia-native/issues/21), [#22](https://github.com/xFractals/pokopia-native/issues/22)), often right after loading a save or as soon as you start moving. One of the game's graphics programs ends with a quick vote among the GPU threads running it, to add up a counter. On RTX 50 cards that vote never finishes once some of those threads are already done, so the card stopped responding. The game now has each thread add its own share instead, which gives the same count. Only that one program changes; every other card runs it the same as before. **Found and fixed by #12's reporter, Xenuim-DiableX**, who tracked the freeze down to the exact instruction. Thank you!
- **Getting it:** Windows: unzip 1.2.5, run **Setup** once, then **Build** with the same build folder: only the game's runtime is replaced, nothing is recompiled. Mac and iPhone: nothing changed for them.

## New in 1.2.4

- **Fixed, Windows: the game freezing when certain particle effects start** ([#17](https://github.com/xFractals/pokopia-native/issues/17), [#20](https://github.com/xFractals/pokopia-native/issues/20)). Some effects draw as many copies of themselves as a number the graphics card writes. The Windows game read that number before the card had written it, once got about a billion, and the card stayed busy past Windows' 10-second limit, so the picture froze. Such a draw is now skipped; where the game used to freeze, that effect may be missing for a moment. Confirmed by #20's reporter. Drawing the effect properly is planned. First seen on RTX 50-series cards, but not limited to them.
- **New, Windows: a report when the graphics card stops responding.** On NVIDIA cards the log now names the shader the card was running and the last one it finished, and the driver's own fault report is logged where it offers one. If the game still freezes for you, attach the newest log from `%LOCALAPPDATA%\Pokopia Native\<game version>\logs\` to an issue.
- **New, builder: "Need help? Ask on Discord"** at the top of the builder window, and in the message when a step fails.
- **Getting it:** Windows: unzip 1.2.4, run **Setup** once, then **Build** with the same build folder: only the game's runtime is replaced, nothing is recompiled. Mac and iPhone: only the builder window's Discord link is new.

## New in 1.2.3

- **New, Windows: sharper on big screens.** The game draws at 1920x1080; on a 1440p or 4K screen (or any window larger than that) it was stretched with a plain filter and looked blurry. A sharp upscaler now sharpens each pixel against its neighbours by how much contrast is around it (strong in flat areas, gentle at edges, so no halos) while it scales. It is on by default and costs no frame rate on the test PC (full screen at 4K: 60 fps either way, about 40 % more edge detail).
- **New, Windows: a Display tab** in the window before the game starts, next to Link Play and Controls ([#16](https://github.com/xFractals/pokopia-native/issues/16), [#18](https://github.com/xFractals/pokopia-native/issues/18)): **Console mode** (docked 1920x1080, or handheld 1280x720 and lighter), **Display mode** (window or full screen) and the **window size**, **Upscaling** (Sharp or Standard) with a **Sharpness** slider, **Frame rate** (60 or 30) and **Vsync**. Saved for this PC and used at every start, "Don't ask again" included. (After "Don't ask again", hold **Shift** while starting to see the window.) The Mac's launcher already has its own Console mode, Display mode and Upscaling (MetalFX).
- **Still to come:** drawing the game itself above 1080p is a bigger change and is planned for a later update.
- **Getting it:** Windows: unzip 1.2.3, run **Setup** once, then **Build** with the same build folder: only the game's runtime and its window are replaced, nothing is recompiled. Mac and iPhone: nothing changed for them.

## New in 1.2.2

- **Fixed, Windows: invisible characters on AMD graphics cards** ([#14](https://github.com/xFractals/pokopia-native/issues/14)). AMD's driver allows a graphics buffer of at most 2 GB, and the game's biggest memory pool is about 3.4 GB. The Windows game left that pool out on AMD, and the character models in it were drawn as nothing. Big pools now get several buffers that share the same memory. Tested by capping our NVIDIA card at AMD's limit: the characters are drawn and the picture matches an uncapped run. Not yet confirmed on a real AMD card; please report how it runs.
- **Fixed, Windows: stuck at "What's your name?" with only a controller.** The name question opens a small box over the game that needed a keyboard, and while it was up the game ignored the controller. Now **A, B or +** on the controller accepts it; an empty box becomes "Player". The game won't let you change your name later, so a keyboard is still best for this one. The box also comes to the front reliably, in full screen too.
- **Getting it:** Windows: unzip 1.2.2, run **Setup** once, then **Build** with the same build folder: only the game's runtime is replaced, nothing is recompiled. Mac and iPhone: nothing changed for them.

## New in 1.2.1

- **Fixed, Mac: the Python packages could not be installed** ("externally-managed-environment", [#13](https://github.com/xFractals/pokopia-native/issues/13)). Homebrew's Python takes no pip packages of its own, so the README's `pip install` line failed. The builder now has its own environment: once, in Terminal,
  ```bash
  brew install python@3.12 python-tk@3.12
  python3.12 -m venv ~/.pokopia-native/venv
  ~/.pokopia-native/venv/bin/python -m pip install "capstone>=5,<6" lz4 fonttools pillow
  ```
  **Pokopia Native Builder.app** uses it by itself, and **Check tools** shows these steps when it finds such a Python. For the README's commands, run `source ~/.pokopia-native/venv/bin/activate` first.
- **Getting it:** Mac: unzip 1.2.1, make the environment above if you haven't, open the Builder and run **Setup** once; the apps you built with 1.2.0 stay as they are. Windows: nothing changed for the Windows game; 1.2.1 only carries the new version number.

## New in 1.2.0

### Interact everywhere (experimental)

Outside Palette Town the game puts everyone in a Link Play session in Spectator Mode: visitors can walk around and watch, but certain moves and items don't work and they can't build. **Interact everywhere** gives every town the rules Palette Town has with Spectator Mode off, so visitors can water, pick up and place things, and build alongside the host.

- **Turn it on on every device:** iPhone and Mac: **Interact everywhere** in the launcher (under Link Play); Windows: the **Interact everywhere (experimental)** box in the window before the game starts. A device with it on only sees hosts that have it on too.
- **Tested:** two Windows PCs; Windows and Mac, each hosting; a Windows PC hosting with an iPhone visiting in Withered Wasteland, where the visitor watered dry ground into grass and picked up boxes and items.
- The game still shows its own words: the host's settings show Spectator Mode On (greyed out), and the visitor is told everyone will be restricted. The session is not restricted.
- As in any Link Play visit, the town and its progress (challenges, building) belong to the host, and what a visitor picks up goes home with them.
- Game version 2.0.1 only; on 1.0.5 the switch does nothing.

### Windows

- **New: a Controls tab for the keyboard.** The window before the game starts has a **Controls** tab: click an action, then press the key you want for it (a key another action had swaps over); **Reset to defaults** brings the standard layout back. Keys count by their place on the keyboard, so a layout works under any keyboard language. It is saved in `%APPDATA%\Pokopia Native\controls.json`. Esc, F11, Alt and the Windows keys can't be used. (After "Don't ask again", hold **Shift** while starting the game to see the window.)
- **New: the window says when there is no save yet.** Without **Import save…** the game quietly started a new save; the window now says so in red and points to Import save.
- **Fixed: console windows during play with builds made by 1.1.0's builder window.** Their settings named `pythonw.exe`, so every tool the run-time shader translator ran opened its own window. The game's launcher now uses `python.exe` beside it, without a rebuild.
- **Fixed: `pip install lz4` failing on Python 3.15** ("Microsoft Visual C++ 14.0 or greater is required"). lz4 has no ready-made package for 3.15 yet. `Pokopia Native Builder.cmd` now uses an installed Python 3.14, 3.13 or 3.12 that has lz4, and **Check tools** explains what to do when 3.15 is the only one.
- **Fixed: Setup failing with "No such file or directory"** for a file in its own folder. Windows Security's Controlled folder access (or an antivirus) blocks Python in Documents, Desktop, Downloads and Pictures. Setup and Build now check first and say what is blocking them: move the folders elsewhere (for example `C:\Pokopia Native`) or allow `python.exe`.

### Mac

- **Fixed: Pokopia Native Builder did nothing on macOS 27** (its log: `nxs2_gui.py: No such file`). macOS ran the app from a temporary copy away from its folder (App Translocation), because the download was still marked as downloaded. The app now finds its real folder by itself.

### The builder window

- **Clearer text, on Windows and the Mac:** numbered steps (your dump, the build folder, what to make), both game versions named (2.0.1 and 1.0.5), and a line saying what each button is for and in which order. When a build finishes, it says what to do next.

### Source

- The Windows source in `windows/` now includes `src/coop/nxnative/` and `src/swapin/nxnative/`, which an ignore rule left out of the 1.1.x snapshots.
- The Windows shader translator (`host/windows/dmsh_xlate_bin`) no longer ships its debug-symbols file.
- `LEGAL.md` says how rights holders can ask for a change or removal.

### Getting it

- **Windows game:** unzip 1.2.0, run **Setup** once, then **Build** with "Windows: play on this PC" ticked, using the same build folder: the game's runtime and its window are replaced, nothing is recompiled.
- **Mac and iPhone:** unzip 1.2.0, run **Setup** once, then **Build** again with the same build folder: the apps' Link Play code changed, so they are rebuilt (only what changed is redone; your recorded shaders stay). Sign the new iPhone app and install it **over** the old one (never delete the app first: that deletes its save).

## New in 1.1.2

- **Fixed, Windows: new games got stuck at "What's your name?".** The Windows game never showed a keyboard when the game asked for text, so a new game could not get past Tangrowth's name question. A small text box now opens over the game: type the name and press Enter (Esc cancels). Mac and iPhone were not affected.
- **Fixed, Windows: freezes on a first play-through.** The first time the game drew something new, the graphics driver built it on the spot, freezing the game for up to half a second. The builder now ships a list of the game's graphics pipelines (2.0.1: the title, a new game and the first towns), and the game builds them on background threads while it starts. In our test with an empty cache, the pipelines built in the middle of play went from 213 to 1, and the opening area ran at a steady 60 fps.

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
- **No installer, no administrator rights, no compiler**: the download brings its own tools in `windows\bin` (their notices in `windows\bin\THIRD-PARTY-NOTICES.txt`). It needs Windows 10/11 64-bit, an AVX2 CPU, a Vulkan 1.3 GPU, Python 3.12–3.14, the .NET 10 runtime and git, and about 12 GB per game version. The build took 20 minutes on the test PC (4 workers; longer on smaller PCs), at idle priority. Details: [docs/WINDOWS.md](https://github.com/xFractals/pokopia-native/blob/main/docs/WINDOWS.md).
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

- **Interact everywhere is experimental.** The game's own messages still mention Spectator Mode, and it has been tested in the first story areas only. Please report what does or doesn't work in later areas.
- **Windows: if the game still freezes for you**, please open an issue with the newest log from `%LOCALAPPDATA%\Pokopia Native\<game version>\logs\`: since 1.2.4 it names the graphics program the card was running when it stopped.
- The Windows game was developed and tested on one PC (Windows 11, NVIDIA). AMD cards needed the 1.2.2 fix above and are otherwise untested, as are Intel GPUs; please report how it runs.
- Physical controllers on Windows were tested with SDL's virtual pads only; `-PadCheck` logs everything for a report.
- **Windows: the Display tab's 30 fps choice has no effect yet**; the game runs at 60. Making it work needs the game's own frame limiter, as on the Mac (thanks to Quantumenis for spotting it).
- The first start on Windows draws some effects late while the run-time shaders are translated (about 150, once).
- **Windows: areas past the first towns** still build their graphics the first time you visit them (short freezes, once: they are kept for later starts). The list grows with each update. Game version 1.0.5 has no list yet.
- The Expansion Pass (DLC) is not supported yet.
- Link Play over the internet needs the host's router to forward a port; there is no matchmaking server. Hosting from an iPhone over the internet is untested, as are Tailscale-style virtual networks.
- The iPhone app reads its settings when it starts: after changing Link Play settings, close it fully and open it again.
- **Internet play must match on every device**: a device with Internet play on does not see a host on the same Wi-Fi that has it off ("destination not found"). For play on one network, leave it off everywhere.

## Upgrading

- **From 1.2.0, 1.2.1, 1.2.2, 1.2.3, 1.2.4 or 1.2.5:** see "Getting it" under 1.2.6 (Mac users coming from 1.2.0 also see 1.2.1's).
- **From 1.1.x:** see "Getting it" under 1.2.0. On Windows, Setup once and Build again (nothing is recompiled); on the Mac, Setup once and Build again, then install the new iPhone app over the old one.
- **From 1.0.x, Mac / iPhone:** unzip the new download, run Setup once in it, build again with your dump: only what changed is redone, and your recorded shaders stay. Install the new iPhone app **over** the old one (never delete the app first: that deletes its save); the game files on the phone only need copying again if you moved to another update of the game. Mac saves live outside the app. Steps: the README's "Updating from an older version".
- **From 1.0.x, Windows (iPhone builder):** the same. The Windows game is new: tick "Windows: play on this PC".
