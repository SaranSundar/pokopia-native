// The in-game Link Play tab's words (front/linkplay.c) for each way of playing: alone, this network, hosting over the internet or a VPN, visiting
#include "linkplay.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define HAS(s, sub) CHECK(strstr((s), (sub)) != NULL)
#define IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static void player(coop_info *i, const char *name, int node, int self, uint32_t ip) {
    coop_player *p = &i->player[i->nplayers++]; snprintf(p->name, sizeof p->name, "%s", name); p->node = node; p->self = self; p->ip = ip;
}

int main(void) {
    lp_view v; coop_info i;
    const uint32_t home[4] = {IP(192, 168, 1, 20), IP(26, 139, 206, 94), IP(169, 254, 3, 4), IP(10, 0, 0, 5)};

    // no Link Play node: alone, and where to change it
    lp_view_make(&v, NULL, home, 4);
    HAS(v.mode, "Alone"); HAS(v.addr_note, "launcher"); CHECK(v.naddr == 0 && !v.room[0] && !v.invite[0] && !v.nplayers);

    // this network, hosting with two visitors: home-network addresses only (a VPN's needs internet play, and the note says so; link-local is left out)
    memset(&i, 0, sizeof i); i.role = COOP_ROLE_AUTO; i.scope = COOP_SCOPE_LAN; i.ldn = COOP_LS_AP_CREATED; i.hosting = 1; i.port = COOP_PORT_DEFAULT;
    player(&i, "Max", 0, 1, 0); player(&i, "Jos\xc3\xa9", 1, 0, IP(192, 168, 1, 21)); player(&i, "Kit", 2, 0, IP(192, 168, 1, 22));
    lp_view_make(&v, &i, home, 4);
    HAS(v.mode, "On this network"); HAS(v.now, "3 players");
    CHECK(v.naddr == 2); CHECK(!strcmp(v.addr[0], "192.168.1.20 (this network)")); CHECK(!strcmp(v.addr[1], "10.0.0.5 (this network)"));
    HAS(v.addr_note, "internet play");
    CHECK(!v.room[0] && !v.invite[0]);
    CHECK(v.nplayers == 3); CHECK(!strcmp(v.player[0], "Max (host) (you)")); CHECK(!strcmp(v.player[1], "Jos?, 192.168.1.21"));
    // the system picked another port: it goes with the address
    i.port = 47601; lp_view_make(&v, &i, home, 1); CHECK(v.naddr == 1 && !strcmp(v.addr[0], "192.168.1.20:47601 (this network)"));

    // hosting over the internet through Radmin VPN: its address first in the invite, with the room code
    memset(&i, 0, sizeof i); i.role = COOP_ROLE_AUTO; i.scope = COOP_SCOPE_INTERNET; i.internet = 1; i.ldn = COOP_LS_AP_CREATED; i.hosting = 1; i.port = COOP_PORT_DEFAULT;
    snprintf(i.room, sizeof i.room, "7K3M-9QXA-2B4C-HJ6N"); player(&i, "Max", 0, 1, 0);
    lp_view_make(&v, &i, home, 2);
    HAS(v.mode, "Hosting over the internet or a VPN"); HAS(v.now, "Waiting for friends");
    CHECK(v.naddr == 2); CHECK(!strcmp(v.addr[1], "26.139.206.94 (Radmin VPN's range)"));
    CHECK(!strcmp(v.room, "7K3M-9QXA-2B4C-HJ6N")); HAS(v.room_note, "Give it");
    HAS(v.invite, "26.139.206.94"); HAS(v.invite, "7K3M-9QXA-2B4C-HJ6N");
    // no VPN and no public address on this PC: the router's address, found by the player, and the port to forward
    lp_view_make(&v, &i, home, 1);
    HAS(v.addr_note, "what is my IP"); HAS(v.addr_note, "47600"); HAS(v.invite, "(my internet address)");

    // visiting over the internet, in the host's town: the host's code, nothing to give
    memset(&i, 0, sizeof i); i.role = COOP_ROLE_AUTO; i.scope = COOP_SCOPE_INTERNET; i.internet = 1; i.ldn = COOP_LS_STATION_CONNECTED;
    i.join_ip = IP(203, 0, 113, 7); i.join_port = 47600; snprintf(i.room, sizeof i.room, "7K3M-9QXA-2B4C-HJ6N");
    player(&i, "Jos\xc3\xa9", 0, 0, IP(203, 0, 113, 7)); player(&i, "Max", 1, 1, 0);
    lp_view_make(&v, &i, home, 4);
    HAS(v.mode, "Visiting 203.0.113.7 over the internet"); CHECK(!strcmp(v.now, "Visiting Jos?'s town."));
    CHECK(v.naddr == 0); HAS(v.addr_note, "the host gives you"); HAS(v.room_note, "host's room code"); CHECK(!v.invite[0]);
    CHECK(!strcmp(v.player[0], "Jos? (host), 203.0.113.7")); CHECK(!strcmp(v.player[1], "Max (you)"));
    // still looking: by address, then with towns found
    i.ldn = COOP_LS_STATION; i.nplayers = 0; lp_view_make(&v, &i, home, 4); HAS(v.now, "203.0.113.7");
    i.found = 2; lp_view_make(&v, &i, home, 4); HAS(v.now, "2 found");

    // a test link on this PC (play.ps1 -CoopHost) and the states the game passes through
    memset(&i, 0, sizeof i); i.role = COOP_ROLE_HOST; i.scope = COOP_SCOPE_LOOPBACK;
    lp_view_make(&v, &i, home, 4); HAS(v.mode, "test link"); HAS(v.now, "Local connection"); CHECK(v.naddr == 0);
    i.ldn = COOP_LS_AP; lp_view_make(&v, &i, home, 4); HAS(v.now, "Opening");
    i.ldn = COOP_LS_ERROR; lp_view_make(&v, &i, home, 4); HAS(v.now, "log");

    // ---------- port forwarding while hosting over the internet (coop_nat.c) ----------
    memset(&i, 0, sizeof i); i.role = COOP_ROLE_AUTO; i.scope = COOP_SCOPE_INTERNET; i.internet = 1; i.ldn = COOP_LS_AP_CREATED; i.hosting = 1; i.port = COOP_PORT_DEFAULT;
    snprintf(i.room, sizeof i.room, "7K3M-9QXA-2B4C-HJ6N"); player(&i, "Max", 0, 1, 0);
    i.nat.state = COOP_NAT_TRYING; i.nat.port = 47600;
    lp_view_make(&v, &i, home, 1); HAS(v.addr_note, "Asking your router");
    i.nat.state = COOP_NAT_MAPPED; i.nat.method = COOP_NATM_UPNP; i.nat.external = IP(203, 0, 113, 7);
    lp_view_make(&v, &i, home, 2);
    CHECK(!strcmp(v.addr[0], "203.0.113.7 (internet, from your router)")); HAS(v.addr_note, "forwards port 47600"); HAS(v.addr_note, "UPnP");
    HAS(v.invite, "address 203.0.113.7,");   // the router's address before the VPN's
    i.nat.port = 47601; lp_view_make(&v, &i, home, 1); CHECK(!strcmp(v.addr[0], "203.0.113.7:47601 (internet, from your router)")); HAS(v.invite, "203.0.113.7:47601");
    i.nat.port = 47600; i.nat.problem = COOP_NATP_CGNAT; i.nat.external = IP(100, 70, 1, 2);
    lp_view_make(&v, &i, home, 1); HAS(v.addr_note, "carrier-grade NAT"); HAS(v.addr_note, "100.70.1.2"); CHECK(!strstr(v.addr[0], "from your router"));
    HAS(v.invite, "(my internet address)");
    i.nat.problem = COOP_NATP_DOUBLE_NAT; i.nat.external = IP(192, 168, 0, 2); lp_view_make(&v, &i, home, 1); HAS(v.addr_note, "Another router"); HAS(v.addr_note, "192.168.0.2");
    i.nat.state = COOP_NAT_FAILED; i.nat.method = 0; i.nat.problem = COOP_NATP_NO_SUPPORT; i.nat.external = 0;
    lp_view_make(&v, &i, home, 1); HAS(v.addr_note, "didn't answer UPnP");
    lp_view_make(&v, &i, home, 2); CHECK(!strstr(v.addr_note, "didn't answer"));   // with a VPN's address the VPN's note stands
    i.nat.problem = COOP_NATP_CONFLICT; lp_view_make(&v, &i, home, 1); HAS(v.addr_note, "another device");
    i.nat.problem = COOP_NATP_REFUSED; lp_view_make(&v, &i, home, 1); HAS(v.addr_note, "refused");

    // ---------- the settings: linkplay.json as play.ps1 writes it (PowerShell 5's UTF-8 mark, its spacing), the other keys kept ----------
    lp_prefs p, q; lp_env e; char err[240], out[4096]; const char *why;
    const char *ps = "\xEF\xBB\xBF{\n    \"keep_room\":  false,\n    \"join\":  \"\",\n    \"ask\":  true,\n    \"room\":  \"\",\n    \"host_room\":  \"7K3M-9QXA-2B4C-HJ6N\",\n"
                     "    \"mode\":  \"host\",\n    \"interact\":  true,\n    \"story\":  false,\n    \"extra\":  {\"a\": [1, \"}\"]}\n}";
    lp_prefs_parse(ps, &p);
#ifdef _WIN32
    CHECK(p.mode == LP_INET_HOST);
#else
    CHECK(p.mode == LP_LAN);   // play.sh's "host" is hosting on this network
#endif
    CHECK(!strcmp(p.host_room, "7K3M-9QXA-2B4C-HJ6N") && p.interact && !p.story && !p.keep_room && !p.join[0]);
    lp_prefs_parse(NULL, &q); CHECK(q.mode == LP_ALONE && !q.interact);
    lp_prefs_parse("not json", &q); CHECK(q.mode == LP_ALONE);
    p.mode = LP_INET_VISIT; snprintf(p.join, sizeof p.join, "26.139.206.94"); snprintf(p.room, sizeof p.room, "7K3M-9QXA-2B4C-HJ6N"); p.story = 1;
    CHECK(lp_prefs_merge(ps, &p, out, sizeof out) == 0);
    HAS(out, "\"ask\":  true"); HAS(out, "\"extra\":  {\"a\": [1, \"}\"]}"); HAS(out, "\"join\":  \"26.139.206.94\""); HAS(out, "\"story\":  true");
    HAS(out, lp_mode_key(LP_INET_VISIT));
    lp_prefs_parse(out, &q); CHECK(q.mode == LP_INET_VISIT && !strcmp(q.join, "26.139.206.94") && q.story && q.interact && !strcmp(q.room, p.room));
    CHECK(lp_prefs_merge(NULL, &p, out, sizeof out) == 0); lp_prefs_parse(out, &q); CHECK(q.mode == LP_INET_VISIT);   // a new file
    CHECK(lp_prefs_merge(ps, &p, out, 40) == -1);
    // the environment each way sets, as the launchers set it, checked as the coop module will read it
    CHECK(lp_prefs_env(&p, &e, err, sizeof err) == 0); CHECK(!strcmp(e.coop, "auto") && !strcmp(e.join, "26.139.206.94") && !strcmp(e.internet, "1"));
    CHECK(!strcmp(e.room, "7K3M-9QXA-2B4C-HJ6N") && !strcmp(e.interact, "1") && !strcmp(e.story, "1"));
    q = p; snprintf(q.room, sizeof q.room, "7k3m 9qxa 2b4c hj6n"); CHECK(lp_prefs_env(&q, &e, err, sizeof err) == 0 && !strcmp(e.room, "7K3M-9QXA-2B4C-HJ6N"));
    q.room[3] = 0; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == -1); HAS(err, "room code");
    q = p; q.join[0] = 0; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == -1); HAS(err, "address");
    q = p; snprintf(q.join, sizeof q.join, "26.1.2.3:99999"); CHECK(lp_prefs_env(&q, &e, err, sizeof err) == -1);
    q = p; q.mode = LP_LAN_JOIN; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == -1); HAS(err, "Radmin VPN"); HAS(err, "internet or a VPN");   // a VPN's address on this network
    snprintf(q.join, sizeof q.join, "192.168.1.20:47601"); CHECK(lp_prefs_env(&q, &e, err, sizeof err) == 0 && !e.internet[0] && !e.room[0] && !strcmp(e.join, "192.168.1.20:47601"));
    q = p; q.mode = LP_INET_HOST; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == 0 && !e.join[0] && !strcmp(e.room, "7K3M-9QXA-2B4C-HJ6N"));
    q.host_room[0] = 0; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == -1);
    q = p; q.mode = LP_LAN; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == 0 && !strcmp(e.coop, "auto") && !e.join[0] && !e.internet[0]);
    q.mode = LP_ALONE; CHECK(lp_prefs_env(&q, &e, err, sizeof err) == 0 && !e.coop[0] && !e.interact[0]);
    // when a change takes effect
    lp_prefs run; memset(&run, 0, sizeof run); run.mode = LP_LAN; q = run;
    CHECK(lp_when(&run, &q, 0, &why) == LP_SAME);
    q.mode = LP_INET_HOST; CHECK(lp_when(&run, &q, 0, &why) == LP_NOW); CHECK(lp_when(&run, &q, 1, &why) == LP_LATER); HAS(why, "Local connection");
    q = run; q.mode = LP_ALONE; CHECK(lp_when(&run, &q, 0, &why) == LP_NEXT_START);
    q = run; q.interact = 1; CHECK(lp_when(&run, &q, 0, &why) == LP_NEXT_START); HAS(why, "Interact everywhere");
    q.mode = LP_INET_HOST; CHECK(lp_when(&run, &q, 0, &why) == LP_NOW); HAS(why, "applies now"); HAS(why, "when the game starts");   // the way now, the mods later
    run.mode = LP_INET_VISIT; snprintf(run.join, sizeof run.join, "203.0.113.7"); q = run; snprintf(q.join, sizeof q.join, "203.0.113.8"); CHECK(lp_when(&run, &q, 0, &why) == LP_NOW);
    run.mode = LP_ALONE; q = run; q.mode = LP_LAN; CHECK(lp_when(&run, &q, 0, &why) == LP_NEXT_START);
    q = run; q.interact = 1; CHECK(lp_when(&run, &q, 0, &why) == LP_SAME);   // alone, it only waits in the file
    CHECK(lp_mode_count() >= 4 && lp_mode_at(0) == LP_ALONE);

    if (g_fail) { fprintf(stderr, "%d failed\n", g_fail); return 1; }
    fprintf(stderr, "linkplay: ok\n");
    return 0;
}
