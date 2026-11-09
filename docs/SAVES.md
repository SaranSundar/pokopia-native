# Moving your save

Where your Pokopia save lives on each platform, and how to copy it between Eden, Windows, Mac, iPhone and Linux.

Back to the [overview](../README.md).

**On this page**

- [A save is three folders](#a-save-is-three-folders)
- [Before you copy](#before-you-copy)
- [Where saves live](#where-saves-live)
- [Into the Windows game](#into-the-windows-game)
- [Into the Linux game](#into-the-linux-game)
- [Into the Mac app](#into-the-mac-app)
- [Into or out of the iPhone app](#into-or-out-of-the-iphone-app)
- [Into an emulator](#into-an-emulator)
- [Coming from Eden](#coming-from-eden)
- [The Android beta](#the-android-beta)
- [Updating without losing your save](#updating-without-losing-your-save)

## A save is three folders

A Pokopia save is three folders: **`B00`, `G00` and `S00`**. Moving a save means copying those three folders from one place to another,
and the same save works everywhere: in an emulator, the Windows game, the Mac app, the iPhone app and the Linux game.

## Before you copy

- **Close the game on both sides.**
- **Keep a copy of the save you're about to replace.** A copy replaces it; saves don't merge.
- **A save that game version 2.0.1 has written may not load in 1.0.5.** Keep each version's save apart; on Windows and Linux they are, by
  default.
- **Link Play:** a copied save is still the same save, and the game won't let two copies of one save play together. For co-op, each player
  needs a save of their own.

## Where saves live

| Where | The save folder (the one holding `B00`, `G00` and `S00`) |
|---|---|
| **Eden**, and emulators built on it such as **Eglantine** | Right-click Pokémon Pokopia in the game list → **Open Save Data Location**. On Windows, Eden's is usually `%APPDATA%\eden\nand\user\save\0000000000000000\<a long user ID>\0400A3A01EE4C000`; a portable install keeps it in the `user` folder next to the program. |
| **Windows game** | `%APPDATA%\Pokopia Native\<game version>\save`, for example `…\Pokopia Native\2.0.1\save` |
| **Mac app** | `~/Library/Application Support/nxnative/0400A3A01EE4C000/save` (Finder → Go → Go to Folder…) |
| **iPhone app** | the `save` folder in the app's Documents, through file sharing ([below](#into-or-out-of-the-iphone-app)) |
| **Linux and Steam Deck** (beta) | `~/.local/share/pokopia-native/<game version>/save` |
| **Android** (beta) | inside the app's folder on the phone ([below](#the-android-beta)) |

## Into the Windows game

In the builder, press **Bring my save** (on the page it shows after a build) or **Import save…** (in Advanced):

- **Yes** takes Eden's save on this PC by itself. Eden's save is only read, never changed.
- **No** lets you choose any save folder: another emulator's, or one you copied off the iPhone, a Mac or Linux.

From a terminal: `python nxs2.py import-save "<build folder>" --eden` for Eden's save, or
`python nxs2.py import-save "<build folder>" "<the folder with B00, G00 and S00>"` for any other.

Build the Windows game first: the save goes beside it. The save that was there is kept beside it as `save.before-<date>`, never deleted.
Saves go the other way by copying the folder: Windows saves load on the Mac and the iPhone.

## Into the Linux game

The Linux builder's last page has **Bring my save**, for a save from Eden, a Mac or the phone. From a terminal, `nxs2.py import-save` works
as on Windows (`--eden` finds Eden's own save), and the save that was there is kept beside it. The
[Linux guide](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md#play) has the exact command.

## Into the Mac app

Quit the app, then copy the three folders into its save folder (above). Make the `save` folder if it isn't there yet. The Mac builder has no
import button: copying is all it takes.

## Into or out of the iPhone app

Fully close the app on the phone first (swipe it away in the app switcher), and open it again when you're done.

- **With a Mac:** connect the phone and select it in Finder → **Files** → the Pokopia app. Drag its `save` folder out: that's your backup,
  or the save to move. To put one in, drag a folder named `save` holding `B00`, `G00` and `S00` onto the app and choose to replace.
- **With Windows:** the **Apple Devices** app from the Microsoft Store (or iTunes) → your iPhone → **Files** → the Pokopia app, the same way.
- **From the command line:** `tools/phone_pull.py` copies the app's Documents to your computer over USB, the `save` folder included
  ([the iPhone guide](IPHONE.md#5-shaders-recorded-on-the-phone) shows the command).

## Into an emulator

Open the emulator's save folder with **Open Save Data Location** (start the game there once first if the folder doesn't exist yet) and copy
the three folders in.

## Coming from Eden

- **Windows:** **Bring my save** (or **Import save…**) → **Yes** takes Eden's save on the same PC.
- **Linux:** **Bring my save**, or `import-save` with `--eden`.
- **Mac and iPhone:** open Eden's save folder (**Open Save Data Location**), then copy `B00`, `G00` and `S00` in as above.

## The Android beta

The Android app keeps its save inside its own folder on the phone. **Bringing a save over to it isn't in this beta yet.** Uninstalling the
app deletes its save, so never uninstall it to update: build again with the same build folder and install over it. The
[Android guide](https://github.com/xFractals/pokopia-native/blob/android-beta/docs/ANDROID.md) has the steps.

## Updating without losing your save

- **Windows, Mac and Linux:** the save lives outside the game and the app, so building a new version keeps it.
- **iPhone:** install the new app **over** the old one. Installing over it keeps the app's Documents, which hold your save and the game
  files. **Never delete the app first: deleting an iPhone app deletes its save with it.**
- **Android:** never uninstall the app to update it: uninstalling deletes the save. Keep your build folder too: it holds the key every
  update of the app needs.
- Backing your save up first never hurts.
