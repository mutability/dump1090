#ifndef __WINSTUBS_H
#define __WINSTUBS_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>

#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#define M_PI 3.14159265358979323846

/* from <endian.h> */
#if BYTE_ORDER == LITTLE_ENDIAN
#   define le16toh(x) (x)
#elif BYTE_ORDER == BIG_ENDIAN
#   define le16toh(x) __builtin_bswap16(x)
#endif


_inline void fchmod(int A, int B) {(void)A; (void)B;}
_inline void usleep(DWORD uSecSleep) {Sleep(uSecSleep/1000);}
_inline void sleep(DWORD secSleep) {Sleep(secSleep*1000);}
_inline int inet_aton(const char * cp, DWORD * ulAddr)
{
    *ulAddr = inet_addr(cp);
    return (INADDR_NONE != *ulAddr);
}
_inline void cls()
{
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = {0, 0};
    DWORD count;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hStdOut, &csbi);
    FillConsoleOutputCharacter(hStdOut, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &count);
    SetConsoleCursorPosition(hStdOut, coord);
}

#endif // __WINSTUBS_H
