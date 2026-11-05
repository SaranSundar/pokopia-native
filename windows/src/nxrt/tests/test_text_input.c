/* The software keyboard's box on Linux (front/text_input_linux.c) through a stand-in zenity on PATH, which records its arguments and answers from a list:
   1. no display, or no dialog program, is -1 (hle_swkbd.c treats it as Cancel);
   2. the text comes back as typed, the game's title and initial text in the dialog's arguments;
   3. longer text is cut to max_chars UTF-16 units at a character (é is one unit, an emoji two);
   4. Cancel is 0;
   5. an answer shorter than g_swkbd_min_chars asks again, with what was typed and the minimum in the box. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nxrt/runtime.h"

NXRT_EARLY_RESERVE()

int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap);
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
        "printf '%s\\n' \"$a\"\n", 0755);
    char path[700], out[256];
    const char *sys_path = "/usr/bin:/bin";

    // 1. no display; no dialog program
    unsetenv("DISPLAY"); unsetenv("WAYLAND_DISPLAY");
    snprintf(path, sizeof path, "%s:%s", g_stubs, sys_path); setenv("PATH", path, 1);
    T(front_text_input("Name", NULL, NULL, "", 10, out, sizeof out) == -1);
    setenv("DISPLAY", ":0", 1); setenv("PATH", g_empty, 1);
    T(front_text_input("Name", NULL, NULL, "", 10, out, sizeof out) == -1);
    setenv("PATH", path, 1);

    // 2. as typed, with the game's title and initial text
    answers("Ann\n"); out[0] = 0;
    T(front_text_input("Your name", "Sub line", "Guide line", "Old", 10, out, sizeof out) == 1);
    T(!strcmp(out, "Ann"));
    T(args_have("--entry\n--title\nYour name\n--text\nSub line\nGuide line\n--entry-text\nOld\n"));

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
    g_swkbd_min_chars = -1;

    const char *files[] = {"bin/zenity", "answers.txt", "n", "args.txt"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) { char p[400]; snprintf(p, sizeof p, "%s/%s", g_dir, files[i]); unlink(p); }
    rmdir(g_stubs); rmdir(g_empty); rmdir(g_dir);
    puts(g_fail ? "text input: FAILED" : "text input: all checks passed");
    return g_fail;
}
