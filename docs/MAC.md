# Playing on a Mac

This guide takes you from your own dump of Pokémon Pokopia to the game running as a Mac app: building it, updating it, the graphics
refresh, the controls and the Mac's own settings.

The same download also builds the iPhone app; [Playing on your iPhone](IPHONE.md) has the phone's side. Back to the [overview](../README.md).

**On this page**

- [What you need](#what-you-need)
- [Get the download](#get-the-download)
- [Build with the step-by-step builder](#build-with-the-step-by-step-builder)
- [Advanced: every button on one screen](#advanced-every-button-on-one-screen)
- [The command line](#the-command-line)
- [Play](#play)
- [The shaders, and Refresh the graphics](#the-shaders-and-refresh-the-graphics)
- [Controls](#controls)
- [The launcher's settings](#the-launchers-settings)
- [Playing together and your save](#playing-together-and-your-save)
- [The app icon](#the-app-icon)
- [Updating](#updating)
- [Problems](#problems)

## What you need

- A **Mac with Apple silicon** (M1 or newer) and about **20 GB** of free disk. The game's code is arm64, the instruction set of Apple's own
  chips, so an Intel Mac can't run it.
- **16 GB of memory** is recommended. The game itself uses about 6 GB, so on an 8 GB Mac it can run out of graphics memory once macOS and
  your other apps have taken their share.
- **Xcode 27**, with its command line tools and the **Metal toolchain** (Xcode → Settings → Components). For the iPhone app you also need the
  **iOS platform** from that same screen, and `xcodegen` (`brew install xcodegen`).
- **Python 3.12 or newer**, with the builder's own environment (next section).
- The **.NET 10 runtime**, which the shader translator runs on: `brew install --cask dotnet-sdk`.
- **Your dump** ([below](#your-dump)).

**Check tools** in the builder (or `python3.12 nxs2.py doctor`) checks all of this and tells you how to get whatever is missing.

### The builder's Python environment

Homebrew's Python takes no pip packages of its own (pip stops with "externally-managed-environment"), so the builder's packages go into a
private environment: a folder with its own Python and its own packages. Make it once, in Terminal:

```bash
brew install python@3.12 python-tk@3.12
python3.12 -m venv ~/.pokopia-native/venv
~/.pokopia-native/venv/bin/python -m pip install "capstone>=5,<6" lz4 fonttools pillow
```

(Pillow is only for the app icon.) **Pokopia Native Builder.app** uses this environment by itself. When it's missing, the builder shows the
one Terminal command that sets it up, with a **Copy** button; run it, then reopen the builder.

For the commands in these guides, run `source ~/.pokopia-native/venv/bin/activate` first in each Terminal window: `python3.12` and `pip` are
then the environment's.

### Your dump

The game's files as your dumping tool wrote them, with the update:

```
<your folder>/updates/<version>/exefs/   main  main.npdm  rtld  sdk  subsdk0
<your folder>/updates/<version>/romfs/   LD_C0_R.BIN  LI_C0_R.IDX  motor_asset_re/  movie_asset/
```

(`exefs` holds the game's code, `romfs` its data.)

- A folder that holds `exefs/` and `romfs/` directly works too.
- So does a **`.nup` file**, a container of an already-decrypted dump: pick it with **.nup file…** in the builder, or give its path to
  `nxs2.py build`. The builder unpacks the update it needs into the build folder once (7 to 10 GB more). It needs Python's `zstandard`
  module: `python3.12 -m pip install zstandard`.
- Two updates of Pokémon Pokopia (title `0400A3A01EE4C000`) are supported, and the builder tells you which one you have: **2.0.1 (v655360)**
  and **1.0.5 (v327680)**. The base game alone isn't enough, and another version won't build until it is added, because the patch recipe and
  the camera's address are different for each version.
- **Copy the dump in binary mode.** If you move it off the console with FTP, set the transfer type to **Binary** (FileZilla: Transfer →
  Transfer type → Binary). The default "auto" mode treats files without an extension as text and quietly drops bytes from `main`, `sdk` and
  `subsdk0`. The builder checks for this and tells you.

## Get the download

Download [**Pokopia Native for Macs**](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip)
(the newest release, 1.3.5) and unzip it into a folder of its own. It builds the Mac app and, if you want it, the iPhone app. The iPhone app
has to be signed by a paid Apple Developer team: read [Playing on your iPhone](IPHONE.md) before you choose it.

**The first time you open the builder**, macOS blocks it, because it isn't signed by a registered developer. Try to open it once, then go to
System Settings → Privacy & Security and press **Open Anyway**. Or, once, in Terminal:
`xattr -dr com.apple.quarantine "Pokopia Native Builder.app"`. A copy you cloned with git opens directly. If the builder won't open at all, it tells you what to install (Python 3.12 with Tk:
`brew install python@3.12 python-tk@3.12`).

## Build with the step-by-step builder

Double-click **Pokopia Native Builder.app**. It walks you through the build one page at a time:

1. **What do you want to make?** Pick **Play on this Mac**, **Play on your iPhone**, or both. You can add the other later.
2. **Checking your Mac.** It checks your tools and runs Setup for you. When something is missing or fails, it says what to do, with the real
   error message and a button to copy it for the [Discord](https://discord.gg/Aj9GqxZj8V). Fix what is marked and press **Check again**.
3. **Find your game files.** **Choose folder…** (or **.nup file…**) picks your dump, and the builder tells you which game version it is.
   **Where your game gets built** shows the build folder and the free space there; **Change…** picks another place.
4. **Ready to build.** **Gentle** keeps your Mac cool and usable while it builds; **Full speed** is quicker, and the fans get loud. Press
   **Start building**.
5. **Building your game.** A progress bar and plain-English steps. You can keep using your Mac, and closing the window is safe: start again
   and it carries on where it stopped. **Show details** shows the raw output.

The first build takes about an hour, nearly all of it compiling the game's shaders for Metal. After that, rebuilds take a couple of minutes.

When it's done, the builder shows what to do next: **Open the Mac app** to play, and for the iPhone, **Sign the iPhone app** with a link to
the iPhone guide. It remembers where you are: open it again after your first play and **Refresh the graphics** is waiting as your next step
([below](#the-shaders-and-refresh-the-graphics)); open it from a new download and it offers **Update my game**.

## Advanced: every button on one screen

**Advanced** (top right) is the one-screen view with every button, for when you know your way around:

- **What to make:** **Mac app**; **iPhone IPA** (the iPhone app, unsigned); **Xcode project, team ID:** with your Apple developer team ID,
  for signing with Xcode ([the iPhone guide](IPHONE.md)); and **Mac only**, which skips the iPhone's shader libraries and is faster.
- **Full speed:** off, the build uses all cores at low priority; on, the same at normal priority.
- The buttons, in order of use: **Check tools**, **Setup** (once for each new download), **Build**, **Refresh shaders** and **Sign IPA**.
  **Open build folder** shows the build folder in Finder.

## The command line

The command line does the same as the window. In Terminal, with the builder's environment active:

```bash
git clone https://github.com/xFractals/pokopia-native.git
cd pokopia-native
python3.12 nxs2.py setup
python3.12 nxs2.py build "/path/to/your dump folder" --mac-app --ipa
```

`setup` fetches nxnative (the project the Mac and iPhone apps run on, see [How it works](HOW-IT-WORKS.md)) at the exact commit this release
is made for, applies the Switch 2 patch to it, and builds the shader lowering tool. It takes about a minute, once for each new download.

`build` does everything else. It makes a build folder **beside** your dump (`<dump folder>_native`; `--out` names another place) and goes
through these stages. If it stops, or you stop it, run it again and it continues where it was.

| Stage | What happens | Time on an M4 Mac |
|---|---|---|
| extract | your ExeFS copied, the RomFS files packed into one image (6 to 7 GB) | seconds |
| rewrite | the game's three code modules rewritten so Apple's kernel can run them | 20 s |
| host | the Mac host compiled with this game's profile | 15 s |
| corpus, lower, package | 4,012 shader programs found in the game files, lowered, packaged with their tables | 25 s |
| chain | the Metal compile of every shader, for the Mac and the phone | 30 to 45 min |
| outputs | the Mac app, the IPA, or the Xcode project | 1 to 3 min |

Outputs land in `<build folder>/out/`:

- `Pokemon Pokopia.app`: the Mac app ([Play](#play)).
- `PokemonPokopia-unsigned.ipa`: the iPhone app without the game files. With `--xcode --team-id <your team>` you get an Xcode project to sign
  with your team instead ([the iPhone guide](IPHONE.md)).

More:

- `--no-ios` builds for the Mac only: it halves the compile.
- `python3.12 nxs2.py run "<build folder>"` plays straight from the build folder in a window, which is handy while you try things out.
- `python3.12 nxs2.py status "<build folder>"` says what the build folder has and what it still lacks.
- `nxs2.py -v` shows each stage's full output live.

## Play

Double-click `Pokemon Pokopia.app` in `<build folder>/out/`, or press **Open the Mac app** in the builder. macOS asks once whether to open it:
right-click the app → **Open**, or System Settings → Privacy & Security → **Open Anyway**. The app can't be notarized (checked and approved by
Apple), because it maps the game's code as executable, which Apple's hardened runtime doesn't allow.

The app's launcher, a settings window, comes up first ([its settings](#the-launchers-settings)). Press **Play**.

- Your save lives in `~/Library/Application Support/nxnative/0400A3A01EE4C000/save`, outside the app, and the logs in
  `~/Library/Logs/nxnative/`.
- The game files inside the app are APFS clones of the build folder's image, so the app doesn't take another 6 GB on the same disk.
- On **the first launch** the title screen is black apart from its text. That's expected: see the next section.
- There's **no settings window inside the game** on the Mac yet (Windows has one on F1); it's coming in a later update. Change settings in the
  launcher before you press Play.

## The shaders, and Refresh the graphics

Shaders are the small programs the graphics card runs to draw each thing on screen. The build translates every shader in the game's files for
Metal. But Pokopia also **makes some shaders itself while it runs**: 140 are known on game version 1.0.5, and 2.0.1 makes 14 more (154 in
all). A fresh build can't contain them, so when the game asks for one your build doesn't have yet, that one effect or object isn't drawn.
Nothing crashes. The title screen alone uses about 20 of them, so **on the very first launch it is black apart from the version number and
"Settings"**. The music plays and A still works. It isn't a broken build, and it's gone after the first refresh.

The app records each such shader the moment the game makes it. To build them in:

1. Play a few minutes into the game: the opening, the character creator, the first area. Then quit the app.
2. Open the builder and press **Refresh the graphics** (in Advanced: **Refresh shaders**; from Terminal:
   `python3.12 nxs2.py refresh "<build folder>"`). It finds the Mac app's recordings by itself, translates the new shaders in a couple of
   minutes, not another hour, and rebuilds the app (and the iPhone app, if your build folder has one).
3. Open the app again.

Repeat when you reach new places and notice something missing; after the opening and the first area you usually have them all. The build
tells you where you stand: `shaders: 3813 from the game files; 0 of the 140 known run-time shaders` right after the first build, and
`140 of the 140` (on 2.0.1: `154 of the 154`) after a refresh with a full set of recordings. If you have never run `setup` in this folder,
run it once before you refresh from Terminal.

**After every rebuild** (an update, say), run **Refresh the graphics** again.

If the build finds run-time shaders beyond the known ones, tell us in the
[issues](https://github.com/xFractals/pokopia-native/issues/new/choose) with the count it prints, so the list can grow. Don't attach the recordings: they are game code. Why the Mac works this way:
[How it works](HOW-IT-WORKS.md#the-shaders).

## Controls

- **Mouse:** click the game picture and the mouse is captured. Move it to look around; left click is A, right click is B, and the middle
  button resets the camera. **Esc** gives the mouse back.
- **Keyboard:** WASD moves, J K U I are A B X Y, Q E are L R, Z C are ZL ZR, Return is + and Tab is −.
- **Trackpad:** works like the mouse. A MacBook's trackpad ignores your finger while keys are held down (palm rejection), so the camera
  doesn't turn while you hold a movement key. An external mouse or a controller doesn't have that problem.
- **A controller** works too.
- **Look sensitivity** and **Invert look up and down** are in the launcher, under Controls.
- **For the smoothest mouse camera,** turn the game's own **Camera Auto-Follow** off (in the game's Settings, Camera tab). With it on, the
  game turns the camera itself while you walk sideways, and the app has to steer it the way a stick would, in small steps. With it off, the
  mouse and the trackpad turn the camera directly.

The keys can't be changed on the Mac yet; the Mac's own settings window is coming in a later update.

## The launcher's settings

The launcher is the settings window the Mac app shows before the game starts. Besides the frame rate, the ones worth knowing:

- **Console mode:** handheld or docked (TV). Docked lets the game pick its higher resolution.
- **Display mode:** Window or Fullscreen.
- **Upscaling:** Standard or **MetalFX** (Apple's upscaler): how the game's picture is scaled up to fill your window or screen.
- **Controls:** Look sensitivity and Invert look up and down.
- **Link Play:** Link Play on this network, Join address, Internet play, Room code, Interact everywhere and Story co-op
  ([Playing together](PLAYING-TOGETHER.md)).

The Mac app runs on its own Metal engine, so Windows' Resolution setting, Wide view and in-game settings window aren't on the Mac.

## Playing together and your save

- **Link Play** works between the Mac, Windows PCs, iPhones and the Linux beta: [Playing together](PLAYING-TOGETHER.md). You turn it on in
  the launcher. To host over the internet from a Mac, forward a port on your router by hand, or use a VPN.
- **Your save** is in `~/Library/Application Support/nxnative/0400A3A01EE4C000/save` (Finder → Go → Go to Folder…). To bring one from
  Windows, the iPhone or Linux, quit the app and copy the save's three folders in: [Moving your save](SAVES.md).

## The app icon

Out of the box the apps get a blank default icon, because the game's own icon isn't part of a dump. `python3.12 nxs2.py icon "<build folder>"`
makes one from your game: it starts the game on a copy of your save, takes a picture of the title screen and cuts the Pokopia logo out of it.
It needs a save that is past the game's opening (play a little first, or point it at one with `--save <folder>`). Or give it a screenshot of
the title screen from your Switch 2 (Capture button): `python3.12 nxs2.py icon "<build folder>" title.jpg`. Then build again with the same
command; it only repacks the apps.

## Updating

Your build folder, the shaders you have recorded and your save all carry over.

1. Download the new version ([Get the download](#get-the-download)) and unzip it into a new folder.
2. Open **Pokopia Native Builder** from the new folder. It offers **Update my game**: it checks your Mac, runs Setup and builds again into the
   same build folder. (In Advanced: **Setup** once, then **Build**. From Terminal: `python3.12 nxs2.py setup`, then the same `build` command
   as before.) Only what changed is redone, so it's much quicker than the first build.
3. Run **Refresh the graphics** again afterwards.
4. If you keep a copy of the app somewhere else (in Applications, say), replace it with the new one. Your save is outside the app and stays.
5. **iPhone:** sign the new app and install it **over** the old one. Never delete the app first: that deletes its save
   ([the iPhone guide](IPHONE.md#updating)).

Link Play needs matching versions on every device ([which versions play together](PLAYING-TOGETHER.md#which-versions-play-together)), so
update them all.

## Problems

- Every build stage writes its full output to `<build folder>/logs/<stage>.log`, and `python3.12 nxs2.py status "<build folder>"` says what
  the build folder has and what it still lacks. The game's own logs are in `~/Library/Logs/nxnative/`.
- The common Mac problems, with what to do: an 8 GB Mac running out of graphics memory, the trackpad and mouse look, Python's
  "externally-managed-environment", a failing Metal check, macOS refusing to open the builder or the app, a different Xcode version:
  [Troubleshooting](TROUBLESHOOTING.md#mac).
- Still stuck? Ask in the [Discord](https://discord.gg/Aj9GqxZj8V), or
  [open an issue](https://github.com/xFractals/pokopia-native/issues/new/choose) with the stage, the last lines of its log and your Mac,
  macOS and Xcode versions. Never attach game files, build folders or recordings.
