# pokopia-native 1.3.3 Linux beta 4: 1.3.3 for Linux and the Steam Deck

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: Linux and Steam Deck reports go to **#linux-beta**.

**Beta 4 brings 1.3.3 to Linux.** Every Linux beta so far was 1.3.0 underneath; beta 4 has 1.3.3's fixes: a lot less data sent to the
graphics card each frame (the lag near water, [#27](https://github.com/xFractals/pokopia-native/issues/27)), the mouse camera while walking
sideways ([#31](https://github.com/xFractals/pokopia-native/issues/31)), two freezes on NVIDIA cards, co-op on game version 1.0.5, and the
32-lane test mode for AMD and Intel graphics. It keeps beta 3's crackle fix for PipeWire and beta 2's fix for the start-up crash outside Ubuntu.

**Already on beta 3, 2 or 1?** Download the new AppImage (or zip), open it, and run **Build** again with the same build folder: it puts the new
game in place, and only what changed is redone.

**This is still an open beta.** We build and test it under WSL (Linux inside Windows) with Mesa's software renderer, which runs the game far
too slowly to play; on real Linux graphics cards it has run only in testers' hands, and **never on a real Steam Deck** yet. When something goes
wrong, **the logs are what we need** (how to send them is below). Windows and Mac players: this pre-release is not for you; use
[1.3.3](https://github.com/xFractals/pokopia-native/releases/latest).

## Which download do I need?

| I have… | Download |
|---|---|
| a Steam Deck, or a Linux PC, and want the easy way | [Pokopia-Native-Builder-1.3.3-beta.4-x86_64.AppImage](https://github.com/xFractals/pokopia-native/releases/download/v1.3.3-linux-beta.4/Pokopia-Native-Builder-1.3.3-beta.4-x86_64.AppImage) |
| a Linux PC and prefer a terminal, or the AppImage will not start | [Pokopia-Native-for-Linux-beta.zip](https://github.com/xFractals/pokopia-native/releases/download/v1.3.3-linux-beta.4/Pokopia-Native-for-Linux-beta.zip) |

Both are the same builder for **x86-64** (the Steam Deck, Intel and AMD PCs); ARM devices are not supported. You need a CPU with AVX2 and FMA3
(anything from about 2015 on), a Vulkan 1.3 driver (SteamOS has it; elsewhere Mesa RADV or ANV, or NVIDIA's), about 15 GB free for the build
folder, and **your own dump** with the game's update (2.0.1: `updates/655360`; 1.0.5 works too).

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump.
Please do not share build folders, `game.so`, shader files or saves.

## What changed in beta 4

- **Less data sent to the graphics card each frame** (1.3.3's fix for the lag near water, #27). The game used to send the graphics card data
  the card had just made itself, again and again: on 1.3.3's Windows test route it now sends about a fifth as much (6.3 MB a frame down to 1.4),
  and what doesn't fit the upload buffer goes in larger blocks. On Linux this matters more than on Windows: noticing which game memory changed
  costs Linux more per page. **@apocal26**'s beta 3 logs (Fedora, Intel Arc 140V) showed exactly this: about 5 ms of every frame went to
  these copies and the upload buffer overflowed a few times a frame, which made the frame rate swing between 52 and 59. We expect beta 4 to
  help there and on the Steam Deck, but haven't measured it on real hardware yet: your reports will tell.
- **The game's on-demand shader preparation now runs at a much lower priority** than the game, so on a 4-core Steam Deck it doesn't take time
  from the thread that draws the frames. A draw that needs a shader still being prepared is skipped until it's ready, as before.
- **Fixed: the mouse camera wouldn't turn left or right while walking sideways or diagonally** (#31).
- **Fixed: two freezes on NVIDIA cards** (1.3.1's and 1.3.3's: two graphics programs that walk a list now stop at its real length). These apply on
  NVIDIA only, as on Windows since 1.3.2.
- **Co-op: Interact everywhere and Story co-op now work on game version 1.0.5 too**, as in 1.3.3: every 1.0.5 player needs 1.3.3 for them.
  On 2.0.1 nothing changed. The launcher turns them off, with a message, on a game version that has neither (as Windows does).
- **New, for AMD and Intel graphics: the 32-lane test mode** (1.3.1's, off by default). The game's graphics programs were written for groups of
  32 (NVIDIA's); AMD runs 64 and Intel 8 to 32. If you see **bright sparkles on edges** or **freezes, often in conversations**, start the game
  once with `NXRT_VK_SUBGROUP32=1` in front of the launcher (`docs/LINUX.md`, "Common problems") and tell us whether it changed anything; the log
  then says "made with 32 lanes". A screenshot of the same place with and without helps most.
- **From beta 3: crackle-free sound on PipeWire.** Thanks again to **@Quantumenis** for diagnosing the crackle; @apocal26's logs confirmed it
  on real PipeWire: the sound never ran dry once in 15 minutes.
- **From beta 2: the game starts outside Ubuntu** ([#30](https://github.com/xFractals/pokopia-native/issues/30)).
- Everything else is as in beta 3: the settings inside the game (**F1**), the Resolution setting, Story co-op, the step-by-step builder. See
  [1.3.3](https://github.com/xFractals/pokopia-native/releases/tag/v1.3.3) (`docs/RELEASE-NOTES-1.3.3.md` in the zip) for the rest of 1.3.1
  to 1.3.3; their Windows-only fixes (accented folder names, the .NET check) don't concern Linux.

## What we checked for beta 4, and what we could not

In WSL (Ubuntu 22.04) with Mesa's software renderer: Linux's own tests pass, among them the one that caught a problem in bringing 1.3.3's
upload rework to Linux (fixed before this beta). The game starts and draws with Ubuntu's Vulkan library and with one built the way Fedora builds
it. The AppImage opens its builder window and builds the game from a dump. The sound, with SDL's test driver asking for it the way PipeWire does, is as good as beta 3's: beta 3 and beta 4 back to back found at most
a few of 6,000 sound requests with nothing ready, on a test system working flat out. **Not checked:** the frame rate gains (Mesa's software
renderer runs the game at about 2 frames a second, too slowly to reach the field), the 32-lane test mode (that renderer has only 8 lanes and
can't be asked for 32; the log says so), real PipeWire, a real Steam Deck, any real graphics card, and Link Play from Linux. If beta 4 runs
better or worse for you than beta 3, please say so in **#linux-beta**, with your system.

## Getting started

1. Download the AppImage and make it executable: in Dolphin (the Steam Deck's file manager, in Desktop Mode) right-click it → **Properties** →
   **Permissions** → tick **Is executable**; or in a terminal `chmod +x Pokopia-Native-Builder-1.3.3-beta.4-x86_64.AppImage`.
2. Double-click it. The first start unpacks the builder to `~/.local/share/pokopia-native/builder/1.3.3-beta.4` (about 1 GB, once; an earlier
   beta's copy in `builder/` can be deleted once your game is built again).
3. The window walks you through it: your game files, the build (the first one takes a while: the game's code is recompiled for your CPU), then
   **Play**. **Bring my save** on the last page brings your save from Windows, the phone or a Mac.

**Steam Deck Game Mode:** add `<your build folder>/out/play-pokopia-2.0.1.sh` to Steam as a non-Steam game (not the AppImage). A new game asks
for your character's name: if no box shows in Game Mode, press **A** and the name is "Player" (to choose one, start the new game in Desktop Mode).
The full guide, including the zip's terminal steps and Arch / CachyOS packages, is `docs/LINUX.md` in the zip.

## Reporting: what helps

1. Make the report archive. AppImage: `./Pokopia-Native-Builder-1.3.3-beta.4-x86_64.AppImage report <your build folder>` in a terminal. Zip:
   `~/pokopia-native-1.3.3-beta-linux/linux/report.sh <your build folder>`. It collects the game's and the build's logs and facts about your computer
   (GPU, driver and Mesa version, distribution or SteamOS version, memory). It **never** includes your save, game files, shader files or your dump,
   replaces your user and computer names, and shows you every file before it writes the archive.
2. Open an issue with the **[Linux / Steam Deck beta report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
   form and attach the archive, or post it in **#linux-beta** on the [Discord](https://discord.gg/Aj9GqxZj8V).
3. A short screen recording of the build and the first minutes helps more than any description (OBS from Discover, or Steam's game recording).
   For the frame rate, play 5 minutes in your town before making the report: the log then shows where each frame's time goes.

## Known

- **Intel Arc (seen on an Arc 140V): bright sparkles on the right-hand edges of characters outdoors.** Not understood yet: it may be how the
  driver runs a few of the game's graphics programs (try the 32-lane test mode above), or the game's own highlights, which can sparkle without
  anti-aliasing on any card. Screenshots of the same place, with and without the test mode, decide it.
- **Short sound stutters that come with the frame rate's dips**: the sound output keeps up (beta 3's fix), but when the game itself hitches the
  sound hitches with it. If the frame rate swings, **F1 → Frame rate → 30 fps** gives a steady 30.
- **The Intel Arc freeze after about 3 minutes** seen in #30's tests on beta 1 did not come back in a 15-minute beta 3 session on the same
  laptop. If the game freezes for you ("the GPU stopped responding" in the log), please send the report.
- **Untested on real hardware by us**: the Steam Deck, AMD, Intel and NVIDIA graphics on Linux, Game Mode, controllers through Steam Input.
- **At 2560x1440, Mesa's software renderer closed the game now and then** in our tests (once in 7 starts, a couple of minutes in); at 1920x1080
  it did not. If the game closes by itself, choose 1920x1080 under Resolution (F1), and please send the report.
- **Link Play** is built in and meant to play with Windows, Mac and iPhone players on 1.3.x, but it has not been tried from Linux yet. Linux
  cannot find hosts on its own network: join by the host's address.
- **The builder window looks plain** (a single basic font); it works.
- **Upgrading later:** download the new AppImage and **Build** again with the same build folder.
