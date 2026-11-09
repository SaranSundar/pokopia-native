# What this repository contains, and what it never contains

This project lets you run a game you own natively on your own Mac, iPhone or Windows PC, and, in open betas, on Linux
(the Steam Deck included) and Android. It ships **tools only**. It
does not contain, and must never be made to contain, any part of the game.

## Never in this repository, never in a release

- The game's executable (`main`, `sdk`, the other ExeFS modules), in original, rewritten or recompiled form (the
  Windows build's `game.dll`, its objects and its `program.db` included).
- The game's data (`RomFS`, `romfs.img`, any file extracted from it).
- The game's shaders in any form: the shader containers from the game files, runtime shader dumps, lowered
  (Maxwell) programs, translated Metal source, compiled Metal libraries, Vulkan SPIR-V, pipeline caches, or shader
  packages built from them.
- Console firmware files such as the system fonts.
- Keys, tickets, certificates, saves, screenshots of other people's content, or logs that name your files.

Every one of those is produced **on your machine, from your own dump, at build time**, and stays in your
build folder (`<dump folder>_native`). The build takes only a dump you provide yourself, checks it is the version
the recipe is for, and refuses any other. Never share a build folder: it holds the game.

## What is in the repository

- The native host and the iOS app sources (the `nxnative` project, MIT, with the Switch 2 changes made for
  this port), as a fork or as a patch set.
- `ampere-lower`: a standalone build of the Switch 2 → Maxwell shader lowering from the Eden-NS2 emulator
  (GPL-3.0-or-later; its own `LICENSE` and `NOTICE.md` are in that folder).
- The build scripts and the per-game recipe: tables that describe *where* to patch and *how* to treat each
  shader (hashes, slot kinds, formats). They contain no game code.
- `windows/`: the Windows port's source (our own recompiler and runtime, MIT; its third-party code keeps its own
  licence, listed in `windows/docs/PROVENANCE.md`), where it is published.
- Documentation.

## What the downloads contain

Each release has one download per computer, made by `tools/package_release.py` from this repository's files:

- **for Macs**: the repository's tools for a Mac.
- **for Windows PCs**: the repository's tools for Windows, plus `windows/bin`: the Windows game's tools built from
  `windows/` (`nxanalyze`, `nxlift` with LLVM, `nxrt`, SDL3), LLVM's `lld-link`, SPIRV-Tools from the Vulkan SDK and the
  Visual C++ runtime files those two need. `windows/bin/MANIFEST.txt` lists every file with its checksum and origin,
  `windows/bin/THIRD-PARTY-NOTICES.txt` their licences.

The open betas have downloads of their own, released from the `linux-beta` and `android-beta` branches: **for Linux**
(and the Steam Deck), whose tools are in `linux/bin` (`linux/bin/MANIFEST.txt` lists them, `THIRD-PARTY-NOTICES.txt`
beside it their licences), and **for Android**, which makes the Android app on a Windows PC.

None of them contains anything of the game. On Windows the game's code is recompiled, and its shaders translated, on your
PC from your own dump, into your build folder, as the Mac and iPhone builds do.

## What the tool does not do

It decrypts nothing and bypasses nothing. It takes a dump that is already plain files, made by you from your own
console with your own tools, and needs no keys, tickets or certificates. If your dump is encrypted or packaged, this
tool cannot read it and will not try.

## Licences

- `ampere-lower/`: GPL-3.0-or-later (Eden-NS2 code; its `LICENSE` and `NOTICE.md` are in that folder).
- `host/`: the changes are MIT; the nxnative and Ryujinx code they modify or build is MIT under their own notices,
  reproduced in `host/NOTICE.md`. The Switch 2 texture layout in `nxnative-switch2.patch` (`dm/gfx/tex/s2tex.c`,
  `dm/gfx/tex/s2tex.h`, `dm/gfx/msl/s2tex.metal`) is this project's own MIT code, written clean-room from a functional
  specification of the layout and verified bit for bit against the game's textures. It replaces the earlier code,
  which followed Eden-NS2's GPL-3.0-or-later code; none of that remains in the patch.
- `windows/`: MIT; the components it builds in or ships beside it are listed with their licences in
  `windows/bin/THIRD-PARTY-NOTICES.txt` of the Windows download (LLVM: Apache-2.0 with LLVM exception; SDL3 and SDL_GameControllerDB: zlib;
  astc-encoder and SPIRV-Tools: Apache-2.0; bc7enc_rdo: MIT; nxnative: MIT).
- `nupread.py`: this project's own MIT code, written from the NUP format specification (github.com/a55uka/nup,
  `SPEC.md`, MIT). It only reads containers of already-decrypted dumps; no keys are involved.
- Everything else here: MIT (`LICENSE`), the same licence as nxnative.

## Trademarks and affiliation

Pokémon and Pokémon Pokopia are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc. / The Pokémon Company;
Nintendo Switch is a trademark of Nintendo; the game is published by Nintendo and developed by Koei Tecmo. This
project is an independent, non-commercial fan project. It is not affiliated with, endorsed by or supported by any of
them, and the names are used only to say which game it is for.

The screenshots in the README show the game only to document the result. They are not game assets for reuse.

## Rights holders

If you hold rights in anything in this repository or its releases and want it changed or removed, open an issue on
this repository (or contact the account that owns it through GitHub) and say which files. It will be taken care of
promptly, without a formal notice needed.

If you fork this project, keep these rules: they are what makes it publishable.
