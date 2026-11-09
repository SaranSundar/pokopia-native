// The in-game menu's Link Play tab (docs/in-game-menu.md). Two views:
// - status: how this game plays Link Play, what to give a friend, the room code with Copy buttons and who is in the session, read from the coop node
//   (coop_get_info) and put in words by linkplay.c;
// - settings: the way to play, the host's address and room code, Interact everywhere and Story co-op, saved to the launcher's linkplay.json
//   (linkplay_prefs.c) and, while the game's Link Play is closed, applied at once by making the coop node again (hle_offline.c dm_coop_remake).
// The present thread draws it and takes its input under the menu's lock. The window thread (lp_event) writes the clipboard, as SDL wants, and takes the
// keys typed into a text field; the fields have their own lock. Our own code, MIT.
#include "menu_int.h"
#include "linkplay.h"
#include "dm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

coop_node *dm_coop(void);     // hle_offline.c: NULL without DM_COOP
int dm_coop_remake(void);     // hle_offline.c
const char *mn_settings_dir(void);   // menu.c
int mn_tab(void);                    // menu.c: the tab shown

static struct {
    lp_view v; int have;
    uint64_t t_info, t_addr;
    uint32_t addrs[8]; int naddr;
    int view;                 // 0 status, 1 settings
    lp_prefs run, want;       // how this game plays now, and what the settings view shows
    int loaded;               // want read from linkplay.json
    char note[220]; int note_bad;   // the settings view's line under the rows: when a change takes effect, or what is wrong
} G;
static SDL_AtomicInt g_ev;    // the clipboard's event type, registered on the first copy
static char g_clip[256];      // written before the event is pushed, read by the window thread when it arrives
static void *g_tmu;   // the settings (the text fields): the window thread types, the present thread draws and applies; SDL's mutexes nest
static int g_edit = -1;       // the text row being typed into, -1 none (under g_tmu)
static char g_undo[64];       // its text before typing began (Esc puts it back)

static SDL_Mutex *tmu(void) {
    SDL_Mutex *m = (SDL_Mutex *)SDL_GetAtomicPointer(&g_tmu);
    if (!m) { SDL_Mutex *n = SDL_CreateMutex(); if (!SDL_CompareAndSwapAtomicPointer(&g_tmu, NULL, n)) SDL_DestroyMutex(n); m = (SDL_Mutex *)SDL_GetAtomicPointer(&g_tmu); }
    return m;
}
// The view again every half second while the tab is shown; the machine's addresses every five (the system call takes a few milliseconds)
static void refresh(void) {
    uint64_t now = SDL_GetTicksNS();
    if (G.have && now - G.t_info < 500000000ull) return;
    if (!G.t_addr || now - G.t_addr > 5000000000ull) { int k = coop_local_ipv4(G.addrs, 8); G.naddr = k < 0 ? 0 : k; G.t_addr = now; }
    coop_node *n = dm_coop(); coop_info i;
    if (n) coop_get_info(n, &i);
    lp_view_make(&G.v, n ? &i : NULL, G.addrs, G.naddr);
    G.t_info = now; G.have = 1;
}

static void copy(const char *t, const char *what) {
    int ev = SDL_GetAtomicInt(&g_ev);
    if (!ev) { ev = (int)SDL_RegisterEvents(1); SDL_SetAtomicInt(&g_ev, ev); }
    if (!ev) { mn_status("Could not copy"); return; }
    snprintf(g_clip, sizeof g_clip, "%s", t);
    SDL_Event e; SDL_zero(e); e.type = (Uint32)ev; SDL_PushEvent(&e);
    char s[80]; snprintf(s, sizeof s, "%s copied: paste it in a message to your friends", what); mn_status(s);
    LOG("[menu] Link Play: %s copied", what);   // never the code itself: logs end up in bug reports
}

// ---------- the settings: linkplay.json in the launcher's folder ----------
static void prefs_path(char *p, size_t n) { snprintf(p, n, "%s/linkplay.json", mn_settings_dir()); }
static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    char *b = malloc(65536); size_t k = b ? fread(b, 1, 65535, f) : 0; fclose(f);
    if (b) b[k] = 0;
    return b;
}
static void settings_open(void) {
    char p[1100]; prefs_path(p, sizeof p);
    char *t = read_all(p);
    SDL_LockMutex(tmu());
    lp_prefs_parse(t, &G.want); lp_prefs_from_env(&G.run);
    if (!t) G.want = G.run;   // no file yet (started with switches, or never chosen): from how this game plays
    G.loaded = 1; G.note[0] = 0; G.note_bad = 0;
    SDL_UnlockMutex(tmu());
    free(t);
}
static int coop_available(void) { const coop_site *s; return coop_interact_sites(dm_main_build(), &s) > 0; }
static int ldn_open(void) { coop_node *n = dm_coop(); return n && coop_ldn_state(n) != COOP_LS_NONE; }
static void env_put(const char *k, const char *v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v && *v) setenv(k, v, 1); else unsetenv(k);
#endif
}
static void apply_locked(void);
static void apply(void) { SDL_LockMutex(tmu()); apply_locked(); SDL_UnlockMutex(tmu()); }
static void apply_locked(void) {
    lp_prefs w = G.want; lp_env e; char err[220];
    if (w.mode == LP_INET_HOST && (!w.keep_room || !w.host_room[0])) {   // a new code for this way, as the launcher makes one
        char norm[COOP_ROOM_LEN + 1]; coop_room_new(norm); coop_room_format(norm, w.host_room);
    }
    if (w.mode == LP_INET_HOST && G.run.mode == LP_INET_HOST && G.run.host_room[0] && w.keep_room) snprintf(w.host_room, sizeof w.host_room, "%s", G.run.host_room);
    if (lp_prefs_env(&w, &e, err, sizeof err)) { snprintf(G.note, sizeof G.note, "%s", err); G.note_bad = 1; return; }
    if (w.mode == LP_INET_VISIT) { char norm[COOP_ROOM_LEN + 1]; if (coop_room_normalize(w.room, norm)) coop_room_format(norm, w.room); }   // saved as players read it
    // saved for the launchers, the keys they don't know kept
    char p[1100]; prefs_path(p, sizeof p);
    char *old = read_all(p), *out = malloc(65536); int bad = !out || lp_prefs_merge(old, &w, out, 65536);
    if (!bad) { SDL_CreateDirectory(mn_settings_dir()); FILE *f = fopen(p, "wb"); bad = !f || fwrite(out, 1, strlen(out), f) != strlen(out); if (f) fclose(f); }
    free(old); free(out);
    if (bad) { snprintf(G.note, sizeof G.note, "Could not save %s.", p); G.note_bad = 1; LOG("[RED] [menu] Link Play: %s could not be written", p); return; }
    G.want = w;
    const char *why; int when = lp_when(&G.run, &w, ldn_open(), &why);
    if (when == LP_NOW) {
        env_put("DM_COOP", e.coop); env_put("DM_COOP_JOIN", e.join); env_put("DM_COOP_INTERNET", e.internet); env_put("DM_COOP_ROOM", e.room);
        int r = dm_coop_remake();
        if (r == 1) { lp_prefs keep = G.run; G.run = w; G.run.interact = keep.interact; G.run.story = keep.story; G.have = 0; }
        else why = r < 0 ? "Saved, but Link Play couldn't start this way: the log says why. It tries again when the game starts." : "Saved: from the next start.";
    }
    snprintf(G.note, sizeof G.note, "%s", why); G.note_bad = 0;
    LOG("[menu] Link Play settings saved: %s%s (%s)", lp_mode_key(w.mode), w.interact ? (w.story ? ", Interact everywhere, Story co-op" : ", Interact everywhere") : "",
        when == LP_NOW ? "applied now" : when == LP_LATER ? "once the game's Link Play closes" : when == LP_NEXT_START ? "from the next start" : "saved");
    mn_status(when == LP_NOW ? "Link Play: applied" : "Link Play: saved");
}

// ---------- the rows of each view ----------
enum { S_MODE, S_ADDR, S_ROOM, S_INTERACT, S_STORY, S_APPLY, S_BACK, S_N };   // settings
enum { B_ROOM, B_INVITE, B_SETTINGS };                                       // status buttons
static int visit_mode(int m) { return m == LP_LAN_JOIN || m == LP_INET_VISIT; }
static int row_off(int r) {   // greyed: nothing to change there as things are
    int m = G.want.mode;
    if (r == S_ADDR) return !visit_mode(m);
    if (r == S_ROOM) return m != LP_INET_VISIT && m != LP_INET_HOST;
    if (r == S_INTERACT || r == S_STORY) return m == LP_ALONE || !coop_available();
    return 0;
}
static int status_buttons(int *b) { int n = 0; if (G.v.room[0]) { b[n++] = B_ROOM; if (G.v.invite[0]) b[n++] = B_INVITE; } b[n++] = B_SETTINGS; return n; }
int lp_rows(void) { refresh(); if (G.view) return S_N; int b[3]; return status_buttons(b); }

// Where everything goes. Status: the left column (what to give, the room code and its buttons), the right one (the players), the settings button.
// Settings: label and value rows as on the Graphics tab, then Apply and Back
typedef struct { float xl, xr, colw, y_sec, y_addr, y_note, y_room, y_code, y_inv, code_w, copy_x, copy_w, inv_w, bh, set_x, set_y, set_w; int nnote; char note[4][200];
                 float row_h, x_val, val_w; } lay;
static int wrap(const char *t, float px, float w, char out[][200], int max);
static void layout(const mn_panel *p, lay *L) {
    float u = p->u;
    L->xl = p->px + 48 * u; L->colw = (p->pw - 96 * u - 40 * u) / 2; L->xr = L->xl + L->colw + 40 * u;
    L->y_sec = p->y0 + 92 * u;
    L->y_addr = L->y_sec + 34 * u;
    L->y_note = L->y_addr + G.v.naddr * 34 * u + (G.v.naddr ? 6 * u : 0);
    L->nnote = G.v.addr_note[0] ? wrap(G.v.addr_note, 19 * u, L->colw, L->note, 4) : 0;
    L->y_room = L->y_note + L->nnote * 25 * u + 26 * u;
    L->y_code = L->y_room + 30 * u;
    L->bh = 56 * u; L->code_w = 330 * u; L->copy_x = L->xl + L->code_w + 16 * u; L->copy_w = 130 * u;
    L->y_inv = L->y_code + L->bh + 14 * u; L->inv_w = 220 * u;
    L->set_w = 300 * u; L->set_x = L->xr; L->set_y = p->py + p->ph - 210 * u;   // "Change how you play", above the hint lines
    L->row_h = 58 * u; L->val_w = fminf(560 * u, p->pw * 0.5f); L->x_val = p->px + p->pw - 48 * u - L->val_w;
}
static float set_row_y(const mn_panel *p, const lay *L, int r) { return p->y0 - 10 * p->u + (r < S_APPLY ? r : S_APPLY) * L->row_h; }
static void set_button(const mn_panel *p, const lay *L, int r, float *x, float *y, float *w) {   // Apply and Back side by side
    *w = 200 * p->u; *y = set_row_y(p, L, S_APPLY) + 18 * p->u; *x = L->x_val + (r == S_APPLY ? 0 : *w + 16 * p->u);
}
int lp_pick(const mn_panel *p, float x, float y) {
    lay L; layout(p, &L);
    if (G.view) {
        for (int r = 0; r < S_APPLY; r++) if (mn_inside(x, y, p->px + 24 * p->u, set_row_y(p, &L, r), p->pw - 48 * p->u, L.row_h)) return r;
        for (int r = S_APPLY; r <= S_BACK; r++) { float bx, by, bw; set_button(p, &L, r, &bx, &by, &bw); if (mn_inside(x, y, bx, by, bw, L.bh)) return r; }
        return -1;
    }
    int b[3], n = status_buttons(b);
    for (int i = 0; i < n; i++) {
        if (b[i] == B_ROOM && mn_inside(x, y, L.copy_x, L.y_code, L.copy_w, L.bh)) return i;
        if (b[i] == B_INVITE && mn_inside(x, y, L.xl, L.y_inv, L.inv_w, 48 * p->u)) return i;
        if (b[i] == B_SETTINGS && mn_inside(x, y, L.set_x, L.set_y, L.set_w, L.bh)) return i;
    }
    return -1;
}
static void edit_start(int r) {
    SDL_LockMutex(tmu());
    g_edit = r; snprintf(g_undo, sizeof g_undo, "%s", r == S_ADDR ? G.want.join : G.want.room);
    SDL_UnlockMutex(tmu());
    mn_status("Type it, or paste with Ctrl+V. Enter keeps it, Esc puts the old one back.");
}
static void step(int r, int d) {   // left / right on a row, or Enter on a choice
    lp_prefs *w = &G.want;
    if (row_off(r)) return;
    SDL_LockMutex(tmu());
    if (r == S_MODE) { int n = lp_mode_count(), i = 0; for (int k = 0; k < n; k++) if (lp_mode_at(k) == w->mode) i = k; w->mode = lp_mode_at((i + d + n) % n); }
    else if (r == S_ROOM && w->mode == LP_INET_HOST) w->keep_room = w->host_room[0] ? !w->keep_room : 0;
    else if (r == S_INTERACT) { w->interact = !w->interact; if (!w->interact) w->story = 0; }
    else if (r == S_STORY) { w->story = !w->story; if (w->story) w->interact = 1; }
    G.note[0] = 0;
    SDL_UnlockMutex(tmu());
}
static void press(int sel) {
    if (G.view) {
        if (sel == S_APPLY) apply();
        else if (sel == S_BACK) { G.view = 0; G.have = 0; }
        else if ((sel == S_ADDR || (sel == S_ROOM && G.want.mode == LP_INET_VISIT)) && !row_off(sel)) edit_start(sel);
        else step(sel, 1);
        return;
    }
    int b[3], n = status_buttons(b); if (sel < 0 || sel >= n) return;
    if (b[sel] == B_ROOM) copy(G.v.room, "The room code");
    else if (b[sel] == B_INVITE) copy(G.v.invite, "The invite");
    else { G.view = 1; settings_open(); }
}
void lp_input(const mn_panel *p, int kind, float x, float y, int *sel) {
    int n = lp_rows();
    if (kind == IN_CLICK) { int b = lp_pick(p, x, y); if (b >= 0) { *sel = b; press(b); } return; }
    if (kind == IN_BACK && G.view) { G.view = 0; *sel = 0; G.have = 0; return; }
    if (!n) return;
    if (G.view && (kind == IN_LEFT || kind == IN_RIGHT) && *sel < S_APPLY) { step(*sel, kind == IN_LEFT ? -1 : 1); return; }
    if (kind == IN_UP || kind == IN_LEFT) *sel = (*sel + n - 1) % n;
    else if (kind == IN_DOWN || kind == IN_RIGHT) *sel = (*sel + 1) % n;
    else if (kind == IN_OK) { int was = G.view; press(*sel); if (G.view != was) *sel = 0; }
}
const char *lp_hint(int sel) {
    if (G.view) {
        static const char *const h[S_N] = {
            "How this game plays Link Play. Left and right choose; Apply saves it for the launcher too.",
            "The host's address, for example 192.168.1.20, or the one the host gives you over the internet or a VPN. Enter, then type.",
            "Visiting over the internet: the host's code. Hosting: a new code each time, or the one you used last.",
            "Visitors can use moves and items and build in every town. Every player needs it on.",
            "With Interact everywhere: visitors can talk to the town's story characters. Every player needs it on.",
            "Saves these settings (the launcher uses them too) and applies them when it can.",
            "Back to what Link Play is doing."};
        return sel >= 0 && sel < S_N ? h[sel] : h[0];
    }
    int b[3], n = status_buttons(b);
    if (sel >= 0 && sel < n && b[sel] == B_ROOM) return "Copies the room code. Everyone who visits needs it.";
    if (sel >= 0 && sel < n && b[sel] == B_INVITE) return "Copies a message with your address and the room code, ready to paste in a chat.";
    return "Change how this game plays Link Play: the way, the host's address and code, Interact everywhere, Story co-op.";
}

// ---------- the window thread: the clipboard, and keys typed into a text field ----------
static int typed_char(const SDL_KeyboardEvent *k, int addr) {
    SDL_Keycode c = k->key; int shift = (k->mod & SDL_KMOD_SHIFT) != 0;
    if (c >= SDLK_0 && c <= SDLK_9 && !shift) return (int)c;
    if (c >= SDLK_KP_1 && c <= SDLK_KP_9) return '1' + (int)(c - SDLK_KP_1);
    if (c == SDLK_KP_0) return '0';
    if (addr) { if (c == SDLK_PERIOD || c == SDLK_KP_PERIOD) return '.'; if (c == SDLK_COLON || (c == SDLK_SEMICOLON && shift)) return ':'; return 0; }
    if (c >= SDLK_A && c <= SDLK_Z) return (int)(c - SDLK_A) + 'A';
    if (c == SDLK_MINUS || c == SDLK_KP_MINUS) return '-';
    return 0;
}
static void field_add(char *f, size_t n, const char *s, int addr) {
    size_t k = strlen(f);
    for (; *s && k + 1 < n; s++) {
        char c = *s; if (c >= 'a' && c <= 'z' && !addr) c = (char)(c - 32);
        int ok = addr ? ((c >= '0' && c <= '9') || c == '.' || c == ':') : ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '-');
        if (ok) { f[k++] = c; f[k] = 0; }
    }
}
int lp_event(const SDL_Event *e) {
    int ev = SDL_GetAtomicInt(&g_ev);
    if (ev && e->type == (Uint32)ev) {
        if (!SDL_SetClipboardText(g_clip)) LOG("[RED] [menu] Link Play: the clipboard refused the text: %s", SDL_GetError());
        return 1;
    }
    if (!SDL_GetAtomicPointer(&g_tmu)) return 0;
    SDL_LockMutex(tmu());
    int r = g_edit, taken = 0;
    if (r >= 0 && menu_is_open() && mn_tab() == 2 && G.view) {
        char *f = r == S_ADDR ? G.want.join : G.want.room; size_t n = r == S_ADDR ? sizeof G.want.join : sizeof G.want.room; int addr = r == S_ADDR;
        if (e->type == SDL_EVENT_KEY_DOWN) {
            taken = 1;
            SDL_Scancode sc = e->key.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) { snprintf(f, n, "%s", g_undo); g_edit = -1; }
            else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_TAB) g_edit = -1;
            else if (sc == SDL_SCANCODE_BACKSPACE) { size_t k = strlen(f); if (k) f[k - 1] = 0; }
            else if (sc == SDL_SCANCODE_F1) { g_edit = -1; taken = 0; }   // the menu closes as usual
            else if (sc == SDL_SCANCODE_V && (e->key.mod & SDL_KMOD_CTRL)) { char *c = SDL_GetClipboardText(); if (c) { field_add(f, n, c, addr); SDL_free(c); } }
            else { int ch = typed_char(&e->key, addr); if (ch) { char s[2] = {(char)ch, 0}; field_add(f, n, s, addr); } }
        } else if (e->type == SDL_EVENT_KEY_UP || e->type == SDL_EVENT_TEXT_INPUT) taken = 1;
        else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) { g_edit = -1; taken = 1; }   // a controller can't type: any button leaves the field
    } else if (r >= 0) g_edit = -1;
    SDL_UnlockMutex(tmu());
    return taken;
}

// ---------- drawing ----------
// Greedy word wrap into at most max lines; the last one keeps the rest (the caller shrinks it to fit)
static int wrap(const char *t, float px, float w, char out[][200], int max) {
    int n = 0;
    while (*t && n < max) {
        while (*t == ' ') t++;
        if (!*t) break;
        size_t take = 0, from = 0;
        for (;;) {
            size_t e = from; while (t[e] && t[e] != ' ') e++;   // the next word's end
            char b[200]; size_t m = e < sizeof b - 1 ? e : sizeof b - 1; memcpy(b, t, m); b[m] = 0;
            if (take && mn_text_w(b, px) > w) break;
            take = m;
            if (!t[e] || m != e) break;
            from = e + 1;
        }
        if (n == max - 1) { take = strlen(t); if (take > 199) take = 199; }
        memcpy(out[n], t, take); out[n][take] = 0; n++;
        t += take;
    }
    return n;
}
static void button(mn_list *out, float x, float y, float w, float h, const char *t, float fs, int on) {
    mn_rect(out, x, y, w, h, h / 2, on ? C_SEL : C_TAB);
    mn_text(out, t, x + (w - mn_text_w(t, fs)) / 2, y + (h - fs) / 2, fs, on ? C_SELTEXT : C_TEXT);
}
static void draw_settings(mn_list *out, const mn_panel *p, const lay *L, int sel) {
    float u = p->u, fs = p->fs; const lp_prefs *w = &G.want; char v[160];
    SDL_LockMutex(tmu()); int editing = g_edit;
    static const char *const label[S_APPLY] = {"How to play", "Host's address", "Room code", "Interact everywhere", "Story co-op"};
    for (int r = 0; r < S_APPLY; r++) {
        float y = set_row_y(p, L, r); int off = row_off(r), on = r == sel;
        if (on) mn_rect(out, p->px + 24 * u, y + 4 * u, p->pw - 48 * u, L->row_h - 8 * u, (L->row_h - 8 * u) / 2, RGBA(255, 244, 196, 255));
        mn_text(out, label[r], L->xl, y + (L->row_h - fs) / 2, fs, off ? C_OFF : C_TEXT);
        mn_rect(out, L->x_val, y + 8 * u, L->val_w, L->row_h - 16 * u, (L->row_h - 16 * u) / 2, editing == r ? C_EDGE : on && !off ? C_SEL : C_PILL);
        switch (r) {
        case S_MODE: snprintf(v, sizeof v, "%s", lp_mode_label(w->mode)); break;
        case S_ADDR: snprintf(v, sizeof v, "%s%s", off ? "Only for visiting" : w->join[0] ? w->join : editing == r ? "" : "Enter, then type it", editing == r ? "_" : ""); break;
        case S_ROOM:
            if (w->mode == LP_INET_VISIT) snprintf(v, sizeof v, "%s%s", w->room[0] ? w->room : editing == r ? "" : "Enter, then type or paste it", editing == r ? "_" : "");
            else if (w->mode == LP_INET_HOST) snprintf(v, sizeof v, w->keep_room && w->host_room[0] ? "Keep %s" : "A new one each time%s", w->keep_room && w->host_room[0] ? w->host_room : "");
            else snprintf(v, sizeof v, "Only internet play uses one");
            break;
        case S_INTERACT: snprintf(v, sizeof v, "%s", !coop_available() ? "Not in this game version" : w->interact ? "On (experimental)" : "Off"); break;
        default: snprintf(v, sizeof v, "%s", !coop_available() ? "Not in this game version" : w->story ? "On (experimental)" : "Off"); break;
        }
        float vs = fs * 0.9f; while (vs > 14 * u && mn_text_w(v, vs) > L->val_w - 60 * u) vs *= 0.94f;
        mn_text(out, v, L->x_val + 24 * u, y + (L->row_h - vs) / 2, vs, on && !off && editing != r ? C_SELTEXT : off ? C_OFF : C_TEXT);
    }
    for (int r = S_APPLY; r <= S_BACK; r++) { float bx, by, bw; set_button(p, L, r, &bx, &by, &bw); button(out, bx, by, bw, L->bh, r == S_APPLY ? "Apply" : "Back", fs, sel == r); }
    // when the shown settings would take effect, or what is wrong with them
    const char *why = G.note;
    if (!why[0]) { const char *w2; int k = lp_when(&G.run, w, ldn_open(), &w2); why = k == LP_SAME ? "" : w2; }
    char lines[2][200]; int nl = why[0] ? wrap(why, 21 * u, p->pw - 96 * u, lines, 2) : 0;
    float y0 = set_row_y(p, L, S_APPLY) + 18 * u + L->bh + 22 * u;
    for (int k = 0; k < nl; k++) mn_text(out, lines[k], L->xl, y0 + k * 28 * u, mn_fit(lines[k], 21 * u, p->pw - 96 * u, u), G.note_bad ? RGBA(170, 30, 50, 255) : C_TITLE);
    SDL_UnlockMutex(tmu());
}
void lp_draw(mn_list *out, const mn_panel *p, int sel) {
    tmu();
    refresh();
    lay L; layout(p, &L);
    if (G.view) { draw_settings(out, p, &L, sel); return; }
    float u = p->u, fs = p->fs;
    const lp_view *v = &G.v;
    int b[3], nb = status_buttons(b), selb = sel >= 0 && sel < nb ? b[sel] : -1;
    mn_text(out, v->mode, L.xl, p->y0 - 12 * u, mn_fit(v->mode, 28 * u, p->pw - 96 * u, u), C_TITLE);
    mn_text(out, v->now, L.xl, p->y0 + 30 * u, mn_fit(v->now, 22 * u, p->pw - 96 * u, u), C_TEXT);
    // what to give a friend
    mn_text(out, "Give your friends", L.xl, L.y_sec, 20 * u, C_SOFT);
    for (int k = 0; k < v->naddr; k++) mn_text(out, v->addr[k], L.xl, L.y_addr + k * 34 * u, mn_fit(v->addr[k], 24 * u, L.colw, u), C_TEXT);
    for (int k = 0; k < L.nnote; k++) mn_text(out, L.note[k], L.xl, L.y_note + k * 25 * u, mn_fit(L.note[k], 19 * u, L.colw, u), C_SOFT);
    // the room code and its buttons
    mn_text(out, "Room code", L.xl, L.y_room, 20 * u, C_SOFT);
    if (v->room[0]) {
        mn_rect(out, L.xl, L.y_code, L.code_w, L.bh, L.bh / 2, C_PILL);
        float cs = mn_fit(v->room, 28 * u, L.code_w - 40 * u, u);
        mn_text(out, v->room, L.xl + (L.code_w - mn_text_w(v->room, cs)) / 2, L.y_code + (L.bh - cs) / 2, cs, C_TITLE);
        button(out, L.copy_x, L.y_code, L.copy_w, L.bh, "Copy", fs, selb == B_ROOM);
        if (v->invite[0]) button(out, L.xl, L.y_inv, L.inv_w, 48 * u, "Copy invite", 22 * u, selb == B_INVITE);
        mn_text(out, v->room_note, L.xl, L.y_inv + (v->invite[0] ? 60 * u : 0), mn_fit(v->room_note, 19 * u, L.colw, u), C_SOFT);
    } else mn_text(out, "None: only internet play uses a room code.", L.xl, L.y_code, 22 * u, C_OFF);
    // who is there
    mn_text(out, "Players", L.xr, L.y_sec, 20 * u, C_SOFT);
    if (!v->nplayers) mn_text(out, "Nobody yet: no session is open.", L.xr, L.y_addr, 22 * u, C_OFF);
    for (int k = 0; k < v->nplayers; k++) mn_text(out, v->player[k], L.xr, L.y_addr + k * 34 * u, mn_fit(v->player[k], 24 * u, L.colw, u), C_TEXT);
    button(out, L.set_x, L.set_y, L.set_w, L.bh, "Change how you play", 22 * u, selb == B_SETTINGS);
}

// ---------- DM_CTL "menu lp ..." (tests), with the menu's lock held ----------
void lp_ctl(const char *c) {
    char a[64] = ""; int v = 0;
    if (!strncmp(c, "status", 6)) { G.view = 0; G.have = 0; }
    else if (!strncmp(c, "settings", 8)) { G.view = 1; settings_open(); }
    else if (!strncmp(c, "apply", 5)) { if (!G.loaded) settings_open(); apply(); }
    else if (sscanf(c, "copy %63s", a) == 1) { refresh(); if (!strcmp(a, "room") && G.v.room[0]) copy(G.v.room, "The room code"); else if (!strcmp(a, "invite") && G.v.invite[0]) copy(G.v.invite, "The invite"); }
    else {
        if (!G.loaded) settings_open();
        SDL_LockMutex(tmu());
        if (sscanf(c, "mode %63s", a) == 1) { for (int m = 0; m < LP_MODES; m++) if (!strcmp(a, lp_mode_key(m))) G.want.mode = m; }
        else if (sscanf(c, "join %63s", a) == 1) snprintf(G.want.join, sizeof G.want.join, "%s", a);
        else if (!strncmp(c, "room ", 5)) snprintf(G.want.room, sizeof G.want.room, "%s", c + 5);
        else if (sscanf(c, "keep %d", &v) == 1) G.want.keep_room = v != 0;
        else if (sscanf(c, "interact %d", &v) == 1) { G.want.interact = v != 0; if (!v) G.want.story = 0; }
        else if (sscanf(c, "story %d", &v) == 1) { G.want.story = v != 0; if (v) G.want.interact = 1; }
        G.note[0] = 0;
        SDL_UnlockMutex(tmu());
    }
    LOG("[menu] DM_CTL: Link Play %s", strncmp(c, "room", 4) ? c : "room (a code)");
}
