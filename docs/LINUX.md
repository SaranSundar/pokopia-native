# Pokopia Native on Linux and the Steam Deck

The **"for Linux" download** builds the Linux game from your own dump: Pokopia recompiled for your computer's x86-64 CPU, played with a Vulkan
renderer. It is the Windows build's recompiler and runtime ported to Linux, so it plays the same way (keyboard and mouse, controllers, Link Play).

Linux and the Steam Deck are a full release since 1.4.0, after five public betas on Steam Decks and Linux PCs. On a Steam Deck the game starts
in its own handheld mode, with its lighter settings. If something goes wrong, the report (the last section) tells us what happened. Nothing of
the game is in the download: everything made from your dump stays in your build folder.

## What you need

- **A Steam Deck** (LCD or OLED, SteamOS 3.6 or newer), or a 64-bit Linux PC with a CPU that has **AVX2 and FMA3** (Intel Haswell from 2013, AMD
  Excavator from 2015, and anything newer) and a **Vulkan 1.3** GPU driver (Mesa RADV or ANV, or NVIDIA's).
- **16 GB of memory** (the Steam Deck has it) and about **15 GB free** where the build folder goes.
- Your **dump** of the game, as for the other builds (the README's "What you need"): the folder with `exefs` and `romfs` (or `updates/<version>/`).
- **Python 3.10 or newer** (SteamOS has it), **git** (SteamOS has it), and the **.NET 10 runtime** (step 3 installs it into your home folder).

No root, no `sudo`, no compiler: the download carries its own tools in `linux/bin` (see `linux/bin/MANIFEST.txt` and `THIRD-PARTY-NOTICES.txt`).

## The easy way: the AppImage

**`Pokopia-Native-Builder-<version>-x86_64.AppImage`** is the builder as one file, with everything it needs inside (its own Python, the .NET
runtime, the Linux tools and nxnative): no terminal steps, no Python packages, no .NET install.

1. **Make it executable**: in Dolphin (the Steam Deck's file manager), right-click it → **Properties** → **Permissions** → tick **Is executable**.
   Or in a terminal: `chmod +x ~/Downloads/Pokopia-Native-Builder-*.AppImage`
2. **Double-click it.** The first start unpacks the builder to `~/.local/share/pokopia-native/builder/<version>` (about 1 GB, once per version;
   the games you build use its Python and .NET from there), then the window opens.
3. The window walks you through it, one page at a time: what to make (the game for this computer), a check of your computer, your game files,
   the build, then **Play**. **Bring my save** on the last page brings your save from Eden, the phone or a Mac. **Advanced** (top right) has
   the one-screen view with every button.

The game itself is still built on your computer from your own dump, into the build folder; the AppImage carries nothing of the game. To play from
Steam (Game Mode), add the game's launcher (`out/play-pokopia-2.0.1.sh` in your build folder) as a non-Steam game, as below, not the AppImage.
The report (the last section) is `~/Downloads/Pokopia-Native-Builder-*.AppImage report` in a terminal: it finds your build folder by itself. If the AppImage does not
start at all, run it in a terminal with `--appimage-extract-and-run` after its name (a system without FUSE), and say so in the report.
The window draws with your desktop's fonts and dialogs (on a system older than 2022, glibc 2.35, it falls back to one plain bitmap font; to force
that, start it with `POKOPIA_TK=x11`). To remove the builder, delete the AppImage and
`~/.local/share/pokopia-native/builder`; your builds and saves are elsewhere and stay.

Everything below is the same build without the AppImage, from the "for Linux" zip and a terminal.

## Arch Linux and CachyOS (a PC, or a Steam Deck running them)

The steps below are the same. First install what the build and the game use from pacman (Python with Tk, git, unzip, zenity for the game's
name box and the Link Play choice, the Vulkan loader), then the Vulkan driver for your GPU:

```bash
sudo pacman -S --needed python tk git unzip zenity vulkan-icd-loader
```

```bash
sudo pacman -S --needed vulkan-radeon
```

(AMD, and the Steam Deck.) For an NVIDIA card (GTX 16xx and newer) instead:

```bash
sudo pacman -S --needed nvidia-utils
```

On KDE, kdialog works in place of zenity. Python's modules still go into the venv of step 2.

## Steam Deck: build in Desktop Mode

Press the **Steam** button → **Power** → **Switch to Desktop**, then open **Konsole** (the terminal, in the application menu under System). Each
step is one command; copy them as they are.

1. **Unzip** the download into your home folder (Dolphin: right-click → Extract → Extract archive here), or:

   ```bash
   cd ~ && unzip ~/Downloads/Pokopia-Native-for-Linux.zip
   ```

2. **Python's modules**, in a folder of their own (SteamOS keeps its system folders read-only, so they go into your home):

   ```bash
   python3 -m venv ~/pokopia-venv && ~/pokopia-venv/bin/pip install lz4 fonttools pillow
   ```

   Add `zstandard` to that list if your dump is a `.nup` file. **Python 3.15** (`python3 --version`) has no ready-made lz4 package yet, so pip
   builds lz4 itself and needs a C compiler first: `sudo pacman -S --needed base-devel` on Arch and CachyOS (`build-essential python3-dev` on
   Debian and Ubuntu), then the same command again.

3. **The .NET 10 runtime** (the shader translator), with Microsoft's own installer script, into `~/.dotnet`:

   ```bash
   curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 10.0 --runtime dotnet
   ```

4. **Check and set up** (setup fetches nxnative, the project's MIT base, with git):

   ```bash
   cd ~/pokopia-native-*/ && ~/pokopia-venv/bin/python nxs2.py doctor && ~/pokopia-venv/bin/python nxs2.py setup --no-ios
   ```

   Every line of `doctor` should say `ok`. If one says `MISSING`, it says what to do; send me the output if it does not help.

5. **Build** (replace the path with your dump's folder; the build folder goes beside it unless you add `--out <folder>`):

   ```bash
   ~/pokopia-venv/bin/python nxs2.py build ~/Games/Pokopia --linux
   ```

   The long part is the recompile: about 30 to 45 minutes on a Steam Deck. The Deck stays usable; leave it plugged in. If the build stops, run the
   same command again: it continues where it was.

## Play

```bash
~/Games/Pokopia_native/out/play-pokopia-2.0.1.sh
```

(the file is named after your game version: `ls ~/Games/Pokopia_native/out/`). A small window asks how to play first: alone, or Link Play. The first
start translates the shaders the game makes while it runs: for about a minute the ground and some menus may be white. That is expected. After
"don't ask again", `pokopia-settings-<version>.sh` beside it asks again, then starts the game.

- **Controls**: the Steam Deck's own controls work as a Switch controller (A/B/X/Y by position, the sticks, the triggers). Keyboard and mouse:
  WASD to move, the mouse to look (click the picture; Esc releases the mouse), J/K/U/I for A/B/X/Y, Return for +, Tab for −, F11 for full screen.
- **Settings in the game**: **F1**, or **−** and **+** together on a controller (the Steam Deck's View and Menu buttons, either side of the screen),
  opens the settings over the game: the picture (Resolution, window or full screen, Upscaling, 30 fps, Vsync, an FPS counter) and the keyboard's
  keys. Changes apply at once (Console mode from the next start) and are kept in `~/.config/pokopia-native/` for the next start.
- **On a Steam Deck the game starts in its handheld mode**: its own lighter settings (about a quarter less graphics work). **F1 → Console mode →
  Docked (TV)** switches back from the next start, and that choice is kept.
- **Your save**: `~/.local/share/pokopia-native/<version>/save`. To bring your save from Eden, a Mac or the phone:
  `~/pokopia-venv/bin/python nxs2.py import-save ~/Games/Pokopia_native <the folder with B00, G00 and S00>` (`--eden` finds Eden's own).
  The save that was there is kept beside it.

### Game Mode (play from the Steam Deck's own interface)

In Desktop Mode, open **Steam** → **Games** → **Add a Non-Steam Game to My Library** → **Browse**, choose
`~/Games/Pokopia_native/out/play-pokopia-<version>.sh`, and add it. Back in Game Mode it is in your library under "Non-Steam".

Game Mode shows no Link Play window: it plays the way you chose last time in Desktop Mode (alone the first time). To choose there instead, open the
game's **Properties** → **Launch Options** and put `%command% --link-play` (host) or `%command% --join 192.168.1.20` (visit that address);
add ` --story` for Story co-op (below).

**Your character's name** (a new game asks for it once, and it cannot be changed later): a box comes up for it. If the box shows in Game Mode,
**Steam + X** opens Steam's keyboard to type into it; tap OK on the screen when done. If no box shows, or you have only a controller, press **A**
(or +) and the name is "Player". In Desktop Mode the same works, and a keyboard types into the box as usual. To choose the name yourself, start a new
game in Desktop Mode.

## Link Play

Linux plays Link Play with Windows PCs, Macs and iPhones that run the same pokopia-native version. Linux does not find hosts on its own network yet:

- **you host**: choose "host here"; the others pick this computer's address (`ip -4 addr` shows it, for example 192.168.1.20). A Linux PC with a
  firewall (`ufw`, `firewalld`) must allow port 47600, TCP and UDP. SteamOS has no firewall.
- **you join**: choose "join a host on this network by its address" and type the host's address.
- **over the internet**: as on Windows (docs/WINDOWS.md, "Playing together"): a room code, and the host's router forwards port 47600.

In the game: Link Play, then **Local connection** (not Online connection). Every player needs a save of their own.

**Interact everywhere** (visitors use moves and items and build in every town) and **Story co-op** (on top of it: visitors talk to the story
characters too) are experimental, and every player needs the same choice: the Link Play choice asks for both, or give `--interact` or `--story`
after the launcher's name. With both off, Linux plays with 1.2.x as well. They work on game versions 2.0.1 and 1.0.5; on 1.0.5 every
player needs pokopia-native 1.3.3 or newer.

## Recording a problem

When something goes wrong, a recording of it helps more than any description.

- **OBS Studio** from **Discover** (the software centre) records the whole screen: start a recording, show what goes wrong, stop it.
- Or **Spectacle** (the screenshot tool), if your desktop's version has screen recording (Plasma 6).
- In Game Mode, Steam's own game recording (Steam → Settings → Game Recording) works too.

Please keep recordings short (a few minutes each) and do not record anything personal on the screen.

## The report

When something goes wrong, open a terminal (Konsole on the Steam Deck) and run the report. With the AppImage:

```bash
~/Downloads/Pokopia-Native-Builder-*.AppImage report
```

With the zip:

```bash
~/pokopia-native-*/linux/report.sh
```

It finds your build folder by itself (the builder's last one, or one in your home folder or on an SD card) and says which one it took. To name
another, put its path after the command as it is, without `<` `>` around it: `~/pokopia-native-*/linux/report.sh /run/media/deck/MyCard/Pokopia`.

It collects the game's logs, the build's logs and facts about your computer (CPU, GPU, driver and Mesa version, SteamOS or distribution version,
memory, display) into one `pokopia-report-<date>.tar.gz` in your home folder, and its last line says where it is. **It never includes your save, the game's files, shader files or your
dump**, and it replaces your home folder, user name and computer name with placeholders. It lists every file and asks before it writes the archive,
so you can read them first.

Then fill in the **[Linux / Steam Deck report](https://github.com/xFractals/pokopia-native/issues/new?template=linux-beta-report.yml)**
form on GitHub and attach that archive: the form asks the rest (your device, the build, the title screen and the game, controls, sound,
saves). Screenshots and short recordings help too; a recording over 10 MB goes in as a link. No GitHub account? Send me the same answers and
the archive directly.

## Common problems

- **"out of memory mappings" or a crash after a while**: some distributions allow few memory mappings per process. SteamOS allows many. Elsewhere:
  `sudo sysctl vm.max_map_count=1048576` (and in `/etc/sysctl.d/` to keep it).
- **No window, or "no Vulkan loader"**: install your distribution's Vulkan loader and Mesa Vulkan drivers (`vulkan-icd-loader` and `vulkan-radeon` on
  Arch, `libvulkan1` and `mesa-vulkan-drivers` on Debian and Ubuntu).
- **`doctor` says the CPU lacks AVX2**: the recompiled game needs it; that CPU cannot run it.
- **The game closes by itself, or the picture breaks up, when a new place loads**: start it once with the driver's bounds checks on and say in the
  report whether that helped: `NXRT_VK_ROBUST=1 ~/Games/Pokopia_native/out/play-pokopia-2.0.1.sh`
- **Bright sparkles on characters' edges (seen on Intel graphics)**: the picture is drawn at an uneven size for your screen (the log says
  "resolution x1.11111", for example). A 16:10 screen such as 1920x1200 gets the game's own 1920x1080 by default, which fits it
  exactly. If you chose a size yourself, **F1 → Resolution → 1920x1080 (the game's own)** or 3840x2160 (exactly twice) avoids it. Thanks to
  Apocal for finding it.
- **AMD or Intel graphics: the game freezes (often in conversations)**: the game's graphics programs were written for groups of 32 (NVIDIA's);
  AMD runs 64 and Intel 8 to 32. Start the game once in the 32-lane test mode and say in the report whether it helped:
  `NXRT_VK_SUBGROUP32=1 ~/Games/Pokopia_native/out/play-pokopia-2.0.1.sh`. The log then says "made with 32 lanes" and for which stages.
- **The build stops with "shorter than its own header says"**: the dump's `exefs` was copied in text mode; copy it again in binary mode.
- **The frame rate swings on a Steam Deck and it feels laggy**: in the game, **F1 → Frame rate → 30 fps** gives a steady 30 instead. Since
  1.4.0 the Deck starts in the game's handheld mode (F1 → Console mode → Docked (TV) switches back), and our graphics work runs on a thread of
  its own beside the game's (`NXRT_REPLAY_THREAD=0` in front of the launcher turns that off, to compare). On an OLED Deck, set the screen to
  60 Hz in the Quick Access menu's Performance page: the game's 60 fps does not divide evenly into 90 Hz. A report after 5 minutes in your
  town (below) shows us where the time goes.
- **The sound crackles**: start the game once with SDL's PulseAudio driver (PipeWire answers it too, through pipewire-pulse) and say in the
  report whether that helped: `SDL_AUDIO_DRIVER=pulseaudio ~/Games/Pokopia_native/out/play-pokopia-2.0.1.sh`. The log's `audio #0 device
  callbacks` lines count the underruns.
