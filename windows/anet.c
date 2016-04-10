// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// anet.c: Windows implementation of TCP socket
//
// Copyright (c) 2016 Grzegorz suder <suder.grzegorz@gmail.com>
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

// This file incorporates work covered by the following copyright and
// permission notice:
//

// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// anet.c: Basic TCP socket stuff made a bit less boring
//
// Copyright (c) 2016 Oliver Jowett <oliver@mutability.co.uk>
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

/* anet.c -- Basic TCP socket stuff made a bit less boring
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "winstubs.h"
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <varargs.h>
#include "anet.h"

char error_description[ANET_ERR_LEN];

static char* WSAGetErrorDescription(DWORD err)
{    
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&error_description, ANET_ERR_LEN, NULL);

    return error_description;
}

static void anetSetError(char *err, const char *fmt, ...)
{
    va_list ap = NULL;

    if (!err) return;
    va_start(ap, fmt);    
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

int anetNonBlock(char *err, int fd)
{
    int iMode = 1;
    if (SOCKET_ERROR == ioctlsocket(fd, FIONBIO, &iMode))
    {
        DWORD error_code = WSAGetLastError();
        anetSetError(err, "ioctlsocket FIONBIO: %s", WSAGetErrorDescription(error_code));
        return ANET_ERR;
    }

    return ANET_OK;
}

int anetSetSendBuffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&buffsize, sizeof(buffsize)) == SOCKET_ERROR)
    {
        DWORD error_code = WSAGetLastError();
        anetSetError(err, "setsockopt SO_SNDBUF: %s", WSAGetErrorDescription(error_code));
        return ANET_ERR;
    }

    return ANET_OK;
}

static int anetCreateSocket(char *err, int domain)
{
    int s, on = 1;

    if ((s = socket(domain, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        DWORD error_code = WSAGetLastError();
        anetSetError(err, "creating socket: %s", WSAGetErrorDescription(error_code));
        return ANET_ERR;
    }

    /* Make sure connection-intensive things like the redis benckmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on)) == SOCKET_ERROR) {
        DWORD error_code = WSAGetLastError();
        anetSetError(err, "setsockopt SO_REUSEADDR: %s", WSAGetErrorDescription(error_code));
        return ANET_ERR;
    }

    return s;
}

#define ANET_CONNECT_NONE 0
#define ANET_CONNECT_NONBLOCK 1
static int anetTcpGenericConnect(char *err, char *addr, char *service, int flags)
{
    int s;
    struct addrinfo gai_hints;
    struct addrinfo *gai_result, *p;
    int gai_error;

    gai_hints.ai_family = AF_UNSPEC;
    gai_hints.ai_socktype = SOCK_STREAM;
    gai_hints.ai_protocol = 0;
    gai_hints.ai_flags = 0;
    gai_hints.ai_addrlen = 0;
    gai_hints.ai_addr = NULL;
    gai_hints.ai_canonname = NULL;
    gai_hints.ai_next = NULL;

    gai_error = getaddrinfo(addr, service, &gai_hints, &gai_result);
    if (gai_error != 0) {
        DWORD error_code = WSAGetLastError();
        anetSetError(err, "can't resolve %s: %s", addr, WSAGetErrorDescription(error_code));
        return ANET_ERR;
    }

    for (p = gai_result; p != NULL; p = p->ai_next) {
        if ((s = anetCreateSocket(err, p->ai_family)) == ANET_ERR)
            continue;

        if (flags & ANET_CONNECT_NONBLOCK) {
            if (anetNonBlock(err,s) != ANET_OK)
                return ANET_ERR;
        }

        if (connect(s, p->ai_addr, p->ai_addrlen) >= 0) {
            freeaddrinfo(gai_result);
            return s;
        }

        DWORD error_code = WSAGetLastError();
        if (errno == EINPROGRESS && (flags & ANET_CONNECT_NONBLOCK)) {
            freeaddrinfo(gai_result);
            return s;
        }

        anetSetError(err, "connect: %s", WSAGetErrorDescription(error_code));

        closesocket(s);
    }

    freeaddrinfo(gai_result);

    return ANET_ERR;
}

int anetTcpConnect(char *err, char *addr, char *service)
{
    return anetTcpGenericConnect(err,addr,service,ANET_CONNECT_NONE);
}

int anetTcpNonBlockConnect(char *err, char *addr, char *service)
{
    return anetTcpGenericConnect(err,addr,service,ANET_CONNECT_NONBLOCK);
}

static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len) {
    if (sa->sa_family == AF_INET6) {
        int on = 1;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(on));
    }

    if (bind(s,sa,len) == -1) {
        DWORD error_code = WSAGetLastError();

        anetSetError(err, "bind: %s", WSAGetErrorDescription(error_code));
        closesocket(s);

        return ANET_ERR;
    }

    /* Use a backlog of 512 entries. We pass 511 to the listen() call because
     * the kernel does: backlogsize = roundup_pow_of_two(backlogsize + 1);
     * which will thus give us a backlog of 512 entries */
    if (listen(s, 511) == -1) {
        DWORD error_code = WSAGetLastError();

        anetSetError(err, "listn: %s", WSAGetErrorDescription(error_code));
        closesocket(s);

        return ANET_ERR;
    }

    return ANET_OK;
}

int anetTcpServer(char *err, char *service, char *bindaddr, int *fds, int nfds)
{
    int s;
    int i = 0;

    struct addrinfo gai_hints;
    struct addrinfo *gai_result, *p;
    int gai_error;

    gai_hints.ai_family = AF_UNSPEC;
    gai_hints.ai_socktype = SOCK_STREAM;
    gai_hints.ai_protocol = 0;
    gai_hints.ai_flags = AI_PASSIVE;
    gai_hints.ai_addrlen = 0;
    gai_hints.ai_addr = NULL;
    gai_hints.ai_canonname = NULL;
    gai_hints.ai_next = NULL;

    gai_error = getaddrinfo(bindaddr, service, &gai_hints, &gai_result);
    if (gai_error != 0) {
        DWORD error_code = WSAGetLastError();
        anetSetError(err, "can't resolve %s: %s", bindaddr, WSAGetErrorDescription(error_code));
        return ANET_ERR;
    }

    for (p = gai_result; p != NULL && i < nfds; p = p->ai_next) {
        if ((s = anetCreateSocket(err, p->ai_family)) == ANET_ERR)
            continue;

        if (anetListen(err, s, p->ai_addr, p->ai_addrlen) == ANET_ERR) {
            continue;
        }

        fds[i++] = s;
    }

    freeaddrinfo(gai_result);

    return (i > 0 ? i : ANET_ERR);
}

static int anetGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len)
{
    int fd;
    DWORD error_code;

    while(1) {
        fd = accept(s,sa,len);
        if (fd == INVALID_SOCKET) {
            error_code = WSAGetLastError();

            /*
            According to the:
            http://www.sockets.com/err_lst1.htm
            WSAEINTR may happen - and according to the social.msdn.microsoft.com/Forums it sometimes happen
            */
            if (error_code == WSAEINTR) {
                continue;
            } else {                
                anetSetError(err, "accept: %s", WSAGetErrorDescription(error_code));
            }
        }
        break;
    }
    return fd;
}

int anetTcpAccept(char *err, int s) {
    int fd;

    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);

    if ((fd = anetGenericAccept(err, s, (struct sockaddr*)&ss, &sslen)) == ANET_ERR)
        return ANET_ERR;

    return fd;
}
