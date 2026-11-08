// The SDL3 front end (front_sdl.c) as the window thread sees it (vk_window.c), and the lock that serialises SDL's subsystem start and stop
// (SDL_Init / SDL_InitSubSystem are not thread safe: the window thread starts video and controllers, an audio output may start audio at the same time).
#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void front_sdl_start(SDL_Window *win);      // on the window thread, once the window exists and the controller subsystem is up
void front_sdl_event(const SDL_Event *e);   // every event the window thread pumps
void front_sdl_tick(void);                  // every turn of the window thread's loop (at least every 50 ms): rumble kept going, the controller check's title
void front_sdl_lock(void);
void front_sdl_unlock(void);
void front_sdl_hints(void);                 // before the controller subsystem starts: the HIDAPI drivers (the Steam Controller's)

// The controllers as the in-game menu's Controls tab sees them (any thread): connected ones, their player (0 = player 1, -1 none) and SDL's GUID as text
typedef struct { uint32_t id; int player, type; char name[64], guid[40]; } front_pad_info;
int front_sdl_pads(front_pad_info *out, int max);
void front_sdl_player1(uint32_t id);        // that controller made player 1 (on the window thread, soon after); the one that was swaps places with it

#ifdef __cplusplus
}
#endif
