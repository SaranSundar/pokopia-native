// The Link Play settings the in-game menu changes (linkplay.h): linkplay.json as play.ps1 and play.sh keep it, the environment each way of playing
// sets, and when a change can take effect. No SDL (tests/test_linkplay.c). Our own code, MIT.
#include "linkplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- the ways of playing, with each launcher's names ----------
#ifdef _WIN32
static const int g_offer[] = {LP_ALONE, LP_LAN, LP_INET_HOST, LP_INET_VISIT};   // play.ps1's window: DNS-SD finds the hosts on this network
static const char *const g_key[LP_MODES] = {"alone", "lan", "lan", "host", "visit"};
#else
static const int g_offer[] = {LP_ALONE, LP_LAN, LP_LAN_JOIN, LP_INET_HOST, LP_INET_VISIT};   // play.sh: no DNS-SD, so visiting on this network goes by address
static const char *const g_key[LP_MODES] = {"alone", "host", "join", "inet-host", "inet-visit"};
#endif
static const char *const g_label[LP_MODES] = {"Alone", "On this network (the same Wi-Fi)", "Visit an address on this network", "Host over the internet or a VPN",
                                               "Visit a host over the internet or a VPN"};
int lp_mode_count(void) { return (int)(sizeof g_offer / sizeof g_offer[0]); }
int lp_mode_at(int i) { return i >= 0 && i < lp_mode_count() ? g_offer[i] : LP_ALONE; }
const char *lp_mode_label(int mode) { return mode >= 0 && mode < LP_MODES ? g_label[mode] : g_label[0]; }
const char *lp_mode_key(int mode) { return mode >= 0 && mode < LP_MODES ? g_key[mode] : g_key[0]; }
static int mode_of_key(const char *k) {
    for (int m = 0; m < LP_MODES; m++) if (!strcmp(k, g_key[m])) return m;
    return LP_ALONE;
}

// ---------- linkplay.json: one object of members; values kept as their text so the launcher's other keys survive a write ----------
typedef struct { const char *k; size_t kn; const char *v; size_t vn; } member;
static const char *ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }
static const char *str_end(const char *p) {   // p at the opening quote: past the closing one, NULL when unterminated
    for (p++; *p; p++) { if (*p == '\\') { if (!*++p) return NULL; } else if (*p == '"') return p + 1; }
    return NULL;
}
static const char *value_end(const char *p) {
    if (*p == '"') return str_end(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        for (; *p; ) {
            if (*p == '"') { p = str_end(p); if (!p) return NULL; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
            p++;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') p++;   // true, false, null, a number
    return p;
}
static int members(const char *json, member *m, int max) {   // -1 when it isn't one object
    if (!json) return -1;
    const char *p = json;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   // PowerShell 5's UTF-8 mark
    p = ws(p); if (*p++ != '{') return -1;
    int n = 0;
    for (;;) {
        p = ws(p);
        if (*p == '}') return n;
        if (*p != '"') return -1;
        const char *k = p + 1, *ke = str_end(p); if (!ke) return -1;
        p = ws(ke); if (*p++ != ':') return -1;
        p = ws(p); const char *ve = value_end(p); if (!ve || ve == p) return -1;
        if (n < max) { m[n].k = k; m[n].kn = (size_t)(ke - 1 - k); m[n].v = p; m[n].vn = (size_t)(ve - p); n++; }
        p = ws(ve);
        if (*p == ',') { p++; continue; }
        if (*p == '}') return n;
        return -1;
    }
}
static int key_is(const member *m, const char *k) { return m->kn == strlen(k) && !memcmp(m->k, k, m->kn); }
static void str_val(const member *m, char *out, size_t n) {   // a string value, simple escapes undone (ours are ASCII); anything else: ""
    size_t k = 0; out[0] = 0;
    if (m->vn < 2 || m->v[0] != '"') return;
    for (size_t i = 1; i + 1 < m->vn && k + 1 < n; i++) { char c = m->v[i]; if (c == '\\' && i + 2 < m->vn) c = m->v[++i]; out[k++] = c; }
    out[k] = 0;
}
static int bool_val(const member *m) { return m->vn == 4 && !memcmp(m->v, "true", 4); }

void lp_prefs_parse(const char *json, lp_prefs *p) {
    memset(p, 0, sizeof *p);
    member m[64]; int n = members(json, m, 64);
    char t[64];
    for (int i = 0; i < n; i++) {
        if (key_is(&m[i], "mode")) { str_val(&m[i], t, sizeof t); p->mode = mode_of_key(t); }
        else if (key_is(&m[i], "join")) str_val(&m[i], p->join, sizeof p->join);
        else if (key_is(&m[i], "room")) str_val(&m[i], p->room, sizeof p->room);
        else if (key_is(&m[i], "host_room")) str_val(&m[i], p->host_room, sizeof p->host_room);
        else if (key_is(&m[i], "keep_room")) p->keep_room = bool_val(&m[i]);
        else if (key_is(&m[i], "interact")) p->interact = bool_val(&m[i]);
        else if (key_is(&m[i], "story")) p->story = bool_val(&m[i]);
    }
    int offered = 0;
    for (int i = 0; i < lp_mode_count(); i++) offered |= lp_mode_at(i) == p->mode;
    if (!offered) p->mode = p->mode == LP_LAN_JOIN ? LP_LAN : LP_ALONE;
}

static int put(char **o, size_t *left, const char *fmt, const char *a, const char *b) {
    int k = snprintf(*o, *left, fmt, a, b);
    if (k < 0 || (size_t)k >= *left) return -1;
    *o += k; *left -= (size_t)k; return 0;
}
static void quote(char *out, size_t n, const char *s) {   // a JSON string of ASCII text
    size_t k = 0; out[k++] = '"';
    for (; *s && k + 3 < n; s++) { if (*s == '"' || *s == '\\') out[k++] = '\\'; out[k++] = (*s >= 0x20 && *s < 0x7F) ? *s : '?'; }
    out[k++] = '"'; out[k] = 0;
}
int lp_prefs_merge(const char *old_json, const lp_prefs *p, char *out, size_t n) {
    static const char *const keys[] = {"mode", "join", "room", "host_room", "keep_room", "interact", "story"};
    enum { NK = sizeof keys / sizeof keys[0] };
    char val[NK][80];
    quote(val[0], sizeof val[0], lp_mode_key(p->mode)); quote(val[1], sizeof val[1], p->join); quote(val[2], sizeof val[2], p->room);
    quote(val[3], sizeof val[3], p->host_room);
    snprintf(val[4], sizeof val[4], "%s", p->keep_room ? "true" : "false"); snprintf(val[5], sizeof val[5], "%s", p->interact ? "true" : "false");
    snprintf(val[6], sizeof val[6], "%s", p->story ? "true" : "false");
    member m[64]; int nm = members(old_json, m, 64); if (nm < 0) nm = 0;
    int done[NK] = {0}; char *o = out; size_t left = n; int first = 1;
    if (put(&o, &left, "%s%s", "{", "")) return -1;
    for (int i = 0; i < nm; i++) {
        char k[64], v[512]; int ours = -1;
        for (int j = 0; j < NK; j++) if (key_is(&m[i], keys[j])) ours = j;
        if (m[i].kn >= sizeof k || m[i].vn >= sizeof v) continue;   // nothing of ours is that long; such a member is dropped rather than cut
        memcpy(k, m[i].k, m[i].kn); k[m[i].kn] = 0;
        if (ours >= 0) { snprintf(v, sizeof v, "%s", val[ours]); done[ours] = 1; } else { memcpy(v, m[i].v, m[i].vn); v[m[i].vn] = 0; }
        if (put(&o, &left, first ? "\n    \"%s\":  %s" : ",\n    \"%s\":  %s", k, v)) return -1;
        first = 0;
    }
    for (int j = 0; j < NK; j++) if (!done[j]) { if (put(&o, &left, first ? "\n    \"%s\":  %s" : ",\n    \"%s\":  %s", keys[j], val[j])) return -1; first = 0; }
    return put(&o, &left, "%s%s", "\n}\n", "");
}

void lp_prefs_from_env(lp_prefs *p) {
    memset(p, 0, sizeof *p);
    const char *c = getenv("DM_COOP"), *j = getenv("DM_COOP_JOIN"), *in = getenv("DM_COOP_INTERNET"), *r = getenv("DM_COOP_ROOM"), *e;
    int on = c && *c && strcmp(c, "off") && strcmp(c, "0"), inet = in && !strcmp(in, "1");
    if (j) snprintf(p->join, sizeof p->join, "%s", j);
    if (r) snprintf(inet && j && *j ? p->room : p->host_room, sizeof p->room, "%s", r);
    p->mode = !on ? LP_ALONE : inet ? (j && *j ? LP_INET_VISIT : LP_INET_HOST) : (j && *j ? LP_LAN_JOIN : LP_LAN);
    p->interact = (e = getenv("DM_COOP_INTERACT")) && !strcmp(e, "1");
    p->story = (e = getenv("DM_COOP_STORY")) && !strcmp(e, "1");
    if (p->story) p->interact = 1;
}

static int addr_ok(const char *a, uint32_t *ip) {   // IPv4[:port]
    unsigned b[4], port = 0; char tail[8] = "";
    int k = sscanf(a, "%3u.%3u.%3u.%3u%7s", &b[0], &b[1], &b[2], &b[3], tail);
    if (k < 4 || b[0] > 255 || b[1] > 255 || b[2] > 255 || b[3] > 255) return 0;
    if (k == 5 && (sscanf(tail, ":%5u", &port) != 1 || !port || port > 65535)) return 0;
    *ip = b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    return 1;
}
int lp_prefs_env(const lp_prefs *p, lp_env *e, char *err, size_t errn) {
    memset(e, 0, sizeof *e); if (errn) err[0] = 0;
    if (p->mode == LP_ALONE) return 0;
    snprintf(e->coop, sizeof e->coop, "auto");
    int visit = p->mode == LP_LAN_JOIN || p->mode == LP_INET_VISIT, inet = p->mode == LP_INET_HOST || p->mode == LP_INET_VISIT;
    if (visit) {
        uint32_t ip;
        if (!addr_ok(p->join, &ip)) { snprintf(err, errn, "Type the host's address, for example 192.168.1.20 (or 203.0.113.7 over the internet)."); return -1; }
        int cls = coop_addr4_class(ip);
        if (p->mode == LP_LAN_JOIN && cls != COOP_ADDR_PRIVATE && cls != COOP_ADDR_LINKLOCAL && cls != COOP_ADDR_LOOPBACK) {
            snprintf(err, errn, "%s%s is not a home-network address: for a VPN or the internet, choose Visit a host over the internet or a VPN.", p->join,
                     coop_addr4_vpn(ip));
            return -1;
        }
        snprintf(e->join, sizeof e->join, "%s", p->join);
    }
    if (inet) {
        const char *code = p->mode == LP_INET_VISIT ? p->room : p->host_room;
        char norm[COOP_ROOM_LEN + 1];
        if (!coop_room_normalize(code, norm)) {
            snprintf(err, errn, p->mode == LP_INET_VISIT ? "Type the host's room code: 16 letters and digits, XXXX-XXXX-XXXX-XXXX." : "No room code to host with yet.");
            return -1;
        }
        e->internet[0] = '1'; coop_room_format(norm, e->room);
    }
    if (p->interact || p->story) e->interact[0] = '1';
    if (p->story) e->story[0] = '1';
    // as the coop module will read it: an address its scope refuses (a VPN's without internet play) says so in the module's own words
    coop_cfg c; char jm[80], why[320] = "";
    if (visit) snprintf(jm, sizeof jm, "join:%s", e->join); else snprintf(jm, sizeof jm, "auto");
    if (coop_cfg_parse_ex(&c, jm, NULL, inet ? "1" : NULL, inet ? e->room : NULL, NULL, why, sizeof why) != 1) {
        const char *w = strstr(why, ": "); snprintf(err, errn, "%s", w ? w + 2 : why); return -1;
    }
    return 0;
}

int lp_when(const lp_prefs *run, const lp_prefs *want, int ldn_open, const char **why) {
    static const char *const s_next_lp = "Link Play turns on or off when the game starts: start it again to play this way.";
    static const char *const s_next_mod = "Interact everywhere and Story co-op take effect when the game starts: start it again.";
    static const char *const s_later = "The game's Link Play is open: leave Local connection, then press Apply again (or start the game again).";
    static const char *const s_now = "Applies now: the next time you open Link Play, then Local connection, it plays this way.";
    static const char *const s_later_mod = "The game's Link Play is open: leave Local connection, then press Apply again. Interact everywhere and Story co-op take "
                                           "effect when the game starts.";
    static const char *const s_now_mod = "The way to play applies now (the next time you open Link Play, then Local connection). Interact everywhere and Story "
                                         "co-op take effect when the game starts.";
    int lp_run = run->mode != LP_ALONE, lp_want = want->mode != LP_ALONE;
    int mods = (!!run->interact != !!want->interact) || (!!run->story != !!want->story);
    int visit = want->mode == LP_LAN_JOIN || want->mode == LP_INET_VISIT;
    int way = run->mode != want->mode || (visit && (strcmp(run->join, want->join) || (want->mode == LP_INET_VISIT && strcmp(run->room, want->room))));
    if (lp_run != lp_want) { *why = s_next_lp; return LP_NEXT_START; }
    if (!lp_want) { *why = "Saved."; return LP_SAME; }
    if (way) { *why = ldn_open ? (mods ? s_later_mod : s_later) : (mods ? s_now_mod : s_now); return ldn_open ? LP_LATER : LP_NOW; }   // the mods wait for the next start
    if (mods) { *why = s_next_mod; return LP_NEXT_START; }
    *why = "Saved."; return LP_SAME;
}
