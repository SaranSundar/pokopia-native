// The in-game Link Play tab's words (docs/in-game-menu.md): what the coop node says, as plain lines for menu_linkplay.c to draw. No SDL, so the tests
// run it alone (tests/test_linkplay.c). Our own code, MIT.
#pragma once
#include "coop.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LP_ADDRS 4
typedef struct lp_view {
    char mode[120];                       // how this game plays Link Play
    char now[160];                        // what is happening in the game's Link Play
    int naddr; char addr[LP_ADDRS][80];   // the addresses to give a friend, each with what it is
    char addr_note[320];                  // how a friend uses them, or why there are none (port forwarding's state too)
    char room[COOP_ROOM_SHOWN];           // internet play's room code ("" without it)
    char room_note[120];
    int nplayers; char player[COOP_INFO_PLAYERS][96];   // the session's players: name, host, you, address
    char invite[220];                     // what Copy invite puts on the clipboard ("" when this game has nothing to give)
} lp_view;

// i NULL: no Link Play in this game (DM_COOP unset or off). addrs: this machine's IPv4 addresses (coop_local_ipv4), host order
void lp_view_make(lp_view *v, const coop_info *i, const uint32_t *addrs, int naddr);

// ---------- The Link Play settings (linkplay_prefs.c): linkplay.json as the launchers keep it, the environment each way of playing sets, and when a
// change can take effect ----------
enum { LP_ALONE, LP_LAN, LP_LAN_JOIN, LP_INET_HOST, LP_INET_VISIT, LP_MODES };
typedef struct lp_prefs {
    int mode;                                      // LP_*
    char join[64];                                 // the host's address (LP_LAN_JOIN, LP_INET_VISIT)
    char room[COOP_ROOM_SHOWN];                    // the host's room code (LP_INET_VISIT)
    char host_room[COOP_ROOM_SHOWN];               // the code this PC hosted with last (LP_INET_HOST); keep_room offers it again
    int keep_room, interact, story;
} lp_prefs;
int lp_mode_count(void);                           // the ways this platform offers: Windows 4 (DNS-SD finds hosts on the LAN), elsewhere 5
int lp_mode_at(int i);
const char *lp_mode_label(int mode);
const char *lp_mode_key(int mode);                 // linkplay.json "mode": play.ps1's alone / lan / host / visit, play.sh's alone / host / join / inet-host / inet-visit
void lp_prefs_parse(const char *json, lp_prefs *p);                        // NULL or unreadable: the defaults (alone)
int lp_prefs_merge(const char *old_json, const lp_prefs *p, char *out, size_t n);   // the file with these keys set and the others kept: 0, or -1 (too long)
void lp_prefs_from_env(lp_prefs *p);               // how this game was started (DM_COOP*), for comparing
// The environment a way of playing sets, as play.ps1 and play.sh set it ("" unset). Checked as the coop module will read it: 0, or -1 with why
typedef struct lp_env { char coop[8], join[64], internet[2], room[COOP_ROOM_SHOWN], interact[2], story[2]; } lp_env;
int lp_prefs_env(const lp_prefs *p, lp_env *e, char *err, size_t errn);
// When a change takes effect: LP_NOW (the way among the Link Play ways, the address or the code changed, and the game's Link Play is closed), LP_LATER (the
// same while it is open: once it closes) or LP_NEXT_START (Link Play on or off, or only Interact everywhere / Story co-op, which always wait for the next
// start); why says it plainly
enum { LP_SAME, LP_NOW, LP_LATER, LP_NEXT_START };
int lp_when(const lp_prefs *running, const lp_prefs *want, int ldn_open, const char **why);

#ifdef __cplusplus
}
#endif
