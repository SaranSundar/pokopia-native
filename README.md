# Pokémon Pokopia, running natively on your Mac, iPhone and Windows PC with Cross-Device Saves and Multiplayer

**Join the community on [Discord](https://discord.gg/Aj9GqxZj8V)**: help with installing, bug reports, finding co-op partners and news of every release.

I got Pokémon Pokopia (the Switch 2 game) running natively on my Mac and on my iPhone. Not through an emulator: the
game's own code runs on the Apple chip, and the graphics go through Metal. After a lot of work on the shaders it looks
exactly like the Switch 2. I lined up screenshots from the Switch 2 against the phone, scene for scene, and could not tell
them apart. Shadows, clouds, time of day, the lot.

This repository is the tool that lets you do the same with **your own copy of the game**. It takes your dump, and on
your Mac it builds a Mac app and an iPhone app. On a Windows PC it builds the game for Windows itself, recompiled for your
PC, and the iPhone app too. Everything that comes from the game is created on your machine and stays in your build
folder. Nothing from the game is in here.

This works for both versions 1.0.5 (v327680) and 2.0.1 (v655360) as of 11/3/2026. The tool is version **1.3.3**.

## Which download do I need?

Every release has one download per computer. Take the one for the computer you build on:

- **I have a Windows PC and want to play on Windows** →
  [**Pokopia Native for Windows PCs**](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip).
  Unzip it, double-click `Pokopia Native Builder.cmd` and tick "Windows: play on this PC". The steps:
  [Windows: play on your PC](#windows-play-on-your-pc).
- **I have a Windows PC and want to build for iPhone** →
  [**Pokopia Native for Windows PCs**](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip) (the same download).
  Tick "iPhone IPA"; it needs Apple's tools and a paid developer team: [docs/WINDOWS.md](docs/WINDOWS.md#the-iphone-app).
- **I have a Mac and want to play on Mac** →
  [**Pokopia Native for Macs**](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip).
  A Mac with Apple silicon (M1 or newer); tick "Mac app". The steps: [How to build](#how-to-build).
- **I have a Mac and want to build for iPhone** →
  [**Pokopia Native for Macs**](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip) (the same download).
  Tick "iPhone IPA", and read [the Apple developer thing](#the-apple-developer-thing-iphone-only-read-this) first.

Playing together (Link Play) works between Windows, Mac and iPhone, on the same Wi-Fi or over the internet:
[Playing together](#playing-together-link-play). Two rules:

- **Co-op needs the same pokopia-native version on every device** (the builder window's title shows it).
- **Each player needs their own save**: the game refuses two copies of the same save, so a save copied to a second device cannot play with itself.

Older versions and the release notes: [all releases](https://github.com/xFractals/pokopia-native/releases).
Already have an older version set up? [Updating from an older version](#updating-from-an-older-version) keeps your save.
Bringing your save from an emulator or another device? [Moving your save](#moving-your-save).

Before anything else:

> **Please Please Please Please do not share game files, this is for your own games files only and that is the intention it was built with.**

That means: do not upload your dump, do not upload the build folder, do not upload the app or the IPA this makes (they
contain the game), do not upload shader caches or recorded shaders (they are pieces of the game too). If you want to
help someone, point them here. They need their own Switch 2, their own copy, their own dump. The tool checks the dump
is the right version and refuses anything else, and it needs no keys of any kind. Keep it that way, and this stays up.

---

## Message From the Creator

Hey guys, I really hope you enjoy this. I just have to give a massive shout-out to the people at NxNative, this wouldn't be possible without them. I'd never tried to do any of this until last week, when they went viral, and once I found out, I kept going. This all started after 0x2593 posted their fork of Eden for NS2, shout out to them as well, that was a great help for getting this going. After running the first emulation of Pokopia, I just knew I needed to fix it and put it on my phone and Mac. Right now all of the PC emulators with Pokopia have issues with the shaders. My work on this native build allowed me to dig deeper and fix all of those issues. If you want to play emulated Pokopia with Eden, that's my other repository "pokopia-fixes" (https://github.com/xFractals/Eden-NS2), which is a fork of https://github.com/0x2593/Eden-NS2. Go check it out if you want, hopefully it gets merged into main soon. Anyways Enjoy!

Also, if you find any bugs, please feel free to submit an issue request or a PR; it's much appreciated!

## Gameplay on the iPhone

Captures from my phone, straight from the build this tool makes. I checked every one of these scene for scene against
Switch 2 footage of the game. The Switch side-by-sides are not posted here because that footage is not mine to repost;
when I have captures from my own console they will go up next to these.

![Night, campfire with Professor Tangrowth](docs/images/iphone-night.png)

![Day, the first area](docs/images/iphone-day.png)

<img width="1007" height="485" alt="Screenshot 2026-11-04 205343" src="https://github.com/user-attachments/assets/238eb6b2-a815-4ac7-aeea-7c4d99479f19" />

![Opening, the workbench at night](docs/images/iphone-intro-18.png)

## What you get

- **A Mac app.** Double-click, press Play, it runs. Saves live in your Library folder like any Mac game.
- **An iPhone app.** The game runs natively on the phone, full quality, 30 fps (the Switch 2 runs this game at 30 in
  handheld too). There is a catch about Apple developer accounts, explained below. Read it before you get excited.
- **A Windows game.** On a Windows PC the game's code is recompiled for your PC and drawn with Vulkan: 60 fps, up to 4K
  (it draws itself at your monitor's size), a settings menu in the game (F1), mouse look, controllers with rumble, your
  save from Eden, the phone or the Mac. [Windows: play on your PC](#windows-play-on-your-pc).

Both are built from your dump by one script, `nxs2.py`. The first build takes about an hour, nearly all of it the
Mac compiling 3,800 shaders. After that, rebuilds take a couple of minutes.

## What you need

- A **Mac with Apple silicon** (M1 or newer) and roughly 20 GB of free disk. The game's code is arm64, so an Intel Mac
  cannot run it. The Mac does the building for the iPhone too, or a Windows PC can (the last item here).
- **16 GB of memory is recommended on the Mac.** The game itself uses about 6 GB there, so on an 8 GB Mac it can run out
  of graphics memory once macOS and your other apps have taken their share (see [Common problems](#common-problems)).
- **Xcode 27** installed, with its command line tools and the Metal toolchain (Xcode → Settings → Components). For the
  iPhone build you also need the iOS platform from that same screen, and `xcodegen` (`brew install xcodegen`).
- **Python 3.12 or newer, with the builder's own environment.** Homebrew's Python takes no pip packages of its own
  ("externally-managed-environment"), so the builder's go into a private environment. Once, in Terminal:
  ```bash
  brew install python@3.12 python-tk@3.12
  python3.12 -m venv ~/.pokopia-native/venv
  ~/.pokopia-native/venv/bin/python -m pip install "capstone>=5,<6" lz4 fonttools pillow
  ```
  (Pillow only for the app icon.) **Pokopia Native Builder.app** uses that environment by itself. For the commands in
  this README, run `source ~/.pokopia-native/venv/bin/activate` first in each Terminal window: `python3.12` and `pip`
  are then the environment's.
- The **.NET 10 runtime** (the shader translator is .NET): `brew install --cask dotnet-sdk`.
- **Your dump.** The game's files as your dumping tool wrote them, with the update:
  ```
  <your folder>/updates/<version>/exefs/   main  main.npdm  rtld  sdk  subsdk0
  <your folder>/updates/<version>/romfs/   LD_C0_R.BIN  LI_C0_R.IDX  motor_asset_re/  movie_asset/
  ```
  A folder that holds `exefs/` and `romfs/` directly works too, and so does a **`.nup` file** (a NUP container of
  an already-decrypted dump: pick it with **.nup file…** in the window, or give its path to `nxs2.py build`). The
  builder unpacks the update it needs into the build folder (about 7–10 GB more), once; it needs Python's `zstandard`
  module: `python3.12 -m pip install zstandard`. Two updates of Pokémon Pokopia (title
  `0400A3A01EE4C000`) are supported, and the tool tells which one you have from the build IDs: **v327680** and
  **2.0.1 (v655360)**. Both get everything, including the mouse and drag look. The base game alone is not enough, and
  another version will not build until it is added, because the patch recipe and the camera address are per version.
- **Copy the dump in binary mode.** If you move it off the console with FTP, set the transfer type to **Binary**
  (FileZilla: Transfer → Transfer type → Binary). The default "auto" mode treats files without an extension as text and
  quietly drops bytes from `main`, `sdk` and `subsdk0`; I lost an evening to that. The tool checks for it and tells you.
- For the **iPhone**: see the next section.
- **No Mac?** A Windows PC builds and signs the iPhone app alone: [docs/WINDOWS.md](docs/WINDOWS.md) (one play session
  on the phone before the title screen draws, see "No Mac?" below).

`python3.12 nxs2.py doctor` (or the window's **Check tools** button) checks all of this and tells you how to get whatever
is missing.

## Common problems

- **The title screen is black apart from its text on the first launch.** That is the shaders the game makes while it
  runs, not a broken build: play a few minutes, then run `python3.12 nxs2.py setup` once (if you never have) and
  `python3.12 nxs2.py refresh "<build folder>"`. Details: [the shaders, and what refresh is for](#the-shaders-and-what-refresh-is-for-read-this-too).
- **On an 8 GB Mac the game freezes at a scene change and the screen later turns pink.** The Mac ran out of graphics
  memory (the log says "Insufficient Memory"). Quit other apps, browsers above all, and start the game again; 16 GB is
  the comfortable size for now.
- **Mouse look stops while you hold movement keys on a MacBook's trackpad.** The trackpad ignores your finger while keys
  are held down (palm rejection), so the game never sees the movement. An external mouse or a controller works.
- **Mac: Pokopia Native Builder does nothing, and its log says `nxs2_gui.py: No such file`** (macOS 27, with a path
  containing `AppTranslocation`). macOS ran the app from a temporary copy away from its folder, because the downloaded
  folder is still marked as downloaded. Since 1.2.0 the app finds its folder by itself. With an older download, move the
  whole unzipped folder somewhere else (into Documents, say) and open the app from there, or run
  `xattr -dr com.apple.quarantine <the unzipped folder>` in Terminal.
- **Mac: `pip install` fails with "externally-managed-environment".** Homebrew's Python takes no pip packages of its own:
  make the builder's environment as in "What you need" and install the packages there. The Builder app picks it up by
  itself from 1.2.1 on.
- **Windows: `pip install lz4` fails with "Microsoft Visual C++ 14.0 or greater is required".** Your Python is 3.15,
  and lz4 has no ready-made package for it yet, so pip tries to compile it (the Visual C++ Redistributable is not what
  it asks for). Install Python 3.14 from python.org beside it and run `py -3.14 -m pip install lz4 fonttools pillow`;
  `Pokopia Native Builder.cmd` then uses 3.14 by itself.
- **Windows: Setup or Build stops with "No such file or directory" (or "cannot write in…") for a file in its own
  folder.** Windows Security's Controlled folder access, or an antivirus, is blocking Python in Documents, Desktop,
  Downloads or Pictures. Move the unzipped folder and the build folder somewhere else, such as `C:\Pokopia Native`, or
  allow `python.exe` under Ransomware protection → Allow an app through Controlled folder access.
- **Your dump is a single file** (.nsp, .xci or something else). The builder takes the game's files as folders, an
  `exefs/` and a `romfs/`, or a `.nup` (see "Your dump" above); have your dumping tool extract the others.

## Updating from an older version

Your build folder, the shaders you have recorded and your save all carry over.

1. Download the new version ([Which download do I need?](#which-download-do-i-need)) and unzip it into a new folder.
2. Press **Setup** once in the new folder (or `python3.12 nxs2.py setup`). Every new download needs it once.
3. Build again from the same dump. The build reuses your build folder (`<dump folder>_native`) and only redoes what
   changed, so it is much quicker than the first time. Shaders you already recorded and refreshed stay.
4. **iPhone:** sign the new IPA and install it **over** the old app. Installing over it keeps the app's Documents, which
   hold your save and the game files. **Do not delete the app first**: deleting an iPhone app deletes its save with it.
5. **Mac:** replace the app. Saves live in `~/Library/Application Support/nxnative/0400A3A01EE4C000/`, outside the app.

The game files on the phone only need copying again if you moved to another update of the game (v327680 to 2.0.1,
say): run `tools/phone_setup.py --romfs "<build folder>/extract/RomFS.img"` once more. Same game update, same dump:
nothing to copy. Backing your save up first never hurts (Finder's file sharing for the phone, your Library folder on
the Mac). Link Play needs the same pokopia-native version on every device, so update them all.

## Moving your save

A Pokopia save is three folders: **`B00`, `G00` and `S00`**. Moving a save means copying those three folders from one
place to another, and the same save works everywhere: in an emulator, the Windows game, the Mac app and the iPhone app.

**Before you copy:** close the game on both sides, and keep a copy of the save you are about to replace (a copy
replaces it; saves don't merge). A save that game version 2.0.1 has written may not load in 1.0.5.

| Where | Its save folder (the one holding `B00`, `G00` and `S00`) |
|---|---|
| **Eden**, and emulators built on it such as **Eglantine** | Right-click Pokémon Pokopia in the game list → **Open Save Data Location**. On Windows Eden's is usually `%APPDATA%\eden\nand\user\save\0000000000000000\<a long user ID>\0400A3A01EE4C000`; a portable install keeps it in the `user` folder next to the program. |
| **Windows game** | `%APPDATA%\Pokopia Native\<game version>\save`, for example `…\Pokopia Native\2.0.1\save` |
| **Mac app** | `~/Library/Application Support/nxnative/0400A3A01EE4C000/save` (Finder → Go → Go to Folder…) |
| **iPhone app** | the `save` folder in the app's Documents, through file sharing (below) |

**Into the Windows game:** **Import save…** in the builder window. **Yes** takes Eden's save on this PC by itself; **No**
lets you choose any save folder: another emulator's, or one you copied off the iPhone or the Mac. The save that was
there is kept beside it as `save.before-<date>`.

**Into the Mac app:** quit it, then copy the three folders into its save folder above (make the `save` folder if it is
not there yet).

**Into or out of the iPhone app:** fully close the app on the phone first (swipe it away in the app switcher), and open
it again when you're done.
- **With a Mac:** connect the phone and select it in Finder → **Files** → the Pokopia app. Drag its `save` folder out
  (that's your backup, or the save to move). To put one in, drag a folder named `save` holding `B00`, `G00` and `S00`
  onto the app and choose to replace.
- **With Windows:** the **Apple Devices** app from the Microsoft Store (or iTunes) → your iPhone → **Files** → the
  Pokopia app, the same way.

**Into an emulator:** open its save folder with **Open Save Data Location** (start the game there once first if the
folder doesn't exist yet) and copy the three folders in.

**Link Play:** a copied save is still the same save, and the game won't let two copies of one save play together. For
co-op each player needs a save of their own.

## The Apple developer thing (iPhone only, read this)

Pokopia is a big game. It reserves 11 GB of memory address space and actually uses several GB of it. To let an iPhone
app do that, the app has to carry two Apple entitlements: **Increased Memory Limit** and **Extended Virtual
Addressing**. Apple only grants the second one when the app is signed by a **paid Apple Developer Program** team (the
99 a year one). An app signed with a free Apple ID, through AltStore, Sideloadly and the like, installs fine and then
stops at start-up when the game cannot reserve its memory.

I happen to have an Apple developer account, so I sign my build with my own team and never needed anything else. **I have
not tested any workaround.** If you know one, something that signs with a profile that carries that entitlement, you are
free to try it and I would like to hear whether it worked, in the issues. But I am not going to tell you it works when I
have not done it myself.

With a paid team, the flow is: `nxs2.py build ... --xcode --team-id <your team>` then `nxs2.py sign`, and the signed
IPA installs over USB. Your Apple ID has to be signed into Xcode once and your phone registered on the team (Xcode does
that itself when the phone is plugged in). The details are in [docs/IPHONE.md](docs/IPHONE.md).

Memory on the phone itself matters too. The game uses a lot of it. I run it on an iPhone19,4 on iOS 27.2. If your
phone has less memory than the recent Pro models, expect trouble.

## How to build

There is a window and there is a command line; they do the same thing.

**The window.** Clone or download the repository, then double-click **Pokopia Native Builder.app** on the Mac
(**Pokopia Native Builder.cmd** on Windows). A walkthrough takes you through it a step at a time: what you want to make,
a check of your computer (it runs Setup for you), your dump folder, then the build, and what to do with what it made.
It remembers where you are: open it again after your first play and the refresh step explained further down is
waiting; open it from a new download and it offers to update your build. **Advanced** (top right) is the one-screen
view with every button (**Check tools**, **Setup**, **Build**, **Refresh shaders**, ...). If the app will not open, it tells you what to install (Python
3.12 with Tk: `brew install python@3.12 python-tk@3.12`). A clone opens directly. A copy downloaded with a browser is
blocked the first time, because the app is not signed by a registered developer: try to open it, then System Settings →
Privacy & Security → **Open Anyway** (or once in Terminal: `xattr -dr com.apple.quarantine "Pokopia Native Builder.app"`).

**The command line.**

```bash
git clone https://github.com/xFractals/pokopia-native.git
cd pokopia-native
python3.12 nxs2.py setup
python3.12 nxs2.py build "/path/to/your/dump folder" --mac-app --ipa
```

`setup` fetches nxnative (the host this runs on, more below) at the exact commit this is made for, applies the Switch 2
patch to it, and builds the shader lowering tool. About a minute.

`build` does everything else. It creates a build folder **beside** your dump (`<dump folder>_native`; `--out` names
another place), and goes through these stages. If it stops or you stop it, run it again and it continues where it was.

| Stage | What happens | Time on my M4 |
|---|---|---|
| extract | your ExeFS copied, the RomFS files packed into one image (6 to 7 GB) | seconds |
| rewrite | the game's three code modules rewritten so Apple's kernel can run them (nxnative's `patch_nso.py`) | 20 s |
| host | the Mac host compiled with this game's profile | 15 s |
| corpus, lower, package | 4,012 shader programs found in the game files, lowered, packaged with their tables | 25 s |
| chain | the Metal compile of every shader for the Mac and the phone | 30 to 45 min |
| outputs | the Mac app, the IPA, or the Xcode project | 1 to 3 min |

Outputs land in `<build folder>/out/`:

- `Pokemon Pokopia.app`: the Mac app. Double-click it, nxnative's settings window comes up (handheld or docked mode,
  frame rate, upscaling), press Play. macOS asks once whether to open it (right-click → Open), because it cannot be
  notarized: it maps the game's code as executable, which Apple's hardened runtime does not allow. Saves and logs go to
  `~/Library/Application Support/nxnative/0400A3A01EE4C000/` and `~/Library/Logs/nxnative/`. The game files inside the
  app are APFS clones of the image, so the app does not cost you another 6 GB on the same disk.
- `PokemonPokopia-unsigned.ipa`: the iPhone app without the game files (they go to the phone separately, once; it takes
  three minutes over USB). Or, with `--xcode --team-id`, an Xcode project to sign with your team. Again,
  [docs/IPHONE.md](docs/IPHONE.md).

Use `--no-ios` if you only want the Mac: it halves the compile. `python3.12 nxs2.py run "<build folder>"` plays straight
from the build folder in a window, which is handy while you are fiddling with things.

**The app icon.** Out of the box the apps get the blank default icon, because the game's own icon isn't part of a dump.
`python3.12 nxs2.py icon "<build folder>"` makes one from your game: on a Mac it starts the game on a copy of your save,
takes a picture of the title screen and cuts the Pokopia logo out of it. It needs a save that is past the game's opening
(play a little with `nxs2.py run` or the Mac app first, or point it at one with `--save <folder>`). If you prefer, give
it a screenshot of the title screen from your Switch 2 (Capture button):
`nxs2.py icon "<build folder>" title.jpg`. Then build again with the same command; it only repacks the apps.

## Windows: play on your PC

New in 1.1.0. The "for Windows PCs" download recompiles the game for your PC from your dump: our own recompiler turns the
game's arm64 code into x86-64 (through LLVM), and our own runtime runs it, with the Switch services ported from nxnative,
the graphics replayed on Vulkan and SDL3 for the window, input and sound. No emulator. As on the Mac, nothing of the game
is in the download; the recompiled game and its shaders are made on your PC and stay in your build folder.

You need Windows 10 or 11 (64-bit) with an AVX2 CPU (2013 or newer), a Vulkan 1.3 graphics card, about 12 GB of disk,
Python 3.12–3.14 with `lz4 fonttools pillow` (not 3.15 yet, see [Common problems](#common-problems)), the .NET 10 runtime and git. No compiler and no installer: the download brings
its own tools. Then:

```
python nxs2.py setup --no-ios
python nxs2.py build "C:\path\to\your dump folder" --windows
```

(or the window: **Setup**, then **Build** with "Windows: play on this PC" ticked). It took 20 minutes on my PC (longer on a smaller one), most of it
the recompile, at idle priority. Then double-click `<build folder>\out\Play Pokopia <version>.cmd`. Keyboard and mouse
as on the Mac below, controllers as Switch controllers. Your save lives in `%APPDATA%\Pokopia Native\<version>\save`;
**Import save…** in the window (or `nxs2.py import-save`) brings the one you have from Eden, the phone or the Mac. The
first start draws a few things late while the game's run-time shaders are translated once. Everything else, including
the save and the controller check: [docs/WINDOWS.md](docs/WINDOWS.md).

## No Mac? Windows builds the iPhone app

A Windows PC can build the iPhone IPA (not the Mac app, and it cannot run the game: the game's code needs an Apple chip).
It uses the tools nxnative's Windows port put together: Apple's own Metal compiler for Windows for the shaders, LLVM with
the iPhone SDK for the app. The same window and the same commands, `--ipa` only. What to install and how to get the SDK
is in [docs/WINDOWS.md](docs/WINDOWS.md). Signing still needs a paid team; the window signs on Windows too, with your
certificate (.p12) and development profile (**Sign IPA**, see docs/WINDOWS.md).

I tested the whole Windows route on my iPhone: built and signed on my PC, it plays like the Mac-built app. The one
difference is that a PC cannot run the game, so it cannot record the shaders the game makes while it runs (next
section). Your first run on the phone does that: the title screen is black apart from its text until you have played a
few minutes, pulled the recordings over and refreshed once. docs/WINDOWS.md has the exact steps.

## Playing together (Link Play)

New in 1.1.0. The game's **Link Play → Local connection** works between our builds: Windows, Mac and iPhone in any mix,
up to four players, at home or over the internet. It never touches Nintendo's servers. (**"Online connection"** in the
same menu is Nintendo's own service: here it just goes back to the menu. Always pick **Local connection**.)

Before you start: every device runs **the same pokopia-native version** and **the same game version** (1.0.5 and 2.0.1
can't play together), every player has **their own save**, and the host has reached the point in the game where Link Play
opens.

**On the same Wi-Fi**

- **iPhone and Mac:** in the launcher (the settings shown before the game starts) turn on **Link Play on this network**
  and allow local network access when asked. With the switch off, the game's Link Play does nothing.
- **Windows:** start the game as usual; the window that opens first asks how you want to play: pick **Link Play with
  players on this network**. When Windows Firewall asks, allow **Private networks**, and make sure your network is Private:
  Settings → Network & internet → your connection → Network profile type: **Private**. On a Public network Windows keeps
  the other devices out.
- In the game: **Link Play → Local connection**. One player hosts; the others look for towns, and the host's appears in a
  few seconds.

**Over the internet** (tested: a PC hosting at home, an iPhone visiting on mobile data)

1. **The host's router** must let visitors in. On Windows the game asks the router to do it while you host (UPnP,
   NAT-PMP or PCP), and F1 → Link Play says whether it worked and what to do if not (for example when your internet
   provider shares one address among many homes, where only a VPN helps). Otherwise, or if your router doesn't do
   that, forward port **47600, TCP and UDP**, to the computer that hosts (the router's "Port forwarding", "NAT" or
   "NAT/Gaming" page; check it points at the right device).
2. **The host** needs a room code, a 16-character password for the session:
   - **Windows:** pick **Host over the internet or a VPN** in the window before the game starts. The next window shows
     your room code with a **Copy** button, and so does the game itself: press **F1** and open the **Link Play** tab.
   - **Mac:** in the launcher turn on Link Play on this network and **Internet play**, leave Room code empty and start the
     game once: the log (`~/Library/Logs/nxnative`) says "this session's room code is …". Type it into **Room code** to
     keep it.
   - Give the visitors the room code and your internet address (search "what is my IP" on the host's network).
3. **The visitors:**
   - **iPhone and Mac:** turn on Link Play on this network, type the host's internet address into **Join address**, turn on
     **Internet play** and type the room code.
   - **Windows:** pick **Visit a host over the internet or a VPN** in the window before the game starts, and type the
     address and the code.
4. In the game: **Link Play → Local connection**, then look for towns.

**Interact everywhere** (experimental, new in 1.2.0)

Outside Palette Town the game puts everyone in a Link Play session in Spectator Mode: certain moves and items can't be used,
and visitors can't build. **Interact everywhere** lifts that: in every town, a session gets the rules Palette Town has with
Spectator Mode off, so visitors can water, pick up and place things, and build alongside the host.

- Turn it on **on every device**: iPhone and Mac: the launcher's **Interact everywhere** (under Link Play); Windows: the
  **Interact everywhere (experimental)** box in the window before the game starts. A device with it on only sees hosts that
  have it on too.
- The game still shows its own words: the host's settings show Spectator Mode On (greyed out), and the visitor is told everyone
  will be restricted. The session is not restricted.
- As in any Link Play visit, the town and its progress (challenges, building) belong to the host, and what a visitor picks up
  goes home with them.
- It works on game versions **2.0.1 and 1.0.5** (until pokopia-native 1.3.2 on 2.0.1 only: on 1.0.5 it did nothing).
- It is experimental: keep a copy of your save before a long session.

**Story co-op** (experimental, new)

In a story town the game lets only the host talk to the town's story characters, such as Professor Tangrowth: a visitor gets
the "Talk" prompt, but nothing happens. **Story co-op** lifts that, on top of Interact everywhere: visitors talk to the
story characters and hear the same story as the host, including the host's current requests.

- What counts for the host: what visitors do in the world, as with Interact everywhere: building, watering, placing and
  picking up, and the host's challenges (habitats too, we expect, but that isn't tested end to end yet).
- What doesn't: **story requests count only for the host.** A visitor can hear a request, but their conversation doesn't
  complete it; the host has to do that part. A visitor can't buy a building kit for the host's town either (the shop sends
  it home), but the host can drop one for them.
- Turn it on **on every device**: iPhone and Mac: the launcher's **Story co-op** (under Link Play); Windows: the
  **Story co-op (experimental)** box in the window before the game starts. It turns Interact everywhere on with it. A device
  with it on only sees hosts that have it on too.
- It works on game versions **2.0.1 and 1.0.5** (pokopia-native 1.3.0 to 1.3.2: 2.0.1 only).
- It is experimental: keep a copy of your save before a long session.

Good to know:

- **On Windows, F1 → Link Play shows what's going on**: how you're playing, the addresses to give your friends, the room
  code with **Copy** and **Copy invite** (a line with your address and the code, ready to paste in a chat), and who is
  connected. **Change how you play** there switches the way to play, the host's address and the room code without the
  launcher's window (at once while the game's Link Play is closed). Turning Link Play on or off, Interact everywhere and
  Story co-op take effect the next time the game starts. The launcher remembers the choice.
- **Internet play must match on every device.** With Internet play on, a device only sees hosts that have it on too
  (with the same room code). For players on the same Wi-Fi, turn **Internet play off** everywhere; otherwise the game
  says the destination cannot be found.
- The iPhone app reads its settings when it starts. After changing them, **close the app completely** (swipe it away in
  the app switcher) and open it again.
- Every connection must prove the room code, and every message is protected against changes, but the traffic **is not
  encrypted**. Remove the port forward when you are done hosting.
- **No access to the router, or playing through a VPN?** A VPN app like Tailscale (free for personal use, on Windows, Mac
  and iPhone), Radmin VPN or Hamachi puts your devices on one network across the internet, with no port forwarding.
  Pokopia Native counts a VPN's addresses (Tailscale's 100.x.y.z, Radmin VPN's 26.x.y.z, Hamachi's 25.x.y.z) as internet
  addresses, so play **over the internet**: the host hosts with Internet play (Windows: **Host over the internet or a
  VPN**), and the visitors use the host's VPN address as the Join address, with Internet play and the room code. **Link
  Play on this network** doesn't reach through a VPN. On Windows, if the visitors can't get in, set the VPN's network to
  Private as well (Settings → Network & internet). (Not tested yet; tell me how it goes.)
- Something not working? The logs say what happened, in the lines about `coop` (Windows: the folder its console window
  names; Mac: `~/Library/Logs/nxnative`; iPhone: the Files app → On My iPhone → the app → `log.txt`).

## The shaders, and what "refresh" is for (read this too)

Here is the one thing about this port that is not just "build and play", and I want to explain it properly so nobody
thinks their build is broken.

A Switch 2 game ships its shaders compiled for the Switch 2's GPU. They have to be translated for Metal, and that
translation cannot happen on the fly on a Mac or a phone with this chain; it is done ahead of time, on the Mac, during
the build. That is the 30 to 45 minute stage. The build finds **every shader in the game's files**, 4,012 programs,
and translates all of them. So far so good.

But Pokopia also **builds some shaders itself while it runs**. They are not in the game files anywhere; the game makes
them on the fly, apparently as it needs them. I know of **140** of these so far on 1.0.5, from the opening, the character
creator and the first area, and 2.0.1 makes **14 more** of its own (154 in all). A fresh build cannot contain them, because there is nothing to take them from until the game has
run. When the game asks for one that your build does not have, that draw is skipped: the game keeps running, but that
one effect or object is not drawn. Nothing crashes. Be warned though: the title screen itself uses about 20 of them, so
on the very first launch it is **black apart from the version number and "Settings"**. The music plays and A still
works. That is this, not a broken build, and it is gone after the first refresh.

What the build does about it: the apps **record** every such shader the moment the game makes it (they have
`DM_NVN_SHDUMP` on by default). Then you run

```bash
python3.12 nxs2.py refresh "<build folder>"
```

which finds the recordings, translates the new shaders (a couple of minutes, not another hour), and rebuilds whatever
outputs your build folder has: the app, the IPA, the Xcode project. The Mac app's recordings are found by itself. The
iPhone's have to be pulled off the phone first with `tools/phone_pull.py` (two commands, in the iPhone doc). Then
reinstall and carry on. Repeat when you reach new places and notice something missing. In my experience, after the
opening and the first area you have the lot.

The build tells you where you stand: `shaders: 3813 from the game files; 0 of the 140 known run-time shaders` right
after the first build, and after a refresh with a full set of recordings `140 of the 140` (on 2.0.1: `154 of the 154`).
The known list is `profile/0400A3A01EE4C000/runtime_keys.tsv`; it holds hashes only, so you can see how far you are,
and no code. The ones only 2.0.1 makes are marked there, so a 1.0.5 build is not counted short for them.

If you find run-time shaders beyond these, your refresh handles them exactly the same way. Tell me about them in the
issues, with the count the build prints, so the list can grow. Do not attach the recordings; they are game code.

## Controls: mouse look on the Mac, drag to look on the iPhone

The game only turns its camera with the right stick, so out of the box a mouse or a finger can only fake holding a stick,
and that feels like a console. I wanted it to feel like a PC or a phone game, so the build now has real look controls:

- **Mac:** click the game picture and the mouse is captured. Move it to look around; left click is A, right click is
  B, the middle button resets the camera. **Esc** gives the mouse back. Keyboard: WASD moves, J K U I are A B X Y, Q E
  are L R, Z C are ZL ZR, Return is + and Tab is −. (On Windows these keys can be changed in the Controls tab of the
  window before the game starts, docs/WINDOWS.md; the Mac and the iPhone keep this layout.)
- **iPhone:** drag anywhere on the right half of the screen that isn't a button, and the camera follows your finger.

**Sensitivity and invert** are in the app's launcher under Controls ("Look sensitivity", "Invert look up and down"),
or in `env.txt` as `DM_LOOK_SCALE` (1.0 = default) and `DM_LOOK_INVERT_Y=1`.

How it works, because it's not just "mouse = stick": faking the stick felt laggy, since Pokopia's stick tops out at
110° per second and ignores anything below about half tilt. So the app found where the game keeps its camera's angle in
memory and sets it directly, every frame, right before the game draws. Moving the mouse or your finger a given amount
turns the camera by exactly that amount (measured to a hundredth of a degree), with no speed limit, no smoothing and no
drift after you stop. Touches and mouse moves are time-stamped, so every frame gets an even share of the movement and a
steady pan stays steady. If the game ever takes the camera over, the app falls back to steering the stick for that
moment.

## Phone settings and heat

The app reads `Documents/env.txt` on the phone at start, one `KEY=VALUE` per line, and the build writes sensible defaults.
The ones worth knowing:

| Switch | What it does |
|---|---|
| `DM_FPS=30` | frame-rate cap. 30 is the build's default and what the Switch 2 does in handheld for this game. 60 runs hotter. |
| `DM_OPMODE=0` | handheld mode (default). `1` is docked: the game picks its higher resolution. |
| `DM_RENDER_SCALE=0.8` | render at a fraction of the game's resolution, 0.5 to 1.0. Cooler phone, softer picture. |
| `DM_TOUCH_AUTORUN=1` | the touch stick makes you run when pushed to its edge. My addition; I like it. |
| `DM_LOOK_SCALE=1.5` | look sensitivity for drags (and the Mac's mouse), as a multiple of the default. Same as the launcher's slider. |
| `DM_LOOK_INVERT_Y=1` | drag up to look down. |
| `DM_SHOW_FPS=1` | frame counter on screen. |

The phone gets hot. At 30 fps mine reaches iOS's "serious" thermal state after about six minutes of play and stays
warm. `DM_RENDER_SCALE=0.8` is the first thing to try if that bothers you.

## How this works, for the curious

- The host is [nxnative](https://github.com/seedds/nxnative), a project that runs Switch games natively on Apple
  systems: it rewrites the game's executable so the Apple kernel accepts it, provides the Switch OS services the game
  calls, and maps NVN (the Switch's graphics API) onto Metal. It did not know the Switch 2 at all. My changes teach it
  the Switch 2 side of things: the newer SDK's system calls and services, the NVN2 entry points and texture layouts, the
  Switch 2 shader format, and a translator fix or two. They are one patch against a pinned nxnative commit
  ([host/UPSTREAM.md](host/UPSTREAM.md)).
- Switch 2 shaders are a newer GPU instruction set than nxnative's shader chain understands. The
  [Eden-NS2](https://github.com/0x2593/Eden-NS2) emulator has a decoder for them that lowers them to the older
  instruction set, and `ampere-lower/` here is a standalone build of exactly that piece, so the rest of nxnative's chain
  (the Ryujinx translator, SPIRV-Cross, Apple's Metal compiler) can take over. While getting the picture to match the
  console I found four bugs in that decoder (wrong colours, missing sun and cloud shadows, a crash); they are in a pull
  request to Eden-NS2, and this tool carries the fixed code.
- `profile/0400A3A01EE4C000/` is the game's recipe: identity and build IDs, the memory layout the game needs (it wants
  11 GB), where in the files the shaders are, a handful of hand-made translation answers, the known run-time shader
  keys, and the entitlements. Numbers and hashes; no game content.

## Problems

- Every stage writes its full output to `<build folder>/logs/<stage>.log`. `nxs2.py -v` shows it live.
- `nxs2.py status "<build folder>"` says what the folder has and what it still lacks.
- A different Xcode than the one nxnative's lock names (27.2) is reported, not refused: the build uses your Metal
  compiler and keeps its libraries apart. Mine is 27.0 and it is fine.
- `[RED] shader <key> is not in the shader map` in a run log is a run-time shader, see above, not a fault.
- Ask in the [Discord](https://discord.gg/Aj9GqxZj8V), or open an issue with the stage, the last lines of its log and your
  Mac/Xcode versions. Never attach game files, build folders or recordings.

## Licences and credits

Everything here is MIT except `ampere-lower/`, which is Eden-NS2 code and stays GPL-3.0-or-later (its own `LICENSE` and
`NOTICE.md` are in that folder). The Windows download's prebuilt tools carry LLVM, SDL3, astc-encoder, bc7enc_rdo and
SPIRV-Tools under their own licences, listed in its `windows/bin/THIRD-PARTY-NOTICES.txt`. The nxnative and Ryujinx code that `host/` modifies and builds keeps their MIT notices,
reproduced in [host/NOTICE.md](host/NOTICE.md). [LEGAL.md](LEGAL.md) is the full statement of what this repository
contains and never will, what the tool does not do (no decryption, no keys), and the trademark note: Pokémon and
Pokémon Pokopia belong to their owners, this is an independent fan project with no affiliation to Nintendo, The Pokémon
Company or Koei Tecmo, and the screenshots above are my own captures from my phone.

Thanks to the people behind [nxnative](https://github.com/seedds/nxnative) and everything it is built from (d2r-mobile,
Ryujinx, SPIRV-Cross, glslang, Capstone, lz4, fontTools, XcodeGen), and to [Eden-NS2](https://github.com/0x2593/Eden-NS2)
for the Switch 2 shader work. I did this port with a lot of help from an AI assistant (Claude), over a couple of very
long weeks, with Switch 2 footage of the game as the reference for every picture.

And once more, because it matters: your own copy, your own dump, your own build. Please do not share game files.
