// The software keyboard's input box on Linux (hle_swkbd.c → front_text_input; docs/LINUX-PLAN.md), the counterpart of text_input_win.c: the desktop's
// own entry dialog, zenity (GNOME, SteamOS: it shows over games in Game Mode too, where Steam's on-screen keyboard types into it) or kdialog (KDE).
// Our own code (MIT), written from zenity's and kdialog's command-line documentation. It runs on the guest thread that asked for the text and waits for
// the dialog; the game window's thread goes on. The game's limits hold as on Windows: at most max_chars UTF-16 units (longer text is cut at a
// character), at least g_swkbd_min_chars (a shorter answer asks again). Cancel is 0; no dialog program (or no display) is -1, as on Windows without a window.
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "front.h"

extern int g_swkbd_min_chars;   // hle_swkbd.c
extern char **environ;
void front_text_window(void *hwnd) { (void)hwnd; }   // the dialogs need no parent window

static int have(const char *prog) {   // on PATH and executable
    const char *path = getenv("PATH");
    char dir[4096];
    for (const char *p = path ? path : "/usr/bin:/bin"; p && *p;) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && n < sizeof dir - 64) {
            memcpy(dir, p, n); snprintf(dir + n, sizeof dir - n, "/%s", prog);
            if (!access(dir, X_OK)) return 1;
        }
        p = e ? e + 1 : NULL;
    }
    return 0;
}

// argv's program run with its output in out (the trailing newline dropped); its exit status, -1 when it could not run
static int run_capture(char *const argv[], char *out, size_t cap) {
    int fd[2];
    if (pipe(fd)) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    if (rc) { close(fd[0]); return -1; }
    size_t n = 0;
    for (;;) {
        ssize_t k = read(fd[0], out + n, cap - 1 - n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0 || n + (size_t)k >= cap - 1) { if (k > 0) n += (size_t)k; break; }
        n += (size_t)k;
    }
    close(fd[0]);
    out[n] = 0;
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// UTF-16 units of UTF-8 text, and where it must be cut to fit max units (at a character boundary)
static size_t utf16_cut(const char *s, int max, int *units) {
    int u = 0; size_t i = 0;
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        for (size_t k = 1; k < len; k++) if (!s[i + k]) { len = k; break; }   // a sequence cut short at the very end
        int w = len == 4 ? 2 : 1;
        if (max > 0 && u + w > max) break;
        u += w; i += len;
    }
    if (units) *units = u;
    return i;
}

int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    if (!out || out_cap < 1) return -1;
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) return -1;
    const int zen = have("zenity"), kde = !zen && have("kdialog");
    if (!zen && !kde) return -1;
    char prompt[1400], start[1100], got[2048];
    snprintf(prompt, sizeof prompt, "%s%s%s", sub ? sub : "", sub && *sub && guide && *guide ? "\n" : "", guide ? guide : "");
    if (!*prompt) snprintf(prompt, sizeof prompt, "Type the text, then press Enter.");
    snprintf(start, sizeof start, "%s", initial ? initial : "");
    const char *t = title && *title ? title : "Pokopia";
    for (int round = 0; round < 8; round++) {
        char *argv[12]; int n = 0;
        if (zen) { argv[n++] = "zenity"; argv[n++] = "--entry"; argv[n++] = "--title"; argv[n++] = (char *)t; argv[n++] = "--text"; argv[n++] = prompt;
                   argv[n++] = "--entry-text"; argv[n++] = start; }
        else { argv[n++] = "kdialog"; argv[n++] = "--title"; argv[n++] = (char *)t; argv[n++] = "--inputbox"; argv[n++] = prompt; argv[n++] = start; }
        argv[n] = NULL;
        int rc = run_capture(argv, got, sizeof got);
        if (rc < 0) return -1;
        if (rc != 0) return 0;   // Cancel, Esc or the window closed
        int units = 0;
        size_t cut = utf16_cut(got, max_chars, &units);
        got[cut] = 0;
        if (units >= (g_swkbd_min_chars > 0 ? g_swkbd_min_chars : 0)) {
            snprintf(out, (size_t)out_cap, "%s", got);
            return 1;
        }
        snprintf(start, sizeof start, "%s", got);   // too short: ask again, keeping what was typed
        size_t pl = strlen(prompt);
        if (round == 0 && pl < sizeof prompt - 64) snprintf(prompt + pl, sizeof prompt - pl, "\n(at least %d characters)", g_swkbd_min_chars);
    }
    return 0;
}
