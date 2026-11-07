# pokopia-native 1.3.4

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)** for install help, bug reports, co-op partners and release news.

**1.3.4 brings Wide view (experimental) for 21:9 and wider screens**, keeps the mouse camera at full speed while walking sideways, and shows a clear message when the game finds no graphics card it can use. Everything from 1.3.3 is here too: co-op on game 1.0.5, the camera while walking sideways, the NVIDIA freeze fixes and the lag near water.

## Which download do I need?

| I have… | and want to… | Download |
|---|---|---|
| a Windows PC | play on Windows | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Windows PC | build the iPhone app | [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) |
| a Mac | play on the Mac | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |
| a Mac | build the iPhone app | [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) |

- **Co-op needs a matching pokopia-native version on every device.** On game version 2.0.1, 1.3.4 plays with 1.3.3, 1.3.2, 1.3.1 and 1.3.0, and with 1.2.x and 1.1.x as long as Interact everywhere and Story co-op are off: the game's network code did not change. With Interact everywhere on (and Story co-op off), every device needs 1.2.0 or newer with it on; with Story co-op on, 1.3.0 or newer with it on. **On game version 1.0.5, Interact everywhere and Story co-op need 1.3.3 or newer on every device.** And every device needs the same game version: 1.0.5 and 2.0.1 can't play together.
- **Each player needs their own save.** The game refuses a second copy of the same save.

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump. Please do not share build folders, apps, IPAs, `game.dll`, shader files or saves.

## New in 1.3.4

### Windows

- **New, experimental: Wide view for 21:9 and wider screens.** Off by default. On an ultrawide monitor or window, the game's world fills the whole width instead of black bars at the sides, and the HUD, menus, text and map stay in the middle 16:9 part of the screen.
  - **Turning it on:** in the launcher's **Display** tab ("Wide view: On (experimental)"), or in the game: **F1 → Graphics → Wide view**. It takes effect at the next start. On a normal 16:9 screen it changes nothing.
  - **For a sharp picture,** pick a Resolution at least as wide as your screen (for a 3440x1440 monitor, **3840x2160**, if your graphics card can handle it). The default counts by height and looks a little soft across the width.
  - **Photos** come out at the usual 16:9, showing what was inside the viewfinder.
  - **Limits:** full-screen menus such as the camera menu and the album show black bars at the sides. Speech bubbles and the markers over things near the sides of the screen drift toward the middle and can be cut off at the edge of the middle area. Not checked yet: the story's movies and 32:9 screens. Game 1.0.5 and 2.0.1. Not on the Mac or iPhone (they run their own Metal engine); Linux uses the same code but isn't tested yet.
  - **Planned:** widening the game's own HUD and menus to fill the whole screen.
- **New: a clear message when the game finds no graphics card it can use.** Until now the window just closed. Now a box names what was found, for example "Intel(R) UHD Graphics (Vulkan 1.2)", and what to do. Most often a gaming laptop's NVIDIA or AMD card isn't seen because its driver is missing: install the newest driver from nvidia.com or amd.com, restart, and try again.

### Windows, Mac and iPhone

- **Fixed: the mouse camera turned slower while walking sideways or diagonally** ([#31](https://github.com/xFractals/pokopia-native/issues/31), after 1.3.3's fix). Fast turns now land in full while you move, the same as standing still. The same goes for the trackpad on the Mac and look drags on the iPhone.

### The builder

- **Fixed: Copy on the checks page put several `pip install` commands on one line**; it now gives one command that installs everything missing. Thanks **@Shabbypenguin** ([#33](https://github.com/xFractals/pokopia-native/pull/33))!

### Linux

- **Linux and the Steam Deck:** the open beta's current release is **v1.3.3-linux-beta.4** (1.3.3). A later beta brings 1.3.4.

### Getting it

- **Windows:** unzip 1.3.4 into a new folder and open **Pokopia Native Builder**: it offers **Update my game** and builds again into your build folder, about 10 to 20 minutes on a typical PC (the shaders and the game's code are prepared again from what's already there). Your settings, save and recorded shaders carry over.
- **Mac and iPhone:** for the camera fix, rebuild the Mac or iPhone app (**Update my game**), then run **Refresh the graphics** again afterwards. Install a new iPhone app **over** the old one (never delete the app first: that deletes its save).
- **Link Play:** 1.3.4 plays with 1.3.3; co-op didn't change.

## Known issues

- **The graphics update was tested on one PC** (Windows 11, an NVIDIA RTX 4070 Ti with 12 GB, a 4K monitor). AMD and Intel graphics, RTX 50 cards and 4 GB cards are untested with it; please report how it runs.
- **Windows: if the game still freezes for you**, please open an issue with the newest log from `%LOCALAPPDATA%\Pokopia Native\<game version>\logs\`: it names the graphics program the card was running when it stopped.
- **Windows: a dotted line of purple squares** across the sky in the opening scene, where Tangrowth says "This way!". So far it's only seen on AMD cards, and not on ours; we're testing with AMD players to find the effect that's drawn wrong.
- **AMD and Intel graphics:** some players see freezes, often in conversations. Try the 32-lane test mode (under New in 1.3.1) and tell us how it goes.
- **At 30 fps** the picture reaches the screen about a frame later than at 60.
- **Interact everywhere and Story co-op on 1.0.5** were checked in the game's code and started on Windows, but not yet played in a session with two players. Please report how they go. (The 1.0.5 dump's title screen says "Ver. 1.0.4": that's the same version.)
- **Interact everywhere and Story co-op are experimental.** The game's own messages still mention Spectator Mode. Story requests count only for the host.
- Physical controllers on Windows were tested with SDL's virtual pads and by hand on one PC; `-PadCheck` logs everything for a report.
- The first start on Windows draws some effects late while the run-time shaders are translated (about 150, once). Areas past the first towns still build their graphics the first time you visit them (short freezes, once).
- **The Mac gets the new builder (and Story co-op), not the graphics update**: the Mac app runs on its own Metal engine, which has its own settings window and MetalFX upscaling.
- The Expansion Pass (DLC) is not supported yet.
- Link Play over the internet needs the host's router to forward a port; there is no matchmaking server.
- The iPhone app reads its settings when it starts: after changing Link Play settings, close it fully and open it again.
- **Internet play must match on every device**: a device with Internet play on does not see a host on the same Wi-Fi that has it off.

## Upgrading

- **From 1.3.3, 1.3.2, 1.3.1 or 1.3.0:** Windows, see "Getting it" above; Mac and iPhone, rebuild for the camera fix, then Refresh.
- **From 1.2.x:** the same as from 1.3.3 on Windows; Mac and iPhone, see "Getting it" under 1.3.0.
- **From 1.1.x:** the same, then (Mac) install the new iPhone app over the old one.
- **From 1.0.x, Mac / iPhone:** unzip the new download and open the builder; build again with your dump: only what changed is redone, and your recorded shaders stay. Install the new iPhone app **over** the old one (never delete the app first: that deletes its save).

## New in 1.3.3

### Windows, Mac and iPhone

- **Fixed: the mouse camera wouldn't turn left or right while walking sideways or diagonally** ([#31](https://github.com/xFractals/pokopia-native/issues/31); up and down still worked). While your character moves sideways, the game's own camera turns to follow you, and it overrode the mouse. Now, while you move sideways, the mouse turns the camera the way a controller's stick does, and the game takes it. The same goes for the trackpad on the Mac and look drags on the iPhone. While moving sideways, a very fast flick turns at most as fast as a full stick tilt.

### Windows

- **Fixed: another freeze on NVIDIA cards** (a GTX 1650 after about an hour on 1.3.1). A second graphics program that walks a list of lights now stops at the list's real length, like 1.3.1's fix. If the game still freezes, the log now records more of what that program was given.
- **Fixed: lag near water** ([#27](https://github.com/xFractals/pokopia-native/issues/27)). The game sent the graphics card data the card had just made itself, again and again; near water that came to 50 to 90 MB a frame and halved the frame rate on some PCs. On our test route it now sends about a fifth as much (6.3 MB a frame down to 1.4), and what doesn't fit the upload buffer goes in larger blocks.
- **For AMD players helping with the purple line:** a new test switch, `NXRT_VK_SKIP`, turns off one graphics effect at a time to find the one that's drawn wrong. The steps are on Discord.

### Co-op

- **Interact everywhere and Story co-op now work on game version 1.0.5 too.** Until 1.3.2 they needed 2.0.1, and with a 1.0.5 dump they quietly did nothing. **Every 1.0.5 player needs to update for co-op to match:** a 1.0.5 device on 1.3.2 or older never really had them on, so a 1.3.3 device with them on won't find it, and the other way round. Turn them on on every device, as before.
- **Playing through a VPN (Radmin VPN, Hamachi, Tailscale):** use the internet choices with the host's VPN address and the room code. On Windows they're now called **Host over the internet or a VPN** and **Visit a host over the internet or a VPN**; through a VPN no port forwarding is needed. **Link Play with players on this network** doesn't reach through a VPN: Pokopia Native counts a VPN's addresses as internet addresses. If you give the Windows launcher a VPN address without internet play, it now says so before the game starts, and the log names the VPN.

### Linux

- **Linux and the Steam Deck:** the open beta's current release is **v1.3.0-linux-beta.3**. A beta 4 with 1.3.3's fixes is being prepared.

## New in 1.3.2

### Windows

- **Fixed: 1.3.1 crashed on AMD graphics cards** about 20 seconds in (seen on a ROG Xbox Ally X; the crash report names `amdvlk64.dll`). 1.3.1's fix for an NVIDIA freeze changes one of the game's graphics programs, and AMD's Windows driver crashed preparing the changed program. That fix now applies on NVIDIA cards only, where the freeze happened; AMD and Intel cards run that program as they did in 1.3.0.
- **If you used the workaround** (`setx NXRT_VK_LOOP_GUARD 0`): on an AMD or Intel card you can leave it, it does the same as 1.3.2 does by itself. On an NVIDIA card, remove it so the freeze fix stays on: open PowerShell and run `reg delete HKCU\Environment /v NXRT_VK_LOOP_GUARD /f`, then close the builder and start the game again.

### Mac, iPhone and Linux

- **Mac and iPhone:** nothing changed but the version number, so there's no need to rebuild. If you rebuild the Mac or iPhone app anyway, run **Refresh the graphics** again afterwards.
- **Linux and the Steam Deck:** the open beta has its own release, **v1.3.0-linux-beta.3**. It never had 1.3.1's change, so this crash doesn't affect it.
- **Link Play:** 1.3.2 plays with 1.3.1 and 1.3.0; co-op didn't change.

## New in 1.3.1

### Windows

- **Fixed: another freeze on NVIDIA cards** (a GTX 1650 after about an hour of play, an RTX 3050 Laptop). The graphics card stopped responding in one of the game's graphics programs, one that walks a list of items: it now stops at the list's real length, so a bad count can't keep the card busy. If the game still freezes for you, please send the newest log: it now also records what that program was given.
- **Fixed: the build failed in a folder with an accented letter** in its path, for example "Pokémon" or a French "Téléchargements" folder ("lld-link: error: could not open ...\p0000.obj: No mapping for the Unicode character exists in the target multi-byte code page"). Any folder name works now.
- **Fixed: ".NET 10 missing" right after installing it.** Windows only gives a new install to programs started afterwards, so the builder's **Check again** couldn't see .NET installed while the builder was open. It now looks in .NET's own folder too.
- **New, for AMD and Intel graphics: a 32-lane test mode, off by default.** The game's graphics programs were written for NVIDIA-style groups of 32; AMD cards run 64 and Intel cards 8 to 32, which may be behind the freezes some of you see, often in conversations. To try it, open PowerShell and run `setx NXRT_VK_SUBGROUP32 1`, close the builder if it's open, then start the game as usual; the log then says "made with 32 lanes". `setx NXRT_VK_SUBGROUP32 0` turns it off again. Please tell us how it goes, on Discord or in a GitHub issue.

### Mac, iPhone and Linux

- **Mac and iPhone:** nothing changed but the version number, so there's no need to rebuild. If you rebuild the Mac or iPhone app anyway, run **Refresh the graphics** again afterwards.
- **Linux and the Steam Deck:** the open beta has its own release, **v1.3.0-linux-beta.2**, which fixes the start-up crash on Fedora, SteamOS (the Steam Deck), Arch and others.
- **Link Play:** 1.3.1 plays with 1.3.0, and with 1.2.x and 1.1.x while Interact everywhere and Story co-op are off; co-op didn't change.

## New in 1.3.0

### Windows: the graphics update

- **New: a sharper picture.** The new **Resolution** setting draws the game itself at a larger size (the 3D scene, its lighting, effects and the HUD), not just the finished picture stretched to your window. The choices are the real sizes the game draws at: **Default: your monitor** (where the game starts), **1920x1080** (the game's own size, as before), **2560x1440** and **3840x2160**. On a graphics card with 4 GB of its own memory or less, Default stops at 2560x1440 so the card doesn't run out; 3840x2160 can still be picked by hand. Bigger sizes need more from the graphics card: on a slower card, pick a smaller one.
- **New: settings inside the game.** Press **F1** (or **Minus + Plus** together on a controller) for a settings window over the game:
  - **Graphics:** Resolution, Display mode, Window size, Console mode, Upscaling, Sharpness, Frame rate, Vsync and an **FPS counter**.
  - **Controls:** the keyboard key for each action. Picking a key another action had swaps the two; Reset to defaults brings the usual keys back.

  Changes apply at once, without a restart (Console mode applies from the next start). They're saved, and the launcher's Display and Controls tabs show the same settings.
- **New: less processor work per frame**, about a third less: 7.5 ms → 5.0 ms in the field on our test PC. A fast PC was already at 60 fps; a slower one should gain frames. This is three changes by **@Quantumenis**: textures rewritten with the same bytes aren't uploaded again, draws record only the settings that changed, and identical resource bindings are reused within a frame. Thank you, Quantum!
- **Fixed: the 30 fps setting did nothing** (a known issue in 1.2.6, spotted by Quantumenis). Each frame now stays on screen for two refreshes, and the world still moves at the right speed.
- **Fixed: the game freezing on some NVIDIA cards**, seen on an RTX 3050 Laptop ("the GPU stopped responding" in the log). It's the same kind of hang 1.2.5 fixed for RTX 50 cards, in a different graphics program. The fix now covers every graphics program of that kind in the game (11 of them), not just one. It applies on NVIDIA cards only, where these freezes happen; AMD and Intel cards run those programs as they were translated, as before 1.2.5. If the game still freezes for you, please send the newest log (see Known issues).
- **The launcher's Display tab** lists the same sizes as the in-game menu, and names the console modes "Docked (TV)" and "Handheld".

### Windows and Mac: a step-by-step builder

- **New: the builder walks you through it.** Open **Pokopia Native Builder** and it asks what you want to make (this PC or Mac, the iPhone), checks your computer and runs Setup for you, finds your game files (and tells you which game version they are), shows where the build goes and the free space there, then builds with plain-English steps and a progress bar. When something is missing or fails, it says what to do, with the real error message and a button to copy it for the Discord.
- **It remembers where you are.** After the first build of the Mac or iPhone app, opening the builder again shows **Refresh the graphics** as your next step. Opening it from a new download offers **Update my game**.
- **Advanced** (top right) is the one-screen window with every button, as before. The command line is unchanged.

### Co-op: Story co-op (experimental)

**Link Play visitors can now talk to the story characters in every town.** In a story town the game lets only the host talk
to characters like Professor Tangrowth: a visitor got the "Talk" prompt, and nothing happened. With **Story co-op** on,
visitors talk to them and hear the same story as the host, including the host's current requests.

- **What counts for the host:** everything visitors do in the world, as with Interact everywhere (which Story co-op turns on
  with it): building, watering, placing and picking up, and the host's challenges (habitats too, we expect, but that
  isn't tested end to end yet).
- **What doesn't:** story requests still count only for the host. A visitor can hear a request, but finishing the
  conversation doesn't complete it; the host has to. A visitor's shop purchases still go to their own town, so for a
  building request the host buys the kit and drops it for the visitor.
- **Turn it on on every device:** iPhone and Mac: **Story co-op** in the launcher (under Link Play); Windows: the **Story
  co-op (experimental)** box in the window before the game starts, or `-Story` from a terminal. A device with it on only
  sees hosts that have it on too; with it off, nothing changes from 1.2.x.
- **It is experimental.** Tested with two players in Withered Wasteland (story areas at Environment Level 2 and 3). Keep a copy
  of your save before a long session, and please report what does or doesn't work.

### Getting it

- **Windows:** unzip 1.3.0 into a new folder and open **Pokopia Native Builder**. The walkthrough checks your PC, runs Setup and builds again into the same build folder: the game's code is relinked from what is already compiled (a few minutes), the runtime and launchers are replaced, and no shader is translated again. (In Advanced: Setup once, then Build.) Your settings and save carry over.
- **Mac:** the same. The game's code rewrite and the Mac host are redone and the app is repacked, a couple of minutes; no shader is recompiled.
- **iPhone:** build the new iPhone app the same way and install it **over** the old one (never delete the app first: that deletes its save). You need it for Story co-op; nothing else changed for the iPhone.

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

- **Fixed, Windows: the game freezing on RTX 50-series graphics cards** ([#12](https://github.com/xFractals/pokopia-native/issues/12), [#21](https://github.com/xFractals/pokopia-native/issues/21), [#22](https://github.com/xFractals/pokopia-native/issues/22)), often right after loading a save or as soon as you start moving. One of the game's graphics programs ends with a quick vote among the GPU threads running it, to add up a counter. On RTX 50 cards that vote never finishes once some of those threads are already done, so the card stopped responding. The game now has each thread add its own share instead, which gives the same count. Only that one program changes; every other card runs it the same as before. **Found and fixed by #12's reporter, @Quantumenis**, who tracked the freeze down to the exact instruction. Thank you!
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
