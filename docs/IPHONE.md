# Playing on your iPhone

The build makes an app that runs the game's own code natively on the phone (no emulator). Two things about it are
unusual and worth knowing before you start:

1. **Memory.** The game reserves 11 GiB of address space and uses several GB of it. The app therefore carries two
   entitlements, *Increased Memory Limit* and *Extended Virtual Addressing*. Apple grants the second one only to apps
   signed by a **paid Apple Developer Program** team. An app signed with a free Apple ID (AltStore, Sideloadly and the
   like) installs but the game stops at start-up when it cannot reserve its memory. The port was played on an
   iPhone19,4 running iOS 27.2.
2. **Game files.** The IPA holds no game files. The 6 GB RomFS image goes to the app's Documents folder over USB after
   the install, once.

## 1. Build and sign

With your team ID (ten characters, in your Apple developer account under Membership), in a Terminal where the builder's
environment is active (`source ~/.pokopia-native/venv/bin/activate`; the README's "What you need" sets it up):

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

Installing a newer build over the old one keeps the app's Documents (the game files and your saves).

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

The game builds some shaders only while it runs (the opening, the character creator, the first area; 133 are known
so far). The phone cannot compile them, so a draw that needs one that is not in the app yet is skipped, and the
shader is recorded in `Documents/shdump`. To add them:

```bash
python3.12 -W ignore tools/phone_pull.py "<build folder>/phone/run-1"
python3.12 nxs2.py refresh "<build folder>"
python3.12 nxs2.py sign "<build folder>" --team-id <TEAM>      # or: open the refreshed Xcode project
pymobiledevice3 apps install "<build folder>/out/PokemonPokopia-signed.ipa"
```

`refresh` finds `<build folder>/phone/*/shdump` by itself, lowers and compiles the new shaders (a minute or two),
and rebuilds the outputs the build folder has.

## Heat

At 30 fps the phone reaches iOS's "serious" thermal state after about six minutes and stays warm. `DM_RENDER_SCALE=0.8`
is the first thing to try; 0.5 is a big step down in picture quality but runs cool.
