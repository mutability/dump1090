#ifndef CLOCK_GETTIME_H
#define CLOCK_GETTIME_H

#include <time.h>

typedef enum
{
    CLOCK_REALTIME,
    CLOCK_MONOTONIC,
    CLOCK_PROCESS_CPUTIME_ID,
    CLOCK_THREAD_CPUTIME_ID
} clockid_t;

int clock_gettime(clockid_t clk_id, struct timespec *tp);

#endif // CLOCK_GETTIME_H
