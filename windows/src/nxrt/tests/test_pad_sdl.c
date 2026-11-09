/* The SDL3 front end (front/front_sdl.c) end to end, without a real controller: SDL's virtual gamepads stand in for physical ones, and the game's reads
   (nn::hid GetNpadStates on No1 / No2) show what reached it:
   1. a connected gamepad is player 1; its right face button is A, its bottom one B; a stick tilted right and up reads right and up; let go, nothing is held;
   2. a second gamepad is player 2 (npad No2) while the first keeps player 1; disconnected, its player is free and nothing stays pressed;
   3. the keyboard events the window delivers (J = A, W = the stick up) and mouse look (a click captures, movement tilts the right stick through the stick path,
      Escape releases) reach player 1; focus lost releases everything;
   4. (M6 phase B) a gamepad with rumble and motion sensors as player 2: the game's vibration (GetVibrationDeviceHandles, SendVibrationValue on No2) reaches its
      strong and weak motors as front_rumble_mix has it and stops again; its accelerometer and gyroscope (flat, turning left) read as the Switch's (0, 0, -1) G and one
      turn a second about z on player 2's six-axis sensor; detached, the game's later vibration goes nowhere;
   5. the in-game settings menu (front/menu.c): F1 opens it and the game sees no key and no controller button while it is open; a key bound in its Controls
      tab is the game's at once and the action that had it takes the old one; a key let go while it is open is let go in the game; Esc closes it; a
      controller's Back + Start opens it, B closes it.
   A hidden window, no game data. */
#include "guest_test.h"
#include "hle_adapters.h"
#include "dm_input.h"
#include "front_sdl.h"
#include "menu.h"
#include <math.h>

NXRT_EARLY_RESERVE()
void dm_guest_frame_tick(void);

typedef struct { int64_t sampling; uint64_t buttons; int32_t lx, ly, rx, ry; uint32_t attributes, _pad; } NpadState;
static NpadState *g_st; static uint32_t *g_id;
static NpadState rd(uint32_t id) {
    NxwGuestCpu c = *nxrt_cur_cpu(); *g_id = id;
    c.x[0] = (uint64_t)(uintptr_t)g_st; c.x[1] = 1; c.x[2] = (uint64_t)(uintptr_t)g_id;
    adapter__ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj(&c);
    return *g_st;
}
static void pump(int ms) {   /* the window thread's loop, here on the test's thread */
    uint64_t end = now_ns_test() + (uint64_t)ms * 1000000;
    do { SDL_Event e; while (SDL_PollEvent(&e)) front_sdl_event(&e); front_sdl_tick(); SDL_Delay(2); } while (now_ns_test() < end);
}
static SDL_JoystickID attach(const char *name) {
    SDL_VirtualJoystickDesc d; SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD; d.naxes = SDL_GAMEPAD_AXIS_COUNT; d.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    d.button_mask = (1u << d.nbuttons) - 1; d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1; d.name = name;
    return SDL_AttachVirtualJoystick(&d);
}
/* M6 phase B: a gamepad with rumble (its motors' last values) and a gyroscope and an accelerometer */
static _Atomic int g_rumbles, g_rum_lo, g_rum_hi;
static bool SDLCALL rumble_cb(void *u, Uint16 lo, Uint16 hi) { (void)u; g_rum_lo = lo; g_rum_hi = hi; g_rumbles++; return true; }
static SDL_JoystickID attach_full(const char *name) {
    static const SDL_VirtualJoystickSensorDesc sensors[2] = {{SDL_SENSOR_GYRO, 200.0f}, {SDL_SENSOR_ACCEL, 200.0f}};
    SDL_VirtualJoystickDesc d; SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD; d.naxes = SDL_GAMEPAD_AXIS_COUNT; d.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    d.button_mask = (1u << d.nbuttons) - 1; d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1; d.name = name;
    d.nsensors = 2; d.sensors = sensors; d.Rumble = rumble_cb;
    return SDL_AttachVirtualJoystick(&d);
}
static SDL_Event key(SDL_Scancode sc, int down) { SDL_Event e; memset(&e, 0, sizeof e); e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.scancode = sc; e.key.down = down; return e; }

/* nxrt.pad_sdl_split (DM_SPLIT=2, split screen's second window): of two gamepads only the second plays, as this window's player 1; it plays on out of focus;
   the first, the keyboard and the mouse do nothing here; the second detached, nothing stays pressed */
static int split_main(void) {
    char sd[600]; const char *ld = getenv("NXRT_LOG_DIR"); snprintf(sd, sizeof sd, "%s/pad-split-settings", ld && *ld ? ld : ".");
    _putenv_s("NXRT_SETTINGS_DIR", sd);
    gt_init();
    g_st = gt_alloc(sizeof *g_st); g_id = gt_alloc(4);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { printf("SKIP: SDL cannot start (%s)\n", SDL_GetError()); return 77; }
    SDL_Window *w = SDL_CreateWindow("nxrt pad split test", 320, 180, SDL_WINDOW_HIDDEN);
    if (!w) { printf("SKIP: no window (%s)\n", SDL_GetError()); return 77; }
    front_sdl_start(w);
    SDL_JoystickID p1 = attach("Split Pad 1"); pump(100); SDL_JoystickID p2 = attach("Split Pad 2"); pump(100);
    SDL_Joystick *j1 = SDL_OpenJoystick(p1), *j2 = SDL_OpenJoystick(p2); T(j1 && j2);
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, true); pump(50);
    NpadState s = rd(0); T(!(s.buttons & 0xffff)); T(!(rd(1).attributes & 1));   /* the first is window 1's: nothing here, and no player 2 either */
    SDL_SetJoystickVirtualButton(j2, SDL_GAMEPAD_BUTTON_NORTH, true); pump(50);
    s = rd(0); T((s.buttons & 0xffff) == DM_BTN_X);                                 /* the second is this window's player 1 */
    SDL_SetJoystickVirtualButton(j2, SDL_GAMEPAD_BUTTON_NORTH, false); pump(30); rd(0);
    SDL_Event e = key(SDL_SCANCODE_J, 1); front_sdl_event(&e); T(!(rd(0).buttons & DM_BTN_A)); e = key(SDL_SCANCODE_J, 0); front_sdl_event(&e);   /* the keyboard is window 1's */
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_LEFT; front_sdl_event(&e); e.type = SDL_EVENT_MOUSE_BUTTON_UP; front_sdl_event(&e);
    T(!(rd(0).buttons & 0xffff));
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_LOST; front_sdl_event(&e);
    SDL_SetJoystickVirtualButton(j2, SDL_GAMEPAD_BUTTON_SOUTH, true); pump(50); T((rd(0).buttons & 0xffff) == DM_BTN_B);   /* out of focus: still playing */
    SDL_CloseJoystick(j2); SDL_DetachVirtualJoystick(p2); pump(100);
    rd(0); T(!(rd(0).buttons & 0xffff));                                            /* detached while pressed: let go */
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, false); SDL_CloseJoystick(j1); SDL_DetachVirtualJoystick(p1); pump(50);
    SDL_DestroyWindow(w); SDL_Quit();
    printf("nxrt.pad_sdl_split: %s\n", gt_fail ? "FAIL" : "ok");
    return gt_fail;
}

int main(void) {
    if (getenv("DM_SPLIT")) return split_main();
    _putenv_s("DM_OPMODE", "1");
    _putenv_s("DM_PAD_CHECK", "1");   /* the controller check's logging and its F5 / F6 / F7 tests run along (M6 phase B) */
    char sd[600]; const char *ld = getenv("NXRT_LOG_DIR"); snprintf(sd, sizeof sd, "%s/pad-test-settings", ld && *ld ? ld : ".");
    _putenv_s("NXRT_SETTINGS_DIR", sd);   /* the menu's controls.json goes there, never into the player's own settings */
    gt_init();
    g_st = gt_alloc(sizeof *g_st); g_id = gt_alloc(4);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* the hidden window never has focus: SDL would hold controllers at rest */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { printf("SKIP: SDL cannot start (%s)\n", SDL_GetError()); return 77; }
    SDL_Window *w = SDL_CreateWindow("nxrt pad test", 320, 180, SDL_WINDOW_HIDDEN);
    if (!w) { printf("SKIP: no window (%s)\n", SDL_GetError()); return 77; }
    /* 0. SDL_GameControllerDB's file beside the program (the build copies it there): an entry for this platform that SDL's own list lacks, which the front
       end's start must add */
    char gguid[40] = "", gname[80] = "";
    {
        char p[1024]; snprintf(p, sizeof p, "%sgamecontrollerdb.txt", SDL_GetBasePath() ? SDL_GetBasePath() : "");
        FILE *f = fopen(p, "rb"); char line[2048], plat[64]; snprintf(plat, sizeof plat, "platform:%s,", SDL_GetPlatform());   /* SDL keeps this platform's lines only */
        T(f != NULL);
        while (f && fgets(line, sizeof line, f) && !gguid[0]) {
            const char *c1 = strchr(line, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
            if (line[0] == '#' || !c2 || c1 - line != 32 || !strstr(line, plat)) continue;
            char g[40]; memcpy(g, line, 32); g[32] = 0;
            char *had = SDL_GetGamepadMappingForGUID(SDL_StringToGUID(g));
            if (!had) { snprintf(gguid, sizeof gguid, "%s", g); snprintf(gname, sizeof gname, "%.*s", (int)(c2 - c1 - 1), c1 + 1); }
            SDL_free(had);
        }
        if (f) fclose(f);
        T(gguid[0]);
    }
    front_sdl_start(w);
    {
        char *m = gguid[0] ? SDL_GetGamepadMappingForGUID(SDL_StringToGUID(gguid)) : NULL;
        T(m && strstr(m, gname));
        printf("gamecontrollerdb.txt: '%s' (%s), not in SDL's own list, is mapped after the start: %s\n", gname, gguid, m ? "yes" : "no");
        SDL_free(m);
    }

    /* 1. player 1's gamepad */
    SDL_JoystickID p1 = attach("Virtual Pad 1");
    T(p1 != 0);
    pump(100);
    SDL_Joystick *j1 = SDL_OpenJoystick(p1);
    T(j1 != NULL);
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, true); pump(50);
    NpadState s = rd(0); T((s.buttons & 0xffff) == DM_BTN_A);
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, false); SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_SOUTH, true); pump(50);
    rd(0); s = rd(0); T((s.buttons & 0xffff) == DM_BTN_B);   /* the first read takes A's press latch */
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_SOUTH, false);
    SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTX, 32767); pump(50);
    s = rd(0); T(s.lx == DM_STICK_MAX && !s.ly && (s.buttons & 0xff0000) == 1u << 18);   /* right, with its direction bit */
    SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTX, 0); SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTY, -32768); pump(50);
    s = rd(0); T(!s.lx && s.ly == DM_STICK_MAX && (s.buttons & 0xff0000) == 1u << 17);   /* up (SDL's y grows downward) */
    SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTX, 23170); SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTY, 23170); pump(50);
    s = rd(0); T(abs(s.lx - 23170) < 600 && abs(s.ly + 23170) < 600);   /* a full diagonal (right, down) keeps its direction */
    SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTX, 0); SDL_SetJoystickVirtualAxis(j1, SDL_GAMEPAD_AXIS_LEFTY, 0); pump(50);
    s = rd(0); T(!s.lx && !s.ly && !(s.buttons & 0xffffff));
    printf("gamepad 1: player 1, right button A, bottom button B, the stick right and up\n");

    /* 2. a second gamepad: player 2 on No2 */
    SDL_JoystickID p2 = attach("Virtual Pad 2"); pump(100);
    SDL_Joystick *j2 = SDL_OpenJoystick(p2); T(j2 != NULL);
    SDL_SetJoystickVirtualButton(j2, SDL_GAMEPAD_BUTTON_NORTH, true); pump(50);
    s = rd(1); T((s.attributes & 1) && (s.buttons & 0xffff) == DM_BTN_X);
    T(!(rd(0).buttons & DM_BTN_X));
    SDL_CloseJoystick(j2); SDL_DetachVirtualJoystick(p2); pump(100);
    T(!(rd(1).attributes & 1) && !(rd(1).buttons & 0xffff));
    printf("gamepad 2: player 2 on No2 while connected; detached, nothing stays pressed\n");

    /* 4. (M6 phase B) a gamepad with rumble, a gyroscope and an accelerometer as player 2: the game's vibration reaches its motors, its sensors the six-axis read */
    SDL_JoystickID p3 = attach_full("Virtual Pad Rumble"); pump(100);
    SDL_Joystick *j3 = SDL_OpenJoystick(p3); T(j3 != NULL);
    uint32_t *vh = gt_alloc(16), *vid = gt_alloc(4); *vid = 1;   /* npad No2, FullKey */
    NxwGuestCpu c = *nxrt_cur_cpu(); c.x[0] = (uint64_t)(uintptr_t)vh; c.x[1] = 2; c.x[2] = (uint64_t)(uintptr_t)vid; c.x[3] = 1;
    adapter__ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE(&c);
    T(c.x[0] == 2 && vh[0] == 0x103 && vh[1] == 0x10103);   /* two motors on No2's FullKey style */
    float *vv = gt_alloc(16);
    float left[4] = {1.0f, 160, 0, 320}, right[4] = {0, 160, 1.0f, 320};   /* the left actuator's low band, the right one's high band, full */
    memcpy(vv, left, 16); c = *nxrt_cur_cpu(); c.x[0] = (uint64_t)(uintptr_t)&vh[0]; c.x[1] = (uint64_t)(uintptr_t)vv; adapter__ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE(&c);
    memcpy(vv, right, 16); c = *nxrt_cur_cpu(); c.x[0] = (uint64_t)(uintptr_t)&vh[1]; c.x[1] = (uint64_t)(uintptr_t)vv; adapter__ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE(&c);
    pump(60);
    T(g_rumbles > 0 && g_rum_lo == 55705 && g_rum_hi == 55705);   /* 0.85 of full on each motor (front_rumble_mix) */
    memset(vv, 0, 16); vv[1] = 160; vv[3] = 320;
    for (int k = 0; k < 2; k++) { c = *nxrt_cur_cpu(); c.x[0] = (uint64_t)(uintptr_t)&vh[k]; c.x[1] = (uint64_t)(uintptr_t)vv; adapter__ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE(&c); }
    pump(60); T(g_rum_lo == 0 && g_rum_hi == 0);
    printf("rumble: the game's two actuators reach the controller's strong and weak motors (%d sends), and stop\n", (int)g_rumbles);
    { SDL_Event k5 = key(SDL_SCANCODE_F5, 1); front_sdl_event(&k5); pump(30); T(g_rum_lo == 0xc000 && g_rum_hi == 0);   /* the check's F5: the strong motor alone */
      pump(1100); T(g_rum_lo == 0 && g_rum_hi == 0); }                                                                  /* stopped after its second */
    printf("controller check: F5 runs the strong motor for a second, then stops it\n");
    /* lying flat and turning left (SDL: +g along y, +2 pi rad/s about y): the Switch frame's (0, 0, -1) G and one turn a second about z */
    for (int k = 0; k < 6; k++) {
        float a[3] = {0, 9.80665f, 0}, w[3] = {0, 6.2831853f, 0}; Uint64 ts = SDL_GetTicksNS();
        SDL_SendJoystickVirtualSensorData(j3, SDL_SENSOR_ACCEL, ts, a, 3); SDL_SendJoystickVirtualSensorData(j3, SDL_SENSOR_GYRO, ts, w, 3); pump(12);
    }
    typedef struct { int64_t delta_ns, sampling; float acc[3], gyro[3], angle[3], dir[9]; uint32_t attributes, _pad; } SixState;
    SixState *six = gt_alloc(sizeof *six); uint32_t *sh = gt_alloc(4); *sh = 0x100;   /* FullKey style bit 0, npad No2 */
    c = *nxrt_cur_cpu(); c.x[0] = (uint64_t)(uintptr_t)six; c.x[1] = 1; c.x[2] = (uint64_t)(uintptr_t)sh;
    adapter__ZN2nn3hid22GetSixAxisSensorStatesEPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE(&c);
    T(c.x[0] == 1 && fabsf(six->acc[0]) < 0.01f && fabsf(six->acc[1]) < 0.01f && fabsf(six->acc[2] + 1) < 0.01f);
    T(fabsf(six->gyro[0]) < 0.01f && fabsf(six->gyro[1]) < 0.01f && fabsf(six->gyro[2] - 1) < 0.01f);
    printf("motion: flat and turning left reads acc %+.2f %+.2f %+.2f G, gyro %+.2f %+.2f %+.2f rev/s on player 2's six-axis sensor\n",
           six->acc[0], six->acc[1], six->acc[2], six->gyro[0], six->gyro[1], six->gyro[2]);
    SDL_CloseJoystick(j3); SDL_DetachVirtualJoystick(p3); pump(100);
    T(!(rd(1).attributes & 1));
    int before = g_rumbles; memcpy(vv, left, 16); c = *nxrt_cur_cpu(); c.x[0] = (uint64_t)(uintptr_t)&vh[0]; c.x[1] = (uint64_t)(uintptr_t)vv;
    adapter__ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE(&c); pump(30);
    T(g_rumbles == before);
    printf("detached: player 2 is free, the game's later vibration goes nowhere\n");

    /* 3. keyboard and mouse look as the window delivers them */
    SDL_Event e = key(SDL_SCANCODE_J, 1); front_sdl_event(&e); e = key(SDL_SCANCODE_W, 1); front_sdl_event(&e);
    s = rd(0); T((s.buttons & DM_BTN_A) && s.ly == DM_STICK_MAX);
    e = key(SDL_SCANCODE_J, 0); front_sdl_event(&e); e = key(SDL_SCANCODE_W, 0); front_sdl_event(&e);
    rd(0); s = rd(0); T(!(s.buttons & 0xffff) && !s.ly);
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_LEFT; front_sdl_event(&e);   /* captures */
    e.type = SDL_EVENT_MOUSE_BUTTON_UP; front_sdl_event(&e);
    dm_guest_frame_tick(); rd(0);
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_MOUSE_MOTION; e.motion.xrel = 100; e.motion.timestamp = SDL_GetTicksNS(); front_sdl_event(&e);   /* 5 degrees right */
    Sleep(20); dm_guest_frame_tick(); s = rd(0); T(s.rx > DM_STICK_MAX / 2 && !s.ry);
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_LEFT; front_sdl_event(&e);   /* captured: the left button is A */
    s = rd(0); T(s.buttons & DM_BTN_A);
    e = key(SDL_SCANCODE_ESCAPE, 1); front_sdl_event(&e);   /* lets the mouse go, its buttons with it */
    rd(0); T(!(rd(0).buttons & DM_BTN_A));
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, true); pump(50); T(rd(0).buttons & DM_BTN_A);
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_LOST; front_sdl_event(&e);
    rd(0); T(!(rd(0).buttons & 0xffff));   /* out of focus: released */
    printf("keyboard J / W, mouse look (capture, a turn, the left button as A, Escape), focus lost: as the Mac front end\n");

    /* 5. the in-game menu; the menu's input is taken up when it is laid out (menu_build, the present thread's in the game) */
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, false); pump(30);
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; front_sdl_event(&e);
    mn_list ml = {0};
    e = key(SDL_SCANCODE_F1, 1); front_sdl_event(&e); e = key(SDL_SCANCODE_F1, 0); front_sdl_event(&e); pump(30);
    T(menu_is_open());
    e = key(SDL_SCANCODE_J, 1); front_sdl_event(&e); T(!(rd(0).buttons & DM_BTN_A)); e = key(SDL_SCANCODE_J, 0); front_sdl_event(&e);
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_DPAD_UP, true); pump(50); T(!(rd(0).buttons & 0xffff)); SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_DPAD_UP, false); pump(30);
    menu_build(&ml, 1920, 1080);
    e = key(SDL_SCANCODE_E, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);        /* the Controls tab, its Keyboard page */
    e = key(SDL_SCANCODE_DOWN, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);     /* below the page switch: the first action, A */
    e = key(SDL_SCANCODE_RETURN, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);   /* waits for a key */
    e = key(SDL_SCANCODE_K, 1); front_sdl_event(&e); e = key(SDL_SCANCODE_K, 0); front_sdl_event(&e);   /* K, B's key until now: the two swap */
    menu_build(&ml, 1920, 1080); T(ml.n > 100);   /* it is drawn: panels and text */
    e = key(SDL_SCANCODE_ESCAPE, 1); front_sdl_event(&e); pump(30); T(!menu_is_open());
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; front_sdl_event(&e);   /* the hidden window never has the focus to take the input up again */
    e = key(SDL_SCANCODE_K, 1); front_sdl_event(&e); T(rd(0).buttons & DM_BTN_A); e = key(SDL_SCANCODE_K, 0); front_sdl_event(&e); rd(0);
    e = key(SDL_SCANCODE_J, 1); front_sdl_event(&e); s = rd(0); T((s.buttons & 0xffff) == DM_BTN_B); e = key(SDL_SCANCODE_J, 0); front_sdl_event(&e); rd(0);
    /* a key held when the menu opens, let go while it is open: its key-up reaches the game, nothing stays held */
    e = key(SDL_SCANCODE_K, 1); front_sdl_event(&e); T(rd(0).buttons & DM_BTN_A);
    e = key(SDL_SCANCODE_F1, 1); front_sdl_event(&e); pump(30); e = key(SDL_SCANCODE_K, 0); front_sdl_event(&e);
    e = key(SDL_SCANCODE_F1, 1); front_sdl_event(&e); pump(30); T(!menu_is_open());
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; front_sdl_event(&e);
    rd(0); T(!(rd(0).buttons & 0xffff));
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_BACK, true); pump(30); SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_START, true); pump(50);
    T(menu_is_open());
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_BACK, false); SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_START, false); pump(30);
    rd(0); T(!(rd(0).buttons & 0xffff));   /* Minus, which reached the game before Plus made the pair, let go */
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, true); pump(50); T(!menu_is_open());   /* B by its label */
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, false); pump(30);
    printf("the in-game menu: F1 opens it, nothing reaches the game while it is open, a key bound in it (K for A, J now B's) plays at once, a key let go "
           "while it is open is let go in the game, Esc closes it; Back + Start opens it, B closes it\n");

    /* 6. the Controls tab's Controller page: a second gamepad made player 1 there (the first takes its place), then the bottom button captured for A; both
       play at once, and both are in controls.json */
    SDL_JoystickID pb = attach("Pad B (virtual)"); pump(100);
    SDL_Joystick *jb = SDL_OpenJoystick(pb); T(jb != NULL);
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; front_sdl_event(&e);   /* B closed the menu: the input taken up again */
    SDL_SetJoystickVirtualButton(jb, SDL_GAMEPAD_BUTTON_EAST, true); pump(50); T((rd(2).buttons & 0xffff) == DM_BTN_A); SDL_SetJoystickVirtualButton(jb, SDL_GAMEPAD_BUTTON_EAST, false); pump(30);
    e = key(SDL_SCANCODE_F1, 1); front_sdl_event(&e); e = key(SDL_SCANCODE_F1, 0); front_sdl_event(&e); pump(30); T(menu_is_open());
    e = key(SDL_SCANCODE_Q, 1); front_sdl_event(&e); e = key(SDL_SCANCODE_E, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);   /* Controls again, at its page switch */
    e = key(SDL_SCANCODE_RIGHT, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);   /* the Controller page */
    e = key(SDL_SCANCODE_DOWN, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);    /* the Player 1 row */
    e = key(SDL_SCANCODE_RIGHT, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080); pump(50);   /* the next controller: Pad B */
    e = key(SDL_SCANCODE_DOWN, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);    /* the first button, A */
    e = key(SDL_SCANCODE_RETURN, 1); front_sdl_event(&e); menu_build(&ml, 1920, 1080);  /* waits for a controller button */
    SDL_SetJoystickVirtualButton(jb, SDL_GAMEPAD_BUTTON_SOUTH, true); pump(50); SDL_SetJoystickVirtualButton(jb, SDL_GAMEPAD_BUTTON_SOUTH, false); pump(30);
    menu_build(&ml, 1920, 1080); T(ml.n > 100);
    e = key(SDL_SCANCODE_ESCAPE, 1); front_sdl_event(&e); pump(30); T(!menu_is_open());
    memset(&e, 0, sizeof e); e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; front_sdl_event(&e);
    SDL_SetJoystickVirtualButton(jb, SDL_GAMEPAD_BUTTON_SOUTH, true); pump(50); s = rd(0); T((s.buttons & 0xffff) == DM_BTN_A);   /* Pad B is player 1; its bottom button is A */
    SDL_SetJoystickVirtualButton(jb, SDL_GAMEPAD_BUTTON_SOUTH, false); pump(30); T(!(rd(0).buttons & 0xffff));
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_SOUTH, true); pump(50); s = rd(2); T((s.buttons & 0xffff) == DM_BTN_A);   /* Pad 1 is player 3 now (Pad B came in as player 3, the lowest no controller had held; the two swapped), the same map */
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_SOUTH, false); pump(30);
    SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, true); pump(50); T((rd(2).buttons & 0xffff) == DM_BTN_B); SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_EAST, false); pump(30);   /* B took A's old right button (the two swapped) */
    {
        char cf[700]; snprintf(cf, sizeof cf, "%s/controls.json", sd); FILE *f = fopen(cf, "rb"); char t[4096] = {0};
        if (f) { fread(t, 1, sizeof t - 1, f); fclose(f); }
        T(strstr(t, "\"pad\"") && strstr(t, "\"A\": \"south\"") && strstr(t, "\"player1_name\":  \"Pad B (virtual)\""));
    }
    SDL_CloseJoystick(jb); SDL_DetachVirtualJoystick(pb); pump(50);
    free(ml.q);
    printf("the Controls tab's Controller page: a gamepad made player 1 there, a button captured for A; both play at once and are saved\n");

    SDL_CloseJoystick(j1); SDL_DetachVirtualJoystick(p1); pump(50);
    SDL_DestroyWindow(w); SDL_Quit();
    printf("nxrt.pad_sdl: %s\n", gt_fail ? "FAIL" : "ok");
    return gt_fail;
}
