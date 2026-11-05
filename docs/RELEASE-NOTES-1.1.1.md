# pokopia-native 1.1.1

---

A quick fix for the Windows builder, and `.nup` dumps. Thank you all for the huge response to 1.1.0!

## Which download do I need?

The same as before: [Pokopia-Native-for-Windows.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Windows.zip)
on a Windows PC, [Pokopia-Native-for-Mac.zip](https://github.com/xFractals/pokopia-native/releases/latest/download/Pokopia-Native-for-Mac.zip)
on a Mac. The README's "Which download do I need?" has the details.

## Fixed

- **Windows: hundreds of terminal windows during the shader step.** Building from the builder window could open a new
  console window for every tool the shader step ran (there are thousands) and freeze the PC. The build now runs in one
  hidden console that all its tools share. If it happened to you, your build folder is fine: build again with 1.1.1 and
  it carries on where it stopped.

## New

- **`.nup` dumps.** A `.nup` file (an open container for already-decrypted dumps) works as your dump: **.nup file…** in
  the window, or `nxs2.py build <file>.nup`. The builder unpacks the update it needs into the build folder once (about
  7 GB more) and builds exactly as from a dump folder. It needs Python's `zstandard` module:
  `python -m pip install zstandard`. Tested with a `.nup` made by the format's own tool: the game files come out
  byte for byte the same as from the dump folder. Thanks to everyone who asked in #6.

## Upgrading

- Unzip 1.1.1 and use it as before, with the same build folder. Nothing in the game itself changed: the Windows game,
  the Mac app and the iPhone app you built with 1.1.0 stay as they are, and they still play Link Play with 1.1.1 builds.
  There is no need to rebuild or reinstall them.
