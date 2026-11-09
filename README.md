<p>
<picture>
  <source media="(max-width: 600px) and (prefers-color-scheme: dark)" srcset="docs/assets/hero-phone-dark.svg">
  <source media="(max-width: 600px)" srcset="docs/assets/hero-phone-light.svg">
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/hero-dark.svg">
  <img src="docs/assets/hero-light.svg" alt="Pokopia Native: Pokopia, native on every device. Rebuilt from your own copy of the game for Windows, Mac, iPhone, Android and Steam Deck.">
</picture>
</p>

**Pokopia Native** takes your own dump of Pokémon Pokopia and rebuilds it to run natively on your computer, phone or handheld. The game's
code is recompiled for your processor and its graphics are translated to Vulkan or Metal, so there's no emulator in between. Your save goes
with you from device to device, and you can play together across them.

<p align="center">
  <a href="https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/get-windows-dark.svg"><img src="docs/assets/get-windows-light.svg" width="404" alt="Download for Windows"></picture></a>
  <a href="https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/get-mac-dark.svg"><img src="docs/assets/get-mac-light.svg" width="404" alt="Download for Mac and iPhone"></picture></a>
  <br>
  <a href="https://github.com/xFractals/pokopia-native/releases?q=linux-beta"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/get-linux-dark.svg"><img src="docs/assets/get-linux-light.svg" width="404" alt="Get the Linux and Steam Deck beta"></picture></a>
  <a href="https://github.com/xFractals/pokopia-native/releases?q=android-beta"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/get-android-dark.svg"><img src="docs/assets/get-android-light.svg" width="404" alt="Get the Android beta"></picture></a>
</p>

**New here?** Every download has a step-by-step guide: **[Windows](docs/WINDOWS.md)** · **[Mac](docs/MAC.md)** ·
**[iPhone](docs/IPHONE.md)** · **[Android](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md)** ·
**[Linux & Steam Deck](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md)**. Either the Windows or the Mac download also
builds the iPhone app. On Android, check your phone first with the device test on the
[Android beta's page](https://github.com/xFractals/pokopia-native/releases?q=android-beta). What's new is in the
[release notes](https://github.com/xFractals/pokopia-native/releases).

<p>
<a href="https://discord.gg/Aj9GqxZj8V"><picture><source media="(max-width: 600px)" srcset="docs/assets/discord-phone.svg"><img src="docs/assets/discord.svg" alt="Come hang out on Discord: install help, co-op partners and release news. Join the server."></picture></a>
</p>

> [!IMPORTANT]
> **Bring your own game.** Pokopia Native contains nothing of the game: no game files, no keys, no dumping tools. Everything is built on
> your computer from your own dump. Please never share build folders, apps, saves or shader files. Not affiliated with Nintendo,
> The Pokémon Company, GAME FREAK or Koei Tecmo ([LEGAL.md](LEGAL.md)).

> [!WARNING]
> **Pokopia Native is free, and always will be.** It will never charge you for anything: not the downloads, the builds, an app, a
> feature, early access, support or help. If anyone asks you to pay for Pokopia Native or for anything made with it, they have nothing to
> do with this project: don't pay them, and don't trust their files. The only official downloads are on this page and its
> [releases](https://github.com/xFractals/pokopia-native/releases).

## Get started

<p>
<picture>
  <source media="(max-width: 600px) and (prefers-color-scheme: dark)" srcset="docs/assets/flow-phone-dark.svg">
  <source media="(max-width: 600px)" srcset="docs/assets/flow-phone-light.svg">
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/flow-dark.svg">
  <img src="docs/assets/flow-light.svg" alt="Your game dump goes into Pokopia Native Builder, which recompiles it on your computer into a native game for your PC, Mac, phone or Steam Deck.">
</picture>
</p>

1. **Download** the zip for your computer (the buttons above) and unzip it into a new folder.
2. **Open Pokopia Native Builder.** It checks your computer, asks for your game files and builds. The first build takes 15 to 40 minutes
   (30 to 60 for Android); updates take 10 to 20.
3. **Play.** On Windows, open **Play Pokopia** in the build's `out` folder. On a Mac, open the app the builder made. For an iPhone or an
   Android phone, the builder makes the app and helps you put it on the phone: see the [iPhone guide](docs/IPHONE.md) or the
   [Android guide](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md).

**Want every step explained?** Open the guide for your device:

<p>
  <a href="https://github.com/xFractals/pokopia-native/blob/main/docs/WINDOWS.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/guide-windows-dark.svg"><img src="docs/assets/guide-windows-light.svg" width="404" alt="Windows guide: set up, build and play"></picture></a>
  <a href="https://github.com/xFractals/pokopia-native/blob/main/docs/MAC.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/guide-mac-dark.svg"><img src="docs/assets/guide-mac-light.svg" width="404" alt="Mac guide: Xcode, Python and the builder"></picture></a>
  <br>
  <a href="https://github.com/xFractals/pokopia-native/blob/main/docs/IPHONE.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/guide-iphone-dark.svg"><img src="docs/assets/guide-iphone-light.svg" width="404" alt="iPhone guide: build, sign and install the app"></picture></a>
  <a href="https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/guide-android-dark.svg"><img src="docs/assets/guide-android-light.svg" width="404" alt="Android guide: device test, build and install"></picture></a>
  <br>
  <a href="https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/guide-linux-dark.svg"><img src="docs/assets/guide-linux-light.svg" width="404" alt="Linux and Steam Deck guide"></picture></a>
  <a href="https://github.com/xFractals/pokopia-native/blob/main/docs/PLAYING-TOGETHER.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/guide-together-dark.svg"><img src="docs/assets/guide-together-light.svg" width="404" alt="Playing together: Link Play, room codes and co-op"></picture></a>
</p>

<details>
<summary><b>What your computer needs</b></summary>
<br>

- **Windows 10 or 11, 64-bit:** a processor with AVX2, a graphics card with Vulkan 1.3, Python 3.12 to 3.14, the .NET 10 runtime and Git,
  and about 12 GB free per game version. No compiler, no administrator rights. [Windows guide](docs/WINDOWS.md)
- **Mac:** Apple silicon, Xcode with its Metal toolchain, Python 3.12 or newer and the .NET 10 runtime. 16 GB of memory is comfortable;
  8 GB struggles. [Mac guide](docs/MAC.md)
- **iPhone:** built by the Windows or Mac download and signed with your Apple account. [iPhone guide](docs/IPHONE.md)
- **Android (beta):** made on a Windows 10 or 11 PC with about 25 GB free, and put on the phone with a USB cable. The phone needs Android 11
  or newer, Snapdragon 8 Gen 1 or newer graphics, 8 GB of memory and about 9 GB free; the device test checks it in a few seconds.
  [Android guide](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md)
- **Linux and the Steam Deck (beta):** x86-64 with AVX2 and a Vulkan 1.3 driver (SteamOS has one), about 15 GB free.
  [Linux guide](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md)

The builder checks all of this for you and tells you what to install.
</details>

## What works where

|  | Windows | Mac | iPhone | Android | Linux &amp;<br>Steam Deck |
|:--|:--:|:--:|:--:|:--:|:--:|
| **Release** | Stable | Stable | Stable | Beta | Beta |
| **Graphics** | Vulkan, 4K | Metal, MetalFX | Metal | Vulkan | Vulkan, 4K |
| **In-game settings** | F1 | Soon | Soon | Back | F1 |
| **Rebind keys** | ✓ | Soon | — | — | ✓ |
| **Rebind controller** | ✓ | Soon | Soon | Next beta | Next beta |
| **Controllers** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Touch controls** | — | — | ✓ | ✓ | — |
| **Link Play** | ✓ | ✓ | ✓ | Not yet | ✓ |
| **Story co-op** | ✓ | ✓ | ✓ | Not yet | ✓ |
| **Auto port forwarding** | ✓ | By hand | By hand | — | Next beta |
| **Import a save** | ✓ | By hand | By hand | Not yet | ✓ |

Link Play is for up to four players; Story co-op is experimental. Saves move between a PC, a Mac and an iPhone ([how](docs/SAVES.md)).

**On the way:** smoother edges (anti-aliasing), and settings on the Mac and iPhone.

## Play together

Link Play connects Pokopia Native on Windows, Mac, iPhone and Linux in any mix, up to four players, without Nintendo's servers. (The
Android beta doesn't have it yet.)

- **On the same Wi-Fi,** hosts find each other (Linux joins by address for now).
- **Over the internet or a VPN,** the host shares an address and a room code. On Windows, **F1 → Link Play → Copy invite** puts both in
  one line, and the game asks your router to open the port.
- **Story co-op** (experimental) lets visitors talk to the story characters and help the host's town along.

In the game: the PC in your house → **Link Play** → **Local connection**. More in the [Playing together guide](docs/PLAYING-TOGETHER.md).

## Help

- **Setting it up?** The step-by-step guides: [Windows](docs/WINDOWS.md) · [Mac](docs/MAC.md) · [iPhone](docs/IPHONE.md) ·
  [Android](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md) ·
  [Linux & Steam Deck](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md).
- **Something not working?** [Common problems](docs/TROUBLESHOOTING.md) has the usual fixes.
- **Moving your save?** [Your saves](docs/SAVES.md) covers the PC, the Mac and the iPhone.
- **Questions?** Ask on [Discord](https://discord.gg/Aj9GqxZj8V): install help, co-op partners and release news.
- **Found a bug?** [Open an issue](https://github.com/xFractals/pokopia-native/issues/new/choose). The forms say which log to attach, never
  your save or game files.
- **Curious how it works?** [How it works](docs/HOW-IT-WORKS.md) covers the recompiler, the shaders, and why it isn't an emulator.

## Credits and licence

**MIT licence** for Pokopia Native's own code. It also carries third-party components under their own licences, all listed in
[LEGAL.md](LEGAL.md).

- **NxNative**, the host the Mac and iPhone apps run on.
- **@Quantumenis**, for the Windows CPU work and the RTX 50 freeze fix.
- **@Shabbypenguin**, for the builder's copyable install command.
- **Testers on the Discord**, who find what we can't.
- Third-party libraries are listed with their licences in `windows/bin/THIRD-PARTY-NOTICES.txt`.

<sub>Pokémon and Pokopia are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc. This project is not affiliated with or endorsed by
them. Rights holders: see [LEGAL.md](LEGAL.md).</sub>
