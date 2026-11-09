# How it works

Pokopia Native runs the Switch 2 game's own code on your computer or phone instead of emulating a Switch 2; this page explains how, in
plain words, for the curious.

Back to the [overview](../README.md).

**On this page**

- [Not an emulator](#not-an-emulator)
- [On the Mac and the iPhone](#on-the-mac-and-the-iphone)
- [On Windows and Linux](#on-windows-and-linux)
- [The shaders](#the-shaders)
- [Mouse and touch look](#mouse-and-touch-look)
- [The game's recipe](#the-games-recipe)
- [Why nothing of the game is in the download](#why-nothing-of-the-game-is-in-the-download)

## Not an emulator

An emulator imitates a whole Switch 2, piece by piece, while the game runs. Pokopia Native does something else: it prepares the game's own
code ahead of time so your processor runs it directly, and it answers the game's calls to the Switch 2's system the way the console would.
The pictures go through your device's own graphics system: Metal on the Mac and the iPhone, Vulkan on Windows and Linux.

## On the Mac and the iPhone

The game's code is arm64, the same instruction set as Apple's own chips (that's why an Intel Mac can't run it). So on the Mac and the
iPhone it runs as it is, inside a host:

- The host is [nxnative](https://github.com/seedds/nxnative), a project that runs Switch games natively on Apple systems. It rewrites the
  game's executable so Apple's kernel accepts it, provides the Switch system services the game calls, and maps NVN (the Switch's graphics
  interface) onto Metal.
- nxnative didn't know the Switch 2 at all. Pokopia Native's changes teach it the Switch 2 side of things: the newer system's calls and
  services, the NVN2 entry points and texture layouts, the Switch 2 shader format, and a translator fix or two. They are one patch against a
  pinned nxnative commit ([host/UPSTREAM.md](../host/UPSTREAM.md)), which `nxs2.py setup` applies.

## On Windows and Linux

A PC's processor speaks x86-64, not arm64, so on Windows the game's code is **recompiled**: the project's own recompiler turns the game's
arm64 code into x86-64 through LLVM (a widely used compiler toolkit), once, while you build. The project's own runtime then runs it, with the
Switch services ported from nxnative, the game's graphics replayed on Vulkan, and SDL3 for the window, input and sound. The recompiled game
is made on your PC and stays in your build folder.

The Linux and Steam Deck beta is the same recompiler and runtime, ported to Linux. The Android beta is made on a Windows PC and put on the
phone over USB; [its guide](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md) has the details.

## The shaders

Shaders are the small programs a graphics card runs to draw each thing on screen. A Switch 2 game ships its shaders compiled for the
Switch 2's graphics chip, so they have to be translated before your device can use them.

- **The lowering.** Switch 2 shaders use a newer graphics instruction set than nxnative's shader chain understands, so a lowering step
  (`ampere-lower/` in this repository) first rewrites them in the older instruction set. Its origin and licence are in
  [LEGAL.md](../LEGAL.md).
- **Mac and iPhone: to Metal, ahead of time.** The rest of nxnative's chain takes over: the Ryujinx translator, SPIRV-Cross and Apple's Metal
  compiler. That translation can't happen on the fly on a Mac or a phone with this chain, so it's done during the build: the build finds
  every shader program in the game's files, 4,012 of them, and translates them all. That's the long stage of a Mac build.
- **Windows: to Vulkan.** The Windows build translates the same shaders to Vulkan's format and checks each one. It also ships a list of the
  graphics pipelines of the title, a new game and the first towns (on 2.0.1), which the game builds in the background while it starts, so
  those areas play without first-time freezes.

### The shaders the game makes while it runs

Pokopia also builds some shaders itself while it runs. They aren't in the game files anywhere, so a fresh build can't contain them: there
is nothing to take them from until the game has run. 140 are known so far on game version 1.0.5, from the opening, the character creator
and the first area, and 2.0.1 makes 14 more of its own (154 in all).

- **Windows and Linux** record each one the first time the game asks for it and translate it on the spot. A few things appear a little late
  the first time; after that they're there from the start.
- **Mac and iPhone** can't translate them on the spot. When the game asks for one the app doesn't have, that one draw is skipped: the game
  keeps running, but that effect or object isn't drawn. The app records the shader (its `DM_NVN_SHDUMP` switch, on by default), and
  **refresh** (`nxs2.py refresh`, or **Refresh the graphics** in the builder) translates the recordings and rebuilds the app, in a couple of
  minutes. The title screen alone uses about 20 of them, which is why it's black apart from its text on the very first launch.
- The Windows game's recordings also finish the iPhone app of the same build folder.
- The list of known run-time shaders is `profile/0400A3A01EE4C000/runtime_keys.tsv`. It holds hashes only, no code, so the build can tell
  you how far you are. The ones only 2.0.1 makes are marked there, so a 1.0.5 build isn't counted short for them.

If you find run-time shaders beyond these, tell us in the [issues](https://github.com/xFractals/pokopia-native/issues/new/choose) with the
count the build prints, so the list can grow. Don't attach the recordings: they are game code.

## Mouse and touch look

The game only turns its camera with the right stick. Faking the stick with a mouse or a finger felt laggy, because Pokopia's stick tops out
at 110° per second and ignores anything below about half tilt. So the apps find where the game keeps its camera's angle in memory and set it
directly, every frame, right before the game draws. Moving the mouse or your finger a given amount turns the camera by exactly that amount
(measured to a hundredth of a degree), with no speed limit, no smoothing and no drift after you stop. Mouse moves and touches are
time-stamped, so every frame gets an even share of the movement and a steady pan stays steady.

When the game takes the camera over itself, the app steers through the stick's path for that moment. That's what happens while you walk
sideways with the game's Camera Auto-Follow on; turn Auto-Follow off for the smoothest camera.

## The game's recipe

`profile/0400A3A01EE4C000/` holds the game's recipe: its identity and build IDs, the memory layout the game needs (it reserves 11 GB of
address space), where in the files the shaders are, a handful of hand-made translation answers, the known run-time shader keys, and the
iPhone app's entitlements. Numbers and hashes; no game content. The patch recipe and the camera's address differ between game versions,
which is why each version has to be added before it builds.

## Why nothing of the game is in the download

The download holds only the tools. Everything that comes from the game (the rewritten or recompiled code, the translated shaders, the packed
game files, the app or the IPA) is made on your computer from your own dump, and stays in your build folder. The tool checks that the dump
is a supported version and refuses anything else; it needs no keys of any kind and decrypts nothing.

That's also why nothing it makes may be shared: the build folder, the apps, `game.dll`, shader files and recordings all contain pieces of
the game. Your own copy, your own dump, your own build. [LEGAL.md](../LEGAL.md) is the full statement.
