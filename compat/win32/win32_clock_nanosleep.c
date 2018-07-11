#include "win32_clock_nanosleep.h"

#include <windows.h>

#define WIN_EPOCH_DELTA 116444736000000000LL

int clock_nanosleep(clockid_t id, int flags, const struct timespec *ts, struct timespec *ots) {
    if (ts == NULL)
        return EFAULT;

    switch (id)
    {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC: {
        HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
        LARGE_INTEGER waitTime;

        if (flags & TIMER_ABSTIME)
        {
            waitTime.QuadPart = ts->tv_sec * 10000000LL + ts->tv_nsec / 100 + WIN_EPOCH_DELTA;
        }
        else
        {
            waitTime.QuadPart = -(ts->tv_sec * 10000000LL + ts->tv_nsec / 100);

            if (ots != NULL) {
                ots->tv_sec = 0;
                ots->tv_nsec = 0;
            }
        }

        SetWaitableTimer(timer, &waitTime, 0, NULL, NULL, 0);
        WaitForSingleObject(timer, INFINITE);

        CloseHandle(timer);
        break;
    }

    default:
        return EINVAL;
    }

    return 0;
}
