/* The POSIX file API of the ported nxnative files: over Win32 (posix_fs_win.h) or Linux's own (posix_fs_linux.h). Include after system headers. */
#pragma once
#ifdef _WIN32
#include "posix_fs_win.h"
#else
#include "posix_fs_linux.h"
#endif
