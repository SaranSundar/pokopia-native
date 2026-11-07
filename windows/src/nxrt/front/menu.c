// The in-game settings menu (menu.h, docs/in-game-menu.md). Our own; the font is Aileron (dotcolon.net, CC0) as menu_font.py makes it.
// Threads: the window thread feeds events (menu_event: the open/close keys, navigation, a key being bound), the present thread draws (menu_build: the layout,
// the mouse, the choices made, their effect). The settings, the open state and the queued input are under one SDL mutex.
#include "menu.h"
#include "menu_font.h"
#include "front_map.h"
#include "dm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// What the menu changes (vk_tex.c, vk_window.c, vk_present.c, gfx_vk.c, front_sdl.c) and reads (hle_oe.c); DM_CTL's "menu" lines (hle_hid.c)
void vkt_request_scale(float s);
float vkt_scale(void);
void vkw_request_fullscreen(int on);
void vkw_request_size(int w, int h);
void vkw_desktop_size(int *w, int *h);
void vkw_pixel_size(int *w, int *h);
int vkw_is_fullscreen(void);
void vkp_set_upscale(int sharp, float sharpness);
void vkp_set_mode(int mode);
void vk_set_fps_target(int fps);
int dm_opmode(void);
void front_sdl_keymap_get(front_keymap *m);
void front_sdl_keymap_set(const front_keymap *m);
void front_sdl_menu_opened(int open);
extern void (*dm_ctl_menu)(const char *cmd);

void menu_font_atlas(const uint8_t **px, int *w, int *h) { *px = mf_atlas; *w = MF_W; *h = MF_H; }

// ---------- Settings: what the launcher's Display and Controls tabs keep (display.json, controls.json) ----------
// Every size shown is the size the game really draws its picture at, in the mode that is running (its own: 1920x1080 docked, 1280x720 handheld)
static const int res_list_h[4] = {720, 1080, 1440, 2160};   // the Resolution list after Default: those at least the game's own height (16:9)
static const char *const win_sizes[5] = {"1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160"};
typedef struct {
    char res[24];                                                         // "default" or "<w>x<h>" as saved (an older size keeps its string: it means that size)
    int fullscreen, win, console, sharp, sharpness, fps30, vsync, counter, wide;   // indexes and switches; sharpness 0..10; counter 0 off, 1 corner, 2 detailed
    front_keymap keys;
} settings;
static struct {
    SDL_Mutex *mu; int ready;
    settings s;
    int open, tab, sel, dd, dd_sel, capture;   // dd: the open dropdown's row (-1 none); capture: the action whose key is awaited (-1 none)
    int wide_now;                              // the wide view this run started with (a change takes effect at the next start)
    float mx, my; int mouse_moved;
    struct { int kind, a; float x, y; } q[64]; int nq;   // input for the next build: navigation and clicks
    int pad_back, pad_start; float stick_y, stick_x; uint64_t stick_t;
    char dir[1024], status[160]; uint64_t status_t;
    uint64_t fps_t0, fps_last; int fps_n; float fps, ms;   // the FPS counter: presents counted over half a second
} M = {.dd = -1, .capture = -1};
enum { IN_UP = 1, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_TABL, IN_TABR, IN_CLICK };

static const char *settings_dir(void) {
    if (M.dir[0]) return M.dir;
    const char *e = getenv("NXRT_SETTINGS_DIR");   // play.ps1 hands its folder (POKOPIA_SETTINGS_DIR in its tests)
    if (e && *e) snprintf(M.dir, sizeof M.dir, "%s", e);
#ifdef _WIN32
    else if ((e = getenv("APPDATA")) && *e) snprintf(M.dir, sizeof M.dir, "%s\\Pokopia Native", e);
#else
    else if ((e = getenv("XDG_CONFIG_HOME")) && *e) snprintf(M.dir, sizeof M.dir, "%s/pokopia-native", e);
    else if ((e = getenv("HOME")) && *e) snprintf(M.dir, sizeof M.dir, "%s/.config/pokopia-native", e);
#endif
    return M.dir;
}
static FILE *settings_file(const char *name, const char *mode) {
    char p[1100]; snprintf(p, sizeof p, "%s/%s", settings_dir(), name);
    if (*mode == 'w') SDL_CreateDirectory(settings_dir());
    return fopen(p, mode);
}

// The height a choice draws at: at least the game's own (a smaller saved size draws the game's own); "default" the monitor's height, at most 2160; a
// saved size at most 3240 (the 5760x3240 the first menu offered). The factor F-5 draws at is that height over the game's own. Pokopia's own picture is
// 1920x1080 docked and handheld alike (WindowCrop 1920x1080 and 1920x1080 targets with DM_OPMODE=0 too, measured 2026-11-06)
static int base_h(void) { return 1080; }
// "default" keeps to 2560x1440 on a GPU with 4 GB of its own memory or less (a 4 GB laptop card on a 4K monitor would run out at 3840x2160); the menu shows
// the size it picked
uint64_t vk_vram_bytes(void);
static int monitor_h(void) {
    int w = 0, h = 0, b = base_h(), top = 2160; uint64_t v = vk_vram_bytes();
    if (v && v <= (4608ull << 20)) top = 1440;   // 4 GB cards report about 4096 MB
    vkw_desktop_size(&w, &h);
    static int told; if (top < 2160 && h > top && !told) { told = 1; LOG("[menu] Default resolution kept to 2560x1440: the GPU has %llu MB of its own memory", (unsigned long long)(v >> 20)); }
    return h < b ? b : h > top ? top : h;
}
static int choice_h(const char *choice) {
    int b = base_h(), w, h;
    if (!choice || !strcmp(choice, "default")) return monitor_h();
    if (sscanf(choice, "%dx%d", &w, &h) == 2) return h < b ? b : h > 3240 ? 3240 : h;
    return b;
}
float menu_resolution_scale(const char *choice) {   // NXRT_VK_RESOLUTION / display.json "resolution": "default" or "<w>x<h>"; a bare number is a factor (tests)
    if (choice && *choice && strcmp(choice, "default") && !strchr(choice, 'x')) { float f = (float)atof(choice); return f >= 1 && f <= 4 ? f : 1.0f; }
    return (float)choice_h(choice) / base_h();
}
// The Resolution list in the running mode: Default, then the game's own size and up (docked 1920x1080, 2560x1440, 3840x2160; handheld 1280x720 first)
static int res_count(void) { int n = 1; for (int i = 0; i < 4; i++) n += res_list_h[i] >= base_h(); return n; }
static int res_entry_h(int j) { if (!j) return 0; for (int i = 0; i < 4; i++) if (res_list_h[i] >= base_h() && !--j) return res_list_h[i]; return base_h(); }
static void res_entry(int j, char *out, size_t n) { int h = res_entry_h(j); if (!h) snprintf(out, n, "default"); else snprintf(out, n, "%dx%d", h * 16 / 9, h); }
static int res_place(const char *choice) {   // the entry a saved choice is, else the nearest by height (an older size not in the list)
    if (!strcmp(choice, "default")) return 0;
    int h = choice_h(choice), best = 1;
    for (int j = 1; j < res_count(); j++) if (abs(res_entry_h(j) - h) < abs(res_entry_h(best) - h)) best = j;
    return best;
}
static void res_text(const char *choice, char *out, size_t n) {   // the size it draws at
    int h = choice_h(choice), w = h * 16 / 9;
    if (!strcmp(choice, "default")) snprintf(out, n, "Default: your monitor (%dx%d)", w, h);
    else snprintf(out, n, "%dx%d%s", w, h, h == base_h() ? " (the game's own)" : "");
}

static void save_display(void) {
    FILE *f = settings_file("display.json", "w"); if (!f) { LOG("[menu] display.json could not be written in %s", settings_dir()); return; }
    static const char *const cnt[3] = {"off", "corner", "detailed"};
    const settings *s = &M.s;
    fprintf(f, "{\n    \"console\":  %d,\n    \"resolution\":  \"%s\",\n    \"fullscreen\":  %s,\n    \"window\":  \"%s\",\n    \"upscale\":  \"%s\",\n    \"sharpness\":  %d,\n"
               "    \"fps\":  %d,\n    \"vsync\":  %s,\n    \"fps_counter\":  \"%s\",\n    \"wide_view\":  %s\n}\n", s->console, s->res, s->fullscreen ? "true" : "false",
            win_sizes[s->win], s->sharp ? "sharp" : "standard", s->sharpness, s->fps30 ? 30 : 60, s->vsync ? "true" : "false", cnt[s->counter], s->wide ? "true" : "false");
    fclose(f);
}
static void save_controls(void) {
    FILE *f = settings_file("controls.json", "w"); if (!f) { LOG("[menu] controls.json could not be written in %s", settings_dir()); return; }
    // the launcher's names (play.ps1 $ControlsDefault) for front_map.h's actions
    fprintf(f, "{\n    \"version\":  1,\n");
    for (int i = 0; i < FRONT_ACTS; i++) fprintf(f, "    \"%s\":  %d%s\n", front_act_names[i], (int)M.s.keys.key[i], i + 1 < FRONT_ACTS ? "," : "");
    fprintf(f, "}\n");
    fclose(f);
}
static void status(const char *t) { snprintf(M.status, sizeof M.status, "%s", t); M.status_t = SDL_GetTicksNS(); }

// ---------- The rows of the Graphics tab ----------
enum { G_RES, G_DISPLAY, G_WIN, G_CONSOLE, G_UPSCALE, G_SHARP, G_FPS, G_VSYNC, G_COUNTER, G_WIDE, G_N };
static const char *const g_label[G_N] = {"Resolution", "Display mode", "Window size", "Console mode", "Upscaling", "Sharpness", "Frame rate", "Vsync", "FPS counter",
                                         "Wide view"};
static const char *const g_hint[G_N] = {
    "The size the game draws its picture at. Bigger looks sharper but works your graphics card harder. Default matches your monitor.",
    "A window, or the whole screen (F11 and Alt+Enter switch too).",
    "The window's size on the desktop (in a window only).",
"Docked is the game's TV mode, handheld its portable mode; both draw 1920x1080 here. Changes at the next start; Resolution keeps its size.",
    "How the picture is stretched when the window is larger than it: Sharp, or Standard (softer).",
    "How strong Sharp upscaling is.",
    "60, or 30 to spare the computer (the game keeps its speed).",
    "On: no tearing. Off: a little less delay, may tear.",
    "Shows frames per second in a corner. Detailed adds frame time and the resolution.",
    "Experimental. On a screen wider than 16:9, the game's view fills it instead of bars; menus and the HUD stay in the middle. Changes at the next start."};
static int g_count(int row) { return row == G_RES ? res_count() : row == G_WIN ? 5 : row == G_SHARP ? 11 : row == G_COUNTER ? 3 : 2; }
static int g_value(int row) {
    const settings *s = &M.s;
    switch (row) { case G_RES: return res_place(s->res); case G_DISPLAY: return s->fullscreen; case G_WIN: return s->win; case G_CONSOLE: return !s->console;
                   case G_UPSCALE: return !s->sharp; case G_SHARP: return s->sharpness; case G_FPS: return s->fps30; case G_VSYNC: return !s->vsync;
                   case G_WIDE: return s->wide; default: return s->counter; }
}
static void g_text(int row, int v, char *out, size_t n) {
    switch (row) {
    case G_RES: { char c[24]; res_entry(v, c, sizeof c); res_text(c, out, n); break; }   // a list entry (the row shows the saved choice: res_text)
    case G_DISPLAY: snprintf(out, n, v ? "Full screen" : "Window"); break;
    case G_WIN: snprintf(out, n, "%s", win_sizes[v]); break;
    case G_CONSOLE: snprintf(out, n, "%s%s", v ? "Handheld" : "Docked (TV)", !v != (dm_opmode() != 0) ? " from the next start" : ""); break;
    case G_UPSCALE: snprintf(out, n, v ? "Standard (softer)" : "Sharp"); break;
    case G_SHARP: snprintf(out, n, "%d", v); break;
    case G_FPS: snprintf(out, n, v ? "30 fps" : "60 fps"); break;
    case G_VSYNC: snprintf(out, n, v ? "Off" : "On"); break;
    case G_WIDE: snprintf(out, n, "%s%s", v ? "On (experimental)" : "Off", v != M.wide_now ? " from the next start" : ""); break;
    default: snprintf(out, n, v == 2 ? "Detailed" : v ? "Corner" : "Off"); break;
    }
}
// A choice made: into the settings, into the game at once (console mode at the next start), into display.json
static void g_set(int row, int v) {
    settings *s = &M.s; char t[120], vt[80];
    switch (row) {
    case G_RES: res_entry(v, s->res, sizeof s->res); vkt_request_scale(menu_resolution_scale(s->res)); break;
    case G_DISPLAY: s->fullscreen = v; vkw_request_fullscreen(v); if (!v) { int w, h; sscanf(win_sizes[s->win], "%dx%d", &w, &h); vkw_request_size(w, h); } break;
    case G_WIN: s->win = v; if (!s->fullscreen) { int w, h; sscanf(win_sizes[v], "%dx%d", &w, &h); vkw_request_size(w, h); } break;
    case G_CONSOLE: s->console = !v; break;   // at the next start: switched live, player 1's controller would become the handheld one under the game
    case G_UPSCALE: s->sharp = !v; vkp_set_upscale(s->sharp, s->sharpness / 10.0f); break;
    case G_SHARP: s->sharpness = v; vkp_set_upscale(s->sharp, s->sharpness / 10.0f); break;
    case G_FPS: s->fps30 = v; vk_set_fps_target(v ? 30 : 60); break;
    case G_VSYNC: s->vsync = !v; vkp_set_mode(s->vsync ? 0 : 1); break;   // VKP_FIFO / VKP_MAILBOX
    case G_WIDE: s->wide = v; break;   // at the next start: the camera hook is set up as the game starts (host/rt.c)
    default: s->counter = v; M.fps_t0 = 0; break;
    }
    save_display();
    if (row == G_RES) res_text(s->res, vt, sizeof vt); else g_text(row, v, vt, sizeof vt);
    snprintf(t, sizeof t, "%s: %s", g_label[row], vt); status(t);
    LOG("[menu] %s", t);
}
// The picture against the window: drawn larger than it (the extra detail is not seen), smaller (stretched: Upscaling and Sharpness matter), or the same
static int picture_fit(int *ww, int *wh) {
    vkw_pixel_size(ww, wh);
    float ph = base_h() * vkt_scale(), pw = ph * 16 / 9;
    if (*ww <= 0 || *wh <= 0) return 0;
    float k = fminf(*ww / pw, *wh / ph);
    return k < 0.99f ? -1 : k > 1.01f ? 1 : 0;
}

// ---------- The Controls tab: the launcher's labels, two columns ----------
static const char *const c_label[FRONT_ACTS] = {"A", "B", "X", "Y", "Left stick press", "Right stick press", "L", "R", "ZL", "ZR", "+ (Plus)", "- (Minus)",
                                                "D-pad left", "D-pad up", "D-pad right", "D-pad down", "Stick up", "Stick left", "Stick down", "Stick right", "Shake"};
enum { C_RESET = FRONT_ACTS, C_N };   // the last row: back to the default keys
static void key_name(SDL_Scancode sc, char *out, size_t n) { const char *k = sc ? SDL_GetScancodeName(sc) : ""; snprintf(out, n, "%s", k && *k ? k : "(none)"); }
static void c_bind(int act, SDL_Scancode sc) {   // a key for an action; the action that had it takes this one's old key (the two swap)
    char t[120], kn[40]; SDL_Scancode old = M.s.keys.key[act];
    for (int i = 0; i < FRONT_ACTS; i++) if (i != act && M.s.keys.key[i] == sc) M.s.keys.key[i] = old;
    M.s.keys.key[act] = sc; front_sdl_keymap_set(&M.s.keys); save_controls();
    key_name(sc, kn, sizeof kn); snprintf(t, sizeof t, "%s: %s", c_label[act], kn); status(t); LOG("[menu] key %s", t);
}
static void c_reset(void) { front_keymap_default(&M.s.keys); front_sdl_keymap_set(&M.s.keys); save_controls(); status("Controls: back to the default keys"); LOG("[menu] the default keys"); }

// ---------- The state at start: what the game was started with ----------
void menu_init(void) {
    if (!M.mu) M.mu = SDL_CreateMutex();
    settings *s = &M.s; const char *e;
    if ((e = getenv("NXRT_VK_RESOLUTION")) && *e && (!strcmp(e, "default") || strchr(e, 'x'))) snprintf(s->res, sizeof s->res, "%s", e);
    else {   // a factor (NXRT_VK_SCALE, tests): the size it draws at
        e = getenv("NXRT_VK_SCALE"); float f = e && *e ? (float)atof(e) : 1.0f; if (!(f >= 1 && f <= 4)) f = 1;
        int h = (int)(f * base_h() + 0.5f); snprintf(s->res, sizeof s->res, "%dx%d", h * 16 / 9, h);
    }
    e = getenv("NXRT_FPS_COUNTER"); s->counter = !e ? 0 : !strcmp(e, "detailed") || !strcmp(e, "2") ? 2 : !strcmp(e, "corner") || !strcmp(e, "1") ? 1 : 0;
    e = getenv("NXRT_VK_FULLSCREEN"); s->fullscreen = e && atoi(e);
    e = getenv("NXRT_VK_WINDOW"); s->win = 0; for (int i = 0; e && i < 5; i++) if (!strcmp(e, win_sizes[i])) s->win = i;
    s->console = dm_opmode() != 0;
    e = getenv("NXRT_VK_UPSCALE"); s->sharp = !(e && !strcmp(e, "standard"));
    e = getenv("NXRT_VK_SHARPNESS"); s->sharpness = e && *e ? (int)(atof(e) * 10 + 0.5) : 5; if (s->sharpness < 0 || s->sharpness > 10) s->sharpness = 5;
    e = getenv("DM_FPS"); s->fps30 = e && atoi(e) == 30;
    e = getenv("NXRT_VK_PRESENT"); s->vsync = !(e && (!strcmp(e, "mailbox") || !strcmp(e, "immediate")));
    e = getenv("NXRT_VK_WIDE"); s->wide = M.wide_now = e && *e == '1';
    front_sdl_keymap_get(&s->keys);
    dm_ctl_menu = menu_ctl;
    M.ready = 1;
    LOG("[menu] the in-game menu: F1, or a controller's Minus + Plus together; settings in %s", settings_dir());
}
int menu_is_open(void) { return M.open; }
int menu_active(void) { return M.ready && (M.open || M.s.counter); }

static void set_open(int open) {
    if (open == M.open) return;
    M.open = open; M.dd = -1; M.capture = -1; M.nq = 0; M.status[0] = 0;
    front_sdl_menu_opened(open);   // the game's input released while open (and the mouse shown), taken up again after
    LOG("[menu] %s", open ? "opened" : "closed");
}
static void push_in(int kind, int a, float x, float y) { if (M.nq < 64) { M.q[M.nq].kind = kind; M.q[M.nq].a = a; M.q[M.nq].x = x; M.q[M.nq].y = y; M.nq++; } }
// A controller button's meaning by its label (A confirms, B goes back, whatever the layout), else by its place
static int pad_ok(SDL_Gamepad *g, int b) { SDL_GamepadButtonLabel l = g ? SDL_GetGamepadButtonLabel(g, (SDL_GamepadButton)b) : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
                                          return l == SDL_GAMEPAD_BUTTON_LABEL_A || (l == SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN && b == SDL_GAMEPAD_BUTTON_SOUTH); }
static int pad_back(SDL_Gamepad *g, int b) { SDL_GamepadButtonLabel l = g ? SDL_GetGamepadButtonLabel(g, (SDL_GamepadButton)b) : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
                                            return l == SDL_GAMEPAD_BUTTON_LABEL_B || (l == SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN && b == SDL_GAMEPAD_BUTTON_EAST); }

// The mouse in the swapchain's pixels (SDL gives window coordinates, fewer on a scaled display)
static float density(SDL_WindowID id) { SDL_Window *w = SDL_GetWindowFromID(id); float d = w ? SDL_GetWindowPixelDensity(w) : 1.0f; return d > 0 ? d : 1.0f; }

int menu_event(const SDL_Event *e) {
    if (!M.ready || !M.mu) return 0;
    int taken = 0;
    SDL_LockMutex(M.mu);
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (e->key.scancode == SDL_SCANCODE_F1 && !e->key.repeat) { set_open(!M.open); taken = 1; break; }
        if (!M.open) break;
        taken = 1;
        if (M.capture >= 0) {   // binding a key: Esc cancels; the keys the launcher refuses (the window's and Windows') are refused here too
            SDL_Scancode sc = e->key.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) { M.capture = -1; status("Not changed"); }
            else if (front_key_reserved(sc) || sc == SDL_SCANCODE_F1 || sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_LGUI || sc == SDL_SCANCODE_RGUI)
                status("That key belongs to the window or to Windows: choose another");
            else { c_bind(M.capture, sc); M.capture = -1; }
            break;
        }
        switch (e->key.scancode) {
        case SDL_SCANCODE_UP: case SDL_SCANCODE_W: push_in(IN_UP, 0, 0, 0); break;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: push_in(IN_DOWN, 0, 0, 0); break;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: push_in(IN_LEFT, 0, 0, 0); break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: push_in(IN_RIGHT, 0, 0, 0); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: push_in(IN_OK, 0, 0, 0); break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: if (M.dd >= 0) push_in(IN_BACK, 0, 0, 0); else set_open(0); break;
        case SDL_SCANCODE_Q: case SDL_SCANCODE_PAGEUP: push_in(IN_TABL, 0, 0, 0); break;
        case SDL_SCANCODE_E: case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_TAB: push_in(IN_TABR, 0, 0, 0); break;
        default: break;
        }
        break;
    case SDL_EVENT_KEY_UP: taken = M.open || e->key.scancode == SDL_SCANCODE_F1; break;
    case SDL_EVENT_MOUSE_MOTION: if (M.open) { float d = density(e->motion.windowID); M.mx = e->motion.x * d; M.my = e->motion.y * d; M.mouse_moved = 1; taken = 1; } break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!M.open) break;
        taken = 1;
        if (e->button.button == SDL_BUTTON_LEFT) { float d = density(e->button.windowID); push_in(IN_CLICK, 0, e->button.x * d, e->button.y * d); }
        else if (e->button.button == SDL_BUTTON_RIGHT) push_in(IN_BACK, 0, 0, 0);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL: taken = M.open; break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        int down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, b = e->gbutton.button;
        SDL_Gamepad *g = SDL_GetGamepadFromID(e->gbutton.which);
        if (b == SDL_GAMEPAD_BUTTON_BACK) M.pad_back = down;
        if (b == SDL_GAMEPAD_BUTTON_START) M.pad_start = down;
        if (down && M.pad_back && M.pad_start && (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_START)) { set_open(!M.open); taken = 1; break; }   // Minus + Plus
        if (!M.open) break;
        taken = 1;
        if (!down || M.capture >= 0) break;   // keys are bound with the keyboard
        if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) push_in(IN_UP, 0, 0, 0); else if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) push_in(IN_DOWN, 0, 0, 0);
        else if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) push_in(IN_LEFT, 0, 0, 0); else if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) push_in(IN_RIGHT, 0, 0, 0);
        else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) push_in(IN_TABL, 0, 0, 0); else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) push_in(IN_TABR, 0, 0, 0);
        else if (pad_ok(g, b)) push_in(IN_OK, 0, 0, 0);
        else if (pad_back(g, b)) { if (M.dd >= 0) push_in(IN_BACK, 0, 0, 0); else set_open(0); }
        break; }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (!M.open) break;
        taken = 1;
        if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY || e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {   // the left stick steps like the D-pad, again every 0.2 s while held
            float v = e->gaxis.value / 32767.0f; uint64_t now = SDL_GetTicksNS(); int y = e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
            float *was = y ? &M.stick_y : &M.stick_x;
            int dir = v > 0.6f ? 1 : v < -0.6f ? -1 : 0, wdir = *was > 0.6f ? 1 : *was < -0.6f ? -1 : 0;
            if (dir && (dir != wdir || now - M.stick_t > 200000000ull)) { push_in(y ? (dir > 0 ? IN_DOWN : IN_UP) : (dir > 0 ? IN_RIGHT : IN_LEFT), 0, 0, 0); M.stick_t = now; }
            *was = v;
        }
        break;
    default: break;
    }
    SDL_UnlockMutex(M.mu);
    return taken;
}

// ---------- Drawing ----------
#define RGBA(r, g, b, a) ((uint32_t)(r) | (uint32_t)(g) << 8 | (uint32_t)(b) << 16 | (uint32_t)(a) << 24)
static const uint32_t C_DIM = RGBA(24, 18, 48, 150), C_PANEL = RGBA(250, 248, 255, 250), C_EDGE = RGBA(200, 186, 246, 255), C_TITLE = RGBA(66, 54, 128, 255),
                      C_TEXT = RGBA(64, 60, 96, 255), C_SOFT = RGBA(130, 124, 160, 255), C_PILL = RGBA(238, 233, 253, 255), C_SEL = RGBA(255, 222, 70, 255),
                      C_SELTEXT = RGBA(70, 52, 0, 255), C_TAB = RGBA(229, 221, 251, 255), C_LIST = RGBA(255, 255, 255, 255), C_OFF = RGBA(170, 166, 190, 255);
static void q_add(mn_list *l, mn_quad q) {
    if (l->n == l->cap) { int c = l->cap ? 2 * l->cap : 1024; mn_quad *n = realloc(l->q, (size_t)c * sizeof *n); if (!n) return; l->q = n; l->cap = c; }
    l->q[l->n++] = q;
}
static void rect(mn_list *l, float x, float y, float w, float h, float r, uint32_t c) { q_add(l, (mn_quad){x, y, x + w, y + h, 0, 0, 0, 0, r, c, MN_RECT}); }
static float text_w(const char *t, float px) { float s = px / MF_EM, w = 0; for (; *t; t++) { int c = (unsigned char)*t; if (c >= 32 && c < 127) w += mf_glyphs[c - 32].adv * s; } return w; }
// Text with its top at y (the font's ascent below it is the baseline)
static void text(mn_list *l, const char *t, float x, float y, float px, uint32_t c) {
    float s = px / MF_EM, base = y + MF_ASCENT * s;
    for (; *t; t++) {
        int ch = (unsigned char)*t; if (ch < 32 || ch >= 127) ch = '?';
        const mf_glyph *g = &mf_glyphs[ch - 32];
        if (g->w) q_add(l, (mn_quad){x + g->bx * s, base + g->by * s, x + (g->bx + g->w) * s, base + (g->by + g->h) * s, (float)g->x / MF_W, (float)g->y / MF_H,
                                     (float)(g->x + g->w) / MF_W, (float)(g->y + g->h) / MF_H, 0, c, MN_GLYPH});
        x += g->adv * s;
    }
}
static int inside(float x, float y, float x0, float y0, float w, float h) { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }
static float fit(const char *t, float px, float w, float u) { while (px > 13 * u && text_w(t, px) > w) px *= 0.95f; return px; }   // smaller until it fits

// Greyed: no effect as things are (the window's size in full screen; upscaling and its sharpness when the picture is not stretched to the window)
static int row_off(int r, int fitk) { return (r == G_WIN && M.s.fullscreen) || ((r == G_UPSCALE || r == G_SHARP) && fitk <= 0); }
static void counter_draw(mn_list *out, int W, int H) {   // the FPS counter, top left
    uint64_t now = SDL_GetTicksNS();
    if (!M.fps_t0 || now - M.fps_last > 1000000000ull) { M.fps_t0 = now; M.fps_n = 0; }
    M.fps_last = now; M.fps_n++;
    if (now - M.fps_t0 >= 500000000ull) { double dt = (now - M.fps_t0) / 1e9; M.fps = (float)((M.fps_n - 1) / dt); M.ms = M.fps_n > 1 ? (float)(dt * 1e3 / (M.fps_n - 1)) : 0; M.fps_t0 = now; M.fps_n = 1; }
    if (!M.s.counter) return;
    char t[96]; float u = fmaxf(H / 1080.0f, 0.6f), fs = 22 * u;
    if (M.s.counter == 1) snprintf(t, sizeof t, "%.0f fps", M.fps);
    else { int h = (int)(base_h() * vkt_scale() + 0.5f); snprintf(t, sizeof t, "%.0f fps  |  %.1f ms  |  %dx%d", M.fps, M.ms, (h * 16 + 4) / 9, h); }
    float w = text_w(t, fs) + 32 * u, h = 40 * u;
    rect(out, 16 * u, 16 * u, w, h, h / 2, C_PANEL);
    text(out, t, 32 * u, 16 * u + (h - fs) / 2, fs, C_TITLE);
}

void menu_build(mn_list *out, int W, int H) {
    out->n = 0;
    if (!M.ready) return;
    SDL_LockMutex(M.mu);
    counter_draw(out, W, H);
    if (!M.open) { SDL_UnlockMutex(M.mu); return; }
    settings *s = &M.s;
    s->fullscreen = vkw_is_fullscreen();   // F11 / Alt+Enter change it too
    int ww, wh, fitk = picture_fit(&ww, &wh);
    float u = H / 1080.0f; if (u < 0.6f) u = 0.6f;
    float pw = fminf(W * 0.9f, 1240 * u), ph = fminf(H * 0.9f, 920 * u), px = (W - pw) / 2, py = (H - ph) / 2;
    float row_h = 54 * u, fs = 26 * u, x_lab = px + 48 * u, val_w = fminf(560 * u, pw * 0.5f), x_val = px + pw - 48 * u - val_w, y_rows = py + 168 * u;
    int nrows = M.tab == 0 ? G_N : C_N;
    // the controls in two columns
    int ccol = (C_N + 1) / 2; float c_row = 48 * u, c_colw = (pw - 96 * u - 32 * u) / 2;
    // input, in the order it came
    for (int i = 0; i < M.nq; i++) {
        int k = M.q[i].kind;
        if (k == IN_TABL || k == IN_TABR) { if (M.dd < 0 && M.capture < 0) { M.tab = !M.tab; M.sel = 0; M.status[0] = 0; } continue; }   // the last change belongs to its tab
        if (M.dd >= 0) {   // a dropdown is open
            int n = g_count(M.dd);
            if (k == IN_UP) M.dd_sel = (M.dd_sel + n - 1) % n; else if (k == IN_DOWN) M.dd_sel = (M.dd_sel + 1) % n;
            else if (k == IN_OK) { g_set(M.dd, M.dd_sel); M.dd = -1; }
            else if (k == IN_BACK) M.dd = -1;
            else if (k == IN_CLICK) {
                float lx = x_val, ly = y_rows + M.dd * row_h + row_h, lh = 44 * u; if (ly + n * lh > py + ph - 20 * u) ly = py + ph - 20 * u - n * lh;
                int hit = -1; for (int j = 0; j < n; j++) if (inside(M.q[i].x, M.q[i].y, lx, ly + j * lh, val_w, lh)) hit = j;
                if (hit >= 0) g_set(M.dd, hit);
                M.dd = -1;
            }
            continue;
        }
        if (k == IN_UP) M.sel = M.tab == 0 ? (M.sel + nrows - 1) % nrows : (M.sel % ccol ? M.sel - 1 : M.sel);
        else if (k == IN_DOWN) M.sel = M.tab == 0 ? (M.sel + 1) % nrows : ((M.sel % ccol) + 1 < ccol && M.sel + 1 < C_N ? M.sel + 1 : M.sel);
        else if ((k == IN_LEFT || k == IN_RIGHT) && M.tab == 0) {   // left / right step a choice without opening the list
            int n = g_count(M.sel), v = g_value(M.sel), nv = k == IN_LEFT ? v - 1 : v + 1;
            if (row_off(M.sel, fitk)) continue;
            if (nv >= 0 && nv < n) g_set(M.sel, nv);
        } else if ((k == IN_LEFT || k == IN_RIGHT) && M.tab == 1) { int c2 = k == IN_LEFT ? M.sel - ccol : M.sel + ccol; if (c2 >= 0 && c2 < C_N) M.sel = c2; }
        else if (k == IN_OK) {
            if (M.tab == 0) { if (row_off(M.sel, fitk)) {} else if (M.sel == G_SHARP) g_set(G_SHARP, (s->sharpness + 1) % 11); else { M.dd = M.sel; M.dd_sel = g_value(M.sel); } }
            else if (M.sel == C_RESET) c_reset(); else { M.capture = M.sel; status("Press a key (Esc: keep it)"); }
        } else if (k == IN_BACK) {}
        else if (k == IN_CLICK) {
            float cx = M.q[i].x, cy = M.q[i].y;
            for (int t = 0; t < 2; t++) if (inside(cx, cy, px + pw - 48 * u - (2 - t) * 200 * u - (1 - t) * 12 * u, py + 40 * u, 200 * u, 56 * u) && M.tab != t) { M.tab = t; M.sel = 0; M.status[0] = 0; }
            if (M.tab == 0) for (int r = 0; r < G_N; r++) {
                if (!inside(cx, cy, px + 24 * u, y_rows + r * row_h, pw - 48 * u, row_h)) continue;
                M.sel = r;
                if (inside(cx, cy, x_val, y_rows + r * row_h, val_w, row_h) && !row_off(r, fitk)) { if (r == G_SHARP) g_set(G_SHARP, (s->sharpness + 1) % 11); else { M.dd = r; M.dd_sel = g_value(r); } }
            } else for (int r = 0; r < C_N; r++) {
                float cx0 = px + 48 * u + (r / ccol) * (c_colw + 32 * u), cy0 = y_rows + (r % ccol) * c_row;
                if (inside(cx, cy, cx0, cy0, c_colw, c_row)) { M.sel = r; if (r == C_RESET) c_reset(); else { M.capture = r; status("Press a key (Esc: keep it)"); } }
            }
            if (inside(cx, cy, px + pw - 48 * u - 180 * u, py + ph - 76 * u, 180 * u, 52 * u)) set_open(0);
        }
    }
    M.nq = 0;
    if (!M.open) { SDL_UnlockMutex(M.mu); return; }
    int moved = M.mouse_moved; M.mouse_moved = 0;   // the mouse picks what it moves over (and leaves the keys' choice alone while it rests)
    if (moved && M.dd < 0 && M.capture < 0) {
        if (M.tab == 0) { for (int r = 0; r < G_N; r++) if (inside(M.mx, M.my, px + 24 * u, y_rows + r * row_h, pw - 48 * u, row_h)) M.sel = r; }
        else for (int r = 0; r < C_N; r++) if (inside(M.mx, M.my, px + 48 * u + (r / ccol) * (c_colw + 32 * u), y_rows + (r % ccol) * c_row, c_colw, c_row)) M.sel = r;
    }
    if (M.sel >= nrows) M.sel = nrows - 1;
    // the panel
    rect(out, 0, 0, (float)W, (float)H, 0, C_DIM);
    rect(out, px - 3 * u, py - 3 * u, pw + 6 * u, ph + 6 * u, 30 * u, C_EDGE);
    rect(out, px, py, pw, ph, 28 * u, C_PANEL);
    text(out, "Settings", px + 48 * u, py + 44 * u, 44 * u, C_TITLE);
    static const char *const tabs[2] = {"Graphics", "Controls"};
    for (int t = 0; t < 2; t++) {
        float tx = px + pw - 48 * u - (2 - t) * 200 * u - (1 - t) * 12 * u, ty = py + 40 * u;
        rect(out, tx, ty, 200 * u, 56 * u, 28 * u, M.tab == t ? C_SEL : C_TAB);
        text(out, tabs[t], tx + (200 * u - text_w(tabs[t], fs)) / 2, ty + 14 * u, fs, M.tab == t ? C_SELTEXT : C_TEXT);
    }
    rect(out, px + 48 * u, py + 126 * u, pw - 96 * u, 3 * u, 1.5f * u, C_TAB);
    char v[200];
    if (M.tab == 0) {
        for (int r = 0; r < G_N; r++) {
            float y = y_rows + r * row_h; int unused = (r == G_UPSCALE || r == G_SHARP) && fitk <= 0, off = row_off(r, fitk) || (r == G_SHARP && !s->sharp);
            if (r == M.sel && M.dd < 0) rect(out, px + 24 * u, y + 4 * u, pw - 48 * u, row_h - 8 * u, (row_h - 8 * u) / 2, RGBA(255, 244, 196, 255));
            text(out, g_label[r], x_lab, y + (row_h - fs) / 2, fs, off ? C_OFF : C_TEXT);
            rect(out, x_val, y + 8 * u, val_w, row_h - 16 * u, (row_h - 16 * u) / 2, r == M.sel && !unused ? C_SEL : C_PILL);
            if (unused) snprintf(v, sizeof v, "Not used: the picture isn't stretched");   // the reason in place of the value
            else if (r == G_RES) res_text(s->res, v, sizeof v); else g_text(r, g_value(r), v, sizeof v);
            float vs = fs * 0.9f; while (vs > 14 * u && text_w(v, vs) > val_w - 72 * u) vs *= 0.94f;
            text(out, v, x_val + 24 * u, y + (row_h - vs) / 2, vs, r == M.sel && !unused ? C_SELTEXT : off ? C_OFF : C_TEXT);
            if (unused) {}
            else if (r != G_SHARP) text(out, "v", x_val + val_w - 40 * u, y + (row_h - fs) / 2 - 2 * u, fs * 0.85f, r == M.sel ? C_SELTEXT : C_SOFT);
            else { float bw = val_w - 160 * u, bx = x_val + 120 * u, by = y + row_h / 2 - 4 * u;   // a bar for the sharpness
                   rect(out, bx, by, bw, 8 * u, 4 * u, r == M.sel ? RGBA(255, 246, 200, 255) : C_TAB); rect(out, bx, by, bw * s->sharpness / 10.0f, 8 * u, 4 * u, C_TITLE); }
        }
        // the picture against the window, in plain words, when it matters
        if (fitk < 0) snprintf(v, sizeof v, "Your window (%dx%d) is smaller than the picture, so you won't see the extra detail. Make the window bigger or use full screen.", ww, wh);
        else if (fitk > 0) snprintf(v, sizeof v, "The picture is stretched to fill your window. Upscaling and Sharpness choose how that looks.");
        if (fitk) text(out, v, x_lab, y_rows + G_N * row_h + 10 * u, fit(v, 21 * u, pw - 96 * u, u), C_TITLE);

    } else {
        for (int r = 0; r < C_N; r++) {
            float x = px + 48 * u + (r / ccol) * (c_colw + 32 * u), y = y_rows + (r % ccol) * c_row, cfs = 22 * u;
            int sel = r == M.sel;
            if (r == C_RESET) { rect(out, x, y + 4 * u, c_colw, c_row - 8 * u, (c_row - 8 * u) / 2, sel ? C_SEL : C_TAB);
                                text(out, "Reset to defaults", x + (c_colw - text_w("Reset to defaults", cfs)) / 2, y + (c_row - cfs) / 2, cfs, sel ? C_SELTEXT : C_TEXT); continue; }
            if (sel) rect(out, x - 8 * u, y + 3 * u, c_colw + 16 * u, c_row - 6 * u, (c_row - 6 * u) / 2, RGBA(255, 244, 196, 255));
            text(out, c_label[r], x + 12 * u, y + (c_row - cfs) / 2, cfs, C_TEXT);
            float kw = c_colw * 0.48f, kx = x + c_colw - kw;
            rect(out, kx, y + 7 * u, kw, c_row - 14 * u, (c_row - 14 * u) / 2, M.capture == r ? C_EDGE : sel ? C_SEL : C_PILL);
            if (M.capture == r) snprintf(v, sizeof v, "press a key..."); else key_name(s->keys.key[r], v, sizeof v);
            text(out, v, kx + (kw - text_w(v, cfs)) / 2, y + (c_row - cfs) / 2, cfs, sel ? C_SELTEXT : C_TEXT);
        }
    }
    // the hint, the last change, the keys, the Close button
    const char *hint = M.tab == 0 ? g_hint[M.sel] : M.sel == C_RESET ? "The keys the game starts with." : "Choose an action, then press the key for it. Controllers keep their buttons.";
    if (M.capture >= 0) hint = "Press the new key. Esc keeps the old one.";
    const char *keys = "Arrows / D-pad: choose    Enter / A: change    Q E / L R: tabs    Esc / B: back    F1 / Minus + Plus: close";
    float tw = pw - 96 * u, kw = pw - 96 * u - 200 * u;
    text(out, hint, px + 48 * u, py + ph - 128 * u, fit(hint, 21 * u, tw, u), C_SOFT);
    if (M.status[0] && SDL_GetTicksNS() - M.status_t < 4000000000ull) text(out, M.status, px + 48 * u, py + ph - 94 * u, fit(M.status, 21 * u, tw, u), C_TITLE);
    text(out, keys, px + 48 * u, py + ph - 58 * u, fit(keys, 19 * u, kw, u), C_SOFT);
    rect(out, px + pw - 48 * u - 180 * u, py + ph - 76 * u, 180 * u, 52 * u, 26 * u, C_TAB);
    text(out, "Close", px + pw - 48 * u - 180 * u + (180 * u - text_w("Close", fs)) / 2, py + ph - 76 * u + (52 * u - fs) / 2, fs, C_TEXT);
    // an open dropdown over everything
    if (M.dd >= 0) {
        int n = g_count(M.dd); float lh = 44 * u, lx = x_val, ly = y_rows + M.dd * row_h + row_h;
        if (ly + n * lh > py + ph - 20 * u) ly = py + ph - 20 * u - n * lh;
        rect(out, lx - 3 * u, ly - 3 * u, val_w + 6 * u, n * lh + 6 * u, 20 * u, C_EDGE);
        rect(out, lx, ly, val_w, n * lh, 18 * u, C_LIST);
        if (moved) for (int j = 0; j < n; j++) if (inside(M.mx, M.my, lx, ly + j * lh, val_w, lh)) M.dd_sel = j;
        for (int j = 0; j < n; j++) {
            float y = ly + j * lh; int on = j == M.dd_sel;
            if (on) rect(out, lx + 6 * u, y + 4 * u, val_w - 12 * u, lh - 8 * u, (lh - 8 * u) / 2, C_SEL);
            g_text(M.dd, j, v, sizeof v); float vs = fs * 0.85f; while (vs > 14 * u && text_w(v, vs) > val_w - 48 * u) vs *= 0.94f;
            text(out, v, lx + 24 * u, y + (lh - vs) / 2, vs, on ? C_SELTEXT : C_TEXT);
        }
    }
    SDL_UnlockMutex(M.mu);
}

// ---------- DM_CTL "menu ..." (tests): open, close, a tab, a setting by its row name and choice index, a row's list opened, a key by action index and
// scancode, reset ----------
void menu_ctl(const char *cmd) {
    if (!M.ready || !M.mu) return;
    static const char *const names[G_N] = {"resolution", "display", "window", "console", "upscale", "sharpness", "fps", "vsync", "counter", "wide"};
    while (*cmd == ' ') cmd++;
    SDL_LockMutex(M.mu);
    char name[32]; int a = -1, b = -1;
    if (!strncmp(cmd, "open", 4)) set_open(1);
    else if (!strncmp(cmd, "close", 5)) set_open(0);
    else if (!strncmp(cmd, "reset", 5)) c_reset();
    else if (sscanf(cmd, "key %d %d", &a, &b) == 2 && a >= 0 && a < FRONT_ACTS && !front_key_reserved(b)) c_bind(a, (SDL_Scancode)b);
    else if (sscanf(cmd, "set %31s %d", name, &a) == 2) { for (int r = 0; r < G_N; r++) if (!strcmp(name, names[r]) && a >= 0 && a < g_count(r)) { M.dd = -1; g_set(r, a); } }
    else if (sscanf(cmd, "list %31s", name) == 1) { for (int r = 0; r < G_N; r++) if (!strcmp(name, names[r]) && r != G_SHARP) { M.tab = 0; M.sel = M.dd = r; M.dd_sel = g_value(r); } }
    else if (!strncmp(cmd, "tab", 3)) { int t = atoi(cmd + 3) ? 1 : 0; if (t != M.tab) M.status[0] = 0; M.tab = t; M.sel = 0; M.dd = -1; }
    SDL_UnlockMutex(M.mu);
}
