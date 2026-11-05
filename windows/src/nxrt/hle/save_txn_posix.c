// The save commit on Linux (docs/LINUX-PLAN.md L-D8): save_txn.c's protocol (save_txn.h), step for step, with POSIX calls. Every file of the copy
// and every folder whose entries change is flushed (fsync), which is what makes a rename durable on Linux file systems. The marker's text is the
// Windows one, so a save folder carried between the two systems reads the same.
#include "save_txn.h"
#include "dm.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>

// ---------- Kill points (tests only) ----------
static void kill_point(const char *name) {
    static char want[32]; static long nth = -1, seen;
    if (nth < 0) {
        const char *e = getenv("NXRT_SAVE_KILL_AT"); nth = 0;
        if (e && *e) { const char *c = strchr(e, ':'); size_t n = c ? (size_t)(c - e) : strlen(e); if (n >= sizeof want) n = sizeof want - 1;
                       memcpy(want, e, n); want[n] = 0; nth = c ? strtol(c + 1, NULL, 10) : 1; if (nth < 1) nth = 1; }
    }
    if (nth > 0 && !strcmp(want, name) && ++seen == nth) _exit(9);   // as abrupt as TerminateProcess: no buffers flushed, no handlers run
}

static int exists(const char *p) { struct stat st; return !lstat(p, &st); }
static int flush_path(const char *p, int dir) {
    int fd = open(p, (dir ? O_RDONLY | O_DIRECTORY : O_RDONLY) | O_CLOEXEC);
    if (fd < 0) return -1;
    int r = fsync(fd), e = errno;
    close(fd); errno = e;
    return r;
}
static int flush_parent(const char *p) {
    char d[4096]; snprintf(d, sizeof d, "%s", p);
    char *s = strrchr(d, '/');
    if (s && s != d) *s = 0; else snprintf(d, sizeof d, s ? "/" : ".");
    return flush_path(d, 1);
}
// The guest names hle_fs.c keeps in user.* attributes travel with the copy, as NTFS streams do with CopyFile
static void copy_xattrs(int in, int out) {
    char names[4096], val[1024];
    ssize_t n = flistxattr(in, names, sizeof names);
    for (ssize_t i = 0; i < n; i += (ssize_t)strlen(names + i) + 1) {
        ssize_t v = fgetxattr(in, names + i, val, sizeof val);
        if (v >= 0) fsetxattr(out, names + i, val, (size_t)v, 0);
    }
}
static int copy_file(const char *a, const char *b) {
    int in = open(a, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    int out = open(b, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (out < 0) { int e = errno; close(in); errno = e; return -1; }
    char buf[1 << 16]; int rc = 0;
    for (;;) {
        ssize_t r = read(in, buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { rc = r < 0 ? -1 : 0; break; }
        for (ssize_t w = 0; w < r && !rc;) { ssize_t k = write(out, buf + w, (size_t)(r - w)); if (k < 0 && errno != EINTR) rc = -1; else if (k > 0) w += k; }
        if (rc) break;
    }
    if (!rc) copy_xattrs(in, out);
    if (!rc && fsync(out)) rc = -1;
    int e = errno;
    close(in); close(out);
    errno = e;
    return rc;
}
int save_txn_copy_tree(const char *from, const char *to) {
    if (mkdir(to, 0755)) return -1;
    DIR *d = opendir(from);
    if (!d) return errno == ENOENT ? 0 : -1;
    struct dirent *de; int rc = 0;
    while (!rc && (de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char a[4096], b[4096]; struct stat st;
        snprintf(a, sizeof a, "%s/%s", from, de->d_name); snprintf(b, sizeof b, "%s/%s", to, de->d_name);
        if (lstat(a, &st)) { rc = -1; break; }
        if (S_ISDIR(st.st_mode)) rc = save_txn_copy_tree(a, b);
        else { rc = copy_file(a, b); if (!rc) kill_point("copied"); }
    }
    closedir(d);
    if (!rc) rc = flush_path(to, 1);   // the folder's entries, so the files just written are found after a crash
    return rc;
}
int save_txn_rm_tree(const char *p) {
    struct stat st;
    if (lstat(p, &st)) return 0;
    if (!S_ISDIR(st.st_mode)) return unlink(p);
    DIR *d = opendir(p); int rc = 0;
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char c[4096]; snprintf(c, sizeof c, "%s/%s", p, de->d_name);
            if (save_txn_rm_tree(c)) rc = -1;
        }
        closedir(d);
    }
    if (rmdir(p)) rc = -1;
    return rc;
}
static int write_marker(const char *marker, const char *save) {
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    char text[8300]; int n = snprintf(text, sizeof text, "nxrt save commit 1\r\n%s.new is complete: it replaces %s\r\n", save, save);
    int ok = write(fd, text, (size_t)n) == n && !fsync(fd), e = errno;
    close(fd);
    if (ok && flush_parent(marker)) ok = 0, e = errno;
    errno = e;
    return ok ? 0 : -1;
}
static int move(const char *a, const char *b) { return rename(a, b) || flush_parent(b) ? -1 : 0; }
static void names(const char *save, char *neu, char *old, char *marker, size_t n) {
    snprintf(neu, n, "%s.new", save); snprintf(old, n, "%s.old", save); snprintf(marker, n, "%s.commit", save);
}

int save_txn_recover(const char *save) {
    char neu[4096], old[4096], marker[4096]; names(save, neu, old, marker, sizeof neu);
    if (exists(marker)) {   // .new was complete: finish the commit from whichever step it stopped at
        if (exists(neu)) {
            if (exists(save)) {
                if (exists(old) && save_txn_rm_tree(old)) return -1;
                if (move(save, old)) return -1;
            }
            if (move(neu, save)) return -1;
        } else if (!exists(save)) { errno = ENOENT; LOG("[RED] save transaction: the commit marker %s names a version that is gone (no %s, no %s)", marker, neu, save); return -1; }
        if (unlink(marker) || flush_parent(marker)) return -1;
        save_txn_rm_tree(old);
        LOG("save transaction: an interrupted commit of %s was finished at start (the new version is the save)", save);
        return 1;
    }
    int r = 0;
    if (exists(neu)) { if (save_txn_rm_tree(neu)) return -1; r = 2; LOG("save transaction: %s, a copy for a commit that never completed, was deleted (the save is the version before it)", neu); }
    if (!exists(save) && exists(old)) { if (move(old, save)) return -1; r = 2; LOG("save transaction: %s was put back as the save", old); }
    else if (exists(old)) save_txn_rm_tree(old);   // the replaced version of a finished commit, whose deletion was cut short
    return r;
}

int save_txn_commit(const char *work, const char *save) {
    char neu[4096], old[4096], marker[4096]; names(save, neu, old, marker, sizeof neu);
    if (save_txn_recover(save) < 0) return -1;   // an earlier commit cut short is finished (or undone) first
    if (save_txn_copy_tree(work, neu)) { int e = errno; save_txn_rm_tree(neu); errno = e; return -1; }
    kill_point("flushed");
    if (write_marker(marker, save)) { int e = errno; unlink(marker); save_txn_rm_tree(neu); errno = e; return -1; }
    kill_point("marker");
    // From here the commit goes forward: a failure leaves the marker, and the next commit or start finishes it
    if (exists(save) && move(save, old)) return -1;
    kill_point("renamed_old");
    if (move(neu, save)) return -1;
    kill_point("renamed_new");
    if (unlink(marker) || flush_parent(marker)) return -1;
    kill_point("unmarked");
    save_txn_rm_tree(old);
    kill_point("done");
    return 0;
}
