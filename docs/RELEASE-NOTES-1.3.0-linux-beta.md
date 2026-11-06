# pokopia-native 1.3.0 for Linux and the Steam Deck (open beta)

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: Linux and Steam Deck reports go to **#linux-beta**.

**This is an open beta, and an early one.** The Linux build has **never run on a real Steam Deck**, nor on a Linux PC with a real graphics card. It was
made and tested only under WSL (Linux inside Windows) with Mesa's software renderer, where it builds the game from a dump and reaches the game's
first screens in a window; nobody has played further than that on Linux yet. On your computer the first start may well fail. That is fine, and exactly what this beta is for: when it does, **the logs are
what we need** (how to send them is below). Windows and Mac players: this pre-release is not for you; use [1.3.0](https://github.com/xFractals/pokopia-native/releases/latest).

## Which download do I need?

| I have… | Download |
|---|---|
| a Steam Deck, or a Linux PC, and want the easy way | [Pokopia-Native-Builder-1.3.0-beta-x86_64.AppImage](https://github.com/xFractals/pokopia-native/releases/download/v1.3.0-linux-beta/Pokopia-Native-Builder-1.3.0-beta-x86_64.AppImage) |
| a Linux PC and prefer a terminal, or the AppImage will not start | [Pokopia-Native-for-Linux-beta.zip](https://github.com/xFractals/pokopia-native/releases/download/v1.3.0-linux-beta/Pokopia-Native-for-Linux-beta.zip) |

Both are the same builder for **x86-64** (the Steam Deck, Intel and AMD PCs); ARM devices are not supported. You need a CPU with AVX2 and FMA3
(anything from about 2015 on), a Vulkan 1.3 driver (SteamOS has it; elsewhere Mesa RADV or ANV, or NVIDIA's), about 15 GB free for the build
folder, and **your own dump** with the game's update (2.0.1: `updates/655360`; 1.0.5 works too).

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump.
Please do not share build folders, `game.so`, shader files or saves.

## New in 1.3.0 for Linux

The Linux build follows [1.3.0](https://github.com/xFractals/pokopia-native/releases/tag/v1.3.0) (all of it: `docs/RELEASE-NOTES-1.3.0.md` in the
zip). What that means on Linux:

- **Settings inside the game:** **F1**, or **−** and **+** together on a controller (the Steam Deck's View and Menu buttons), opens the settings over
  the game: Resolution (the size the game draws at: Default, 1920x1080, 2560x1440, 3840x2160), window or full screen, Upscaling, 30 fps, Vsync, an FPS
  counter, and the keyboard's keys. Changes apply at once and are kept for the next start.
- **Story co-op (experimental)** and **Interact everywhere**: the Link Play choice asks for both; from Steam's Launch Options, `%command% --link-play --story`.
  Every player needs the same choice; with both off, Linux plays with 1.2.x as before.
- **The builder walks you through it:** the AppImage's window goes one step at a time (what to make, a check of your computer, your game files, the
  build, Play). **Advanced** (top right) has the one-screen window with every button.
- 1.3.0's renderer changes come along: less processor work per frame, and the 30 fps setting that did nothing now works.
- 1.3.0's fix for the NVIDIA freeze rewrites some of the game's graphics programs. On Linux it is used on NVIDIA GPUs only: in our tests the
  rewritten programs crashed Mesa's software renderer, and they assume NVIDIA's way of grouping work, which AMD's (the Steam Deck's) is not.

## What we checked for 1.3.0, and what we could not

In WSL (Ubuntu 22.04) with Mesa's software renderer, on a copy of a test save: the AppImage checks the computer and builds the game; the game
starts and draws its first screen (the language choice); **F1** opens the settings over the game and closes them again, and so does **Esc** (real
key presses), with the Graphics tab, the Resolution list and the FPS counter; Resolution changes apply at once, are saved and are used again at
the next start; Linux's own tests pass (including the menu's keyboard and controller input). **Not checked:** anything on a Steam Deck or a real
graphics card, playing further than that first screen (the software renderer draws about 2 frames a second), **−** and **+** on a real
controller, Story co-op and Link Play from Linux.

## Getting started

1. Download the AppImage and make it executable: in Dolphin (the Steam Deck's file manager, in Desktop Mode) right-click it → **Properties** →
   **Permissions** → tick **Is executable**; or in a terminal `chmod +x Pokopia-Native-Builder-1.3.0-beta-x86_64.AppImage`.
2. Double-click it. The first start unpacks the builder to `~/.local/share/pokopia-native/builder/1.3.0-beta` (about 1 GB, once).
3. The window walks you through it: your game files, the build (the first one takes a while: the game's code is recompiled for your CPU), then
   **Play**. **Bring my save** on the last page brings your save from Eden, the phone or a Mac.

Already built with 1.2.6? Open the new AppImage and **Build** again with the same build folder: only what changed is redone.

**Steam Deck Game Mode:** add `<your build folder>/out/play-pokopia-2.0.1.sh` to Steam as a non-Steam game (not the AppImage). A new game asks
for your character's name: if no box shows in Game Mode, press **A** and the name is "Player" (to choose one, start the new game in Desktop Mode).
The full guide, including the zip's terminal steps and Arch / CachyOS packages, is `docs/LINUX.md` in the zip.

## Reporting: what helps

1. Make the report archive. AppImage: `./Pokopia-Native-Builder-1.3.0-beta-x86_64.AppImage report <your build folder>` in a terminal. Zip:
   `~/pokopia-native-1.3.0-beta-linux/linux/report.sh <your build folder>`. It collects the game's and the build's logs and facts about your computer
   (GPU, driver and Mesa version, distribution or SteamOS version, memory). It **never** includes your save, game files, shader files or your dump,
   replaces your user and computer names, and shows you every file before it writes the archive.
2. Open an issue with the **[Linux / Steam Deck beta report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
   form and attach the archive, or post it in **#linux-beta** on the [Discord](https://discord.gg/Aj9GqxZj8V).
3. A short screen recording of the build and the first minutes helps more than any description (OBS from Discover, or Steam's game recording).

If the game closes by itself or the picture breaks up when a new place loads, start it once with `NXRT_VK_ROBUST=1` in front of the launcher and
say whether that helped.

## Known

- **Untested on real hardware**: the Steam Deck, AMD, Intel and NVIDIA graphics on Linux, Game Mode, controllers through Steam Input, and the
  in-game settings with a real keyboard or controller on Linux.
- **Link Play** is built in and meant to play with Windows, Mac and iPhone players on the same version (the same rules as 1.3.0), but it has not been
  tried from Linux yet. Linux cannot find hosts on its own network: join by the host's address.
- **The builder window looks plain** (a single basic font); it works.
- **Upgrading later:** download the new AppImage and **Build** again with the same build folder.
