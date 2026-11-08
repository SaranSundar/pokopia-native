# pokopia-native 1.3.3 Linux beta 5: no more sparkles, a steadier frame rate, a builder that looks right

---

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: Linux and Steam Deck reports go to **#linux-beta**.

**Beta 5 fixes the bright sparkles on 16:10 screens and steadies the frame rate on the Steam Deck and other integrated graphics,** both found by
**@apocal26 (Apocal)**, who tracked them down on his Intel laptop and let us use his findings. The builder window now uses your desktop's fonts and
dialogs, and the tester report finds your build folder by itself. Underneath it is still 1.3.3, as in beta 4.

**Already on beta 4 or older?** Download the new AppImage (or zip), open it, and run **Build** again with the same build folder: it puts the new
game in place, and only what changed is redone.

**This is still an open beta.** We build and test it under WSL (Linux inside Windows) with Mesa's software renderer, which runs the game far
too slowly to play; on real Linux graphics cards it has run only in testers' hands. When something goes wrong, **the logs are what we need**
(how to send them is below). Windows and Mac players: this pre-release is not for you; use
[the latest release](https://github.com/xFractals/pokopia-native/releases/latest).

## Which download do I need?

| I have… | Download |
|---|---|
| a Steam Deck, or a Linux PC, and want the easy way | [Pokopia-Native-Builder-1.3.3-beta.5-x86_64.AppImage](https://github.com/xFractals/pokopia-native/releases/download/v1.3.3-linux-beta.5/Pokopia-Native-Builder-1.3.3-beta.5-x86_64.AppImage) |
| a Linux PC and prefer a terminal, or the AppImage will not start | [Pokopia-Native-for-Linux-beta.zip](https://github.com/xFractals/pokopia-native/releases/download/v1.3.3-linux-beta.5/Pokopia-Native-for-Linux-beta.zip) |

Both are the same builder for **x86-64** (the Steam Deck, Intel and AMD PCs); ARM devices are not supported. You need a CPU with AVX2 and FMA3
(anything from about 2015 on), a Vulkan 1.3 driver (SteamOS has it; elsewhere Mesa RADV or ANV, or NVIDIA's), about 15 GB free for the build
folder, and **your own dump** with the game's update (2.0.1: `updates/655360`; 1.0.5 works too).

As always: **your own copy, your own dump.** Neither download contains anything of the game; everything is made on your computer from your dump.
Please do not share build folders, `game.so`, shader files or saves.

## What changed in beta 5

- **Fixed: bright sparkles on characters' edges** on 16:10 screens such as 1920x1200. "Default: your monitor" drew the picture at 2133x1200,
  wider than the screen, and shrank it to fit; that uneven size made thin bright edges sparkle on Intel graphics. Now a 16:10 screen gets the
  largest 16:9 picture that fits it (1920x1080 on 1920x1200, at the game's own size). A size you pick yourself in **F1 → Resolution** is still
  drawn as you chose. Found by Apocal.
- **Steadier frame rate on integrated graphics** (the Steam Deck, Intel and AMD laptops): the buffer the game's data goes through on its way to
  the graphics card is 4 times larger (256 MB) there. On Apocal's laptop the old one overflowed thousands of times a session, each overflow a
  short wait; the new one hardly ever did, and with the fix above the graphics card's time per frame went from about 17 to 13.5 ms. Also found by
  Apocal. (`NXRT_VK_STAGING_MB=64` in front of the launcher brings the old size back, for comparison.)
- **The report finds your build folder by itself** ([#34](https://github.com/xFractals/pokopia-native/issues/34)). Our guide showed
  `report.sh <your build folder>`, and the shell reads `<` `>` as something else, so the report never ran. Now it's just
  `~/pokopia-native-*/linux/report.sh` (or the AppImage's `report`): it takes the builder's last build folder, or finds one in your home folder or
  on an SD card, says which one it took, and at the end where the report file is.
- **The builder window looks like your desktop**: real fonts instead of a tiny bitmap one (it now brings a Tk built with Xft; a system older than
  2022, glibc 2.35, keeps the old look, and `POKOPIA_TK=x11` forces it), your desktop's own message boxes and file pickers (KDE's on the Steam
  Deck, GNOME's where zenity is installed) that open in your home folder instead of `/`, sizes that follow your desktop's scaling, and a layout that stays a
  readable column when maximised.
- **From beta 4:** 1.3.3's upload rework, its NVIDIA freeze fixes, co-op on game version 1.0.5, the 32-lane test mode. **From beta 3:** the
  PipeWire crackle fix (thanks again, @Quantumenis). **From beta 2:** the game starts outside Ubuntu
  ([#30](https://github.com/xFractals/pokopia-native/issues/30)).
- **Not in this beta:** 1.3.4's and 1.3.5's Windows features (the Wide view, controller rebinding). They come to Linux with a later beta.

## Still open

- **A trembling, vibrating sound on PipeWire** for some players. The logs show the sound output never runs dry, so it isn't the old crackle;
  we're looking into it together with Apocal. If you hear it, `SDL_AUDIO_DRIVER=pulseaudio` in front of the launcher is worth one try, and please
  say in **#linux-beta** whether it changed anything.
- **The Steam Deck's frame pacing**: 20 to 40 fps in busy places, and movement that feels laggy even at a steady 30 (#34). The report after a
  few minutes in your town shows us where each frame's time goes; please send one.
- **A crash on alt-tab at the language screen** (#34, once). If it happens to you, the report has the crash file.

## What we checked for beta 5, and what we could not

In WSL (Ubuntu 22.04) with Mesa's software renderer: Linux's own tests pass, among them new checks that 16:10, 16:9, 21:9 and 4K screens get
the right default picture. The AppImage opens its builder window with the new fonts, and its report finds the build folder by itself. **Not
checked by us:** the sparkles and the frame rate on real hardware (Apocal's measurements are the ones above), a real Steam Deck, and the
builder's dialogs on a real KDE or GNOME desktop.

## Getting started

1. Download the AppImage and make it executable: in Dolphin (the Steam Deck's file manager, in Desktop Mode) right-click it → **Properties** →
   **Permissions** → tick **Is executable**; or in a terminal `chmod +x Pokopia-Native-Builder-1.3.3-beta.5-x86_64.AppImage`.
2. Double-click it. The first start unpacks the builder to `~/.local/share/pokopia-native/builder/1.3.3-beta.5` (about 1 GB, once; an earlier
   beta's copy in `builder/` can be deleted once your game is built again).
3. The window walks you through it: your game files, the build (the first one takes a while: the game's code is recompiled for your CPU), then
   **Play**. **Bring my save** on the last page brings your save from Eden, the phone or a Mac.

**Steam Deck Game Mode:** add the game's launcher (`out/play-pokopia-2.0.1.sh` in your build folder) to Steam as a non-Steam game (not the
AppImage). A new game asks for your character's name: if no box shows in Game Mode, press **A** and the name is "Player" (to choose one, start the
new game in Desktop Mode). The full guide, including the zip's terminal steps and Arch / CachyOS packages, is `docs/LINUX.md` in the zip.

## Reporting: what helps

1. Make the report file in a terminal (Konsole on the Steam Deck): `~/Downloads/Pokopia-Native-Builder-1.3.3-beta.5-x86_64.AppImage report`
   (the AppImage) or `~/pokopia-native-1.3.3-beta-linux/linux/report.sh` (the zip). It finds your build folder by itself, collects the game's and
   the build's logs and facts about your computer (GPU, driver and Mesa version, distribution or SteamOS version, memory), and **never** includes
   your save, game files, shader files or your dump. It replaces your user and computer names, shows you every file, and asks before it writes.
2. Open an issue with the **[Linux / Steam Deck beta report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
   form and attach the file, or post it in **#linux-beta** on the [Discord](https://discord.gg/Aj9GqxZj8V).
3. A short screen recording of the build and the first minutes helps more than any description (OBS from Discover, or Steam's game recording).
   For the frame rate, play 5 minutes in your town before making the report.

## Known

- **Untested on real hardware by us**: the Steam Deck, AMD, Intel and NVIDIA graphics on Linux, Game Mode, controllers through Steam Input.
- **At 2560x1440, Mesa's software renderer closed the game now and then** in our tests (once in 7 starts, a couple of minutes in); at 1920x1080
  it did not. If the game closes by itself, choose 1920x1080 under Resolution (F1), and please send the report.
- **Link Play** is built in and meant to play with Windows, Mac and iPhone players on 1.3.x, but it has not been tried from Linux yet. Linux
  cannot find hosts on its own network: join by the host's address.
- **Upgrading later:** download the new AppImage and **Build** again with the same build folder.
