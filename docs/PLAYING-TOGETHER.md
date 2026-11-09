# Playing together (Link Play)

The game's own **Link Play** works between Pokopia Native on Windows, Mac, iPhone and Linux, in any mix, up to four players, on the same
Wi-Fi or over the internet, and it never touches Nintendo's servers.

Back to the [overview](../README.md).

**On this page**

- [Before you start](#before-you-start)
- [Which versions play together](#which-versions-play-together)
- [What each device can do](#what-each-device-can-do)
- [On the same Wi-Fi](#on-the-same-wi-fi)
- [Over the internet](#over-the-internet)
- [Through a VPN](#through-a-vpn)
- [Interact everywhere (experimental)](#interact-everywhere-experimental)
- [Story co-op (experimental)](#story-co-op-experimental)
- [On Windows: the window before the game, and F1](#on-windows-the-window-before-the-game-and-f1)
- [Good to know](#good-to-know)
- [Known issues](#known-issues)

## Before you start

- **In the game, always pick Link Play → Local connection.** That's the one that works between our builds. **Online connection** in the
  same menu is Nintendo's own service: here it just goes back to the menu.
- **Every device needs the same game version.** 1.0.5 and 2.0.1 can't play together.
- **Every device needs a matching pokopia-native version** (the builder window's title shows yours): the next section has the rules.
- **Each player needs their own save.** The game refuses a second copy of the same save, so a save copied to a second device can't play
  with itself.
- **The host has reached the point in the game where Link Play opens.**

## Which versions play together

Different pokopia-native versions can often play together, because the game's network code didn't change. The rules:

- **On game version 2.0.1**, 1.3.5 plays with 1.3.4, 1.3.3, 1.3.2, 1.3.1 and 1.3.0, and with 1.2.x and 1.1.x as long as Interact
  everywhere and Story co-op are off.
- **With Interact everywhere on** (and Story co-op off), every device needs 1.2.0 or newer, with it on.
- **With Story co-op on**, every device needs 1.3.0 or newer, with it on.
- **On game version 1.0.5**, Interact everywhere and Story co-op need 1.3.3 or newer on every device. (The 1.0.5 dump's title screen
  says "Ver. 1.0.4": that's the same version.)
- **The Linux beta** (v1.3.3-linux-beta.5) is built on 1.3.3, so it counts as 1.3.3 here.

When in doubt, update every device to the newest release.

## What each device can do

| Device | On the same Wi-Fi | Over the internet | Where you set it up |
|---|---|---|---|
| **Windows** (1.3.5) | finds hosts by itself | host or visit; the game asks the router to open the port | the window before the game starts, and **F1 → Link Play** |
| **Mac and iPhone** (1.3.5) | finds hosts by itself | host (with a port forwarded by hand, or a VPN) or visit | the app's launcher |
| **Linux and Steam Deck** (beta) | hosts, or joins a host by its address; doesn't find hosts by itself yet | host (with a port forwarded by hand) or visit | the Link Play choice before the game starts |
| **Android** (beta) | no Link Play yet | no Link Play yet | |

## On the same Wi-Fi

**Windows:** start the game as usual. The window that opens first asks how you want to play: pick **Link Play with players on this
network**. The first time, Windows Firewall asks whether the game may use networks: allow **Private networks**. Your network must be
Private too (Settings → Network & internet → your connection → Network profile type: **Private**): on a Public network Windows keeps the
other devices out, and the game's log says so.

**Mac and iPhone:** in the launcher (the settings shown before the game starts), turn on **Link Play on this network** and allow local
network access when asked. Leave **Internet play** off. With Link Play on this network off, the game's Link Play does nothing.

**Linux and the Steam Deck:** Linux doesn't find hosts on its own network yet. To host, choose "host here" and give the others this
computer's address; to visit, choose "join a host on this network by its address" and type the host's address. The
[Linux guide](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md#link-play) has the details, including Game Mode on
the Steam Deck.

**In the game:** **Link Play → Local connection**. One player hosts; the others look for towns, and the host's appears in a few seconds.

## Over the internet

Tested: a Windows PC hosting at home, with an iPhone visiting on mobile data. There is no matchmaking server: visitors connect straight to
the host, so the host's router has to let them in.

### 1. The host's router

The host's router must pass Link Play's port, **47600, TCP and UDP**, to the computer that hosts.

- **A Windows host asks the router itself.** While you host, the game asks your router to open the port (through UPnP, NAT-PMP or PCP, the
  ways a router lets a program open a port for itself), and closes it again when you stop. **F1 → Link Play** says whether it worked, and
  what to do if it didn't. Your router needs UPnP (or NAT-PMP or PCP) turned on, and Windows Firewall still asks the first time you host.
  This is new in 1.3.5 and was tested against simulated routers only, so tell us how it goes with yours.
- **By hand,** if the router doesn't do it, or when a Mac, an iPhone or a Linux PC hosts: open the router's "Port forwarding", "NAT" or
  "NAT/Gaming" page and forward port **47600, TCP and UDP**, to the computer that hosts. Check it points at the right device. A Linux PC with
  a firewall (`ufw`, `firewalld`) must allow that port too.
- **When your internet provider shares one internet address among many homes,** no router setting helps; F1 → Link Play says so. Only a VPN
  helps then: [Through a VPN](#through-a-vpn).

### 2. The host gets a room code

The room code is a 16-character password for the session, written `XXXX-XXXX-XXXX-XXXX`. Every visitor needs it.

- **Windows:** pick **Host over the internet or a VPN** in the window before the game starts. The next window shows your room code with a
  **Copy** button; **Keep my room code from last time** saves your friends typing a new one. In the game, **F1 → Link Play** shows it too,
  with **Copy invite**: your address and the code in one line, ready to paste in a chat.
- **Mac:** in the launcher, turn on **Link Play on this network** and **Internet play**, leave **Room code** empty and start the game once.
  The log (`~/Library/Logs/nxnative`) says "this session's room code is …". Type that code into **Room code** to keep it.
- **Linux:** see the [Linux guide](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md#link-play).

Give the visitors the room code and your internet address (search "what is my IP" on the host's network). On Windows, **Copy invite**
has both.

### 3. The visitors

- **Mac and iPhone:** in the launcher, turn on **Link Play on this network**, type the host's internet address into **Join address**, turn
  on **Internet play** and type the room code into **Room code**.
- **Windows:** pick **Visit a host over the internet or a VPN** in the window before the game starts, and type the address and the code.
- **Linux:** see the [Linux guide](https://github.com/xFractals/pokopia-native/blob/linux-beta/docs/LINUX.md#link-play).

### 4. In the game

**Link Play → Local connection**, then look for towns.

## Through a VPN

No access to the router, or an internet provider that shares your address? A VPN app such as Tailscale (free for personal use, on Windows,
Mac and iPhone), Radmin VPN or Hamachi puts your devices on one private network across the internet, with no port forwarding.

Pokopia Native counts a VPN's addresses (Tailscale's 100.x.y.z, Radmin VPN's 26.x.y.z, Hamachi's 25.x.y.z) as internet addresses, so play
**over the internet**:

- The host hosts with Internet play: on Windows, **Host over the internet or a VPN**.
- The visitors use the host's **VPN address** as the address, with Internet play and the room code: on Windows, **Visit a host over the
  internet or a VPN**.
- **Link Play with players on this network** doesn't reach through a VPN. If you give the Windows launcher a VPN address without internet
  play, it says so before the game starts, and the log names the VPN.
- On Windows, if the visitors can't get in, set the VPN's network to Private as well (Settings → Network & internet).

## Interact everywhere (experimental)

Outside Palette Town the game puts everyone in a Link Play session in Spectator Mode: visitors can walk around and watch, but certain moves
and items can't be used, and visitors can't build. **Interact everywhere** lifts that: in every town, a session gets the rules Palette Town
has with Spectator Mode off, so visitors can water, pick up and place things, and build alongside the host.

- **Turn it on on every device.** Mac and iPhone: **Interact everywhere** in the launcher (under Link Play). Windows: the **Interact
  everywhere (experimental)** box in the window before the game starts. Linux: the Link Play choice asks for it. A device with it on only
  sees hosts that have it on too.
- The game still shows its own words: the host's settings show Spectator Mode On (greyed out), and the visitor is told everyone will be
  restricted. The session is not restricted.
- As in any Link Play visit, the town and its progress (challenges, building) belong to the host, and what a visitor picks up goes home
  with them.
- It works on game versions 2.0.1 and 1.0.5 (on 1.0.5, every device needs 1.3.3 or newer).
- It is experimental: keep a copy of your save before a long session.

## Story co-op (experimental)

In a story town the game lets only the host talk to the town's story characters, such as Professor Tangrowth: a visitor gets the "Talk"
prompt, and nothing happens. **Story co-op** lifts that, on top of Interact everywhere: visitors talk to the story characters and hear the
same story as the host, including the host's current requests.

- **What counts for the host:** what visitors do in the world, as with Interact everywhere: building, watering, placing and picking up,
  and the host's challenges (habitats too, we expect, but that isn't tested end to end yet).
- **What doesn't:** story requests count only for the host. A visitor can hear a request, but their conversation doesn't complete it; the
  host has to do that part. A visitor's shop purchases go to their own town, so for a building request the host buys the kit and drops it
  for the visitor.
- **Turn it on on every device.** Mac and iPhone: **Story co-op** in the launcher (under Link Play). Windows: the **Story co-op
  (experimental)** box in the window before the game starts. Linux: the Link Play choice asks for it. It turns Interact everywhere on with
  it. A device with it on only sees hosts that have it on too.
- It works on game versions 2.0.1 and 1.0.5 (on 1.0.5, every device needs 1.3.3 or newer).
- It is experimental: it was tested with two players in Withered Wasteland. Keep a copy of your save before a long session, and tell us
  what does or doesn't work.

## On Windows: the window before the game, and F1

**Before the game starts,** a small window asks how you want to play: **Play alone**, **Link Play with players on this network**, **Host
over the internet or a VPN** or **Visit a host over the internet or a VPN**, with the **Interact everywhere (experimental)** and **Story
co-op (experimental)** boxes under them. It remembers your choice. **Don't ask again** starts the same way every time; **Pokopia Settings
<version>.cmd**, beside the Play launcher in your build folder's `out` folder, shows the window again (so does holding **Shift** while
starting the game).

**While you play,** press **F1** (or **Minus + Plus** together on a controller) and open the **Link Play** tab. It shows:

- how you're playing, and who is connected;
- the addresses to give your friends. It lists every network adapter's address, virtual ones too (WSL, Hyper-V, VirtualBox, VMware). The
  first one is the one your PC uses for the internet: give your friends that one, or your VPN address for VPN play;
- the room code, with **Copy** and **Copy invite**;
- when you host over the internet, whether the router opened the port, and what to do if it didn't.

**Change how you play,** in the same tab, switches the way to play, the host's address and the room code without going back to the
launcher. These apply at once while the game's Link Play is closed. Turning Link Play on or off, Interact everywhere and Story co-op apply
from the next start. The launcher remembers your choice.

The switches for starting the game from a terminal (`-LinkPlay`, `-Join`, `-Internet`, `-Room`, `-Interact`, `-Story` and more) are in the
[Windows guide](WINDOWS.md).

## Good to know

- **Internet play must match on every device.** With Internet play on, a device only sees hosts that have it on too, with the same room
  code. For players on the same Wi-Fi, turn Internet play off everywhere; otherwise the game says the destination cannot be found.
- **The iPhone app reads its settings when it starts.** After changing them, close the app completely (swipe it away in the app switcher)
  and open it again.
- **Several network adapters** on a Windows PC (Wi-Fi and Ethernet, an iPhone plugged in by USB, a virtual machine's adapter) are fine: the
  game tells the others all its home-network addresses, and they pick the one they can reach.
- **Security:** every connection must prove the room code, and every message is protected against changes, but the traffic **is not
  encrypted**. If you forwarded the port by hand, remove the forward when you're done hosting.
- **Something not working?** The logs say what happened, in the lines about `coop`: who connected, a wrong room code, a different
  pokopia-native version on the other side. Windows: the folder its console window names
  (`%LOCALAPPDATA%\Pokopia Native\<game version>\logs`); Mac: `~/Library/Logs/nxnative`; iPhone: the Files app → On My iPhone → the app →
  `log.txt`. Since 1.3.5 the Windows log holds only the first group of your room code, so it's safe to attach to a report
  ([Troubleshooting](TROUBLESHOOTING.md#logs-and-how-to-report-a-problem)).

## Known issues

- **Story co-op:** while a Link Play session is open, a Pokémon with a "Time to get chatty!" request can ask the host to "bring" themselves
  over. That's the game's own line; end the session and talk to it again alone. A fix is being worked on.
- **Interact everywhere and Story co-op on 1.0.5** were checked in the game's code and started on Windows, but not yet played in a session
  with two players. Please report how they go.
- **The in-game Link Play tab and automatic port forwarding are Windows only** for now. The Mac and iPhone get their own settings window in
  a later update; until then, hosting over the internet from them needs a port forwarded by hand, or a VPN.
- **The Android beta has no Link Play yet.**
