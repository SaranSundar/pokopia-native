/* The software keyboard's box on Linux (front/text_input_linux.c) through a stand-in zenity on PATH, which records its arguments and answers from a list:
   1. no display is -1 (hle_swkbd.c treats it as Cancel); front_text_pad takes no press while no box is up;
   2. the text comes back as typed, the game's title and initial text in the dialog's arguments, with the controller hint;
   3. longer text is cut to max_chars UTF-16 units at a character (é is one unit, an emoji two);
   4. Cancel is 0;
   5. an answer shorter than g_swkbd_min_chars asks again, with what was typed and the minimum in the box;
   6. a controller's A while the dialog is up closes it: "Player" (X first is taken but changes nothing);
   7. with a display and no dialog program the box waits for a controller's + and answers "Player", cut to max_chars. No game data. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "nxrt/runtime.h"
#include "dm_input.h"
#include "front.h"

NXRT_EARLY_RESERVE()

extern int g_swkbd_min_chars;

static int g_fail;
#define T(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

static char g_dir[256], g_stubs[300], g_empty[300];

static void put(const char *name, const char *text, int mode) {
    char p[400]; snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *f = fopen(p, "w"); fputs(text, f); fclose(f); chmod(p, mode);
}
static void answers(const char *lines) { put("answers.txt", lines, 0644); put("n", "0\n", 0644); put("args.txt", "", 0644); }
static int args_have(const char *s) {
    char p[400], buf[8192]; snprintf(p, sizeof p, "%s/args.txt", g_dir);
    FILE *f = fopen(p, "r"); size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
    return strstr(buf, s) != NULL;
}
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

// A controller as front_sdl.c reports it: after 300 ms the first press, after 300 ms more the second (0: none); what front_text_pad answered
typedef struct { uint64_t first, second; int took_first, took_second; } presses;
static void *press(void *arg) {
    presses *p = arg;
    usleep(300 * 1000); p->took_first = front_text_pad(p->first);
    if (p->second) { usleep(300 * 1000); p->took_second = front_text_pad(p->second); }
    return NULL;
}

int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/nxrt-text-input-XXXXXX");
    if (!mkdtemp(g_dir)) { puts("FAIL mkdtemp"); return 1; }
    snprintf(g_stubs, sizeof g_stubs, "%s/bin", g_dir); mkdir(g_stubs, 0755);
    snprintf(g_empty, sizeof g_empty, "%s/none", g_dir); mkdir(g_empty, 0755);
    put("bin/zenity",
        "#!/bin/sh\n"
        "d=$(dirname \"$0\")/..\n"
        "printf '%s\\n' \"$@\" >> \"$d/args.txt\"; echo --- >> \"$d/args.txt\"\n"
        "n=$(( $(cat \"$d/n\") + 1 )); echo $n > \"$d/n\"\n"
        "a=$(sed -n \"${n}p\" \"$d/answers.txt\")\n"
        "[ \"$a\" = CANCEL ] && exit 1\n"
        "[ \"$a\" = WAIT ] && exec sleep 30\n"   /* a dialog nobody types into: exec keeps the pid front_text_pad signals */
        "printf '%s\\n' \"$a\"\n", 0755);
    char path[700], out[256];
    const char *sys_path = "/usr/bin:/bin";

    // 1. no display; no box, no press taken
    unsetenv("DISPLAY"); unsetenv("WAYLAND_DISPLAY");
    snprintf(path, sizeof path, "%s:%s", g_stubs, sys_path); setenv("PATH", path, 1);
    T(front_text_input("Name", NULL, NULL, "", 10, out, sizeof out) == -1);
    T(front_text_pad(DM_BTN_A) == 0);
    setenv("DISPLAY", ":0", 1);

    // 2. as typed, with the game's title and initial text
    answers("Ann\n"); out[0] = 0;
    T(front_text_input("Your name", "Sub line", "Guide line", "Old", 10, out, sizeof out) == 1);
    T(!strcmp(out, "Ann"));
    T(args_have("--entry\n--title\nYour name\n--text\nSub line\nGuide line\n(No keyboard? A or + on the controller gives \"Player\".)\n--entry-text\nOld\n"));

    // 3. cut at max_chars UTF-16 units, at a character
    answers("Pok\xc3\xa9mon\n");
    T(front_text_input("Name", NULL, NULL, "", 4, out, sizeof out) == 1 && !strcmp(out, "Pok\xc3\xa9"));
    answers("ab\xf0\x9f\x98\x80" "c\n");
    T(front_text_input("Name", NULL, NULL, "", 3, out, sizeof out) == 1 && !strcmp(out, "ab"));

    // 4. Cancel
    answers("CANCEL\n");
    T(front_text_input("Name", NULL, NULL, "", 10, out, sizeof out) == 0);

    // 5. too short: asked again with what was typed and the minimum
    answers("a\nabc\n"); g_swkbd_min_chars = 3;
    T(front_text_input("Name", NULL, NULL, "", 10, out, sizeof out) == 1 && !strcmp(out, "abc"));
    T(args_have("--entry-text\na\n") && args_have("(at least 3 characters)"));

    // 6. a controller while the dialog is up: X is taken and changes nothing, A closes it
    answers("WAIT\n");
    presses pr = {DM_BTN_X, DM_BTN_A, 0, 0}; pthread_t th; pthread_create(&th, NULL, press, &pr);
    double t0 = now_s();
    T(front_text_input("Name", NULL, NULL, "", 12, out, sizeof out) == 1 && !strcmp(out, "Player"));
    double took = now_s() - t0;
    pthread_join(th, NULL);
    T(pr.took_first == 1 && pr.took_second == 1);
    T(took > 0.5 && took < 5);   // closed by the second press, not by the first or the stand-in's own 30 s
    g_swkbd_min_chars = -1;

    // 7. no dialog program: the box waits for + and answers "Player", cut to max_chars
    setenv("PATH", g_empty, 1);
    presses p2 = {DM_BTN_PLUS, 0, 0, 0}; pthread_create(&th, NULL, press, &p2);
    T(front_text_input("Name", NULL, NULL, "", 3, out, sizeof out) == 1 && !strcmp(out, "Pla"));
    pthread_join(th, NULL);
    T(p2.took_first == 1 && front_text_pad(DM_BTN_A) == 0);
    setenv("PATH", path, 1);

    const char *files[] = {"bin/zenity", "answers.txt", "n", "args.txt"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) { char p[400]; snprintf(p, sizeof p, "%s/%s", g_dir, files[i]); unlink(p); }
    rmdir(g_stubs); rmdir(g_empty); rmdir(g_dir);
    puts(g_fail ? "text input: FAILED" : "text input: all checks passed");
    return g_fail;
}
