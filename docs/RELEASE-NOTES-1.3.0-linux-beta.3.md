# pokopia-native 1.3.0 Linux beta 3: crackle-free sound on PipeWire

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: Linux and Steam Deck reports go to **#linux-beta**.

**The sound no longer crackles on PipeWire.** Most current Linux systems play sound through PipeWire: SteamOS on the Steam Deck, Fedora, Arch,
CachyOS and more. There the game's sound crackled all the time; beta 3 fixes it. Thanks to **@Quantumenis** for diagnosing the crackle: his
measurements across buffer sizes and audio drivers showed that the game's queue of three sound blocks was running dry on every request, and his
PulseAudio workaround proved it.

**Already on beta 2 or beta 1?** Download the new AppImage (or zip), open it, and run **Build** again with the same build folder: it puts the fixed
game in place, and only what changed is redone. Beta 3 keeps beta 2's fix for the start-up crash outside Ubuntu
([#30](https://github.com/xFractals/pokopia-native/issues/30)).

**This is still an open beta, and an early one.** The Linux build has **never run on a real Steam Deck** yet, and has run on real Linux graphics
cards only in testers' hands. We made and tested it under WSL (Linux inside Windows) with Mesa's software renderer, where it builds the game from a
dump and reaches the game's first screens in a window. On your computer the first start may still fail. When it does, **the logs are what we need**
(how to send them is below). Windows and Mac players: this pre-release is not for you; use [1.3.1](https://github.com/xFractals/pokopia-native/releases/latest).

## Which download do I need?

| I have… | Download |
|---|---|
| a Steam Deck, or a Linux PC, and want the easy way | [Pokopia-Native-Builder-1.3.0-beta.3-x86_64.AppImage](https://github.com/xFractals/pokopia-native/releases/download/v1.3.0-linux-beta.3/Pokopia-Native-Builder-1.3.0-beta.3-x86_64.AppImage) |
| a Linux PC and prefer a terminal, or the AppImage will not start | [Pokopia-Native-for-Linux-beta.zip](https://github.com/xFractals/pokopia-native/releases/download/v1.3.0-linux-beta.3/Pokopia-Native-for-Linux-beta.zip) |

Both are the same builder for **x86-64** (the Steam Deck, Intel and AMD PCs); ARM devices are not supported. You need a CPU with AVX2 and FMA3
(anything from about 2015 on), a Vulkan 1.3 driver (SteamOS has it; elsewhere Mesa RADV or ANV, or NVIDIA's), about 15 GB free for the build
folder, and **your own dump** with the game's update (2.0.1: `updates/655360`; 1.0.5 works too).

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump.
Please do not share build folders, `game.so`, shader files or saves.

## What changed in beta 3

- **Fixed: crackling sound on PipeWire.** The game keeps three short blocks of sound (20 ms in all) waiting for the sound card, and makes the
  next one only when one has played. PipeWire asks for a longer stretch at once (21 ms by default, more with a bigger buffer), so every request
  came up short and the gap was filled with silence: the crackle, and a bigger buffer made it worse. Now the Linux game keeps one of the sound
  card's stretches ready ahead of it, filled from the game's blocks as they come. It adds about one stretch (around 20 ms) of sound delay; sound
  cards that ask for short stretches (Windows' usual 10 ms) work as before. If the sound still crackles for you, start the game once with
  `SDL_AUDIO_DRIVER=pulseaudio` in front of the launcher and tell us whether that helped (`docs/LINUX.md`, "Common problems").
- **From beta 2: the game starts outside Ubuntu** ([#30](https://github.com/xFractals/pokopia-native/issues/30)). Every Linux beta before beta 2
  could only start on Ubuntu and the systems built on it; elsewhere pressing **Play** did nothing. Thanks to **@apocal26**, who found the cause
  and a workaround, and to **@Quantumenis** for confirming it on CachyOS with an NVIDIA RTX 5070 Laptop, which showed it wasn't limited to
  Fedora or Intel graphics.
- Everything else is 1.3.0 beta 1: the settings inside the game (**F1**), the Resolution setting, Story co-op, the step-by-step builder. See
  [1.3.0 beta 1](https://github.com/xFractals/pokopia-native/releases/tag/v1.3.0-linux-beta) and
  [1.3.0](https://github.com/xFractals/pokopia-native/releases/tag/v1.3.0) (`docs/RELEASE-NOTES-1.3.0.md` in the zip).

## What we checked for beta 3, and what we could not

In WSL (Ubuntu 22.04) with Mesa's software renderer, and SDL's test sound driver set to ask for sound the way PipeWire does: with beta 2 about
840 of every 2,000 sound requests found nothing ready (as in Quantumenis's logs); with beta 3 none of 6,000 did at 1,024 frames, and 3 of 6,000
at 2,048 frames, all within the first 2,000, as the game started. Linux's
own tests pass, among them a new one for exactly this. The game starts and draws with Ubuntu's Vulkan library and with one built the way
Fedora builds it, and the AppImage opens its builder window. **Not checked:** real PipeWire (our test system has none), a real Steam Deck, and
any real graphics card. If the sound is clean for you now, or still isn't, please say so in **#linux-beta**, with your system.

## Getting started

1. Download the AppImage and make it executable: in Dolphin (the Steam Deck's file manager, in Desktop Mode) right-click it → **Properties** →
   **Permissions** → tick **Is executable**; or in a terminal `chmod +x Pokopia-Native-Builder-1.3.0-beta.3-x86_64.AppImage`.
2. Double-click it. The first start unpacks the builder to `~/.local/share/pokopia-native/builder/1.3.0-beta.3` (about 1 GB, once; an earlier
   beta's copy in `builder/` can be deleted once your game is built again).
3. The window walks you through it: your game files, the build (the first one takes a while: the game's code is recompiled for your CPU), then
   **Play**. **Bring my save** on the last page brings your save from Windows, the phone or a Mac.

**Steam Deck Game Mode:** add `<your build folder>/out/play-pokopia-2.0.1.sh` to Steam as a non-Steam game (not the AppImage). A new game asks
for your character's name: if no box shows in Game Mode, press **A** and the name is "Player" (to choose one, start the new game in Desktop Mode).
The full guide, including the zip's terminal steps and Arch / CachyOS packages, is `docs/LINUX.md` in the zip.

## Reporting: what helps

1. Make the report archive. AppImage: `./Pokopia-Native-Builder-1.3.0-beta.3-x86_64.AppImage report <your build folder>` in a terminal. Zip:
   `~/pokopia-native-1.3.0-beta-linux/linux/report.sh <your build folder>`. It collects the game's and the build's logs and facts about your computer
   (GPU, driver and Mesa version, distribution or SteamOS version, memory). It **never** includes your save, game files, shader files or your dump,
   replaces your user and computer names, and shows you every file before it writes the archive.
2. Open an issue with the **[Linux / Steam Deck beta report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
   form and attach the archive, or post it in **#linux-beta** on the [Discord](https://discord.gg/Aj9GqxZj8V).
3. A short screen recording of the build and the first minutes helps more than any description (OBS from Discover, or Steam's game recording).

If the game closes by itself or the picture breaks up when a new place loads, start it once with `NXRT_VK_ROBUST=1` in front of the launcher and
say whether that helped.

## Known

- **The sound fix is untested on real PipeWire** until testers confirm it: we could only reproduce PipeWire's way of asking for sound.
- **Intel Arc graphics (seen on an Arc 140V, Lunar Lake): the game froze after about 3 minutes** in 3D areas ("the GPU stopped responding" in
  the log), after heavy stutter, in #30's tests. Not fixed in this beta. Reports from other Intel and AMD GPUs help.
- **1.3.1's fixes for Windows and Mac** (another NVIDIA freeze among them) are not in this Linux beta yet; it is still 1.3.0 underneath.
- **Untested on real hardware by us**: the Steam Deck, AMD, Intel and NVIDIA graphics on Linux, Game Mode, controllers through Steam Input.
- **At 2560x1440, Mesa's software renderer still closed the game now and then** in our tests (once in 7 starts, a couple of minutes in);
  at 1920x1080 it did not. If the game closes by itself, choose 1920x1080 under Resolution (F1), and please send the report.
- **Link Play** is built in and meant to play with Windows, Mac and iPhone players on 1.3.0 or 1.3.1, but it has not been tried from Linux yet.
  Linux cannot find hosts on its own network: join by the host's address.
- **The builder window looks plain** (a single basic font); it works.
- **Upgrading later:** download the new AppImage and **Build** again with the same build folder.
