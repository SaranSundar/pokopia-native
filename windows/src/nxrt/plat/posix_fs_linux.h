/* Linux counterpart of posix_fs_win.h (docs/LINUX-PLAN.md): the real file API, plus the Apple calls the ported nxnative files use.
   - getxattr / setxattr / removexattr in Apple's six-argument form, over Linux's user.* attributes. A file system without them (exFAT, some
     network shares) answers "no attribute": hle_fs.c only stores a guest name when two names collide on a case-insensitive file system.
   - clonefile copies a tree (copy_file_range: a reflink on Btrfs and XFS, a copy elsewhere).
   - renamex_np(RENAME_SWAP) is renameat2(RENAME_EXCHANGE), atomic where the file system supports it, else three renames as on Windows; the
     save's commit itself is hle/save_txn.c, as on Windows (DESIGN D13). */
#ifndef NXRT_POSIX_FS_LINUX_H
#define NXRT_POSIX_FS_LINUX_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#ifndef ENOATTR
#define ENOATTR ENODATA
#endif
#define XATTR_NOFOLLOW 1
#define RENAME_SWAP 2
#ifndef O_BINARY
#define O_BINARY 0
#endif

static inline int nx_xattr_name(char *out, size_t n, const char *name) { return snprintf(out, n, "user.%s", name) < (int)n ? 0 : -1; }
static inline ssize_t nx_getxattr(const char *p, const char *name, void *buf, size_t n, uint32_t pos, int opt) {
    (void)pos; char a[256];
    if (nx_xattr_name(a, sizeof a, name)) { errno = EINVAL; return -1; }
    ssize_t r = opt & XATTR_NOFOLLOW ? lgetxattr(p, a, buf, n) : getxattr(p, a, buf, n);
    if (r < 0 && errno == ENOTSUP) errno = ENOATTR;
    return r;
}
static inline int nx_setxattr(const char *p, const char *name, const void *v, size_t n, uint32_t pos, int opt) {
    (void)pos; char a[256];
    if (nx_xattr_name(a, sizeof a, name)) { errno = EINVAL; return -1; }
    return opt & XATTR_NOFOLLOW ? lsetxattr(p, a, v, n, 0) : setxattr(p, a, v, n, 0);
}
static inline int nx_removexattr(const char *p, const char *name, int opt) {
    char a[256];
    if (nx_xattr_name(a, sizeof a, name)) { errno = EINVAL; return -1; }
    int r = opt & XATTR_NOFOLLOW ? lremovexattr(p, a) : removexattr(p, a);
    if (r < 0 && errno == ENOTSUP) errno = ENOATTR;
    return r;
}
#define getxattr(p, n, b, s, pos, opt) nx_getxattr(p, n, b, s, pos, opt)
#define setxattr(p, n, v, s, pos, opt) nx_setxattr(p, n, v, s, pos, opt)
#define removexattr(p, n, opt) nx_removexattr(p, n, opt)

static inline int nx_copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    struct stat st;
    if (fstat(in, &st)) { int e = errno; close(in); errno = e; return -1; }
    int out = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, st.st_mode & 0777);
    if (out < 0) { int e = errno; close(in); errno = e; return -1; }
    int rc = 0;
    for (off_t left = st.st_size; left > 0;) {
        ssize_t k = copy_file_range(in, NULL, out, NULL, (size_t)left, 0);
        if (k < 0 && (errno == EXDEV || errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP)) {   /* older kernels, other file systems */
            char b[1 << 16];
            ssize_t r = read(in, b, sizeof b);
            if (r <= 0) { rc = r < 0 ? -1 : 0; break; }
            for (ssize_t w = 0; w < r;) { ssize_t x = write(out, b + w, (size_t)(r - w)); if (x < 0) { rc = -1; break; } w += x; }
            if (rc) break;
            left -= r;
            continue;
        }
        if (k <= 0) { rc = k < 0 ? -1 : 0; break; }
        left -= k;
    }
    int e = errno;
    close(in); close(out);
    errno = e;
    return rc;
}
static inline int clonefile(const char *from, const char *to, int flags) {
    (void)flags;
    struct stat st;
    if (lstat(from, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return nx_copy_file(from, to);
    if (mkdir(to, st.st_mode & 0777)) return -1;
    DIR *d = opendir(from);
    if (!d) return -1;
    struct dirent *e; int rc = 0;
    while (!rc && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char f[4096], t[4096];
        snprintf(f, sizeof f, "%s/%s", from, e->d_name); snprintf(t, sizeof t, "%s/%s", to, e->d_name);
        rc = clonefile(f, t, 0);
    }
    closedir(d);
    return rc;
}
static inline int renamex_np(const char *a, const char *b, int flags) {
    if (flags != RENAME_SWAP) { errno = EINVAL; return -1; }
    if (!renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE)) return 0;
    if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP) return -1;
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.swap", b);
    if (rename(b, tmp)) return -1;
    if (rename(a, b)) { int e = errno; rename(tmp, b); errno = e; return -1; }
    return rename(tmp, a);
}

#endif
