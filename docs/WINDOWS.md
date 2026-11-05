# Building on Windows

A Windows PC builds two things from your dump, with the **"for Windows PCs" download**
([Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip)):

- **the Windows game**: Pokopia recompiled for your PC, played in a window (or full screen) with a Vulkan renderer. The next section.
- **the iPhone app** (the IPA), as before: [further down](#the-iphone-app). The Mac app needs a Mac.

## The Windows game: play on this PC

**Status (1.1.0): works** for 1.0.5 and 2.0.1 on the PC it was developed on (an NVIDIA GPU). It is new; tell me in the issues
how it runs on yours, with the log (it names no files of yours beyond the paths of your build folder).

The game's arm64 code is recompiled for x86-64 on your PC (our own recompiler, `nxlift`, built on LLVM), and runs on our
own runtime (`nxrt`: the Switch services ported from nxnative, NVN replayed on Vulkan, SDL3 for the window, input and
sound). Nothing of the game is in the download: the recompiled game, its shaders and everything else made from your dump
stay in your build folder.

### What you need

- **Windows 10 or 11, 64-bit**, with a CPU that has **AVX2 and FMA3** (Intel Haswell from 2013 or AMD Excavator from
  2015, and anything newer), and **16 GB of memory** to be comfortable (the game itself took about 5 GB on the test PC).
- A **graphics card with Vulkan 1.3** and a current driver (NVIDIA, AMD or Intel).
- **Python 3.12, 3.13 or 3.14** from python.org (tick "Add python.exe to PATH"), then
  `python -m pip install lz4 fonttools pillow` (add `zstandard` if your dump is a `.nup` file). Python 3.15 is too new
  for now: lz4 and zstandard have no ready-made packages for it yet, so pip tries to compile them and stops with
  "Microsoft Visual C++ 14.0 or greater is required" (the Visual C++ Redistributable does not help). Install 3.14
  beside it and use `py -3.14 -m pip install …`; `Pokopia Native Builder.cmd` then picks 3.14 by itself.
- The **.NET 10 runtime** (the shader translator) from dotnet.microsoft.com, and **git** (git-scm.com: setup fetches nxnative).
- About **12 GB** free where the build folder goes, per game version.
- Your dump, as for the other builds (the README's "What you need").

No compiler, no SDK, no installer and no administrator rights: the download carries its own tools in `windows\bin`
(see `windows\bin\MANIFEST.txt` and `THIRD-PARTY-NOTICES.txt`). **Check tools** in the window says what is missing.

### Build

Unzip the download, double-click **Pokopia Native Builder.cmd**, untick "iPhone IPA" (unless you want it too), press
**Setup** once (it fetches nxnative; with the IPA unticked it needs none of Apple's tools), choose your dump folder, tick
**Windows: play on this PC** and press **Build**. From a terminal:

```
python nxs2.py setup --no-ios
python nxs2.py build "C:\path\to\your dump folder" --windows
```

The stages, with the times measured on my PC (an i9, 4 workers; the default on a PC with 8 threads is 2, about twice as long):

| Stage | What happens | Time |
|---|---|---|
| extract | the ExeFS copied, the RomFS packed into one image (about 7 GB) | under a minute |
| corpus, lower, package | the game's shaders found, lowered (ampere-lower) and packaged, as for the other builds | 2 minutes |
| analysis | `nxanalyze`: the game's functions, control flow and bindings (`program.db`) | seconds |
| shaders | `nxshader`: 3,813 shaders translated to Vulkan SPIR-V and checked with `spirv-val` | 6 minutes |
| recompile | `nxlift`: 200,414 functions through LLVM into `game.dll` (390 MB), at idle priority | 11 minutes |
| runtime | `nxrt.exe` and the launcher beside the game | seconds |

The recompile is the long part; it runs at idle priority with at most four workers, so the PC stays usable. If the build
stops, run it again: it continues where it was. When a new pokopia-native comes out, build again with the new download:
only what changed is redone.

### Play

Double-click **`<build folder>\out\Play Pokopia <version>.cmd`** (or **Play** in the window, or `python nxs2.py run "<build folder>"`).

- **Keyboard and mouse:** WASD moves; click the picture and the mouse looks around (Esc gives it back); J K U I are
  A B X Y, Q E are L R, Z C are ZL ZR, Return is +, Tab is −; F11 is full screen.
- **Your own keys:** the window before the game starts has a **Controls** tab: click an action, then press the key you
  want for it (a key another action had swaps over); **Reset to defaults** brings the layout above back. Keys count by
  their place on the keyboard, so the map works the same under any keyboard language. It is saved for this PC in
  `%APPDATA%\Pokopia Native\controls.json`, and the game's log lists the keys it uses. (Hold **Shift** while starting the
  game if you ticked "Don't ask again".) Esc, F11, Alt and the Windows keys can't be used. The Mac and the iPhone keep
  their own layout.
- **Controllers:** Xbox, PlayStation and Switch Pro controllers work as Switch controllers (face buttons by position),
  with rumble and motion. Close Steam or turn off Steam Input for the controller if it does not show up.
- Switches for the launcher go after its name in a terminal: `-Fullscreen`, `-Window 1600x900`, `-Silent`, `-PadCheck`
  (the controller check: every button and stick is logged and shown in the title bar).

**The first start** takes a moment longer: the game makes some of its shaders while it runs (about 150 of them, 20 on the
title screen alone). The Windows game records each one the first time the game asks for it and translates it on the
spot, so a few things appear a little late on the first title screen and in the first minutes of play; after that they
are there from the start. Nothing to do for you, and nothing is skipped for long. (The same recordings also finish the
iPhone app of this build folder: `nxs2.py refresh` takes them, see the README's shader section.) The graphics pipelines
of the title, a new game and the first towns (2.0.1) are built on background threads while the game starts, from a list
the builder ships (`profile/0400A3A01EE4C000/pipelines.2.0.1.tsv`), so those areas play without first-time freezes;
places further on build theirs the first time you visit, once.

### Your save

The Windows game plays the save in **`%APPDATA%\Pokopia Native\<version>\save`** (the game's own `B00`, `G00` and `S00`
folders), started empty the first time. To bring the save you already have:

- **From Eden on this PC:** **Import save…** in the window → Yes, or `python nxs2.py import-save "<build folder>" --eden`.
  Eden's save is only read.
- **From the iPhone, a Mac or anywhere else:** a folder holding `B00`, `G00` and `S00` (the iPhone's
  `Documents\save` as `tools\phone_pull.py` brings it, the Mac's `~/Library/Application Support/nxnative/0400A3A01EE4C000/save`):
  **Import save…** → No → choose it, or `python nxs2.py import-save "<build folder>" "<that folder>"`.

The save that was there is kept beside it as `save.before-<date>`, never deleted. A save the 2.0.1 game has written may
not load in 1.0.5: keep each version's save apart (they are, by default). Saves go back the other way by copying the
folder: Windows saves load on the Mac and the iPhone.

### Link Play (co-op) between our builds

Link Play → **Local connection** in the game works with the Mac and iPhone apps and other Windows PCs (the README's
[Playing together](../README.md#playing-together-link-play) has the whole picture). Every device needs **the same
pokopia-native version**, and **each player needs their own save** (the game refuses a second copy of the host's save, so
a save copied from one device to another cannot join itself).

**Before the game starts, a small window asks how you want to play:**

- **Play alone**;
- **Link Play with players on this network** (the same Wi-Fi): host, or find the other players' towns;
- **Host over the internet**: your router forwards port **47600, TCP and UDP**, to this PC. A second window shows your
  **room code** with a **Copy** button; give it to the visitors with this PC's internet address. "Keep my room code from
  last time" saves them typing a new one;
- **Visit a host over the internet**: type the host's address and room code.

Under them, **Interact everywhere (experimental)** lets Link Play players use moves and items and build in every town, not only
Palette Town (the README's [Playing together](../README.md#playing-together-link-play) explains it). Every player needs it on,
the iPhone and Mac in their launcher.

It remembers your choice. **Don't ask again** starts the same way every time; hold **Shift** while starting the game to
see the window again. The settings live in `%APPDATA%\Pokopia Native\linkplay.json`.

From a terminal, switches after the launcher's name skip the window: `-LinkPlay`; `-Join <address>[:<port>]` visits one
host by address; `-Internet` with `-Room <code>` (a host without one gets a new code); `-Relay <address>[:<port>]` goes
through a relay you trust; `-Interact` turns on Interact everywhere.

- **The first time**, Windows Firewall asks whether the game may use networks: allow **Private networks**. Your network
  must be Private (Settings → Network & internet → your connection → Network profile type: Private): on a Public network
  Windows keeps the other devices out, and the game's log says so.
- **Host and visitors choose the same way**: "on this network" with the iPhone's and Mac's Internet play off, "over the
  internet" with it on. A device with Internet play on does not see a host on the same Wi-Fi without it.
- **Several network adapters** (Wi-Fi and Ethernet, an iPhone plugged in by USB, a virtual machine's adapter) are fine: the
  game tells the others all its home-network addresses, and they pick the one they can reach.
- The log (the folder the console window names) has a `[coop]` line for everything Link Play does: who connected, a wrong
  room code, a different pokopia-native version on the other side.

### Problems

- The launcher's console window says where the log is (`%LOCALAPPDATA%\Pokopia Native\<version>\logs`). The first
  lines name the GPU, the driver and the game version.
- `nxs2.py status "<build folder>"` lists what the build folder has.
- A black or flickering window right after the start, with the music playing: the first-start shaders above; give it a
  minute.
- Open an issue with the stage or the log's last lines, your GPU and Windows version. Never attach the build folder,
  `game.dll`, shader files or saves.

## The iPhone app

A Windows PC builds and signs the iPhone app, the same app a Mac builds. It cannot build the Mac app. The build uses the
tools [nxnative's Windows port](https://github.com/zenvyzenshiro/NXNativeWindows) maps Apple's tools onto: the shaders go
through Apple's own Metal compiler for Windows, the app is compiled with LLVM against the iPhone SDK.

**Status (2026-11-03): works,** for 1.0.5 and 2.0.1. Built and signed on a PC, the app plays on my iPhone like the Mac-built one. One thing is
different from a Mac: a PC building only the iPhone app cannot record the shaders the game makes while it runs (see the
README's shader section). Until your phone has recorded them once, the title screen is black apart from its text; the
steps for that are at the end of this page. (If you also play the Windows game from the same build folder, its
recordings do the same job: `nxs2.py refresh` picks them up.)

### What to install (once)

`Pokopia Native Builder.cmd` → **Check tools** (or `python nxs2.py doctor`) lists these and says what is missing:

| Tool | Where | Why |
|---|---|---|
| Python 3.12, 3.13 or 3.14 (3.15 lacks lz4 packages so far) | python.org, tick "Add python.exe to PATH" (Tk comes with it) | the build script and the window |
| `python -m pip install "capstone>=5,<6" lz4 fonttools pillow` | | the code rewriter, the shader tools, the fonts, the app icon |
| .NET 10 runtime | dotnet.microsoft.com | the shader translator |
| LLVM for Windows | github.com/llvm/llvm-project/releases, `LLVM-<version>-win64.exe` | compiles and links the iPhone app (`clang`, `ld64.lld`) |
| Metal Developer Tools for Windows | developer.apple.com/download (Apple ID) | Apple's Metal compiler: the shaders and the host's own Metal code |
| Vulkan SDK | vulkan.lunarg.com | its `spirv-cross.exe` turns the translated shaders into Metal source |
| git | git-scm.com | fetches nxnative and its Windows port at the pinned commits |
| Xcode (`Xcode_27.x.xip`) | developer.apple.com/download (Apple ID; about 8 GB, downloaded once) | the iPhoneOS SDK the app is built against, extracted below |

The iPhoneOS SDK is taken out of the Xcode archive after **Setup** has run (Setup fetches the extractor):

```
python nxnative\win\sdk\extract_xip.py "C:\path\to\Xcode_27.1.xip"
```

A few minutes; the SDK lands in `nxnative\win\sdk\out\`, where the build finds it (`NXW_IOS_SDK=<folder>` names another).

Apple's licence terms for Xcode and its SDK are between you and Apple; the Windows port's README says to read them
before using the SDK off a Mac. The Windows build was made and tested here with LLVM 23, Metal Developer Tools 6.2 and
the iPhoneOS 27 SDK.

### Build the IPA

Double-click `Pokopia Native Builder.cmd`: tick "iPhone IPA", press **Setup** once (with the IPA ticked it also fetches the
iPhone toolchain), then choose your dump folder and press **Build**. From a terminal:

```
python nxs2.py setup
python nxs2.py build "C:\path\to\your dump folder" --ipa
```

The stages are the Mac's minus the Mac host: the game image, the rewritten code, the shader corpus, the lowering, the
package, the Metal compile (iOS libraries only), the app. The Metal compile is the long part here too.

**Load.** By default the build runs a quarter of your CPU threads (at most 8) at low priority, so the PC stays cool and
usable while it works; expect an hour or so. **Full speed** in the window (`--full-speed`, or `-j N`) uses every thread
and is much faster. Only use it on a machine you trust with a long all-core load: my own PC, an i9-13900KF on an old
BIOS, blue-screened a minute into a full-speed shader compile. 13th and 14th generation Intel Core CPUs need the BIOS
update with Intel's 2024 microcode fix (0x12B or newer) for that; **Check tools** warns when yours does not have it.

The output is `<build folder>\out\PokemonPokopia-unsigned.ipa`: the app without the game files, its executable carrying
the two kernel entitlements in an ad-hoc signature, plus `romfs.id`, the identity of your game image.

For the home-screen icon, take a screenshot of the game's title screen on your Switch 2 (Capture button), copy it to the
PC and run `python nxs2.py icon "<build folder>" title.jpg` before the build (or build again afterwards).

### Sign and install

The IPA has to be signed with a **paid Apple Developer Program** team (see the README: the game's memory needs the
Extended Virtual Addressing entitlement, which only that signing carries). On Windows the build tool signs it itself,
with [rcodesign](https://github.com/indygreg/apple-platform-rs) (fetched by Setup and checked against its published
checksum). It needs two files from your developer account:

- **Your Apple Development certificate with its private key, as a `.p12`.** If you have a Mac with Xcode, export it from
  Keychain Access (My Certificates, right-click "Apple Development: ...", Export). Without a Mac: make a certificate
  signing request with `tools-bin\rcodesign.exe generate-certificate-signing-request`, upload it at developer.apple.com →
  Certificates (+, Apple Development), download the certificate and combine it with the key into a `.p12`.
- **A development provisioning profile (`.mobileprovision`)** for the bundle ID `org.nxnative.0400a3a01ee4c000`, with your
  iPhone on it. At developer.apple.com: Identifiers (+, App IDs, that bundle ID, and tick **Increased Memory Limit** and
  **Extended Virtual Addressing** under Capabilities), Devices (your iPhone's UDID), Profiles (+, iOS App Development,
  that App ID, your certificate, your iPhone). If Xcode already signed this app for you, that profile works too.

Then **Sign IPA** in the window (it asks for both files and the certificate's password), or:

```
python nxs2.py sign "<build folder>" --p12 my-cert.p12 --profile Pokopia.mobileprovision
```

The result is `<build folder>\out\PokemonPokopia-signed.ipa`. The tool checks that the profile matches the app and
grants both memory capabilities, and after signing it reads the signature back and stops if an entitlement is missing.

Then, as on the Mac ([IPHONE.md](IPHONE.md)): `pymobiledevice3 apps install <ipa>`, and `tools\phone_setup.py --romfs
<build folder>\extract\RomFS.img` for the game files.

**The first run (once).** Start the game on the phone and let it reach the title screen: it is black apart from the
version number and "Settings", the music plays. Press A and play into the game for a few minutes (the opening, the first
area), then close it. The phone has now recorded the shaders the game made. Bring them over and build them in:

```
python tools\phone_pull.py "<build folder>\phone\first-run"
python nxs2.py refresh "<build folder>"
```

(`refresh` finds what is under `<build folder>\phone\` by itself.)

Sign the new IPA again and install it over the old one (your save stays). From then on the title screen and everything
you played through are drawn. Anything new the game makes later is recorded the same way; refresh again whenever you like.
