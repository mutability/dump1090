// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// anet.h: Basic TCP socket stuff made a bit less boring
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

#ifndef ANET_H
#define ANET_H

#define ANET_OK 0
#define ANET_ERR (socket_t)-1
#define ANET_ERR_LEN 1024

#define ANET_CONCAT(A, B) A ## B

#if defined(__sun)
#define AF_LOCAL AF_UNIX
#endif

#ifndef _WIN32

#define anetErrorIs(A) (errno == ANET_CONCAT(E, A))
typedef int socket_t;

#else // _WIN32

#include <winsock2.h>

#define WSAEAGAIN WSAEWOULDBLOCK
#define anetErrorIs(A) (WSAGetLastError() == ANET_CONCAT(WSAE, A))
typedef SOCKET socket_t;

#endif // _WIN32

int anetInit(char *err);
int anetCloseConnection(socket_t fd);
socket_t anetTcpConnect(char *err, char *addr, char *service);
socket_t anetTcpNonBlockConnect(char *err, char *addr, char *service);
int anetRead(socket_t fd, char *buf, int count);
int anetTcpServer(char *err, char *service, char *bindaddr, socket_t *fds, int nfds);
socket_t anetTcpAccept(char *err, socket_t serversock);
int anetWrite(socket_t fd, char *buf, int count);
int anetNonBlock(char *err, socket_t fd);
int anetTcpNoDelay(char *err, socket_t fd);
int anetTcpKeepAlive(char *err, socket_t fd);
int anetSetSendBuffer(char *err, socket_t fd, int buffsize);

#endif
