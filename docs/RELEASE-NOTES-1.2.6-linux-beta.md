# pokopia-native 1.2.6 for Linux and the Steam Deck (open beta)

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: Linux and Steam Deck reports go to **#linux-beta**.

**This is an open beta, and an early one.** The Linux build has **never run on a real Steam Deck**, nor on a Linux PC with a real graphics card. It was
made and tested only under WSL (Linux inside Windows) with Mesa's software renderer, where it builds the game from a dump and reaches the title
screen in a window; nobody has played further than that on Linux yet. On your computer the first start may well fail. That is fine, and exactly what this beta is for: when it does, **the logs are
what we need** (how to send them is below). Windows and Mac players: this pre-release is not for you; use [1.2.6](https://github.com/xFractals/pokopia-native/releases/latest).

## Which download do I need?

| I have… | Download |
|---|---|
| a Steam Deck, or a Linux PC, and want the easy way | [Pokopia-Native-Builder-1.2.6-beta-x86_64.AppImage](https://github.com/xFractals/pokopia-native/releases/download/v1.2.6-linux-beta/Pokopia-Native-Builder-1.2.6-beta-x86_64.AppImage) |
| a Linux PC and prefer a terminal, or the AppImage will not start | [Pokopia-Native-for-Linux-beta.zip](https://github.com/xFractals/pokopia-native/releases/download/v1.2.6-linux-beta/Pokopia-Native-for-Linux-beta.zip) |

Both are the same builder for **x86-64** (the Steam Deck, Intel and AMD PCs); ARM devices are not supported. You need a CPU with AVX2 and FMA3
(anything from about 2015 on), a Vulkan 1.3 driver (SteamOS has it; elsewhere Mesa RADV or ANV, or NVIDIA's), about 15 GB free for the build
folder, and **your own dump** with the game's update (2.0.1: `updates/655360`; 1.0.5 works too).

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump.
Please do not share build folders, `game.so`, shader files or saves.

## Getting started

1. Download the AppImage and make it executable: in Dolphin (the Steam Deck's file manager, in Desktop Mode) right-click it → **Properties** →
   **Permissions** → tick **Is executable**; or in a terminal `chmod +x Pokopia-Native-Builder-1.2.6-beta-x86_64.AppImage`.
2. Double-click it. The first start unpacks the builder to `~/.local/share/pokopia-native/builder/1.2.6-beta` (about 1 GB, once).
3. In the window: **Choose…** your dump folder, **Build** (the first build takes a while: the game's code is recompiled for your CPU), then **Play**.
   **Import save…** brings your save from Eden, the phone or a Mac.

**Steam Deck Game Mode:** add `<your build folder>/out/play-pokopia-2.0.1.sh` to Steam as a non-Steam game (not the AppImage). A new game asks
for your character's name: if no box shows in Game Mode, press **A** and the name is "Player" (to choose one, start the new game in Desktop Mode).
The full guide, including the zip's terminal steps and Arch / CachyOS packages, is `docs/LINUX.md` in the zip.

## Reporting: what helps

1. Make the report archive. AppImage: `./Pokopia-Native-Builder-1.2.6-beta-x86_64.AppImage report <your build folder>` in a terminal. Zip:
   `~/pokopia-native-1.2.6-beta-linux/linux/report.sh <your build folder>`. It collects the game's and the build's logs and facts about your computer
   (GPU, driver and Mesa version, distribution or SteamOS version, memory). It **never** includes your save, game files, shader files or your dump,
   replaces your user and computer names, and shows you every file before it writes the archive.
2. Open an issue with the **[Linux / Steam Deck beta report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
   form and attach the archive, or post it in **#linux-beta** on the [Discord](https://discord.gg/Aj9GqxZj8V).
3. A short screen recording of the build and the first minutes helps more than any description (OBS from Discover, or Steam's game recording).

If the game closes by itself or the picture breaks up when a new place loads, start it once with `NXRT_VK_ROBUST=1` in front of the launcher and
say whether that helped.

## Known

- **Untested on real hardware**: the Steam Deck, AMD, Intel and NVIDIA graphics on Linux, Game Mode, controllers through Steam Input.
- **Link Play** is built in and meant to play with Windows, Mac and iPhone players on 1.2.x (the same rules as 1.2.6), but it has not been tried
  from Linux yet. Linux cannot find hosts on its own network: join by the host's address.
- **The builder window looks plain** (a single basic font); it works.
- **Upgrading later:** download the new AppImage and **Build** again with the same build folder.
