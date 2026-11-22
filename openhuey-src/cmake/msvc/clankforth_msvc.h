/* ClankForth 0.3 is written for GCC/Clang on POSIX: what MSVC has no notion of, kept here and given
 * to its sources by cmake/clankforth-msvc.patch (forth_internal.h includes this on MSVC). Beside it
 * in this folder is a <dirent.h> (system.c's `dir-list`). The game itself uses neither: only the
 * Forth library is built with them, on the include path openhuey-src/CMakeLists.txt gives it. */
#ifndef CLANKFORTH_MSVC_H
#define CLANKFORTH_MSVC_H

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN   /* (so windows.h leaves Winsock 1 out: net.c includes Winsock 2 itself) */
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX   /* (its min/max macros are no help to C here: Forth has its own words) */
#define NOMINMAX
#endif
#include <windows.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* stat's folder test: MSVC spells the mode bits _S_IFMT / _S_IFDIR (sys/stat.h) */
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif

/* the clocks ClankForth names (net.c's timeouts, system.c's now) */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/* (a monotonic one from the performance counter; the real one from the system time, 1601 to 1970) */
static int clock_gettime(int which, struct timespec *ts) {
    if (which == CLOCK_MONOTONIC) {
        static LARGE_INTEGER freq;
        LARGE_INTEGER now;

        if (freq.QuadPart == 0) {
            QueryPerformanceFrequency(&freq);
        }
        QueryPerformanceCounter(&now);
        ts->tv_sec = (time_t)(now.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)((now.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
    } else {
        FILETIME ft;
        ULARGE_INTEGER u;

        GetSystemTimeAsFileTime(&ft);
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        u.QuadPart -= 116444736000000000ULL;   /* (1601 to 1970) */
        ts->tv_sec = (time_t)(u.QuadPart / 10000000ULL);
        ts->tv_nsec = (long)((u.QuadPart % 10000000ULL) * 100ULL);
    }
    return 0;
}

/* ... and a sleep in nanoseconds (system.c's sleep word): Sleep takes milliseconds, up to it */
static int nanosleep(const struct timespec *req, struct timespec *left) {
    long long ns;

    (void)left;
    if (req == NULL) {
        errno = EINVAL;
        return -1;
    }
    ns = (long long)req->tv_sec * 1000000000LL + (long long)req->tv_nsec;
    if (ns > 0) {
        DWORD ms = (DWORD)((ns + 999999) / 1000000);

        Sleep(ms > 0x7fffffffu ? 0x7fffffffu : ms);
    }
    return 0;
}

#endif   /* _MSC_VER */
#endif   /* CLANKFORTH_MSVC_H */
