// from nxnative dm/host/front.h (MIT, © seedds): the host's interface to its front end. Windows has no front end yet (SDL3 comes with M6, DESIGN D14):
// only the touch, pointer-look and text-input entry points the ported files use are kept. Dropped: front_run, the Metal layer, screen modes, progress,
// preloading, front_save_failed (hle_fs.c logs it).
#pragma once
#include <stdint.h>

// Touchscreen (hle_touch.c): the whole set of touch points, n = 0 = all lifted; x / y normalized [0,1) on the output frame, origin at top left
typedef struct { uint32_t id; float x, y; } dm_touch;
void dm_touch_set(const dm_touch *t, int n);
void dm_touch_script(const dm_touch *t, int n);   // scripted touches (DM_KEYS, DM_CTL; hle_hid.c): as dm_touch_set, but never recorded by DM_REC
// Pointer look (hle_hid.c): turn the camera by this many degrees (right, up) through the right stick
void dm_look_add(float right_deg, float up_deg);
void dm_look_add_at(float right_deg, float up_deg, double age_s);
int dm_look_on(void);
float dm_look_sens(float def);
// Text input for the software keyboard (hle_swkbd.c; front/text_input_win.c): a modal box over the game window, blocking the calling (guest) thread.
// title / sub / guide: the game's texts (UTF-8, may be empty); initial: the text to start from; max_chars: the limit in UTF-16 units (0 = none).
// 1 OK (out holds the UTF-8 text), 0 cancelled, -1 no window to show it over
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap);
void front_text_window(void *hwnd);   // front_sdl.c: the game window (Windows HWND) the box belongs to; NULL when it closes
// front_sdl.c, on a controller's button press: while a box is up, A, B or + accepts it (an empty box takes a default text). 1 = a box was up and took the press
int front_text_pad(uint64_t buttons);
