// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// posix_time.c: windows stubs for POSIX time functions.
//
// Copyright (c) 2016 Grzegorz Suder <suder.grzegorz@gmail.com>
//
// This file is free software: you may copy, redistribute and/or modify it  
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your  
// option) any later version.  
//
// This file is distributed in the hope that it will be useful, but  
// WITHOUT ANY WARRANTY; without even the implied warranty of  
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License  
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "winstubs.h"

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    SYSTEMTIME system_time;
    GetLocalTime(&system_time);

    tv->tv_sec = system_time.wSecond + (60 * (system_time.wMinute + (60 * system_time.wHour)));
    tv->tv_usec = system_time.wMilliseconds * 1000;

    return 0;
}

struct tm *localtime_r(const time_t *timer, struct tm *result)
{
    struct tm local_result;
    errno_t err;

    err = localtime_s(&local_result, timer);

    if (err != 0 || result == NULL)
        return NULL;

    memcpy(result, &local_result, sizeof(struct tm));

    return result;
}

int clock_nanosleep(clockid_t id, int flags, const struct timespec *ts, struct timespec *ots)
{
#pragma message("clock_nanosleep is not supported in Windows OS (unless we figure out how to stub it)")
    return 0;
}

int clock_gettime(clockid_t clk_id, struct timeval *tp)
{
    static BOOL initialized = FALSE;
    static double countsPerMicrosecond;

    LARGE_INTEGER current_time;

    if (FALSE == initialized)
    {
        LARGE_INTEGER freq;
        initialized = TRUE;

        if (FALSE == QueryPerformanceFrequency(&freq))
        {
            return EINVAL;
        }

        countsPerMicrosecond = (double)((double)freq.QuadPart / (double)1000000);
    }

    if (FALSE == QueryPerformanceCounter(&current_time))
    {
        return EINVAL;
    }

    current_time.QuadPart = (LONGLONG)((double)current_time.QuadPart / countsPerMicrosecond);

    tp->tv_sec = (long)(current_time.QuadPart / 1000000);
    tp->tv_usec = current_time.QuadPart % 1000000;

    return 0;
}