/* The folder separator in paths nxrt builds itself (docs/LINUX-PLAN.md): a backslash on Windows, a slash on Linux. C and C++. */
#ifndef NXRT_PATH_SEP_H
#define NXRT_PATH_SEP_H
#ifdef _WIN32
#define NXRT_SEP "\\"
#define NXRT_SEPC '\\'
#define NXRT_WSEP L"\\"
#define NXRT_WSEPC L'\\'
#else
#define NXRT_SEP "/"
#define NXRT_SEPC '/'
#define NXRT_WSEP L"/"
#define NXRT_WSEPC L'/'
#endif
#endif
