# Automatic port forwarding (stage 3 of the in-game Link Play work)

Branch `linkplay-ui`, 2026-11-07. Shared coop code (`src/coop/coop_nat.c`), so every platform can use it; nxrt (Windows, Linux) uses it now.

## What it does

When this device hosts over the internet (internet play on, hosting here rather than through a relay), the coop node asks the router to forward
its hosting port (47600, or the one the system picked), **TCP and UDP**, to this device:

1. **NAT-PMP** (RFC 6886) to the default gateway's UDP port 5351: the router's internet address, then the two mappings.
2. **PCP** (RFC 6887, the MAP opcode) when the router answers NAT-PMP with "another version", or doesn't answer it.
3. **UPnP IGD**: SSDP M-SEARCH for an Internet Gateway Device, its description (the WANIPConnection:2/1 or WANPPPConnection:1 service and its
   control URL), then GetExternalIPAddress and AddPortMapping (TCP, UDP; description "Pokopia Native").

Each step has a short timeout (NAT-PMP and PCP: 250, 500, 1000 ms; SSDP 2.5 s; HTTP 3-4 s), and a worker thread does the talking, so hosting
never waits for it. Mappings have a **one-hour lease**, renewed at half time while hosting. A UPnP router that only takes permanent mappings (error
725) gets one without a lease, added again daily. When hosting ends (the game leaves Local connection) the worker gives the ports back
(NAT-PMP / PCP lifetime 0, UPnP DeletePortMapping); when the game window closes, `dm_coop_quit` gives them back at once (`ExitProcess` runs no
`atexit`). After a crash the lease runs out within the hour.

`DM_COOP_NAT=0` turns it off. Nothing is asked of a third party: the internet address shown is the one the router reports itself.

## What the player is shown

The F1 menu's Link Play tab (docs/in-game-menu.md), while hosting over the internet:

| State | The tab says |
|---|---|
| asking | "Asking your router to forward port 47600 to this PC (UPnP, NAT-PMP or PCP)..." |
| forwarded | the router's internet address first in "Give your friends" ("203.0.113.7 (internet, from your router)", with the port when it isn't 47600), "Your router forwards port 47600 to this PC (UPnP) while you host", and Copy invite uses that address |
| carrier-grade NAT (the router's own address in 100.64/10) | "Your internet provider shares one internet address among many homes (carrier-grade NAT: your router's own address is 100.x.y.z), so nobody reaches you from the internet. Use a VPN (Tailscale, Radmin VPN, Hamachi)." |
| a second router in front (the router's own address is private) | "Another router is in front of yours (your router's own address, 192.168.x.y, is a home-network one). Forward port 47600 on that one too, or use a VPN." |
| the port forwarded to another device (UPnP 718) | "Your router already forwards port 47600 to another device: remove that forward in the router, or host from that device. Or use a VPN." |
| refused (NAT-PMP 2, a UPnP error) | "Your router refused to forward port 47600 automatically: forward it (TCP and UDP) to this PC in its settings, or use a VPN." |
| no answer, or no router found | "Your router didn't answer UPnP or NAT-PMP: turn UPnP on in its settings, or forward port 47600 (TCP and UDP) to this PC by hand, or use a VPN." (with a VPN's address on this PC, the VPN's note instead) |

The log has a `[coop] nat:` line for each result ("port 47600 (TCP and UDP) forwarded here by the router (UPnP, lease 3600 s); its internet
address 203.0.113.7", "port 47600 not forwarded: ...", "port 47600 given back to the router").

Not detectable from here: a second NAT beyond a router that reports a public address, and a firewall at the provider. The tab can't know those; a
friend who still can't connect is the sign.

## How it is built

- `coop_nat.c`: the protocols and the worker. `cnat_want(node, port)` (from `cmp_host_open` / `cmp_host_close`, under the node lock) starts or ends
  the forwarding; `cnat_stop` (coop_destroy) and `coop_nat_quit` (the game quits) give it back. What a router sends is checked before use: lengths,
  the answer's source (the gateway's address and port 5351), the nonce and protocol of a PCP answer, URLs (IPv4 literals only), text cut to the buffers.
- `cplat_gateway` (coop_plat.c): Windows `GetBestRoute` towards 8.8.8.8; Linux `/proc/net/route`; elsewhere (Apple) the route's address with .1.
- `coop_info.nat` carries the state to the tab (front/linkplay.c words it).
- Mac and iPhone: `apply_coop.py` copies `coop_nat.c` in (`hle_coop_nat.c`) with the rest, and their glue (`ldn_glue.c`) gives the mapping back
  through `atexit` when the app quits: macOS quits through `exit()`; iOS usually ends an app without it, and the lease runs out within the hour.

Our own implementation, written from the RFCs and the UPnP Forum's published specifications; no library (miniupnpc and libnatpmp were allowed, not
needed), so nothing to add to the notices.

## Tests

`coop.nat` (coop_test_nat.c, 114 checks): the URL, SSDP and XML pieces; NAT-PMP and PCP against small routers on loopback (the address, a mapping, its
deletion, an answer from another address ignored, a PCP-only router); a host node over the internet with its router given (`coop_cfg.nat_gw`,
`nat_gport`, `nat_location`; "-" no UPnP, so a test never sends SSDP to the real network): UPnP with a chunked description (mapped TCP and UDP for
127.0.0.1, given back when hosting ends), permanent leases only (725), the port taken (718), carrier-grade NAT, a second router, NAT-PMP, PCP (given
back by `coop_nat_quit`), and no router at all. `nxrt.linkplay` checks the tab's words for each state.

**Not yet tried against a real router.** Trying it adds a port forward of 47600 to this PC on the router for as long as a test host runs (then it is
removed); that needs the user's OK first.
