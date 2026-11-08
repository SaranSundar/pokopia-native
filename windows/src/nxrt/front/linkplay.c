// The in-game Link Play tab's words (linkplay.h). Our own code, MIT.
#include "linkplay.h"
#include <stdio.h>
#include <string.h>

static void ip_str(char *out, size_t n, uint32_t ip) { snprintf(out, n, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 0xFF, ip >> 8 & 0xFF, ip & 0xFF); }
// The menu's font has ASCII only: each UTF-8 sequence beyond it becomes one '?'
static void ascii_name(char *out, size_t n, const char *in) {
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && k + 1 < n; p++) {
        if (*p < 0x80) out[k++] = (char)*p;
        else if ((*p & 0xC0) != 0x80) out[k++] = '?';   // a sequence's first byte; its continuation bytes are skipped
    }
    out[k] = 0;
}
// What an address is to a player: this network, a VPN's range, or the internet; NULL for one not worth giving (link-local)
static const char *addr_kind(uint32_t ip) {
    int c = coop_addr4_class(ip);
    if (c == COOP_ADDR_PRIVATE) return "this network";
    if (c != COOP_ADDR_PUBLIC) return NULL;
    const char *v = coop_addr4_vpn(ip);   // " (Radmin VPN's range)"
    return *v ? v : "internet";
}

void lp_view_make(lp_view *v, const coop_info *i, const uint32_t *addrs, int naddr) {
    memset(v, 0, sizeof *v);
    char hip[24] = "", t[96];
    if (!i) {
        snprintf(v->mode, sizeof v->mode, "Alone: Link Play is off for this game.");
        snprintf(v->now, sizeof v->now, "In the game, Link Play doesn't find anyone.");
        snprintf(v->addr_note, sizeof v->addr_note, "To play together, choose a way to play in the launcher's window (Pokopia Settings), then start the game again.");
        return;
    }
    if (i->join_ip) ip_str(hip, sizeof hip, i->join_ip);
    int test = i->scope == COOP_SCOPE_LOOPBACK, visits = i->join_ip != 0 && !i->hosting, can_host = !visits && i->role != COOP_ROLE_JOIN;
    // how this game plays
    if (test && i->role == COOP_ROLE_HOST) snprintf(v->mode, sizeof v->mode, "A test link on this PC%s.", i->internet ? ", with a room code" : "");
    else if (test) snprintf(v->mode, sizeof v->mode, "A test link to this PC (port %u).", i->join_port);
    else if (i->internet) { if (visits) snprintf(v->mode, sizeof v->mode, "Visiting %s over the internet or a VPN.", hip); else snprintf(v->mode, sizeof v->mode, "Hosting over the internet or a VPN."); }
    else if (visits) snprintf(v->mode, sizeof v->mode, "Visiting %s on this network.", hip);
    else snprintf(v->mode, sizeof v->mode, "On this network (the same Wi-Fi): host, or visit a town nearby.");
    // what is happening
    const char *host_name = i->nplayers > 0 && i->player[0].node == 0 ? i->player[0].name : "";
    switch (i->ldn) {
    case COOP_LS_AP: snprintf(v->now, sizeof v->now, "Opening your town..."); break;
    case COOP_LS_AP_CREATED:
        if (i->nplayers > 1) snprintf(v->now, sizeof v->now, "Your town is open: %d players.", i->nplayers);
        else snprintf(v->now, sizeof v->now, "Your town is open. Waiting for friends to join.");
        break;
    case COOP_LS_STATION:
        if (i->found) snprintf(v->now, sizeof v->now, "Looking for towns: %d found.", i->found);
        else if (i->join_ip) snprintf(v->now, sizeof v->now, "Looking for the town at %s...", hip);
        else snprintf(v->now, sizeof v->now, "Looking for towns nearby...");
        break;
    case COOP_LS_STATION_CONNECTED: ascii_name(t, sizeof t, host_name); snprintf(v->now, sizeof v->now, "Visiting %s's town.", *t ? t : "the host"); break;
    case COOP_LS_ERROR: snprintf(v->now, sizeof v->now, "Link Play stopped with an error. The log says why."); break;
    default: snprintf(v->now, sizeof v->now, "Not in a session. In the game: Link Play, then Local connection."); break;
    }
    // what to give a friend
    if (visits) snprintf(v->addr_note, sizeof v->addr_note, "You're visiting: the host gives you their address%s.", i->internet ? " and room code" : "");
    else if (can_host && !test) {
        int vpn = 0, pub = 0;
        const coop_nat_info *nt = &i->nat; char ra[32] = "";
        int router_ok = i->internet && nt->state == COOP_NAT_MAPPED && nt->problem == COOP_NATP_NONE && nt->external;
        if (router_ok) {   // the router's own internet address, its port forwarded here: what friends over the internet use
            ip_str(ra, sizeof ra, nt->external);
            if (nt->port && nt->port != COOP_PORT_DEFAULT) snprintf(ra + strlen(ra), sizeof ra - strlen(ra), ":%u", nt->port);
            snprintf(v->addr[v->naddr++], sizeof v->addr[0], "%s (internet, from your router)", ra);
        }
        for (int k = 0; k < naddr; k++) {
            const char *kind = addr_kind(addrs[k]);
            if (!kind) continue;
            int is_vpn = kind[0] == ' ', is_pub = kind[0] == 'i';
            vpn += is_vpn; pub += is_pub;
            if ((!i->internet && (is_vpn || is_pub)) || v->naddr == LP_ADDRS) continue;   // on this network only home-network addresses work
            char a[32]; ip_str(a, sizeof a, addrs[k]);
            if (i->port && i->port != COOP_PORT_DEFAULT) snprintf(a + strlen(a), sizeof a - strlen(a), ":%u", i->port);   // the system picked another port
            if (is_vpn) snprintf(v->addr[v->naddr++], sizeof v->addr[0], "%s%s", a, kind);
            else snprintf(v->addr[v->naddr++], sizeof v->addr[0], "%s (%s)", a, kind);
        }
        uint16_t port = i->port ? i->port : COOP_PORT_DEFAULT;
        char ext[24] = ""; if (nt->external) ip_str(ext, sizeof ext, nt->external);
        static const char *const how[4] = {"", "NAT-PMP", "PCP", "UPnP"};
        if (i->internet && nt->state == COOP_NAT_TRYING)
            snprintf(v->addr_note, sizeof v->addr_note, "Asking your router to forward port %u to this PC (UPnP, NAT-PMP or PCP)...", nt->port ? nt->port : port);
        else if (router_ok)
            snprintf(v->addr_note, sizeof v->addr_note, "Your router forwards port %u to this PC (%s) while you host. Give friends over the internet the first "
                     "address with the room code%s.", nt->port, how[nt->method & 3], vpn ? "; through a VPN, its address" : "");
        else if (i->internet && nt->problem == COOP_NATP_CGNAT)
            snprintf(v->addr_note, sizeof v->addr_note, "Your internet provider shares one internet address among many homes (carrier-grade NAT: your router's "
                     "own address is %s), so nobody reaches you from the internet. Use a VPN (Tailscale, Radmin VPN, Hamachi).", ext);
        else if (i->internet && nt->problem == COOP_NATP_DOUBLE_NAT)
            snprintf(v->addr_note, sizeof v->addr_note, "Another router is in front of yours (your router's own address, %s, is a home-network one). Forward port "
                     "%u on that one too, or use a VPN.", ext, port);
        else if (i->internet && nt->problem == COOP_NATP_CONFLICT)
            snprintf(v->addr_note, sizeof v->addr_note, "Your router already forwards port %u to another device: remove that forward in the router, or host "
                     "from that device. Or use a VPN.", port);
        else if (i->internet && nt->problem == COOP_NATP_REFUSED)
            snprintf(v->addr_note, sizeof v->addr_note, "Your router refused to forward port %u automatically: forward it (TCP and UDP) to this PC in its "
                     "settings, or use a VPN.", port);
        else if (i->internet && (nt->problem == COOP_NATP_NO_SUPPORT || nt->problem == COOP_NATP_NO_GATEWAY) && !vpn)
            snprintf(v->addr_note, sizeof v->addr_note, "Your router didn't answer UPnP or NAT-PMP: turn UPnP on in its settings, or forward port %u (TCP and "
                     "UDP) to this PC by hand, or use a VPN.", port);
        else if (i->internet && !vpn && !pub)
            snprintf(v->addr_note, sizeof v->addr_note, "Over the internet, give your internet address (search \"what is my IP\" on this network) and the room code; "
                     "your router forwards port %u to this PC.", port);
        else if (i->internet)
            snprintf(v->addr_note, sizeof v->addr_note, "Give the address your friends can reach with the room code: a VPN's through that VPN, else your internet address "
                     "(port %u forwarded to this PC).", port);
        else if (vpn)
            snprintf(v->addr_note, sizeof v->addr_note, "Friends on the same Wi-Fi find your town by themselves. Through a VPN, every player needs internet play.");
        else snprintf(v->addr_note, sizeof v->addr_note, "Friends on the same Wi-Fi find your town by themselves; the address is for visiting by address.");
    }
    // the room code
    if (i->internet && i->room[0]) {
        snprintf(v->room, sizeof v->room, "%s", i->room);
        snprintf(v->room_note, sizeof v->room_note, "%s", can_host ? "Give it to your friends with the address." : "The host's room code.");
        if (can_host) {
            const char *a = NULL; char ab[32];
            const coop_nat_info *nt = &i->nat;
            if (nt->state == COOP_NAT_MAPPED && nt->problem == COOP_NATP_NONE && nt->external) {   // the router's address, forwarded: first
                ip_str(ab, sizeof ab, nt->external);
                if (nt->port && nt->port != COOP_PORT_DEFAULT) snprintf(ab + strlen(ab), sizeof ab - strlen(ab), ":%u", nt->port);
                a = ab;
            }
            for (int k = 0; k < naddr && !a; k++) { const char *kind = addr_kind(addrs[k]); if (kind && *kind == ' ') { ip_str(ab, sizeof ab, addrs[k]); a = ab; } }   // a VPN's first
            for (int k = 0; k < naddr && !a; k++) { const char *kind = addr_kind(addrs[k]); if (kind && kind[0] == 'i') { ip_str(ab, sizeof ab, addrs[k]); a = ab; } }
            snprintf(v->invite, sizeof v->invite, "Join my Pokopia town: address %s, room code %s (Pokopia Native: Visit a host over the internet or a VPN)",
                     a ? a : "(my internet address)", i->room);
        }
    }
    // who is there
    for (int k = 0; k < i->nplayers && k < COOP_INFO_PLAYERS; k++) {
        const coop_player *p = &i->player[k]; char a[24] = "";
        ascii_name(t, sizeof t, p->name);
        if (p->ip) ip_str(a, sizeof a, p->ip);
        snprintf(v->player[v->nplayers++], sizeof v->player[0], "%s%s%s%s%s", *t ? t : "(no name)", p->node == 0 ? " (host)" : "", p->self ? " (you)" : "",
                 *a ? ", " : "", a);
    }
}
