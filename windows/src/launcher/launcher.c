// Play Pokopia <version>.exe: the game's launcher as a Windows program, for a double-click, a taskbar pin or Steam's "Add a Non-Steam Game".
// It does what out\Play Pokopia <version>.cmd does: windows\runtime\play.ps1 -Config windows\play.json, its own arguments passed on, without a
// console window. play.ps1's output is kept, and when it ends with an error a message box shows its last lines (the .cmd paused for the same).
// A copy whose name contains "Settings" (Pokopia Settings <version>.exe) adds -Settings: the window before the game, even after "Don't ask again".
// POKOPIA_LAUNCH_NOUI=1 writes the message to stderr instead of a box (tests).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TAIL (16 * 1024)   // what play.ps1 printed last, for the message

static wchar_t *join(const wchar_t *a, const wchar_t *b) {
    size_t n = wcslen(a) + wcslen(b) + 1; wchar_t *r = malloc(n * sizeof *r);
    if (r) { wcscpy(r, a); wcscat(r, b); }
    return r;
}

static int exists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static void say(const wchar_t *title, const wchar_t *text) {
    wchar_t v[8];
    if (GetEnvironmentVariableW(L"POKOPIA_LAUNCH_NOUI", v, 8) && v[0] == L'1') { fwprintf(stderr, L"%ls: %ls\n", title, text); return; }
    MessageBoxW(NULL, text, title, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

// The command line after the program's own name, as Windows gave it (quotes kept), so play.ps1 gets the switches exactly as typed
static const wchar_t *own_args(void) {
    const wchar_t *c = GetCommandLineW();
    if (*c == L'"') { c++; while (*c && *c != L'"') c++; if (*c) c++; }
    else while (*c && *c != L' ' && *c != L'\t') c++;
    while (*c == L' ' || *c == L'\t') c++;
    return c;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)inst; (void)prev; (void)cmdline; (void)show;
    wchar_t self[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(NULL, self, sizeof self / sizeof *self);
    if (!n || n >= sizeof self / sizeof *self) return 1;
    wchar_t *slash = wcsrchr(self, L'\\'); if (!slash) return 1;
    const wchar_t *name = slash + 1;
    int settings = wcsstr(name, L"Settings") != NULL;
    *slash = 0;   // self is now the folder this launcher is in (the build folder's out)

    // The build folder: out\..; play.ps1 and play.json as the .cmd names them
    wchar_t root[MAX_PATH * 4];
    if (!GetFullPathNameW(join(self, L"\\.."), sizeof root / sizeof *root, root, NULL)) return 1;
    wchar_t *ps1 = join(root, L"\\windows\\runtime\\play.ps1"), *cfg = join(root, L"\\windows\\play.json");
    if (!ps1 || !cfg) return 1;
    if (!exists(ps1) || !exists(cfg)) {
        wchar_t msg[2048];
        _snwprintf(msg, sizeof msg / sizeof *msg,
                   L"The game's files aren't next to this launcher.\n\nIt looks for windows\\runtime\\play.ps1 and windows\\play.json in the folder above it:\n%ls\n\n"
                   L"Keep this file in your build folder's \"out\" folder (a shortcut to it can go anywhere), or build the game again with Pokopia Native Builder.", root);
        say(L"Pokopia Native", msg);
        return 2;
    }

    // Windows PowerShell from System32, never one found on PATH
    wchar_t sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return 1;
    wchar_t *ps = join(sys, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    const wchar_t *more = own_args();
    size_t len = wcslen(ps) + wcslen(ps1) + wcslen(cfg) + wcslen(more) + 128;
    wchar_t *cl = malloc(len * sizeof *cl);
    if (!ps || !cl) return 1;
    _snwprintf(cl, len, L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -File \"%ls\" -Config \"%ls\"%ls%ls%ls",
               ps, ps1, cfg, settings ? L" -Settings" : L"", *more ? L" " : L"", more);

    // play.ps1's output into a pipe (kept for the message), no console window; it runs from the build folder's windows folder, as the .cmd's
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi;
    wchar_t *wd = join(root, L"\\windows");
    if (!CreateProcessW(ps, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, wd, &si, &pi)) {
        wchar_t msg[512]; _snwprintf(msg, sizeof msg / sizeof *msg, L"Windows PowerShell could not be started (error %lu).", GetLastError());
        say(L"Pokopia Native", msg);
        return 3;
    }
    CloseHandle(wr); CloseHandle(pi.hThread);

    static char tail[TAIL]; size_t used = 0; char buf[4096]; DWORD got;
    while (ReadFile(rd, buf, sizeof buf, &got, NULL) && got) {   // ends when play.ps1 and everything it started have closed the pipe
        if (got >= TAIL) { memcpy(tail, buf + got - TAIL, TAIL); used = TAIL; continue; }
        if (used + got > TAIL) { size_t drop = used + got - TAIL; memmove(tail, tail + drop, used - drop); used -= drop; }
        memcpy(tail + used, buf, got); used += got;
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1; GetExitCodeProcess(pi.hProcess, &rc); CloseHandle(pi.hProcess);

    if (rc != 0) {   // the last few lines play.ps1 printed: what it says went wrong
        size_t start = used, lines = 0;
        while (start > 0 && lines < 12) { start--; if (tail[start] == '\n' && start + 1 < used) lines++; }
        if (start < used && tail[start] == '\n') start++;
        int wn = MultiByteToWideChar(CP_OEMCP, 0, tail + start, (int)(used - start), NULL, 0);
        wchar_t *w = calloc((size_t)wn + 1, sizeof *w);
        if (w) MultiByteToWideChar(CP_OEMCP, 0, tail + start, (int)(used - start), w, wn);
        wchar_t msg[8192];
        _snwprintf(msg, sizeof msg / sizeof *msg, L"Pokopia didn't start (exit code %lu). The launcher said:\n\n%ls\n\n"
                   L"The game's log is in %%LOCALAPPDATA%%\\Pokopia Native\\<game version>\\logs. Stuck? Ask in the Discord.", rc, w && *w ? w : L"(nothing)");
        say(L"Pokopia Native", msg);
    }
    return (int)rc;
}
