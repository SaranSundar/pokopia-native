// Shared by menu.c and the tabs kept in files of their own (menu_controls.c, menu_linkplay.c): the colours, the drawing calls, the input kinds and the panel's geometry
#pragma once
#include "menu.h"

#define RGBA(r, g, b, a) ((uint32_t)(r) | (uint32_t)(g) << 8 | (uint32_t)(b) << 16 | (uint32_t)(a) << 24)
static const uint32_t C_DIM = RGBA(24, 18, 48, 150), C_PANEL = RGBA(250, 248, 255, 250), C_EDGE = RGBA(200, 186, 246, 255), C_TITLE = RGBA(66, 54, 128, 255),
                      C_TEXT = RGBA(64, 60, 96, 255), C_SOFT = RGBA(130, 124, 160, 255), C_PILL = RGBA(238, 233, 253, 255), C_SEL = RGBA(255, 222, 70, 255),
                      C_SELTEXT = RGBA(70, 52, 0, 255), C_TAB = RGBA(229, 221, 251, 255), C_LIST = RGBA(255, 255, 255, 255), C_OFF = RGBA(170, 166, 190, 255);
enum { IN_UP = 1, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_TABL, IN_TABR, IN_CLICK };

// The panel in window pixels, the scale (1 at 1080 lines), the text size and the top of a tab's rows
typedef struct mn_panel { float px, py, pw, ph, u, fs, y0; } mn_panel;
void mn_rect(mn_list *l, float x, float y, float w, float h, float r, uint32_t c);
void mn_text(mn_list *l, const char *t, float x, float y, float px, uint32_t c);   // its top at y
float mn_text_w(const char *t, float px);
float mn_fit(const char *t, float px, float w, float u);                           // smaller until it fits
int mn_inside(float x, float y, float x0, float y0, float w, float h);
void mn_status(const char *t);                                                       // the last change's line

// The Link Play tab (menu_linkplay.c). With the menu's lock held, except lp_event: the window thread, each SDL event before anything else
int lp_event(const SDL_Event *e);
int lp_rows(void);                                                // its buttons
int lp_pick(const mn_panel *p, float x, float y);                 // the button under the mouse, -1 none
void lp_input(const mn_panel *p, int kind, float x, float y, int *sel);
void lp_draw(mn_list *out, const mn_panel *p, int sel);
const char *lp_hint(int sel);
void lp_ctl(const char *cmd);   // DM_CTL "menu lp ..." (tests): status, settings, mode <launcher name>, join <address>, room <code>, keep|interact|story 0|1, apply, copy room|invite

// The Controls tab (menu_controls.c): the keyboard's keys and the controllers' buttons, and which controller is player 1. With the menu's lock held
void ct_init(void);              // menu_init: the maps the game started with
void ct_close(void);             // the menu closed: nothing awaited
int ct_capturing(void);          // a key or a button is awaited (the tabs stay put)
int ct_event(const SDL_Event *e);   // the window thread, the menu open: the key or button awaited; 1 taken
int ct_rows(void);
int ct_pick(const mn_panel *p, float x, float y);
void ct_input(const mn_panel *p, int kind, float x, float y, int *sel);
void ct_draw(mn_list *out, const mn_panel *p, int sel);
const char *ct_hint(int sel);
void ct_ctl(const char *cmd);    // DM_CTL "menu reset|key ...|ct ..." (tests)
