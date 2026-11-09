# Playing on your iPhone

The build makes an app that runs the game's own code natively on the phone (no emulator). Two things about it are
unusual and worth knowing before you start:

1. **Memory.** The game reserves 11 GiB of address space and uses several GB of it. The app therefore carries two
   entitlements (permissions Apple writes into an app's signature), *Increased Memory Limit* and *Extended Virtual
   Addressing*. Apple grants the second one only to apps signed by a **paid Apple Developer Program** team. An app
   signed with a free Apple ID (AltStore, Sideloadly and the like) installs but the game stops at start-up when it
   cannot reserve its memory. The port was played on an iPhone19,4 running iOS 27.2.
2. **Game files.** The IPA holds no game files. The 6 GB RomFS image goes to the app's Documents folder over USB after
   the install, once.

**No workaround has been tested.** This app has only ever been signed with a paid team. If you know a way to sign it with a
profile that carries Extended Virtual Addressing without one, you're free to try it, and please say in the
[issues](https://github.com/xFractals/pokopia-native/issues/new/choose) whether it worked; but don't count on it.

**Memory on the phone itself matters too.** The game uses a lot of it: on a phone with less memory than the recent Pro
models, expect trouble.

**No Mac?** A Windows PC builds and signs the iPhone app too: [Building on Windows](WINDOWS.md#the-iphone-app). A PC
can't run the game, so it can't record the shaders the game makes while it runs (section 5): the first time, the title
screen on the phone is black apart from its text until you've played a few minutes, pulled the recordings over and
refreshed once. The Windows guide has those steps; installing and the game files work as below.

Back to the [overview](../README.md).

## 1. Build and sign

**With the builder:** pick **Play on your iPhone** on its first page ([the Mac guide](MAC.md#build-with-the-step-by-step-builder) shows each
page). When the build is done, **Sign the iPhone app** signs it with your Apple Development certificate (a `.p12` file) and a development
provisioning profile (a `.mobileprovision` file), the same two files as on Windows: [how to get them](WINDOWS.md#sign-and-install). To sign
with Xcode instead, use **Advanced**: tick **Xcode project, team ID:**, enter your team ID, then **Build** and **Sign IPA**.

**From Terminal,** with your team ID (ten characters, in your Apple developer account under Membership), in a Terminal where the builder's
environment is active (`source ~/.pokopia-native/venv/bin/activate`; [the Mac guide](MAC.md#the-builders-python-environment) sets it up):

```bash
python3.12 nxs2.py build "<your dump folder>" --xcode --team-id <TEAM>
python3.12 nxs2.py sign "<build folder>" --team-id <TEAM>
```

`sign` archives and exports a development build with Xcode's automatic signing. Before the first run: your Apple ID is
signed into Xcode (Xcode → Settings → Accounts) and your iPhone's UDID is registered on the team (developer.apple.com →
Devices; Xcode does this itself when the phone is plugged in and the project is opened once). Run `sign` the first
time from Terminal on the Mac itself, since Xcode may ask for the keychain. The output is
`<build folder>/out/PokemonPokopia-signed.ipa`.

If you prefer Xcode: open `<build folder>/xcode/DmIOS.xcodeproj`, pick your team, plug the phone in, press Run.

Without a paid team, `--ipa` makes `PokemonPokopia-unsigned.ipa`: its executable carries the two entitlements ad-hoc,
for a re-signing tool that keeps entitlements. Whether the game then starts depends on the profile that tool
obtains; a free profile does not carry Extended Virtual Addressing.

## 2. Install

On the Mac, with the phone plugged in, unlocked and trusted, Developer Mode on (Settings → Privacy & Security):

```bash
source ~/.pokopia-native/venv/bin/activate   # pymobiledevice3 goes into the builder's environment too
pip install pymobiledevice3
pymobiledevice3 apps install "<build folder>/out/PokemonPokopia-signed.ipa"
```

Installing a newer build over the old one keeps the app's Documents (the game files and your saves). **Never delete the
app first: deleting an iPhone app deletes its save with it.**

## 3. The game files

```bash
python3.12 -W ignore tools/phone_setup.py --romfs "<build folder>/extract/RomFS.img"
```

About three minutes over USB 3. It also writes `Documents/env.txt` with shader recording on (below).

## 4. Settings: `Documents/env.txt`

One `KEY=VALUE` per line; the app reads it at start and it overrides the build's defaults. Useful ones:

| Switch | Meaning |
|---|---|
| `DM_FPS=30` | frame-rate cap (default in the build: 30, the Switch 2's handheld rate for this game; 60 runs hotter) |
| `DM_OPMODE=0` | handheld mode (0, default in the build) or docked (1): the game's own resolution choice |
| `DM_RENDER_SCALE=0.8` | render at a fraction of the game's resolution (0.5 to 1.0); cooler phone, softer picture |
| `DM_TOUCH_AUTORUN=1` | the touch stick runs when pushed to its edge (the port's own option) |
| `DM_LOOK_SCALE=1.0` | look sensitivity of drags on the right half of the screen, a multiple of the default (the launcher's "Look sensitivity") |
| `DM_LOOK_INVERT_Y=1` | drag up to look down (the launcher's "Invert look up and down") |
| `DM_NVN_SHDUMP=shdump` | record shaders the game builds while running (on by default; see below) |
| `DM_SHOW_FPS=1` | frame counter on screen |

You can edit the file from the Mac: `pymobiledevice3 apps` ... or simply run `phone_setup.py --env my_env.txt`.

## 5. Shaders recorded on the phone

The game builds some shaders only while it runs (the opening, the character creator, the first area; 140 are known
on game version 1.0.5, and 2.0.1 makes 14 more). The phone cannot compile them, so a draw that needs one that is not in
the app yet is skipped, and the shader is recorded in `Documents/shdump`. That is why the title screen is black apart
from its text on the very first launch: it uses about 20 of them. To add them:

```bash
python3.12 -W ignore tools/phone_pull.py "<build folder>/phone/run-1"
python3.12 nxs2.py refresh "<build folder>"
python3.12 nxs2.py sign "<build folder>" --team-id <TEAM>      # or: open the refreshed Xcode project
pymobiledevice3 apps install "<build folder>/out/PokemonPokopia-signed.ipa"
```

`refresh` finds `<build folder>/phone/*/shdump` by itself, lowers and compiles the new shaders (a minute or two),
and rebuilds the outputs the build folder has. In the builder, **Refresh the graphics** (Advanced: **Refresh shaders**)
does the `refresh` step once the recordings are pulled over; sign and install the new app afterwards. If you also play
the Mac app or the Windows game from the same build folder, their recordings count too.
[How it works](HOW-IT-WORKS.md#the-shaders) explains these shaders.

## Controls

- **Look around:** drag anywhere on the right half of the screen that isn't a button, and the camera follows your finger.
- **Move:** the touch stick. With `DM_TOUCH_AUTORUN=1`, pushing it to its edge makes you run.
- **Look sensitivity** and **Invert look up and down** are in the app's launcher (the settings it shows before the game
  starts) under Controls, or `DM_LOOK_SCALE` and `DM_LOOK_INVERT_Y` in `env.txt`.
- **For the smoothest look,** turn the game's own **Camera Auto-Follow** off (in the game's Settings, Camera tab). With it
  on, the camera turns in small steps while you walk sideways.

The controls can't be changed on the iPhone yet; its own settings window is coming in a later update.

## Heat

At 30 fps the phone reaches iOS's "serious" thermal state after about six minutes and stays warm. `DM_RENDER_SCALE=0.8`
is the first thing to try; 0.5 is a big step down in picture quality but runs cool.

## Updating

1. Build the new app with the new download: the builder offers **Update my game**, or run the same `build` command.
   Only what changed is redone, and the shaders you have recorded stay.
2. Run `refresh` (**Refresh the graphics**) again, then sign the new app.
3. Install it **over** the old app with `pymobiledevice3 apps install`. That keeps the app's Documents: your save and
   the game files. **Never delete the app first: deleting an iPhone app deletes its save with it.**
4. The game files only need copying again if you moved to another game version (1.0.5 to 2.0.1, say): run
   `phone_setup.py --romfs` once more. Same game version, same dump: nothing to copy.

Backing your save up first never hurts: [Moving your save](SAVES.md#into-or-out-of-the-iphone-app). Link Play needs
matching versions on every device, so update them all.

## Link Play, your save and the log

- **Link Play** is set in the app's launcher, under Link Play: [Playing together](PLAYING-TOGETHER.md). The app reads its
  settings when it starts: after changing them, close the app completely (swipe it away in the app switcher) and open it
  again. Hosting over the internet from an iPhone needs a port forwarded by hand on the router, or a VPN.
- **Your save** is the `save` folder in the app's Documents: [Moving your save](SAVES.md).
- **The log** is in the Files app → On My iPhone → the app → `log.txt`. Problems and answers:
  [Troubleshooting](TROUBLESHOOTING.md#iphone).
