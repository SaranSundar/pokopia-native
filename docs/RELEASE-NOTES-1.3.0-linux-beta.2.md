# pokopia-native 1.3.0 Linux beta 2: the game now starts outside Ubuntu (Steam Deck, Fedora, Arch and more)

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: Linux and Steam Deck reports go to **#linux-beta**.

**The game now starts on more than Ubuntu.** Every Linux beta so far (1.1.0 to 1.3.0 beta 1) could only start on Ubuntu and the systems built on
it (Linux Mint, Pop!_OS, Zorin…). Everywhere else the build finished, but pressing **Play** did nothing: the game closed before its first frame.
That is Fedora, where it was reported, and every system that builds its Vulkan library the same way, which by all signs includes **SteamOS on the
Steam Deck**, Arch, CachyOS, Bazzite, openSUSE and Debian. This beta fixes that. Many thanks to **@apocal26**, who found the cause and a workaround
([#30](https://github.com/xFractals/pokopia-native/issues/30)).

**Already on 1.3.0 beta 1?** Download the new AppImage (or zip), open it, and run **Build** again with the same build folder: it puts the fixed
game in place, and only what changed is redone. Nothing else changed from beta 1.

**This is still an open beta, and an early one.** The Linux build has **never run on a real Steam Deck** yet, and has run on real Linux graphics
cards only in testers' hands. We made and tested it under WSL (Linux inside Windows) with Mesa's software renderer, where it builds the game from a
dump and reaches the game's first screens in a window. On your computer the first start may still fail. When it does, **the logs are what we need**
(how to send them is below). Windows and Mac players: this pre-release is not for you; use [1.3.0](https://github.com/xFractals/pokopia-native/releases/latest).

## Which download do I need?

| I have… | Download |
|---|---|
| a Steam Deck, or a Linux PC, and want the easy way | [Pokopia-Native-Builder-1.3.0-beta.2-x86_64.AppImage](https://github.com/xFractals/pokopia-native/releases/download/v1.3.0-linux-beta.2/Pokopia-Native-Builder-1.3.0-beta.2-x86_64.AppImage) |
| a Linux PC and prefer a terminal, or the AppImage will not start | [Pokopia-Native-for-Linux-beta.zip](https://github.com/xFractals/pokopia-native/releases/download/v1.3.0-linux-beta.2/Pokopia-Native-for-Linux-beta.zip) |

Both are the same builder for **x86-64** (the Steam Deck, Intel and AMD PCs); ARM devices are not supported. You need a CPU with AVX2 and FMA3
(anything from about 2015 on), a Vulkan 1.3 driver (SteamOS has it; elsewhere Mesa RADV or ANV, or NVIDIA's), about 15 GB free for the build
folder, and **your own dump** with the game's update (2.0.1: `updates/655360`; 1.0.5 works too).

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump.
Please do not share build folders, `game.so`, shader files or saves.

## What changed in beta 2

- **Fixed: the game closed before its first frame on Fedora and most systems other than Ubuntu (#30).** The Linux game shared its own
  Vulkan function pointers with the rest of the system under the very names of the Vulkan library's functions. Wherever that library is built
  without a particular link option (Ubuntu adds it; Fedora doesn't, and by all signs neither do Arch and SteamOS), it then handed the game its own pointer back in place of
  the real function, and the game crashed at once (`execute nxrt+0xf2f270` in the crash report). Now the game shares only the names the
  recompiled game needs, and a test keeps it that way.
- Everything else is 1.3.0 beta 1: the settings inside the game (**F1**), the Resolution setting, Story co-op, the step-by-step builder. See
  [1.3.0 beta 1](https://github.com/xFractals/pokopia-native/releases/tag/v1.3.0-linux-beta) and
  [1.3.0](https://github.com/xFractals/pokopia-native/releases/tag/v1.3.0) (`docs/RELEASE-NOTES-1.3.0.md` in the zip).

## What we checked for beta 2, and what we could not

In WSL (Ubuntu 22.04) with Mesa's software renderer: Linux's own tests pass, among them a new one that fails if the game shares anything beyond
what it must. With a Vulkan library built the way Fedora builds it (the Vulkan SDK's own build), beta 1's game crashed at the reported address
and beta 2's game starts and draws; with Ubuntu's own library both start. The AppImage checks the computer and opens its builder window.
**Not checked:** a real Steam Deck, Fedora, Arch or SteamOS themselves, and any real graphics card. If it starts for you now, please say so in
**#linux-beta** or on [#30](https://github.com/xFractals/pokopia-native/issues/30), with your system.

## Getting started

1. Download the AppImage and make it executable: in Dolphin (the Steam Deck's file manager, in Desktop Mode) right-click it → **Properties** →
   **Permissions** → tick **Is executable**; or in a terminal `chmod +x Pokopia-Native-Builder-1.3.0-beta.2-x86_64.AppImage`.
2. Double-click it. The first start unpacks the builder to `~/.local/share/pokopia-native/builder/1.3.0-beta.2` (about 1 GB, once; beta 1's copy in
   `builder/1.3.0-beta` can be deleted once your game is built again).
3. The window walks you through it: your game files, the build (the first one takes a while: the game's code is recompiled for your CPU), then
   **Play**. **Bring my save** on the last page brings your save from Eden, the phone or a Mac.

**Steam Deck Game Mode:** add `<your build folder>/out/play-pokopia-2.0.1.sh` to Steam as a non-Steam game (not the AppImage). A new game asks
for your character's name: if no box shows in Game Mode, press **A** and the name is "Player" (to choose one, start the new game in Desktop Mode).
The full guide, including the zip's terminal steps and Arch / CachyOS packages, is `docs/LINUX.md` in the zip.

## Reporting: what helps

1. Make the report archive. AppImage: `./Pokopia-Native-Builder-1.3.0-beta.2-x86_64.AppImage report <your build folder>` in a terminal. Zip:
   `~/pokopia-native-1.3.0-beta-linux/linux/report.sh <your build folder>`. It collects the game's and the build's logs and facts about your computer
   (GPU, driver and Mesa version, distribution or SteamOS version, memory). It **never** includes your save, game files, shader files or your dump,
   replaces your user and computer names, and shows you every file before it writes the archive.
2. Open an issue with the **[Linux / Steam Deck beta report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
   form and attach the archive, or post it in **#linux-beta** on the [Discord](https://discord.gg/Aj9GqxZj8V).
3. A short screen recording of the build and the first minutes helps more than any description (OBS from Discover, or Steam's game recording).

If the game closes by itself or the picture breaks up when a new place loads, start it once with `NXRT_VK_ROBUST=1` in front of the launcher and
say whether that helped.

## Known

- **Intel Arc graphics (seen on an Arc 140V, Lunar Lake): the game froze after about 3 minutes** in 3D areas ("the GPU stopped responding" in
  the log), after heavy stutter, in #30's tests. Not fixed in this beta. We're working on how Intel GPUs run some of the game's graphics programs,
  and on the memory pressure just before the freeze. Reports from other Intel and AMD GPUs help.
- **Untested on real hardware by us**: the Steam Deck, AMD, Intel and NVIDIA graphics on Linux, Game Mode, controllers through Steam Input.
- **At 2560x1440, Mesa's software renderer still closed the game now and then** in our tests (once in 7 starts, a couple of minutes in);
  at 1920x1080 it did not. If the game closes by itself, choose 1920x1080 under Resolution (F1), and please send the report.
- **Link Play** is built in and meant to play with Windows, Mac and iPhone players on 1.3.0 (the same rules as 1.3.0), but it has not been
  tried from Linux yet. Linux cannot find hosts on its own network: join by the host's address.
- **The builder window looks plain** (a single basic font); it works.
- **Upgrading later:** download the new AppImage and **Build** again with the same build folder.
