#ifndef WIN32_COMPAT_H
#define WIN32_COMPAT_H

#if defined(_MSC_VER)

#include <stdlib.h>
#include <stdint.h>

#define bswap_16 _byteswap_ushort
#define bswap_32 _byteswap_ulong
#define bswap_64 _byteswap_uint64

#define le16toh(x) (x)
#define le32toh(x) (x)

#endif // _MSC_VER

#include "win32_clock_nanosleep.h"
#include "win32_clock_gettime.h"

#define __attribute__(A)

#include <io.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <winsock2.h>
#include <windows.h>

typedef intptr_t ssize_t;

#define M_PI 3.14159265358979323846264338327950288
#define STDIN_FILENO _fileno(stdin)
#define STDOUT_FILENO _fileno(stdout)

#undef O_RDONLY
#define O_RDONLY (_O_RDONLY | O_BINARY)
#undef O_RDWR
#define O_RDWR (_O_RDWR | O_BINARY)
#undef O_WRONLY
#define O_WRONLY (_O_WRONLY | O_BINARY)

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

#define realpath(P, R) strcpy(R, P)

#ifndef SIGPIPE
#define SIGPIPE 13
#endif

#ifndef SIGWINCH
#define SIGWINCH 28
#endif

typedef void (* linux_sighandler_t)(int);

static inline linux_sighandler_t linux_signal(int signum, linux_sighandler_t handler)
{
    switch (signum)
    {
    case SIGABRT:
    case SIGFPE:
    case SIGILL:
    case SIGINT:
    case SIGSEGV:
    case SIGTERM:
        return signal(signum, handler);

    default: break;
    }

    return SIG_ERR;
}

#define signal(S, H) linux_signal(S, H)

static inline struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    localtime_s(result, timep);
    return result;
}

static inline int usleep(__int64 usec)
{
    struct timespec ts;

    ts.tv_sec = (time_t)usec / 1000000;
    ts.tv_nsec = (long)(usec % 1000000) * 1000;

    int err = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);

    if (err != 0)
    {
        errno = err;
        return -1;
    }

    return 0;
}

#define sleep(sec) usleep((sec) * 1000000ULL)
#define strcasecmp stricmp

#endif // WIN32_COMPAT_H
