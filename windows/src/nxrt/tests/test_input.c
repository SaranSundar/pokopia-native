/* Input (M6, docs/m6-input-gameplay.md): what the game reads for player 1 through nn::hid's GetNpadStates (full-key, npad No1: the profile's console mode)
   1. from the front end's paths: the keyboard (dm_input_set) and a controller (dm_pad_set_player) are ORed, the more deflected stick wins, a press shorter than a
      frame is latched for the next read, the stick-direction bits follow the sticks; a second controller is player 2 only once its npad is supported;
   2. from DM_KEYS frame steps, at the reads of their frames (keys_sync), a hold of 0 frames through the press latch; in a child process, a seconds list (keys_main);
   3. from DM_CTL: lines appended to its file with CRLF endings press with holds, "shot <path>" reaches the renderer's hook with the path intact, a line still being
      written waits for its end, "#" lines are skipped;
   4. pointer look without look_cam (no main module here): the stick path tilts the right stick fully for the degrees gathered;
   5. the front end's mapping (front_map.h): the Mac's key layout, controller buttons by position and by label, sticks through the dead zones with y turned up,
      triggers, the touch point on the letterboxed picture; the player slots (pad_slots.h). No game data, no window, no controller. */
#include "guest_test.h"
#include "hle_adapters.h"
#include "dm_input.h"
#include "front.h"
#include "front_map.h"
#include "pad_slots.h"
#include "ctl_gfx.h"

NXRT_EARLY_RESERVE()
void hid_keys_init(void);
void dm_guest_frame_tick(void);
uint64_t dm_guest_frame(void);

typedef struct { int64_t sampling; uint64_t buttons; int32_t lx, ly, rx, ry; uint32_t attributes, _pad; } NpadState;
static NpadState *g_st; static uint32_t *g_id;
static NpadState rd(uint32_t id) {   /* one read of npad id, as the game makes it: GetNpadStates(states, 1, id) */
    NxwGuestCpu c = *nxrt_cur_cpu(); *g_id = id;
    c.x[0] = (uint64_t)(uintptr_t)g_st; c.x[1] = 1; c.x[2] = (uint64_t)(uintptr_t)g_id;
    adapter__ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj(&c);
    return *g_st;
}
static NpadState p1(void) { return rd(0); }
static void next_frame(void) { dm_guest_frame_tick(); }
static int wait_btn(uint64_t bit, int want, int ms) {   /* reads until the button is (or is not) pressed */
    for (int t = 0; t < ms; t += 10) { if (!!(p1().buttons & bit) == want) return 1; Sleep(10); }
    return 0;
}

static char g_shot[1100]; static volatile LONG g_shots;
static void fake_shot(const char *path) { snprintf(g_shot, sizeof g_shot, "%s", path); InterlockedIncrement(&g_shots); }
static void append(const char *path, const char *text) { FILE *f = fopen(path, "ab"); fputs(text, f); fclose(f); }

static int child_seconds(void) {   /* DM_KEYS in seconds (keys_main's thread): X pressed from 0.3 s for 0.4 s */
    gt_init(); g_st = gt_alloc(sizeof *g_st); g_id = gt_alloc(4);
    hid_keys_init();
    uint64_t t0 = now_ns_test(); int ok = 1;
    ok &= !(p1().buttons & DM_BTN_X);
    ok &= wait_btn(DM_BTN_X, 1, 2000); double on = (now_ns_test() - t0) / 1e9;
    ok &= wait_btn(DM_BTN_X, 0, 2000); double off = (now_ns_test() - t0) / 1e9;
    printf("  child: DM_KEYS seconds step pressed at %.2f s, released at %.2f s\n", on, off);
    ok &= on >= 0.25 && on < 0.6 && off - on >= 0.3 && off - on < 0.8;
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--seconds")) return child_seconds();
    char ctl[MAX_PATH]; GetTempPathA(MAX_PATH, ctl); strcat(ctl, "nxrt-test-input-ctl.txt"); DeleteFileA(ctl);
    FILE *f = fopen(ctl, "wb"); fclose(f);
    _putenv_s("DM_CTL", ctl);
    _putenv_s("DM_KEYS", "f5:A~f2,f10:LUp~f3,f20:B~f0");
    _putenv_s("DM_OPMODE", "1");
    gt_init();
    g_st = gt_alloc(sizeof *g_st); g_id = gt_alloc(4);
    dm_gfx_ctl.shot_at = fake_shot;
    hid_keys_init();

    /* 2. DM_KEYS frame steps (the frame counter starts at 0 here) */
    while (dm_guest_frame() < 4) next_frame();
    T(!(p1().buttons & DM_BTN_A));
    next_frame(); T(p1().buttons & DM_BTN_A);                      /* frame 5 */
    next_frame(); T(p1().buttons & DM_BTN_A);                      /* frame 6: held for 2 frames */
    next_frame(); T(!(p1().buttons & DM_BTN_A));                   /* frame 7: let go */
    while (dm_guest_frame() < 10) next_frame();
    NpadState s = p1(); T(s.ly == DM_STICK_MAX && s.lx == 0 && (s.buttons & (1u << 17)));   /* LUp: the stick and its direction bit */
    while (dm_guest_frame() < 13) next_frame();
    s = p1(); T(s.ly == 0 && !(s.buttons & (1u << 17)));
    while (dm_guest_frame() < 20) next_frame();
    T(p1().buttons & DM_BTN_B);                                    /* a hold of 0 frames reaches its frame's read through the latch */
    T(!(p1().buttons & DM_BTN_B));
    printf("DM_KEYS frame steps: A at frames 5-6, LUp 10-12, B for one read at 20\n");

    /* 1. the front end's paths */
    dm_input_set(DM_BTN_A, 0, DM_STICK_MAX, 0, 0);
    dm_pad_set_player(0, DM_BTN_ZR, 1000, -20000, 0, 0);
    s = p1();
    T((s.buttons & 0xffff) == (DM_BTN_A | DM_BTN_ZR));           /* ORed */
    T(s.lx == 1000 && s.ly == DM_STICK_MAX);                       /* each axis: the more deflected source */
    dm_input_set(0, 0, 0, 0, 0); dm_pad_set_player(0, 0, 0, 0, 0, 0);
    s = p1(); T(!(s.buttons & 0xffff) && !s.lx && !s.ly);
    dm_input_set(DM_BTN_Y, 0, 0, 0, 0); dm_input_set(0, 0, 0, 0, 0);   /* pressed and let go between two reads */
    T(p1().buttons & DM_BTN_Y); T(!(p1().buttons & DM_BTN_Y));
    dm_pad_set_player(1, DM_BTN_X, 0, 0, 0, 0); dm_pad_players(3);   /* a second controller: the game supports No2 until it says otherwise */
    T(!(p1().buttons & DM_BTN_X) && (rd(1).buttons & DM_BTN_X) && (rd(1).attributes & 1));
    dm_pad_players(1); T(!(rd(1).attributes & 1));
    printf("front end paths: ORed buttons, the more deflected stick, the press latch, a second player on No2\n");

    /* 4. pointer look, the stick path (no main module, so no look_cam) */
    T(dm_look_on());
    next_frame(); p1();
    dm_look_add(5.0f, 0.0f);
    Sleep(20); next_frame(); s = p1();
    T(s.rx == DM_STICK_MAX && s.ry == 0);
    int frames = 0; for (; frames < 20; frames++) { Sleep(17); next_frame(); s = p1(); if (!s.rx) break; }
    printf("pointer look: 5 degrees right tilted the right stick fully, for %d more frame(s)\n", frames);
    T(frames >= 1 && frames <= 5);

    /* 3. DM_CTL */
    append(ctl, "# a comment\r\nB~0.4\r\n");
    T(wait_btn(DM_BTN_B, 1, 1500)); T(wait_btn(DM_BTN_B, 0, 1500));
    append(ctl, "shot C:\\some folder\\shot 1.png\r\n");
    for (int i = 0; i < 100 && !g_shots; i++) Sleep(10);
    T(g_shots == 1 && !strcmp(g_shot, "C:\\some folder\\shot 1.png"));
    append(ctl, "X+Down~0.3");   /* no line end yet: not read */
    Sleep(400); T(!(p1().buttons & DM_BTN_X));
    append(ctl, "\n");
    T(wait_btn(DM_BTN_X, 1, 1500)); s = p1(); T(s.buttons & DM_BTN_DOWN); T(wait_btn(DM_BTN_X, 0, 1500));
    printf("DM_CTL: a comment skipped, B held, a screenshot path intact, a partial line waited for\n");

    /* 2b. DM_KEYS in seconds, in a child process (hid_keys_init reads the environment once) */
    {
        char self[MAX_PATH], cmd[MAX_PATH + 32]; GetModuleFileNameA(NULL, self, MAX_PATH); snprintf(cmd, sizeof cmd, "\"%s\" --seconds", self);
        _putenv_s("DM_KEYS", "0.3:X~0.4"); _putenv_s("DM_CTL", "");
        STARTUPINFOA si = {sizeof si}; PROCESS_INFORMATION pi; DWORD code = 99;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 20000); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
        T(code == 0);
        printf("DM_KEYS seconds steps (child process): %s\n", code ? "FAIL" : "ok");
    }

    /* 5. the front end's mapping */
    front_kbd k = {0};
    T(front_kbd_key(&k, SDL_SCANCODE_J, 1) && k.btn == DM_BTN_A);
    T(front_kbd_key(&k, SDL_SCANCODE_W, 1) && front_kbd_key(&k, SDL_SCANCODE_D, 1));
    int32_t lx, ly; front_kbd_axes(&k, &lx, &ly); T(lx == DM_STICK_MAX && ly == DM_STICK_MAX);
    T(!front_kbd_key(&k, SDL_SCANCODE_W, 1));   /* no change */
    T(!front_kbd_key(&k, SDL_SCANCODE_P, 1));   /* not a game key */
    T(front_kbd_key(&k, SDL_SCANCODE_J, 0) && k.btn == 0);
    static const struct { SDL_Scancode sc; uint64_t b; } keys[] = {{SDL_SCANCODE_K, DM_BTN_B}, {SDL_SCANCODE_U, DM_BTN_X}, {SDL_SCANCODE_I, DM_BTN_Y}, {SDL_SCANCODE_Q, DM_BTN_L},
        {SDL_SCANCODE_E, DM_BTN_R}, {SDL_SCANCODE_Z, DM_BTN_ZL}, {SDL_SCANCODE_C, DM_BTN_ZR}, {SDL_SCANCODE_F, DM_BTN_LS}, {SDL_SCANCODE_G, DM_BTN_RS}, {SDL_SCANCODE_RETURN, DM_BTN_PLUS},
        {SDL_SCANCODE_TAB, DM_BTN_MINUS}, {SDL_SCANCODE_LEFT, DM_BTN_LEFT}, {SDL_SCANCODE_UP, DM_BTN_UP}, {SDL_SCANCODE_RIGHT, DM_BTN_RIGHT}, {SDL_SCANCODE_DOWN, DM_BTN_DOWN},
        {SDL_SCANCODE_SPACE, DM_BTN_SHAKE}};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) T(front_key_button(keys[i].sc) == keys[i].b);
    /* a key map (DM_KEYMAP, play.ps1's Controls tab): A on Space takes Space from the shake, the stick on IJKL takes I / J / K from Y, A and B;
       Esc and F11 are refused, a key asked twice goes to the first action; strings and unknown names are skipped */
    {
        front_keymap m; int refused = -1;
        T(front_keymap_parse(&m, "{\"version\": 1, \"labels\": {\"A\": \"J\"}, \"A\": 44, \"StickUp\": 12, \"StickLeft\": 13, \"StickDown\": 14, \"StickRight\": 15, "
                                 "\"X\": 41, \"Y\": 68, \"ZR\": 20, \"ZL\": 20, \"Nope\": 30}", &refused) == 6 && refused == 3);
        T(front_keymap_button(&m, SDL_SCANCODE_SPACE) == DM_BTN_A && front_keymap_button(&m, SDL_SCANCODE_J) == 0);   /* J (the old A) now moves the stick left */
        T(front_keymap_button(&m, SDL_SCANCODE_Q) == DM_BTN_ZL && front_keymap_button(&m, SDL_SCANCODE_C) == DM_BTN_ZR);   /* ZL comes first: ZR's Q refused, keeps C */
        T(m.key[2] == SDL_SCANCODE_U && m.key[FRONT_ACT_SHAKE] == SDL_SCANCODE_UNKNOWN && m.key[1] == SDL_SCANCODE_UNKNOWN && m.key[3] == SDL_SCANCODE_UNKNOWN);
        T(m.key[6] == SDL_SCANCODE_UNKNOWN);   /* L lost Q to ZL */
        front_kbd k2 = {0};
        T(front_kbd_key_map(&k2, &m, SDL_SCANCODE_I, 1) && front_kbd_key_map(&k2, &m, SDL_SCANCODE_L, 1));
        int32_t x2, y2; front_kbd_axes(&k2, &x2, &y2); T(x2 == DM_STICK_MAX && y2 == DM_STICK_MAX);
        T(!front_kbd_key_map(&k2, &m, SDL_SCANCODE_W, 1));   /* W no longer moves anything */
        T(front_keymap_parse(&m, "", &refused) == 0 && refused == 0 && front_keymap_button(&m, SDL_SCANCODE_J) == DM_BTN_A);   /* empty: the default layout */
        T(front_keymap_parse(&m, NULL, NULL) == 0 && m.key[FRONT_ACT_STICK_UP] == SDL_SCANCODE_W);
    }
    /* controller buttons: by position (a Nintendo pad's labels agree), by label for an Xbox pad, shapes by position */
    T(front_pad_button(SDL_GAMEPAD_BUTTON_EAST, 0, SDL_GAMEPAD_BUTTON_LABEL_B) == DM_BTN_A);
    T(front_pad_button(SDL_GAMEPAD_BUTTON_SOUTH, 0, SDL_GAMEPAD_BUTTON_LABEL_A) == DM_BTN_B);
    T(front_pad_button(SDL_GAMEPAD_BUTTON_NORTH, 0, SDL_GAMEPAD_BUTTON_LABEL_Y) == DM_BTN_X);
    T(front_pad_button(SDL_GAMEPAD_BUTTON_WEST, 0, SDL_GAMEPAD_BUTTON_LABEL_X) == DM_BTN_Y);
    T(front_pad_button(SDL_GAMEPAD_BUTTON_SOUTH, 1, SDL_GAMEPAD_BUTTON_LABEL_A) == DM_BTN_A);      /* DM_PAD_FACE=labels, Xbox */
    T(front_pad_button(SDL_GAMEPAD_BUTTON_EAST, 1, SDL_GAMEPAD_BUTTON_LABEL_A) == DM_BTN_A);       /* labels, Nintendo: the same */
    T(front_pad_button(SDL_GAMEPAD_BUTTON_EAST, 1, SDL_GAMEPAD_BUTTON_LABEL_CIRCLE) == DM_BTN_A);  /* labels, PlayStation: by position */
    T(front_pad_button(SDL_GAMEPAD_BUTTON_START, 0, 0) == DM_BTN_PLUS && front_pad_button(SDL_GAMEPAD_BUTTON_BACK, 0, 0) == DM_BTN_MINUS);
    T(front_pad_button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0, 0) == DM_BTN_L && front_pad_button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0, 0) == DM_BTN_RS);
    T(front_pad_button(SDL_GAMEPAD_BUTTON_DPAD_UP, 0, 0) == DM_BTN_UP && !front_pad_button(SDL_GAMEPAD_BUTTON_GUIDE, 0, 0) && !front_pad_button(SDL_GAMEPAD_BUTTON_MISC1, 0, 0));
    /* sticks: SDL's y down to the Switch's y up, an inner and an outer dead zone */
    int32_t x, y;
    front_stick(0, 0, 0.08f, 0.92f, &x, &y); T(!x && !y);
    front_stick(2000, -1500, 0.08f, 0.92f, &x, &y); T(!x && !y);                       /* drift */
    front_stick(32767, 0, 0.08f, 0.92f, &x, &y); T(x == DM_STICK_MAX && !y);
    front_stick(0, -32768, 0.08f, 0.92f, &x, &y); T(!x && y == DM_STICK_MAX);          /* up */
    front_stick(0, (int32_t)(0.95 * 32767), 0.08f, 0.92f, &x, &y); T(!x && y == -DM_STICK_MAX);   /* a pad topping out at 0.95 still reaches full tilt */
    front_stick(16384, 0, 0.08f, 0.92f, &x, &y); T(abs(x - 16383) <= 2 && !y);        /* half way stays about half way */
    front_stick(23170, 23170, 0.08f, 0.92f, &x, &y); T(abs(x - 23170) <= 2 && abs(y + 23170) <= 2);   /* diagonals keep their direction */
    T(!front_trigger(3000) && front_trigger(20000));
    float tx, ty; front_touch_point(640, 360, 1280, 720, &tx, &ty); T(fabsf(tx - 0.5f) < 1e-3f && fabsf(ty - 0.5f) < 1e-3f);
    front_touch_point(100, 300, 1920, 1200, &tx, &ty); T(fabsf(tx - 100 / 1920.0f) < 1e-3f && fabsf(ty - 240 / 1080.0f) < 1e-3f);   /* 16:10: 60 pixels of letterbox above the picture */
    /* player slots */
    pad_slots ps = {0}; int a1, a2, a3;
    a1 = pad_slot_take(&ps, &a1, "Pro Controller"); a2 = pad_slot_take(&ps, &a2, "Xbox Controller");
    T(a1 == 0 && a2 == 1 && pad_slot_mask(&ps) == 3);
    T(pad_slot_free(&ps, &a1) == 0 && pad_slot_mask(&ps) == 2);
    a3 = pad_slot_take(&ps, &a3, "DualSense"); T(a3 == 0);                               /* player 1 free: taken */
    pad_slot_free(&ps, &a3); pad_slot_free(&ps, &a2);
    T(pad_slot_take(&ps, &a2, "Xbox Controller") == 1);                                  /* reconnected: its player back */
    /* player 1 chosen (the in-game menu, or the controller remembered for it): the two swap, names and all; a keyboard that holds player 1 keeps it */
    {
        pad_slots q = {0}; int b1, b2, b3; char kb = 0;
        pad_slot_take(&q, &b1, "Steam Deck"); pad_slot_take(&q, &b2, "Xbox Controller"); pad_slot_take(&q, &b3, "DualSense");
        T(pad_slot_first(&q, &b3, NULL) == 0 && pad_slot_of(&q, &b3) == 0 && pad_slot_of(&q, &b1) == 2 && pad_slot_of(&q, &b2) == 1);
        T(!strcmp(q.last[0], "DualSense") && !strcmp(q.last[2], "Steam Deck") && pad_slot_mask(&q) == 7);
        T(pad_slot_first(&q, &b3, NULL) == 0 && pad_slot_of(&q, &b3) == 0);              /* already player 1 */
        int nope; T(pad_slot_first(&q, &nope, NULL) == -1);                                /* holds no slot */
        pad_slots r = {0}; int c1;
        pad_slot_take(&r, &kb, "keyboard"); pad_slot_take(&r, &c1, "Pro Controller");
        T(pad_slot_first(&r, &c1, &kb) == -1 && pad_slot_of(&r, &kb) == 0 && pad_slot_of(&r, &c1) == 1);   /* DM_KBD_PLAYER=own */
    }
    /* the controllers' map (controls.json's "pad"): the default is the layout above by place; a file's map; refusals; swaps; the buttons it makes */
    {
        front_padmap pm; int refused = -1; uint8_t held[FRONT_INS] = {0};
        front_padmap_default(&pm);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {   /* the default map plays as front_pad_button does */
            memset(held, 0, sizeof held); held[b] = 1;
            T(front_padmap_buttons(&pm, held) == front_pad_button((SDL_GamepadButton)b, 0, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN));
        }
        memset(held, 0, sizeof held); held[FRONT_IN_LT] = 1; held[FRONT_IN_RT] = 1; T(front_padmap_buttons(&pm, held) == (DM_BTN_ZL | DM_BTN_ZR));
        T(front_padmap_parse(&pm, "{\"version\": 1, \"A\": 13}", &refused) == -1 && refused == 0);   /* no "pad": the layout is kept */
        T(front_padmap_parse(&pm, NULL, NULL) == -1);
        /* A on the bottom button takes it from B (B is left with none); ZL on the right paddle; Guide and unknown names refused; X asked twice: the first wins;
           Minus none; the keyboard's numbers beside it are no business of this map */
        T(front_padmap_parse(&pm, "{\"A\": 44, \"pad\": {\"A\": \"south\", \"ZL\": \"rightpaddle1\", \"Plus\": \"guide\", \"R\": \"nosuch\", \"X\": \"misc1\", "
                                  "\"Y\": \"misc1\", \"Minus\": \"none\"}, \"player1\": \"03000000\"}", &refused) == 4 && refused == 3);
        T(pm.in[0] == SDL_GAMEPAD_BUTTON_SOUTH && pm.in[1] == -1 && pm.in[8] == SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 && pm.in[2] == SDL_GAMEPAD_BUTTON_MISC1);
        T(pm.in[3] == SDL_GAMEPAD_BUTTON_WEST && pm.in[10] == SDL_GAMEPAD_BUTTON_START && pm.in[11] == -1 && pm.in[7] == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        memset(held, 0, sizeof held); held[SDL_GAMEPAD_BUTTON_SOUTH] = 1; held[SDL_GAMEPAD_BUTTON_BACK] = 1;
        T(front_padmap_buttons(&pm, held) == DM_BTN_A);   /* bottom is A now; Back plays nothing */
        char js[900]; front_padmap_json(&pm, js, sizeof js);   /* written and read back the same */
        char file[1000]; snprintf(file, sizeof file, "{\"version\": 1, %s, \"player1\": \"x\"}", js);
        front_padmap q; T(front_padmap_parse(&q, file, &refused) == FRONT_PACTS && refused == 0 && !memcmp(&q, &pm, sizeof q));
        front_padmap_bind(&pm, 9, SDL_GAMEPAD_BUTTON_SOUTH);   /* ZR takes the bottom button from A: A gets ZR's right trigger */
        T(pm.in[9] == SDL_GAMEPAD_BUTTON_SOUTH && pm.in[0] == FRONT_IN_RT);
        front_padmap_bind(&pm, 9, SDL_GAMEPAD_BUTTON_GUIDE); T(pm.in[9] == SDL_GAMEPAD_BUTTON_SOUTH);   /* Guide refused */
        char g[40]; T(front_json_str(file, "player1", g, sizeof g) && !strcmp(g, "x") && !front_json_str(file, "player2", g, sizeof g));
        char cn[16]; front_name_clean("A \"B\"\\C", cn, sizeof cn); T(!strcmp(cn, "A _B__C"));
        front_keymap km; int kr = 0; T(front_keymap_parse(&km, file, &kr) == 0 && kr == 0 && km.key[0] == SDL_SCANCODE_J);   /* the keys ignore "pad" */
    }
    printf("front end mapping: keys, buttons by position and by label, dead zones, triggers, touch point, player slots, player 1 chosen, the controllers' map\n");

    DeleteFileA(ctl);
    printf("nxrt.input: %s\n", gt_fail ? "FAIL" : "ok");
    TerminateProcess(GetCurrentProcess(), gt_fail);   /* the DM_CTL and DM_KEYS threads run on */
    return gt_fail;
}
