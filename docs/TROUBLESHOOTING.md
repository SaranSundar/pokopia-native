# Common problems

Answers to the problems players run into most, by platform, and how to report one that isn't covered here.

Back to the [overview](../README.md).

**On this page**

- [Everyone](#everyone)
- [Windows](#windows)
- [Mac](#mac)
- [iPhone](#iphone)
- [Linux, the Steam Deck and Android](#linux-the-steam-deck-and-android)
- [Logs, and how to report a problem](#logs-and-how-to-report-a-problem)

## Everyone

**The title screen is black apart from its text the first time.**
That's not a broken build. The game makes some of its shaders (the small programs that draw each effect) while it runs, and a fresh build
can't contain them yet; the title screen alone uses about 20. The music plays and A still works.
- Windows: the game translates them on the spot. A few things appear late on the first title screen and in the first minutes; give it a
  minute.
- Mac: play a few minutes, then press **Refresh the graphics** in the builder ([the Mac guide](MAC.md#the-shaders-and-refresh-the-graphics)).
- iPhone: play a few minutes, pull the recordings over, refresh and install the app again
  ([the iPhone guide](IPHONE.md#5-shaders-recorded-on-the-phone)).

**My dump is a single file (.nsp, .xci or something else).**
The builder takes the game's files as folders, an `exefs/` and a `romfs/`, or a `.nup` file. Have your dumping tool extract them
([what the builder expects](MAC.md#your-dump)).

**The build stops: a file "is … bytes shorter than its own header says".**
The dump was copied in text mode, which drops bytes from files without an extension (`main`, `sdk`, `subsdk0`). Copy the `exefs` folder
again in binary mode (FileZilla: Transfer → Transfer type → Binary).

**The builder doesn't accept my dump.**
Two updates of the game are supported: 2.0.1 (v655360) and 1.0.5 (v327680). The base game alone isn't enough: the dump needs the update.
Another version won't build until it is added.

**My 1.0.5 game says "Ver. 1.0.4" on its title screen.**
That's the same version.

**The mouse camera turns in small steps while I walk sideways.**
Turn the game's own **Camera Auto-Follow** off (the game's Settings, Camera tab). With it on, the game turns the camera itself as you walk
sideways, and the app has to steer it the way a stick would, in steps. With it off, the mouse (or the Mac's trackpad, or a drag on the
iPhone) turns the camera directly and smoothly. This needs 1.3.5.

**Link Play doesn't find the host, or says the destination cannot be found.**
Check that every device has the same game version, matching pokopia-native versions, and the same Internet play, Interact everywhere and
Story co-op choices: a device only sees hosts that chose the same. [Playing together](PLAYING-TOGETHER.md) has the rules.

**My friend can't join with a copy of my save.**
The game refuses a second copy of the same save. Each player needs a save of their own.

**My save from 2.0.1 doesn't load in 1.0.5.**
A save that 2.0.1 has written may not load in 1.0.5. Keep each version's save apart ([Moving your save](SAVES.md)).

**Is the Expansion Pass (DLC) supported?**
Not yet.

## Windows

The [Windows guide](WINDOWS.md) has the details on building and playing on Windows.

**`pip install lz4` fails with "Microsoft Visual C++ 14.0 or greater is required".**
Your Python is 3.15, and lz4 has no ready-made package for it yet, so pip tries to compile it (the Visual C++ Redistributable is not what
it asks for). Install Python 3.14 from python.org beside it and run `py -3.14 -m pip install lz4 fonttools pillow`;
`Pokopia Native Builder.cmd` then uses 3.14 by itself.

**Pokopia Native Builder.cmd opens no window.**
It says why and waits (no Python, a Python older than 3.12, the Microsoft Store's "python" placeholder, a Python without Tk), and writes
`builder-log.txt` beside itself for a report.

**Setup or Build stops with "No such file or directory" (or "cannot write in…") for a file in its own folder.**
Windows Security's Controlled folder access, or an antivirus, is blocking Python in Documents, Desktop, Downloads or Pictures. Move the
unzipped folder and the build folder somewhere else, such as `C:\Pokopia Native`, or allow `python.exe` under Windows Security →
Ransomware protection → Allow an app through Controlled folder access.

**A box says the game found no graphics card it can use.**
The box names what it found, for example "Intel(R) UHD Graphics (Vulkan 1.2)"; the game needs a graphics card with Vulkan 1.3. Most often a
gaming laptop's NVIDIA or AMD card isn't seen because its driver is missing: install the newest driver from nvidia.com or amd.com, restart,
and try again.

**The window is black or flickers right after the start, or things appear late at first.**
The first start translates the shaders the game makes while it runs, about 150 of them, once. Give it a minute; after that they are there
from the start. Places past the first towns build their graphics the first time you visit them, with short freezes, once.

**The game freezes after a while.**
1.3.5 fixed the freezes known on NVIDIA and AMD cards, so update first. If it still freezes, open an issue with the newest log from
`%LOCALAPPDATA%\Pokopia Native\<game version>\logs\`: it names the graphics program the card was running when it stopped.

**AMD or Intel graphics: the game freezes, often in conversations.**
Try the 32-lane test mode. Open PowerShell and run `setx NXRT_VK_SUBGROUP32 1`, close the builder if it's open, then start the game as
usual; the log then says "made with 32 lanes". `setx NXRT_VK_SUBGROUP32 0` turns it off again. Please tell us how it goes.

**AMD: a dotted line of purple squares in the sky.**
A known issue, so far only seen on AMD cards. It doesn't affect the game otherwise. We're testing with AMD players to find the effect
that's drawn wrong; the steps are on the [Discord](https://discord.gg/Aj9GqxZj8V).

**My controller doesn't show up.**
Close Steam, or turn off Steam Input for the controller. Xbox, PlayStation, Switch Pro and Steam Controllers work, and many others through
SDL's community list of controllers; one the list doesn't know gets a guessed layout, which you can fix in **F1 → Controls → Controller**.
For a report, start the game with `-PadCheck`: every button and stick is logged.

**The wrong controller is player 1.**
**F1 → Controls → Controller → Player 1** picks it, and it's remembered. Two controllers of the same model look identical to the game, so
the remembered one is whichever of them connects first.

**Stuck at "What's your name?" with only a controller.**
Press A, B or + to accept the box; an empty box becomes "Player". The game won't let you change your name later, so a keyboard is best for
this one.

**At 30 fps the game feels a little behind.**
At 30 fps the picture reaches the screen about a frame later than at 60.

**Link Play: the others don't see my game on the same Wi-Fi.**
When Windows Firewall asks, allow the game on **Private networks**, and make sure your network is Private: Settings → Network & internet →
your connection → Network profile type: **Private**.

**Link Play: I host over the internet and nobody gets in.**
**F1 → Link Play** says whether the router opened the port. If it didn't, forward port 47600, TCP and UDP, by hand. If your internet
provider shares one address among many homes, only a VPN helps ([Playing together](PLAYING-TOGETHER.md#over-the-internet)).

## Mac

**On an 8 GB Mac the game freezes at a scene change, and the screen later turns pink.**
The Mac ran out of graphics memory (the log says "Insufficient Memory"). Quit other apps, browsers above all, and start the game again.
16 GB is the comfortable size for now.

**Mouse look stops while I hold movement keys on a MacBook's trackpad.**
The trackpad ignores your finger while keys are held down (palm rejection), so the game never sees the movement. An external mouse or a
controller works.

**The builder says Python add-ons are missing, or `pip install` fails with "externally-managed-environment".**
Homebrew's Python takes no pip packages of its own. The builder shows the one Terminal command that sets up its own Python, with a **Copy**
button: run it, then reopen the builder. The steps: [the builder's Python environment](MAC.md#the-builders-python-environment).

**The Metal check fails, although Xcode and its Metal toolchain are installed.**
Most often Terminal is set to Apple's separate Command Line Tools instead of Xcode. The check spots that and gives you the command that
switches to Xcode. It also catches a not-yet-accepted Xcode license, Xcode's first-launch setup, and a missing Metal toolchain or iOS
platform: do what it says, then check again.

**macOS won't open Pokopia Native Builder or the Mac app.**
Neither is signed by a registered developer, and the app can't be notarized because it maps the game's code as executable. Try to open it
once, then go to System Settings → Privacy & Security and press **Open Anyway**. For the builder, `xattr -dr com.apple.quarantine "Pokopia
Native Builder.app"` in Terminal works too. If the builder still opens no window, it says what to install (Python 3.12 with Tk:
`brew install python@3.12 python-tk@3.12`).

**The build says my Xcode isn't the version nxnative's lock names.**
That is reported, not refused: the build uses your Metal compiler and keeps its libraries apart. The lock names 27.2; 27.0 works fine.

**A run log says `[RED] shader <key> is not in the shader map`.**
That's a shader the game makes while it runs, not a fault: play on, then **Refresh the graphics**.

**The build stopped.**
Run it again: it continues where it was. Every stage writes its full output to `<build folder>/logs/<stage>.log` (`nxs2.py -v` shows it
live), and `python3.12 nxs2.py status "<build folder>"` says what the build folder has and what it still lacks.

## iPhone

**The app installs, then stops at start-up.**
It was signed without a paid Apple Developer Program team (a free Apple ID, AltStore, Sideloadly). Without the Extended Virtual Addressing
entitlement that only that signing carries, the game can't reserve its memory ([the iPhone guide](IPHONE.md)).

**Signing with my certificate stops because of the provisioning profile.**
The tool checks that the profile matches the app and grants both memory capabilities, and after signing it reads the signature back and
stops if an entitlement is missing. In your developer account, the App ID `org.nxnative.0400a3a01ee4c000` needs **Increased Memory
Limit** and **Extended Virtual Addressing** ticked, and the profile needs your iPhone on it
([how to make it](WINDOWS.md#sign-and-install)).

**Will it run on my iPhone?**
The game uses a lot of memory. It was played on an iPhone19,4 running iOS 27.2; on a phone with less memory than the recent Pro models,
expect trouble.

**The phone gets hot.**
At 30 fps it reaches iOS's "serious" thermal state after about six minutes. Put `DM_RENDER_SCALE=0.8` in the app's `env.txt` first; 0.5
runs cool but looks much softer ([the iPhone guide](IPHONE.md#4-settings-documentsenvtxt)).

**I changed a setting in the launcher and nothing changed.**
The app reads its settings when it starts. Close it completely (swipe it away in the app switcher) and open it again.

**Can I delete the app and install the new one fresh?**
No: deleting an iPhone app deletes its save with it. Always install the new app **over** the old one. Back up your save first if you like
([Moving your save](SAVES.md#into-or-out-of-the-iphone-app)).

## Linux, the Steam Deck and Android

Both are open betas with their own guides, problems and report tools:

- **Linux and the Steam Deck** (v1.3.3-linux-beta.5):
  [Common problems](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md#common-problems) and
  [the report](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md#the-report) in the Linux guide.
- **Android** (v1.3.3-android-beta.2): [Questions](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md#questions)
  and **Make a report** in the [Android guide](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md).

## Logs, and how to report a problem

**Where the logs are:**

| What | Its log |
|---|---|
| The Windows game | `%LOCALAPPDATA%\Pokopia Native\<game version>\logs\` (the console window names the folder). The first lines name the graphics card, the driver and the game version. |
| The Windows builder, when it opens no window | `builder-log.txt`, beside `Pokopia Native Builder.cmd` |
| A build or refresh (Windows and Mac) | `<build folder>/logs/<stage>.log`. In the builder, **Show details** shows the output, and a failed step has a button to copy the error. |
| The Mac app | `~/Library/Logs/nxnative/` |
| The iPhone app | the Files app → On My iPhone → the app → `log.txt` |
| Linux, the Steam Deck, Android | their report tools collect everything ([above](#linux-the-steam-deck-and-android)) |

Since 1.3.5 the Windows log holds only the first group of a Link Play room code, so it's safe to attach.

**How to report:** ask in the [Discord](https://discord.gg/Aj9GqxZj8V), or
[open an issue](https://github.com/xFractals/pokopia-native/issues/new/choose). Say:

- what you did and what happened;
- your pokopia-native version (the builder window's title shows it) and the game version (2.0.1 or 1.0.5);
- the stage that failed and the last lines of its log, or the newest game log;
- your computer: on Windows, the Windows version and the graphics card; on a Mac, the macOS and Xcode versions; on an iPhone, the model and
  the iOS version.

**Never attach** your dump or any game files, the build folder, the app or the IPA, `game.dll`, shader files or recordings, or your saves.
They contain the game, or your own data.
