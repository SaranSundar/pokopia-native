// The in-game settings menu (docs/in-game-menu.md): F1, or a controller's Minus + Plus together, opens it over the running game; while it is open the game
// gets no input. A Graphics tab (resolution, window or full screen, window size, docked or handheld, upscaling, sharpness, frame rate, vsync) and a Controls
// tab (the keyboard's keys). Every change applies at once and is saved to the files the launcher uses (display.json, controls.json), so both agree.
// Platform-neutral: SDL3 events in, a list of quads out (vk_menu.c draws them over the picture); the window thread feeds it, the present thread draws it.
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MN_GLYPH = 0, MN_RECT = 1 };
// One quad in window pixels: a glyph (u, v: its place in the font atlas, 0..1) or a rounded rectangle of radius r; colour sRGB, alpha in the top byte
typedef struct { float x0, y0, x1, y1, u0, v0, u1, v1, r; uint32_t rgba; int kind; } mn_quad;
typedef struct { mn_quad *q; int n, cap; } mn_list;

void menu_init(void);                       // the window thread, once the front end has started: the settings read, the keymap known
int menu_event(const SDL_Event *e);          // the window thread: 1 when the menu took the event (the game does not see it)
int menu_is_open(void);
int menu_active(void);                       // something to draw: the menu open, or the FPS counter on
void menu_build(mn_list *out, int w, int h); // the present thread, each present while active: the quads for a w x h window
void menu_font_atlas(const uint8_t **px, int *w, int *h);   // the signed-distance font (vk_menu.c uploads it once)
void menu_ctl(const char *cmd);              // DM_CTL "menu open|close|set <setting> <choice>|key <action> <scancode>|reset" (tests)

#ifdef __cplusplus
}
#endif
