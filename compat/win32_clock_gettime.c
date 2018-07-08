#include "win32_clock_gettime.h"

#include <stdint.h>
#include <windows.h>

#define WIN_EPOCH_DELTA 116444736000000000LL

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (tp == NULL)
    {
        errno = EFAULT;
        return -1;
    }

    switch (clk_id)
    {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC: {
        FILETIME ft;

        GetSystemTimeAsFileTime(&ft);

        int64_t ns100 = (((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - WIN_EPOCH_DELTA;

        tp->tv_sec = ns100 / 10000000;
        tp->tv_nsec = (ns100 % 10000000) * 100;
        return 0;
    }

    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID: {
        FILETIME creationTime;
        FILETIME exitTime;
        FILETIME kernelTime;
        FILETIME userTime;

        // These may fail, but there is no error for failure according to the man page, so we just return whatever is in the structures
        if (clk_id == CLOCK_PROCESS_CPUTIME_ID)
        {
            GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime);
        }
        else
        {
            GetThreadTimes(GetCurrentThread(), &creationTime, &exitTime, &kernelTime, &userTime);
        }

        // Calculate the kernel + user time
        int64_t ns100 = ((int64_t)kernelTime.dwHighDateTime << 32) + kernelTime.dwLowDateTime + ((int64_t)userTime.dwHighDateTime << 32) + userTime.dwLowDateTime;

        tp->tv_sec = ns100 / 10000000;
        tp->tv_nsec = (ns100 % 10000000) * 100;
        return 0;
    }

    default:
        break;
    }

    errno = EINVAL;
    return -1;
}
