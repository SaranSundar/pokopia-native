// The software keyboard's input box on Windows (hle_swkbd.c → front_text_input): a small modal dialog from an in-memory template (no resource file),
// placed over the game window. Our own code (MIT), written from the Win32 dialog documentation. It runs on the guest thread that asked for the text and
// has no owner window: a dialog owned by the game window would disable it through a message to the window's thread, which may be waiting for this one.
// Typing goes to the box (the game window loses focus, so SDL also lets the mouse go while it looks around); Enter confirms, Esc cancels. The game's
// minimum length (g_swkbd_min_chars) keeps OK disabled until the text is long enough, as on the Switch.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "front.h"

extern int g_swkbd_min_chars;   // hle_swkbd.c
static void *volatile g_hwnd;
void front_text_window(void *hwnd) { g_hwnd = hwnd; }

enum { ID_TEXT = 100, ID_EDIT = 101, OUT_N = 1003 };
typedef struct { const wchar_t *title, *prompt, *initial; HWND game; int max, min; wchar_t out[OUT_N]; } box;

static void enable_ok(HWND d, const box *b) { EnableWindow(GetDlgItem(d, IDOK), GetWindowTextLengthW(GetDlgItem(d, ID_EDIT)) >= (b->min > 0 ? b->min : 0)); }

static INT_PTR CALLBACK box_proc(HWND d, UINT m, WPARAM w, LPARAM l) {
    box *b = (box *)GetWindowLongPtrW(d, DWLP_USER);
    switch (m) {
    case WM_INITDIALOG: {
        b = (box *)l; SetWindowLongPtrW(d, DWLP_USER, (LONG_PTR)b);
        SetWindowTextW(d, b->title); SetDlgItemTextW(d, ID_TEXT, b->prompt); SetDlgItemTextW(d, ID_EDIT, b->initial);
        if (b->max > 0) SendDlgItemMessageW(d, ID_EDIT, EM_SETLIMITTEXT, (WPARAM)b->max, 0);
        enable_ok(d, b);
        RECT r, g; GetWindowRect(d, &r);   // centred over the game window, above it even in full screen
        if (b->game && GetWindowRect(b->game, &g))
            SetWindowPos(d, HWND_TOPMOST, (g.left + g.right - (r.right - r.left)) / 2, (g.top + g.bottom - (r.bottom - r.top)) / 2, 0, 0, SWP_NOSIZE);
        else SetWindowPos(d, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(d);
        SetFocus(GetDlgItem(d, ID_EDIT)); SendDlgItemMessageW(d, ID_EDIT, EM_SETSEL, 0, -1);
        return FALSE;   // the focus is set here
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_EDIT && HIWORD(w) == EN_CHANGE) enable_ok(d, b);
        else if (LOWORD(w) == IDOK) { GetDlgItemTextW(d, ID_EDIT, b->out, OUT_N); EndDialog(d, 1); }
        else if (LOWORD(w) == IDCANCEL) EndDialog(d, 0);
        return TRUE;
    }
    return FALSE;
}

static WORD *put_str(WORD *p, const wchar_t *s) { size_t n = wcslen(s) + 1; memcpy(p, s, n * sizeof *s); return p + n; }
static WORD *put_item(WORD *p, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t *text) {
    p = (WORD *)(((uintptr_t)p + 3) & ~(uintptr_t)3);   // every item starts DWORD-aligned
    DLGITEMTEMPLATE *t = (DLGITEMTEMPLATE *)p;
    t->style = style | WS_CHILD | WS_VISIBLE; t->dwExtendedStyle = 0; t->x = x; t->y = y; t->cx = cx; t->cy = cy; t->id = id;
    p = (WORD *)(t + 1); *p++ = 0xFFFF; *p++ = cls;   // a predefined class by atom: 0x80 button, 0x81 edit, 0x82 static
    p = put_str(p, text); *p++ = 0;                    // no creation data
    return p;
}
static void widen(const char *s, wchar_t *w, int n) { if (!s || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0; w[n - 1] = 0; }

int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    HWND game = (HWND)g_hwnd;
    if (!game || !IsWindow(game) || !out || out_cap < 1) return -1;
    static box b; memset(&b, 0, sizeof b);   // one keyboard at a time (the game asks from one thread)
    static wchar_t wt[200], ws[400], wg[800], wp[1210], wi[OUT_N];
    widen(title, wt, 200); widen(sub, ws, 400); widen(guide, wg, 800); widen(initial, wi, OUT_N);
    _snwprintf(wp, 1210, L"%ls%ls%ls", ws, ws[0] && wg[0] ? L"\n" : L"", wg); wp[1209] = 0;
    b.title = wt[0] ? wt : L"Pokopia"; b.prompt = wp[0] ? wp : L"Type the text, then press Enter."; b.initial = wi;
    b.game = game; b.max = max_chars; b.min = g_swkbd_min_chars;
    DWORD tpl[256]; memset(tpl, 0, sizeof tpl);   // DWORD-aligned, as the template must be
    DLGTEMPLATE *dt = (DLGTEMPLATE *)tpl;
    dt->style = DS_MODALFRAME | DS_SETFONT | DS_SETFOREGROUND | WS_POPUP | WS_CAPTION | WS_SYSMENU; dt->cdit = 4; dt->cx = 260; dt->cy = 80;
    WORD *p = (WORD *)(dt + 1); *p++ = 0; *p++ = 0; *p++ = 0;   // no menu, the default class, the title set at WM_INITDIALOG
    *p++ = 9; p = put_str(p, L"Segoe UI");
    p = put_item(p, SS_LEFT | SS_NOPREFIX, 7, 7, 246, 28, ID_TEXT, 0x82, L"");
    p = put_item(p, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 7, 39, 246, 14, ID_EDIT, 0x81, L"");
    p = put_item(p, BS_DEFPUSHBUTTON | WS_TABSTOP, 146, 60, 50, 14, IDOK, 0x80, L"OK");
    p = put_item(p, BS_PUSHBUTTON | WS_TABSTOP, 203, 60, 50, 14, IDCANCEL, 0x80, L"Cancel");
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), dt, NULL, box_proc, (LPARAM)&b);
    if (r != 1) return r == 0 ? 0 : -1;
    if (!WideCharToMultiByte(CP_UTF8, 0, b.out, -1, out, out_cap, NULL, NULL)) out[0] = 0;
    out[out_cap - 1] = 0;
    return 1;
}
#else
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    (void)title; (void)sub; (void)guide; (void)initial; (void)max_chars; (void)out; (void)out_cap; return -1;
}
void front_text_window(void *hwnd) { (void)hwnd; }
#endif
