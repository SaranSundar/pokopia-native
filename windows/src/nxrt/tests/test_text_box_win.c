// test_text_box_win.c (ctest nxrt.text_box_win): the software keyboard's box (front/text_input_win.c) answers a controller as front_sdl.c hands it over.
// A real box opens over a stand-in game window for a moment each time: A on an empty box gives the default name, + keeps typed text, X leaves the box
// up, and with no box up a press is not taken.
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include "front.h"
#include "dm_input.h"

extern int g_swkbd_min_chars;   // hle_swkbd.c
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static struct { int r; char out[64]; } g_res;
static unsigned __stdcall asker(void *a) {
    (void)a;
    g_swkbd_min_chars = 1;   // as the game's name question: at least one character
    g_res.r = front_text_input("What's your name?", "", "", "", 12, g_res.out, sizeof g_res.out);
    return 0;
}
static HWND g_found;
static BOOL CALLBACK find_box(HWND w, LPARAM l) { (void)l; if (GetDlgItem(w, 101)) { g_found = w; return FALSE; } return TRUE; }

// Opens a box on its own thread, waits until it is up, runs act, and returns once the box has closed (or fails after 5 s)
static int run(void (*act)(HANDLE th, unsigned tid)) {
    memset(&g_res, 0, sizeof g_res); g_res.r = -9;
    unsigned tid; HANDLE th = (HANDLE)_beginthreadex(NULL, 0, asker, NULL, 0, &tid);
    for (int i = 0; i < 500 && !front_text_pad(0); i++) Sleep(10);   // front_text_pad(0): 1 once the box is up, presses nothing
    if (!front_text_pad(0)) { printf("FAIL: the box never came up\n"); fails++; TerminateThread(th, 0); CloseHandle(th); return 0; }
    act(th, tid);
    int done = WaitForSingleObject(th, 5000) == WAIT_OBJECT_0;
    if (!done) { printf("FAIL: the box did not close\n"); fails++; TerminateThread(th, 0); }
    CloseHandle(th);
    return done;
}
static void press_a(HANDLE th, unsigned tid) { (void)th; (void)tid; CHECK(front_text_pad(DM_BTN_A), "A while the box is up was not taken"); }
static void type_then_plus(HANDLE th, unsigned tid) {
    (void)th; g_found = NULL; EnumThreadWindows(tid, find_box, 0);
    CHECK(g_found, "the box's window was not found"); if (g_found) SetDlgItemTextW(g_found, 101, L"Ash");
    CHECK(front_text_pad(DM_BTN_PLUS), "+ while the box is up was not taken");
}
static void x_then_b(HANDLE th, unsigned tid) {
    (void)tid;
    CHECK(front_text_pad(DM_BTN_X), "X while the box is up was not taken (it must not reach the game)");
    CHECK(WaitForSingleObject(th, 300) == WAIT_TIMEOUT, "X closed the box");
    CHECK(front_text_pad(DM_BTN_B), "B while the box is up was not taken");
}

int main(void) {
    HWND game = CreateWindowExW(0, L"STATIC", L"stand-in game window", WS_POPUP | WS_VISIBLE, 0, 0, 320, 180, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!game) { printf("SKIP: no window could be made here\n"); return 77; }
    front_text_window(game);
    CHECK(!front_text_pad(DM_BTN_A), "a press was taken with no box up");

    if (run(press_a)) CHECK(g_res.r == 1 && !strcmp(g_res.out, "Player"), "A on an empty box: r %d, text '%s' (expected 1, 'Player')", g_res.r, g_res.out);
    if (run(type_then_plus)) CHECK(g_res.r == 1 && !strcmp(g_res.out, "Ash"), "+ after typing: r %d, text '%s' (expected 1, 'Ash')", g_res.r, g_res.out);
    if (run(x_then_b)) CHECK(g_res.r == 1 && !strcmp(g_res.out, "Player"), "X then B: r %d, text '%s' (expected 1, 'Player')", g_res.r, g_res.out);
    CHECK(!front_text_pad(DM_BTN_A), "a press was taken after the box closed");

    front_text_window(NULL); DestroyWindow(game);
    printf(fails ? "nxrt.text_box_win: %d failed\n" : "nxrt.text_box_win: ok\n", fails);
    return fails ? 1 : 0;
}
