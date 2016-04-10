// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// winstubs.h: windows stubs for some POSIX functions
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

#ifndef WINSTUBS_H
#define WINSTUBS_H

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "pthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include "clock_nanosleep\clock_nanosleep.h"
#include <BaseTsd.h>

typedef SSIZE_T ssize_t;
typedef int socklen_t;

// File
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define PATH_MAX FILENAME_MAX
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)

// Time
#define usleep(x) Sleep(x/1000)
#define sleep(x) Sleep(x)

// Other
#define snprintf _snprintf

#endif