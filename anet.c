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

// This file incorporates work covered by the following copyright and
// permission notice:
//

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

#include "anet.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#define CONCAT(A, B) A ## B

#ifndef _WIN32

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define anetErrorIs(A) (errno == CONCAT(E, A))
#define anetErrorString() strerror(errno)

#else

#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define setsockopt(A, B, C, D, E) setsockopt(A, B, C, (const char *)(D), E)

#define anetErrorIs(A) (WSAGetLastError() == CONCAT(WSAE, A))

static inline const char *anetErrorString()
{
    static char msg[ANET_ERR_LEN]; // This will get clobbered every call, make sure you use it before you call this function again

    msg[0] = 0;
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, WSAGetLastError(), 0, msg, sizeof(msg), NULL);
    return msg;
}

typedef int socklen_t;

#endif

static void anetSetError(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

int anetInit(char *err)
{
#ifdef _WIN32
    // Initialize Winsock
    WSADATA data;

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        anetSetError(err, "WSAStartup failed: %s", anetErrorString());
        return ANET_ERR;
    }
#else
    (void)err;
#endif

    return ANET_OK;
}

int anetCloseConnection(socket_t fd)
{
#ifdef _WIN32
    shutdown(fd, SD_SEND);
    return closesocket(fd);
#else
    shutdown(fd, SHUT_WR);
    return close(fd);
#endif
}

int anetNonBlock(char *err, socket_t fd)
{
#ifndef _WIN32
    int flags;

    /* Set the socket nonblocking.
    * Note that fcntl(2) for F_GETFL and F_SETFL can't be
    * interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anetSetError(err, "fcntl(F_GETFL): %s", anetErrorString());
        return ANET_ERR;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        anetSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s", anetErrorString());
        return ANET_ERR;
    }
#else
    u_long iMode = 1;

    if (ioctlsocket(fd, FIONBIO, &iMode) != 0) {
        anetSetError(err, "ioctlsocket(FIONBIO, 1): %s", anetErrorString());
        return ANET_ERR;
    }
#endif

    return ANET_OK;
}

int anetTcpNoDelay(char *err, socket_t fd)
{
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void*)&yes, sizeof(yes)) == -1)
    {
        anetSetError(err, "setsockopt TCP_NODELAY: %s", anetErrorString());
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetSetSendBuffer(char *err, socket_t fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&buffsize, sizeof(buffsize)) == -1)
    {
        anetSetError(err, "setsockopt SO_SNDBUF: %s", anetErrorString());
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetTcpKeepAlive(char *err, socket_t fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s", anetErrorString());
        return ANET_ERR;
    }
    return ANET_OK;
}

static socket_t anetCreateSocket(char *err, int domain)
{
    socket_t s;
    int on = 1;

    if ((s = socket(domain, SOCK_STREAM, 0)) == -1) {
        anetSetError(err, "creating socket: %s", anetErrorString());
        return ANET_ERR;
    }

    /* Make sure connection-intensive things like the redis benckmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on)) == -1) {
        anetSetError(err, "setsockopt SO_REUSEADDR: %s", anetErrorString());
        return ANET_ERR;
    }
    return s;
}

#define ANET_CONNECT_NONE 0
#define ANET_CONNECT_NONBLOCK 1
static socket_t anetTcpGenericConnect(char *err, char *addr, char *service, int flags)
{
    socket_t s;
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
        anetSetError(err, "can't resolve %s: %s", addr, gai_strerror(gai_error));
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

        if ((anetErrorIs(INPROGRESS) || anetErrorIs(WOULDBLOCK)) && (flags & ANET_CONNECT_NONBLOCK)) {
            freeaddrinfo(gai_result);
            return s;
        }

        anetSetError(err, "connect: %s", anetErrorString());
        anetCloseConnection(s);
    }

    freeaddrinfo(gai_result);
    return ANET_ERR;
}

socket_t anetTcpConnect(char *err, char *addr, char *service)
{
    return anetTcpGenericConnect(err,addr,service,ANET_CONNECT_NONE);
}

socket_t anetTcpNonBlockConnect(char *err, char *addr, char *service)
{
    return anetTcpGenericConnect(err,addr,service,ANET_CONNECT_NONBLOCK);
}

/* Convenience function for reading from a socket */
int anetRead(socket_t fd, char *buf, int count)
{
    return recv(fd, buf, count, 0);
}

/* Attempts to write to the socket; if the socket would block,
 * then attempt to temporarily enter blocking mode, send the rest of the data, and re-enter non-blocking mode */
int anetWrite(socket_t fd, char *buf, int count)
{
    int nwritten = send(fd, buf, count, 0);

    // Everything written, success
    if (nwritten >= count)
        return nwritten;

    // Error other than would block
    if (nwritten == -1 && !anetErrorIs(AGAIN) && !anetErrorIs(WOULDBLOCK))
        return ANET_ERR;

    // Would block, so enter blocking mode temporarily
#ifndef _WIN32
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1)
        return ANET_ERR;

    nwritten += send(fd, buf + nwritten, count - nwritten, 0);
    fcntl(fd, F_SETFL, flags);
#else
    u_long iMode = 0;

    if (ioctlsocket(fd, FIONBIO, &iMode) != 0)
        return ANET_ERR;

    nwritten += send(fd, buf + nwritten, count - nwritten, 0);
    iMode = 1;
    ioctlsocket(fd, FIONBIO, &iMode);
#endif

    if (nwritten >= count)
        return nwritten;

    return ANET_ERR;
}

static int anetListen(char *err, socket_t s, struct sockaddr *sa, socklen_t len) {
    if (sa->sa_family == AF_INET6) {
        int on = 1;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    }

    if (bind(s,sa,len) == -1) {
        anetSetError(err, "bind: %s", anetErrorString());
        anetCloseConnection(s);
        return ANET_ERR;
    }

    /* Use a backlog of 512 entries. We pass 511 to the listen() call because
     * the kernel does: backlogsize = roundup_pow_of_two(backlogsize + 1);
     * which will thus give us a backlog of 512 entries */
    if (listen(s, 511) == -1) {
        anetSetError(err, "listen: %s", anetErrorString());
        anetCloseConnection(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetTcpServer(char *err, char *service, char *bindaddr, socket_t *fds, int nfds)
{
    socket_t s;
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
        anetSetError(err, "can't resolve %s: %s", bindaddr, gai_strerror(gai_error));
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

static socket_t anetGenericAccept(char *err, socket_t s, struct sockaddr *sa, socklen_t *len)
{
    socket_t fd;
    while(1) {
        fd = accept(s,sa,len);
        if (fd == -1) {
            if (anetErrorIs(INTR)) {
                continue;
            } else {
                anetSetError(err, "accept: %s", anetErrorString());
            }
        }
        break;
    }
    return fd;
}

socket_t anetTcpAccept(char *err, socket_t s) {
    socket_t fd;
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);

    if ((fd = anetGenericAccept(err, s, (struct sockaddr*)&ss, &sslen)) == ANET_ERR)
        return ANET_ERR;

    return fd;
}
