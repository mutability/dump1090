#ifndef COMPAT_UTIL_H
#define COMPAT_UTIL_H

/*
 * Platform-specific bits
 */

#if defined(__APPLE__)

/*
 * Mach endian conversion
 */
# include <libkern/OSByteOrder.h>
# define bswap_16 OSSwapInt16
# define bswap_32 OSSwapInt32
# define bswap_64 OSSwapInt64
# include <machine/endian.h>
# define le16toh(x) OSSwapLittleToHostInt16(x)
# define le32toh(x) OSSwapLittleToHostInt32(x)

#elif defined(_MSC_VER)

#include <stdlib.h>
#include <stdint.h>

#define bswap_16 _byteswap_ushort
#define bswap_32 _byteswap_ulong
#define bswap_64 _byteswap_uint64

#define le16toh(x) (x)
#define le32toh(x) (x)

#include "win32_clock_nanosleep.h"
#include "win32_clock_gettime.h"

#else // other platforms

# include <endian.h>

#endif

#ifdef MISSING_NANOSLEEP
#include "clock_nanosleep/clock_nanosleep.h"
#endif

#ifdef MISSING_GETTIME
#include "clock_gettime/clock_gettime.h"
#endif

#endif //COMPAT_UTIL_H
