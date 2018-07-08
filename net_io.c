// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// net_io.c: network handling.
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
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
//   Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
//   All rights reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are
//   met:
//
//    *  Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//    *  Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dump1090.h"

/* for PRIX64 */
#include <inttypes.h>

#include <assert.h>

//
// ============================= Networking =============================
//
// Note: here we disregard any kind of good coding practice in favor of
// extreme simplicity, that is:
//
// 1) We only rely on the kernel buffers for our I/O without any kind of
//    user space buffering.
// 2) We don't register any kind of event handler, from time to time a
//    function gets called and we accept new connections. All the rest is
//    handled via non-blocking I/O and manually polling clients to see if
//    they have something new to share with us when reading is needed.

static int decodeBinMessage(struct client *c, char *p);
static int decodeHexMessage(struct client *c, char *hex);
#ifdef ENABLE_WEBSERVER
static int handleHTTPRequest(struct client *c, char *p);
#endif

static void send_raw_heartbeat(struct net_service *service);
static void send_beast_heartbeat(struct net_service *service);
static void send_sbs_heartbeat(struct net_service *service);

static void writeFATSVEvent(struct modesMessage *mm, struct aircraft *a);

//
//=========================================================================
//
// Networking "stack" initialization
//

// Init a service with the given read/write characteristics, return the new service.
// Doesn't arrange for the service to listen or connect
struct net_service *serviceInit(const char *descr, struct net_writer *writer, heartbeat_fn hb, const char *sep, read_fn handler)
{
    struct net_service *service;

    if (!(service = calloc(sizeof(*service), 1))) {
        fprintf(stderr, "Out of memory allocating service %s\n", descr);
        exit(1);
    }

    service->next = Modes.services;
    Modes.services = service;

    service->descr = descr;
    service->listener_count = 0;
    service->connections = 0;
    service->writer = writer;
    service->read_sep = sep;
    service->read_handler = handler;

    if (service->writer) {
        if (! (service->writer->data = malloc(MODES_OUT_BUF_SIZE)) ) {
            fprintf(stderr, "Out of memory allocating output buffer for service %s\n", descr);
            exit(1);
        }

        service->writer->service = service;
        service->writer->dataUsed = 0;
        service->writer->lastWrite = mstime();
        service->writer->send_heartbeat = hb;
    }

    return service;
}

// Create a client attached to the given service using the provided socket FD
struct client *createSocketClient(struct net_service *service, socket_t fd)
{
    anetSetSendBuffer(Modes.aneterr, fd, (MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size));
    return createGenericClient(service, fd);
}

// Create a client attached to the given service using the provided FD (might not be a socket!)
struct client *createGenericClient(struct net_service *service, socket_t fd)
{
    struct client *c;

    anetNonBlock(Modes.aneterr, fd);

    if (!(c = (struct client *) malloc(sizeof(*c)))) {
        fprintf(stderr, "Out of memory allocating a new %s network client\n", service->descr);
        exit(1);
    }

    c->service    = service;
    c->next       = Modes.clients;
    c->fd         = fd;
    c->buflen     = 0;
    Modes.clients = c;

    ++service->connections;
    if (service->writer && service->connections == 1) {
        service->writer->lastWrite = mstime(); // suppress heartbeat initially
    }

    return c;
}

// Initiate an outgoing connection which will use the given service.
// Return the new client or NULL if the connection failed
struct client *serviceConnect(struct net_service *service, char *addr, int port)
{
    socket_t s;
    char buf[20];

    // Bleh.
    snprintf(buf, 20, "%d", port);
    s = anetTcpConnect(Modes.aneterr, addr, buf);
    if (s == ANET_ERR)
        return NULL;

    return createSocketClient(service, s);
}

// Set up the given service to listen on an address/port.
// _exits_ on failure!
void serviceListen(struct net_service *service, char *bind_addr, char *bind_ports)
{
    socket_t *fds = NULL;
    int n = 0;
    char *p, *end;
    char buf[128];

    if (service->listener_count > 0) {
        fprintf(stderr, "Tried to set up the service %s twice!\n", service->descr);
        exit(1);
    }

    if (!bind_ports || !strcmp(bind_ports, "") || !strcmp(bind_ports, "0"))
        return;

    p = bind_ports;
    while (p && *p) {
        socket_t newfds[16];
        int nfds, i;

        end = strpbrk(p, ", ");
        if (!end) {
            strncpy(buf, p, sizeof(buf));
            buf[sizeof(buf)-1] = 0;
            p = NULL;
        } else {
            size_t len = end - p;
            if (len >= sizeof(buf))
                len = sizeof(buf) - 1;
            memcpy(buf, p, len);
            buf[len] = 0;
            p = end + 1;
        }

        nfds = anetTcpServer(Modes.aneterr, buf, bind_addr, newfds, sizeof(newfds));
        if (nfds == ANET_ERR) {
            fprintf(stderr, "Error opening the listening port %s (%s): %s\n",
                    buf, service->descr, Modes.aneterr);
            exit(1);
        }

        fds = realloc(fds, (n+nfds) * sizeof(socket_t));
        if (!fds) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }

        for (i = 0; i < nfds; ++i) {
            anetNonBlock(Modes.aneterr, newfds[i]);
            fds[n++] = newfds[i];
        }
    }

    service->listener_count = n;
    service->listener_fds = fds;
}

struct net_service *makeBeastInputService(void)
{
    return serviceInit("Beast TCP input", NULL, NULL, NULL, decodeBinMessage);
}

struct net_service *makeFatsvOutputService(void)
{
    return serviceInit("FATSV TCP output", &Modes.fatsv_out, NULL, NULL, NULL);
}

void modesInitNet(void) {
    struct net_service *s;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    Modes.clients = NULL;
    Modes.services = NULL;

    if (anetInit(Modes.aneterr) == ANET_ERR) {
        fprintf(stderr, "Error initializing networking: %s\n", Modes.aneterr);
        exit(1);
    }

    // set up listeners
    s = serviceInit("Raw TCP output", &Modes.raw_out, send_raw_heartbeat, NULL, NULL);
    serviceListen(s, Modes.net_bind_address, Modes.net_output_raw_ports);

    s = serviceInit("Beast TCP output", &Modes.beast_out, send_beast_heartbeat, NULL, NULL);
    serviceListen(s, Modes.net_bind_address, Modes.net_output_beast_ports);

    s = serviceInit("Basestation TCP output", &Modes.sbs_out, send_sbs_heartbeat, NULL, NULL);
    serviceListen(s, Modes.net_bind_address, Modes.net_output_sbs_ports);

    s = serviceInit("Raw TCP input", NULL, NULL, "\n", decodeHexMessage);
    serviceListen(s, Modes.net_bind_address, Modes.net_input_raw_ports);

    s = makeBeastInputService();
    serviceListen(s, Modes.net_bind_address, Modes.net_input_beast_ports);

#ifdef ENABLE_WEBSERVER
    s = serviceInit("HTTP server", NULL, NULL, "\r\n\r\n", handleHTTPRequest);
    serviceListen(s, Modes.net_bind_address, Modes.net_http_ports);
#endif
}
//
//=========================================================================
//
// This function gets called from time to time when the decoding thread is
// awakened by new data arriving. This usually happens a few times every second
//
static struct client * modesAcceptClients(void) {
    socket_t fd;
    struct net_service *s;

    for (s = Modes.services; s; s = s->next) {
        int i;
        for (i = 0; i < s->listener_count; ++i) {
            while ((fd = anetTcpAccept(Modes.aneterr, s->listener_fds[i])) >= 0) {
                createSocketClient(s, fd);
            }
        }
    }

    return Modes.clients;
}
//
//=========================================================================
//
// On error free the client, collect the structure, adjust maxfd if needed.
//
static void modesCloseClient(struct client *c) {
    if (!c->service) {
        fprintf(stderr, "warning: double close of net client\n");
        return;
    }

    // Clean up, but defer removing from the list until modesNetCleanup().
    // This is because there may be stackframes still pointing at this
    // client (unpredictably: reading from client A may cause client B to
    // be freed)

    anetCloseConnection(c->fd);
    c->service->connections--;

    // mark it as inactive and ready to be freed
    c->fd = -1;
    c->service = NULL;
}
//
//=========================================================================
//
// Send the write buffer for the specified writer to all connected clients
//
static void flushWrites(struct net_writer *writer) {
    struct client *c;

    for (c = Modes.clients; c; c = c->next) {
        if (!c->service)
            continue;
        if (c->service == writer->service) {
            int nwritten = anetWrite(c->fd, writer->data, writer->dataUsed);

            if (nwritten != writer->dataUsed) {
                modesCloseClient(c);
            }
        }
    }

    writer->dataUsed = 0;
    writer->lastWrite = mstime();
}

// Prepare to write up to 'len' bytes to the given net_writer.
// Returns a pointer to write to, or NULL to skip this write.
static void *prepareWrite(struct net_writer *writer, int len) {
    if (!writer ||
        !writer->service ||
        !writer->service->connections ||
        !writer->data)
        return NULL;

    if (len > MODES_OUT_BUF_SIZE)
        return NULL;

    if (writer->dataUsed + len >= MODES_OUT_BUF_SIZE) {
        // Flush now to free some space
        flushWrites(writer);
    }

    return (char *)writer->data + writer->dataUsed;
}

// Complete a write previously begun by prepareWrite.
// endptr should point one byte past the last byte written
// to the buffer returned from prepareWrite.
static void completeWrite(struct net_writer *writer, void *endptr) {
    writer->dataUsed = (char *)endptr - (char *)writer->data;

    if (writer->dataUsed >= Modes.net_output_flush_size) {
        flushWrites(writer);
    }
}

//
//=========================================================================
//
// Write raw output in Beast Binary format with Timestamp to TCP clients
//
static void modesSendBeastOutput(struct modesMessage *mm) {
    int  msgLen = mm->msgbits / 8;
    char *p = prepareWrite(&Modes.beast_out, 2 + 2 * (7 + msgLen));
    char ch;
    int  j;
    int sig;
    unsigned char *msg = (Modes.net_verbatim ? mm->verbatim : mm->msg);

    if (!p)
        return;

    *p++ = 0x1a;
    if      (msgLen == MODES_SHORT_MSG_BYTES)
      {*p++ = '2';}
    else if (msgLen == MODES_LONG_MSG_BYTES)
      {*p++ = '3';}
    else if (msgLen == MODEAC_MSG_BYTES)
      {*p++ = '1';}
    else
      {return;}

    /* timestamp, big-endian */
    *p++ = (ch = (mm->timestampMsg >> 40));
    if (0x1A == ch) {*p++ = ch; }
    *p++ = (ch = (mm->timestampMsg >> 32));
    if (0x1A == ch) {*p++ = ch; }
    *p++ = (ch = (mm->timestampMsg >> 24));
    if (0x1A == ch) {*p++ = ch; }
    *p++ = (ch = (mm->timestampMsg >> 16));
    if (0x1A == ch) {*p++ = ch; }
    *p++ = (ch = (mm->timestampMsg >> 8));
    if (0x1A == ch) {*p++ = ch; }
    *p++ = (ch = (mm->timestampMsg));
    if (0x1A == ch) {*p++ = ch; }

    sig = round(sqrt(mm->signalLevel) * 255);
    if (mm->signalLevel > 0 && sig < 1)
        sig = 1;
    if (sig > 255)
        sig = 255;
    *p++ = ch = (char)sig;
    if (0x1A == ch) {*p++ = ch; }

    for (j = 0; j < msgLen; j++) {
        *p++ = (ch = msg[j]);
        if (0x1A == ch) {*p++ = ch; }
    }

    completeWrite(&Modes.beast_out, p);
}

static void send_beast_heartbeat(struct net_service *service)
{
    static char heartbeat_message[] = { 0x1a, '1', 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    char *data;

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, sizeof(heartbeat_message));
    if (!data)
        return;

    memcpy(data, heartbeat_message, sizeof(heartbeat_message));
    completeWrite(service->writer, data + sizeof(heartbeat_message));
}

//
//=========================================================================
//
// Write raw output to TCP clients
//
static void modesSendRawOutput(struct modesMessage *mm) {
    int  msgLen = mm->msgbits / 8;
    char *p = prepareWrite(&Modes.raw_out, msgLen*2 + 15);
    int j;
    unsigned char *msg = (Modes.net_verbatim ? mm->verbatim : mm->msg);

    if (!p)
        return;

    if (Modes.mlat && mm->timestampMsg) {
        /* timestamp, big-endian */
        sprintf(p, "@%012" PRIX64,
                mm->timestampMsg);
        p += 13;
    } else
        *p++ = '*';

    for (j = 0; j < msgLen; j++) {
        sprintf(p, "%02X", msg[j]);
        p += 2;
    }

    *p++ = ';';
    *p++ = '\n';

    completeWrite(&Modes.raw_out, p);
}

static void send_raw_heartbeat(struct net_service *service)
{
    static char *heartbeat_message = "*0000;\n";
    char *data;
    int len = strlen(heartbeat_message);

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, len);
    if (!data)
        return;

    memcpy(data, heartbeat_message, len);
    completeWrite(service->writer, data + len);
}

//
//=========================================================================
//
// Write SBS output to TCP clients
//
static void modesSendSBSOutput(struct modesMessage *mm, struct aircraft *a) {
    char *p;
    struct timespec now;
    struct tm    stTime_receive, stTime_now;
    int          msgType;

    // For now, suppress non-ICAO addresses
    if (mm->addr & MODES_NON_ICAO_ADDRESS)
        return;

    p = prepareWrite(&Modes.sbs_out, 200);
    if (!p)
        return;

    //
    // SBS BS style output checked against the following reference
    // http://www.homepages.mcb.net/bones/SBS/Article/Barebones42_Socket_Data.htm - seems comprehensive
    //

    // Decide on the basic SBS Message Type
    switch (mm->msgtype) {
    case 4:
    case 20:
        msgType = 5;
        break;
        break;

    case 5:
    case 21:
        msgType = 6;
        break;

    case 0:
    case 16:
        msgType = 7;
        break;

    case 11:
        msgType = 8;
        break;

    case 17:
    case 18:
        if (mm->metype >= 1 && mm->metype <= 4) {
            msgType = 1;
        } else if (mm->metype >= 5 && mm->metype <=  8) {
            msgType = 2;
        } else if (mm->metype >= 9 && mm->metype <= 18) {
            msgType = 3;
        } else if (mm->metype == 19) {
            msgType = 4;
        } else {
            return;
        }
        break;

    default:
        return;
    }

    // Fields 1 to 6 : SBS message type and ICAO address of the aircraft and some other stuff
    p += sprintf(p, "MSG,%d,1,1,%06X,1,", msgType, mm->addr);

    // Find current system time
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &stTime_now);

    // Find message reception time
    localtime_r(&mm->sysTimestampMsg.tv_sec, &stTime_receive);

    // Fields 7 & 8 are the message reception time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_receive.tm_year+1900),(stTime_receive.tm_mon+1), stTime_receive.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03u,", stTime_receive.tm_hour, stTime_receive.tm_min, stTime_receive.tm_sec, (unsigned) (mm->sysTimestampMsg.tv_nsec / 1000000U));

    // Fields 9 & 10 are the current time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_now.tm_year+1900),(stTime_now.tm_mon+1), stTime_now.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03u", stTime_now.tm_hour, stTime_now.tm_min, stTime_now.tm_sec, (unsigned) (now.tv_nsec / 1000000U));

    // Field 11 is the callsign (if we have it)
    if (mm->callsign_valid) {p += sprintf(p, ",%s", mm->callsign);}
    else                    {p += sprintf(p, ",");}

    // Field 12 is the altitude (if we have it)
    if (mm->altitude_valid) {
        if (Modes.use_gnss) {
            if (mm->altitude_source == ALTITUDE_GNSS) {
                p += sprintf(p, ",%dH", mm->altitude);
            } else if (trackDataValid(&a->gnss_delta_valid)) {
                p += sprintf(p, ",%dH", mm->altitude + a->gnss_delta);
            } else {
                p += sprintf(p, ",%d", mm->altitude);
            }
        } else {
            if (mm->altitude_source == ALTITUDE_BARO) {
                p += sprintf(p, ",%d", mm->altitude);
            } else if (trackDataValid(&a->gnss_delta_valid)) {
                p += sprintf(p, ",%d", mm->altitude - a->gnss_delta);
            } else {
                p += sprintf(p, ",");
            }
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 13 is the ground Speed (if we have it)
    if (mm->speed_valid && mm->speed_source == SPEED_GROUNDSPEED) {
        p += sprintf(p, ",%d", mm->speed);
    } else {
        p += sprintf(p, ","); 
    }

    // Field 14 is the ground Heading (if we have it)       
    if (mm->heading_valid && mm->heading_source == HEADING_TRUE) {
        p += sprintf(p, ",%d", mm->heading);
    } else {
        p += sprintf(p, ",");
    }

    // Fields 15 and 16 are the Lat/Lon (if we have it)
    if (mm->cpr_decoded) {
        p += sprintf(p, ",%1.5f,%1.5f", mm->decoded_lat, mm->decoded_lon);
    } else {
        p += sprintf(p, ",,");
    }

    // Field 17 is the VerticalRate (if we have it)
    if (mm->vert_rate_valid) {
        p += sprintf(p, ",%d", mm->vert_rate);
    } else {
        p += sprintf(p, ",");
    }

    // Field 18 is  the Squawk (if we have it)
    if (mm->squawk_valid) {
        p += sprintf(p, ",%04x", mm->squawk);
    } else {
        p += sprintf(p, ",");
    }

    // Field 19 is the Squawk Changing Alert flag (if we have it)
    if (mm->alert_valid) {
        if (mm->alert) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 20 is the Squawk Emergency flag (if we have it)
    if (mm->squawk_valid) {
        if ((mm->squawk == 0x7500) || (mm->squawk == 0x7600) || (mm->squawk == 0x7700)) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 21 is the Squawk Ident flag (if we have it)
    if (mm->spi_valid) {
        if (mm->spi) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 22 is the OnTheGround flag (if we have it)
    switch (mm->airground) {
    case AG_GROUND:
        p += sprintf(p, ",-1");
        break;
    case AG_AIRBORNE:
        p += sprintf(p, ",0");
        break;
    default:
        p += sprintf(p, ",");
        break;
    }

    p += sprintf(p, "\r\n");

    completeWrite(&Modes.sbs_out, p);
}

static void send_sbs_heartbeat(struct net_service *service)
{
    static char *heartbeat_message = "\r\n";  // is there a better one?
    char *data;
    int len = strlen(heartbeat_message);

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, len);
    if (!data)
        return;

    memcpy(data, heartbeat_message, len);
    completeWrite(service->writer, data + len);
}

//
//=========================================================================
//
void modesQueueOutput(struct modesMessage *mm, struct aircraft *a) {
    int is_mlat = (mm->source == SOURCE_MLAT);

    if (!is_mlat && mm->correctedbits < 2) {
        // Don't ever forward 2-bit-corrected messages via SBS output.
        // Don't ever forward mlat messages via SBS output.
        modesSendSBSOutput(mm, a);
    }

    if (!is_mlat && (Modes.net_verbatim || mm->correctedbits < 2)) {
        // Forward 2-bit-corrected messages via raw output only if --net-verbatim is set
        // Don't ever forward mlat messages via raw output.
        modesSendRawOutput(mm);
    }

    if ((!is_mlat || Modes.forward_mlat) && (Modes.net_verbatim || mm->correctedbits < 2)) {
        // Forward 2-bit-corrected messages via beast output only if --net-verbatim is set
        // Forward mlat messages via beast output only if --forward-mlat is set
        modesSendBeastOutput(mm);
    }

    if (!is_mlat) {
        writeFATSVEvent(mm, a);
    }
}
//
//=========================================================================
//
// This function decodes a Beast binary format message
//
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
//
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no
// case where we want broken messages here to close the client connection.
//
static int decodeBinMessage(struct client *c, char *p) {
    int msgLen = 0;
    int  j;
    char ch;
    unsigned char msg[MODES_LONG_MSG_BYTES];
    static struct modesMessage zeroMessage;
    struct modesMessage mm;
    MODES_NOTUSED(c);
    memset(&mm, 0, sizeof(mm));

    ch = *p++; /// Get the message type
    if (0x1A == ch) {p++;} 

    if       ((ch == '1') && (Modes.mode_ac)) { // skip ModeA/C unless user enables --modes-ac
        msgLen = MODEAC_MSG_BYTES;
    } else if (ch == '2') {
        msgLen = MODES_SHORT_MSG_BYTES;
    } else if (ch == '3') {
        msgLen = MODES_LONG_MSG_BYTES;
    }

    if (msgLen) {
        mm = zeroMessage;

        // Mark messages received over the internet as remote so that we don't try to
        // pass them off as being received by this instance when forwarding them
        mm.remote      =    1;

        // Grab the timestamp (big endian format)
        mm.timestampMsg = 0;
        for (j = 0; j < 6; j++) {
            ch = *p++;
            mm.timestampMsg = mm.timestampMsg << 8 | (ch & 255);
            if (0x1A == ch) {p++;}
        }

        // record reception time as the time we read it.
        clock_gettime(CLOCK_REALTIME, &mm.sysTimestampMsg);

        ch = *p++;  // Grab the signal level
        mm.signalLevel = ((unsigned char)ch / 255.0);
        mm.signalLevel = mm.signalLevel * mm.signalLevel;
        if (0x1A == ch) {p++;}

        for (j = 0; j < msgLen; j++) { // and the data
            msg[j] = ch = *p++;
            if (0x1A == ch) {p++;}
        }

        if (msgLen == MODEAC_MSG_BYTES) { // ModeA or ModeC
            Modes.stats_current.remote_received_modeac++;
            decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));
        } else {
            int result;

            Modes.stats_current.remote_received_modes++;
            result = decodeModesMessage(&mm, msg);
            if (result < 0) {
                if (result == -1)
                    Modes.stats_current.remote_rejected_unknown_icao++;
                else
                    Modes.stats_current.remote_rejected_bad++;
                return 0;
            } else {
                Modes.stats_current.remote_accepted[mm.correctedbits]++;
            }
        }

        useModesMessage(&mm);
    }
    return (0);
}
//
//=========================================================================
//
// Turn an hex digit into its 4 bit decimal value.
// Returns -1 if the digit is not in the 0-F range.
//
static int hexDigitVal(int c) {
    c = tolower(c);
    if (c >= '0' && c <= '9') return c-'0';
    else if (c >= 'a' && c <= 'f') return c-'a'+10;
    else return -1;
}
//
//=========================================================================
//
// This function decodes a string representing message in raw hex format
// like: *8D4B969699155600E87406F5B69F; The string is null-terminated.
// 
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
// 
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no 
// case where we want broken messages here to close the client connection.
//
static int decodeHexMessage(struct client *c, char *hex) {
    int l = strlen(hex), j;
    unsigned char msg[MODES_LONG_MSG_BYTES];
    struct modesMessage mm;
    static struct modesMessage zeroMessage;

    MODES_NOTUSED(c);
    mm = zeroMessage;

    // Mark messages received over the internet as remote so that we don't try to
    // pass them off as being received by this instance when forwarding them
    mm.remote      =    1;
    mm.signalLevel =    0;

    // Remove spaces on the left and on the right
    while(l && isspace(hex[l-1])) {
        hex[l-1] = '\0'; l--;
    }
    while(isspace(*hex)) {
        hex++; l--;
    }

    // Turn the message into binary.
    // Accept *-AVR raw @-AVR/BEAST timeS+raw %-AVR timeS+raw (CRC good) <-BEAST timeS+sigL+raw
    // and some AVR records that we can understand
    if (hex[l-1] != ';') {return (0);} // not complete - abort

    switch(hex[0]) {
        case '<': {
            mm.signalLevel = ((hexDigitVal(hex[13])<<4) | hexDigitVal(hex[14])) / 255.0;
            mm.signalLevel = mm.signalLevel * mm.signalLevel;
            hex += 15; l -= 16; // Skip <, timestamp and siglevel, and ;
            break;}

        case '@':     // No CRC check
        case '%': {   // CRC is OK
            hex += 13; l -= 14; // Skip @,%, and timestamp, and ;
            break;}

        case '*':
        case ':': {
            hex++; l-=2; // Skip * and ;
            break;}

        default: {
            return (0); // We don't know what this is, so abort
            break;}
    }

    if ( (l != (MODEAC_MSG_BYTES      * 2)) 
      && (l != (MODES_SHORT_MSG_BYTES * 2)) 
      && (l != (MODES_LONG_MSG_BYTES  * 2)) )
        {return (0);} // Too short or long message... broken

    if ( (0 == Modes.mode_ac) 
      && (l == (MODEAC_MSG_BYTES * 2)) ) 
        {return (0);} // Right length for ModeA/C, but not enabled

    for (j = 0; j < l; j += 2) {
        int high = hexDigitVal(hex[j]);
        int low  = hexDigitVal(hex[j+1]);

        if (high == -1 || low == -1) return 0;
        msg[j/2] = (high << 4) | low;
    }

    // record reception time as the time we read it.
    clock_gettime(CLOCK_REALTIME, &mm.sysTimestampMsg);

    if (l == (MODEAC_MSG_BYTES * 2)) {  // ModeA or ModeC
        Modes.stats_current.remote_received_modeac++;
        decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));
    } else {       // Assume ModeS
        int result;

        Modes.stats_current.remote_received_modes++;
        result = decodeModesMessage(&mm, msg);
        if (result < 0) {
            if (result == -1)
                Modes.stats_current.remote_rejected_unknown_icao++;
            else
                Modes.stats_current.remote_rejected_bad++;
            return 0;
        } else {
            Modes.stats_current.remote_accepted[mm.correctedbits]++;
        }
    }

    useModesMessage(&mm);
    return (0);
}
//
//=========================================================================
//
// Return a description of planes in json. No metric conversion
//

// usual caveats about function-returning-pointer-to-static-buffer apply
static const char *jsonEscapeString(const char *str) {
    static char buf[1024];
    const char *in = str;
    char *out = buf, *end = buf + sizeof(buf) - 10;

    for (; *in && out < end; ++in) {
        unsigned char ch = *in;
        if (ch == '"' || ch == '\\') {
            *out++ = '\\';
            *out++ = ch;
        } else if (ch < 32 || ch > 127) {
            out += snprintf(out, end - out, "\\u%04x", ch);
        } else {
            *out++ = ch;
        }
    }

    *out++ = 0;
    return buf;
}

static char *append_flags(char *p, char *end, struct aircraft *a, datasource_t source)
{
    p += snprintf(p, end-p, "[");
    if (a->squawk_valid.source == source)
        p += snprintf(p, end-p, "\"squawk\",");
    if (a->callsign_valid.source == source)
        p += snprintf(p, end-p, "\"callsign\",");
    if (a->position_valid.source == source)
        p += snprintf(p, end-p, "\"lat\",\"lon\",");
    if (a->altitude_valid.source == source)
        p += snprintf(p, end-p, "\"altitude\",");
    if (a->heading_valid.source == source)
        p += snprintf(p, end-p, "\"track\",");
    if (a->speed_valid.source == source)
        p += snprintf(p, end-p, "\"speed\",");
    if (a->vert_rate_valid.source == source)
        p += snprintf(p, end-p, "\"vert_rate\",");
    if (a->category_valid.source == source)
        p += snprintf(p, end-p, "\"category\",");
    if (p[-1] != '[')
        --p;
    p += snprintf(p, end-p, "]");
    return p;
}

static const char *addrtype_short_string(addrtype_t type) {
    switch (type) {
    case ADDR_ADSB_ICAO:
        return "adsb_icao";
    case ADDR_ADSB_ICAO_NT:
        return "adsb_icao_nt";
    case ADDR_ADSR_ICAO:
        return "adsr_icao";
    case ADDR_TISB_ICAO:
        return "tisb_icao";
    case ADDR_ADSB_OTHER:
        return "adsb_other";
    case ADDR_ADSR_OTHER:
        return "adsr_other";
    case ADDR_TISB_OTHER:
        return "tisb_other";
    case ADDR_TISB_TRACKFILE:
        return "tisb_trackfile";
    default:
        return "unknown";
    }
}

char *generateAircraftJson(const char *url_path, int *len) {
    uint64_t now = mstime();
    struct aircraft *a;
    int buflen = 1024; // The initial buffer is incremented as needed
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf+buflen;
    int first = 1;

    MODES_NOTUSED(url_path);

    p += snprintf(p, end-p,
                  "{ \"now\" : %.1f,\n"
                  "  \"messages\" : %u,\n"
                  "  \"aircraft\" : [",
                  now / 1000.0,
                  Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    for (a = Modes.aircrafts; a; a = a->next) {
        if (a->modeACflags & MODEAC_MSG_FLAG) { // skip any fudged ICAO records Mode A/C
            continue;
        }

        if (a->messages < 2) { // basic filter for bad decodes
            continue;
        }

        if (first)            
            first = 0;
        else
            *p++ = ',';
            
        p += snprintf(p, end-p, "\n    {\"hex\":\"%s%06x\"", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
        if (a->addrtype != ADDR_ADSB_ICAO)
            p += snprintf(p, end-p, ",\"type\":\"%s\"", addrtype_short_string(a->addrtype));
        if (trackDataValid(&a->squawk_valid))
            p += snprintf(p, end-p, ",\"squawk\":\"%04x\"", a->squawk);
        if (trackDataValid(&a->callsign_valid))
            p += snprintf(p, end-p, ",\"flight\":\"%s\"", jsonEscapeString(a->callsign));
        if (trackDataValid(&a->position_valid))
            p += snprintf(p, end-p, ",\"lat\":%f,\"lon\":%f,\"nucp\":%u,\"seen_pos\":%.1f", a->lat, a->lon, a->pos_nuc, (now - a->position_valid.updated)/1000.0);
        if (trackDataValid(&a->airground_valid) && a->airground_valid.source >= SOURCE_MODE_S_CHECKED && a->airground == AG_GROUND)
            p += snprintf(p, end-p, ",\"altitude\":\"ground\"");
        else if (trackDataValid(&a->altitude_valid))
            p += snprintf(p, end-p, ",\"altitude\":%d", a->altitude);
        if (trackDataValid(&a->vert_rate_valid))
            p += snprintf(p, end-p, ",\"vert_rate\":%d", a->vert_rate);
        if (trackDataValid(&a->heading_valid))
            p += snprintf(p, end-p, ",\"track\":%d", a->heading);
        if (trackDataValid(&a->speed_valid))
            p += snprintf(p, end-p, ",\"speed\":%d", a->speed);
        if (trackDataValid(&a->category_valid))
            p += snprintf(p, end-p, ",\"category\":\"%02X\"", a->category);

        p += snprintf(p, end-p, ",\"mlat\":");
        p = append_flags(p, end, a, SOURCE_MLAT);
        p += snprintf(p, end-p, ",\"tisb\":");
        p = append_flags(p, end, a, SOURCE_TISB);

        p += snprintf(p, end-p, ",\"messages\":%ld,\"seen\":%.1f,\"rssi\":%.1f}",
                      a->messages, (now - a->seen)/1000.0,
                      10 * log10((a->signalLevel[0] + a->signalLevel[1] + a->signalLevel[2] + a->signalLevel[3] +
                                  a->signalLevel[4] + a->signalLevel[5] + a->signalLevel[6] + a->signalLevel[7] + 1e-5) / 8));
        
        // If we're getting near the end of the buffer, expand it.
        if ((end - p) < 512) {
            int used = p - buf;
            buflen *= 2;
            buf = (char *) realloc(buf, buflen);
            p = buf+used;
            end = buf + buflen;
        }
    }

    p += snprintf(p, end-p, "\n  ]\n}\n");
    *len = p-buf;
    return buf;
}

static char * appendStatsJson(char *p,
                              char *end,
                              struct stats *st,
                              const char *key)
{
    int i;

    p += snprintf(p, end-p,
                  "\"%s\":{\"start\":%.1f,\"end\":%.1f",
                  key,
                  st->start / 1000.0,
                  st->end / 1000.0);

    if (!Modes.net_only) {
        p += snprintf(p, end-p,
                      ",\"local\":{\"samples_processed\":%llu"
                      ",\"samples_dropped\":%llu"
                      ",\"modeac\":%u"
                      ",\"modes\":%u"
                      ",\"bad\":%u"
                      ",\"unknown_icao\":%u",
                      (unsigned long long)st->samples_processed,
                      (unsigned long long)st->samples_dropped,
                      st->demod_modeac,
                      st->demod_preambles,
                      st->demod_rejected_bad,
                      st->demod_rejected_unknown_icao);

        for (i=0; i <= Modes.nfix_crc; ++i) {
            if (i == 0) p += snprintf(p, end-p, ",\"accepted\":[%u", st->demod_accepted[i]);
            else p += snprintf(p, end-p, ",%u", st->demod_accepted[i]);
        }

        p += snprintf(p, end-p, "]");

        if (st->signal_power_sum > 0 && st->signal_power_count > 0)
            p += snprintf(p, end-p,",\"signal\":%.1f", 10 * log10(st->signal_power_sum / st->signal_power_count));
        if (st->noise_power_sum > 0 && st->noise_power_count > 0)
            p += snprintf(p, end-p,",\"noise\":%.1f", 10 * log10(st->noise_power_sum / st->noise_power_count));
        if (st->peak_signal_power > 0)
            p += snprintf(p, end-p,",\"peak_signal\":%.1f", 10 * log10(st->peak_signal_power));

        p += snprintf(p, end-p,",\"strong_signals\":%d}", st->strong_signal_count);
    }

    if (Modes.net) {
        p += snprintf(p, end-p,
                      ",\"remote\":{\"modeac\":%u"
                      ",\"modes\":%u"
                      ",\"bad\":%u"
                      ",\"unknown_icao\":%u",
                      st->remote_received_modeac,
                      st->remote_received_modes,
                      st->remote_rejected_bad,
                      st->remote_rejected_unknown_icao);

        for (i=0; i <= Modes.nfix_crc; ++i) {
            if (i == 0) p += snprintf(p, end-p, ",\"accepted\":[%u", st->remote_accepted[i]);
            else p += snprintf(p, end-p, ",%u", st->remote_accepted[i]);
        }

        p += snprintf(p, end-p, "]}");

#ifdef ENABLE_WEBSERVER
        p += snprintf(p, end-p, ",\"http_requests\":%u", st->http_requests);
#endif
    }

    {
        uint64_t demod_cpu_millis = (uint64_t)st->demod_cpu.tv_sec*1000UL + st->demod_cpu.tv_nsec/1000000UL;
        uint64_t reader_cpu_millis = (uint64_t)st->reader_cpu.tv_sec*1000UL + st->reader_cpu.tv_nsec/1000000UL;
        uint64_t background_cpu_millis = (uint64_t)st->background_cpu.tv_sec*1000UL + st->background_cpu.tv_nsec/1000000UL;

        p += snprintf(p, end-p,
                      ",\"cpr\":{\"surface\":%u"
                      ",\"airborne\":%u"
                      ",\"global_ok\":%u"
                      ",\"global_bad\":%u"
                      ",\"global_range\":%u"
                      ",\"global_speed\":%u"
                      ",\"global_skipped\":%u"
                      ",\"local_ok\":%u"
                      ",\"local_aircraft_relative\":%u"
                      ",\"local_receiver_relative\":%u"
                      ",\"local_skipped\":%u"
                      ",\"local_range\":%u"
                      ",\"local_speed\":%u"
                      ",\"filtered\":%u}"
                      ",\"altitude_suppressed\":%u"
                      ",\"cpu\":{\"demod\":%llu,\"reader\":%llu,\"background\":%llu}"
                      ",\"tracks\":{\"all\":%u"
                      ",\"single_message\":%u}"
                      ",\"messages\":%u}",
                      st->cpr_surface,
                      st->cpr_airborne,
                      st->cpr_global_ok,
                      st->cpr_global_bad,
                      st->cpr_global_range_checks,
                      st->cpr_global_speed_checks,
                      st->cpr_global_skipped,
                      st->cpr_local_ok,
                      st->cpr_local_aircraft_relative,
                      st->cpr_local_receiver_relative,
                      st->cpr_local_skipped,
                      st->cpr_local_range_checks,
                      st->cpr_local_speed_checks,
                      st->cpr_filtered,
                      st->suppressed_altitude_messages,
                      (unsigned long long)demod_cpu_millis,
                      (unsigned long long)reader_cpu_millis,
                      (unsigned long long)background_cpu_millis,
                      st->unique_aircraft,
                      st->single_message_aircraft,
                      st->messages_total);
    }

    return p;
}
    
char *generateStatsJson(const char *url_path, int *len) {
    struct stats add;
    char *buf = (char *) malloc(4096), *p = buf, *end = buf + 4096;

    MODES_NOTUSED(url_path);

    p += snprintf(p, end-p, "{\n");
    p = appendStatsJson(p, end, &Modes.stats_current, "latest");
    p += snprintf(p, end-p, ",\n");

    p = appendStatsJson(p, end, &Modes.stats_1min[Modes.stats_latest_1min], "last1min");
    p += snprintf(p, end-p, ",\n");

    p = appendStatsJson(p, end, &Modes.stats_5min, "last5min");
    p += snprintf(p, end-p, ",\n");

    p = appendStatsJson(p, end, &Modes.stats_15min, "last15min");
    p += snprintf(p, end-p, ",\n");

    add_stats(&Modes.stats_alltime, &Modes.stats_current, &add);
    p = appendStatsJson(p, end, &add, "total");
    p += snprintf(p, end-p, "\n}\n");    

    assert(p <= end);

    *len = p-buf;
    return buf;
}

//
// Return a description of the receiver in json.
//
char *generateReceiverJson(const char *url_path, int *len)
{
    char *buf = (char *) malloc(1024), *p = buf;
    int history_size;

    MODES_NOTUSED(url_path);

    // work out number of valid history entries
    if (Modes.json_aircraft_history[HISTORY_SIZE-1].content == NULL)
        history_size = Modes.json_aircraft_history_next;
    else
        history_size = HISTORY_SIZE;

    p += sprintf(p, "{ " \
                 "\"version\" : \"%s\", "
                 "\"refresh\" : %.0f, "
                 "\"history\" : %d",
                 MODES_DUMP1090_VERSION, 1.0*Modes.json_interval, history_size);

    if (Modes.json_location_accuracy && (Modes.fUserLat != 0.0 || Modes.fUserLon != 0.0)) {
        if (Modes.json_location_accuracy == 1) {
            p += sprintf(p, ", "                \
                         "\"lat\" : %.2f, "
                         "\"lon\" : %.2f",
                         Modes.fUserLat, Modes.fUserLon);  // round to 2dp - about 0.5-1km accuracy - for privacy reasons
        } else {
            p += sprintf(p, ", "                \
                         "\"lat\" : %.6f, "
                         "\"lon\" : %.6f",
                         Modes.fUserLat, Modes.fUserLon);  // exact location
        }
    }

    p += sprintf(p, " }\n");

    *len = (p - buf);
    return buf;
}

char *generateHistoryJson(const char *url_path, int *len)
{
    int history_index = -1;

    if (sscanf(url_path, "/data/history_%d.json", &history_index) != 1)
        return NULL;

    if (history_index < 0 || history_index >= HISTORY_SIZE)
        return NULL;

    if (!Modes.json_aircraft_history[history_index].content)
        return NULL;

    *len = Modes.json_aircraft_history[history_index].clen;
    return strdup(Modes.json_aircraft_history[history_index].content);
}

// Write JSON to file
void writeJsonToFile(const char *file, char * (*generator) (const char *,int*))
{
    char pathbuf[PATH_MAX];
    char tmppath[PATH_MAX];
    int fd;
    int len = 0;
    char *content;

    if (!Modes.json_dir)
        return;

#ifdef _WIN32
    snprintf(tmppath, PATH_MAX, "%s/%s.%lli", Modes.json_dir, file, mstime());
    tmppath[PATH_MAX - 1] = 0;
    fd = open(tmppath, _O_CREAT | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);

    if (fd < 0)
        return;
#else
    snprintf(tmppath, PATH_MAX, "%s/%s.XXXXXX", Modes.json_dir, file);
    tmppath[PATH_MAX-1] = 0;
    fd = mkstemp(tmppath);

    if (fd < 0)
        return;
    
    mode_t mask = umask(0);
    umask(mask);
    fchmod(fd, 0644 & ~mask);
#endif

    snprintf(pathbuf, PATH_MAX, "/data/%s", file);
    pathbuf[PATH_MAX-1] = 0;
    content = generator(pathbuf, &len);

    if (write(fd, content, len) != len)
        goto error_1;

    if (close(fd) < 0)
        goto error_2;

    snprintf(pathbuf, PATH_MAX, "%s/%s", Modes.json_dir, file);
    pathbuf[PATH_MAX-1] = 0;
#ifdef _WIN32 // Windows doesn't replace
    unlink(pathbuf);
#endif
    rename(tmppath, pathbuf);
    free(content);
    return;

 error_1:
    close(fd);
 error_2:
    unlink(tmppath);
    free(content);
    return;
}


#ifdef ENABLE_WEBSERVER

//
//=========================================================================
//
#define MODES_CONTENT_TYPE_HTML "text/html;charset=utf-8"
#define MODES_CONTENT_TYPE_CSS  "text/css;charset=utf-8"
#define MODES_CONTENT_TYPE_JSON "application/json;charset=utf-8"
#define MODES_CONTENT_TYPE_JS   "application/javascript;charset=utf-8"
#define MODES_CONTENT_TYPE_GIF  "image/gif"

static struct {
    char *path;
    char * (*handler)(const char*,int*);
    char *content_type;
    int prefix;
} url_handlers[] = {
    { "/data/aircraft.json", generateAircraftJson, MODES_CONTENT_TYPE_JSON, 0 },
    { "/data/receiver.json", generateReceiverJson, MODES_CONTENT_TYPE_JSON, 0 },
    { "/data/stats.json", generateStatsJson, MODES_CONTENT_TYPE_JSON, 0 },
    { "/data/history_", generateHistoryJson, MODES_CONTENT_TYPE_JSON, 1 },
    { NULL, NULL, NULL, 0 }
};

//
// Get an HTTP request header and write the response to the client.
// gain here we assume that the socket buffer is enough without doing
// any kind of userspace buffering.
//
// Returns 1 on error to signal the caller the client connection should
// be closed.
//
static int handleHTTPRequest(struct client *c, char *p) {
    char hdr[512];
    int clen, hdrlen;
    int httpver, keepalive;
    int statuscode = 500;
    const char *statusmsg = "Internal Server Error";
    char *url, *content = NULL;
    char *ext;
    char *content_type = NULL;
    int i;

    if (Modes.debug & MODES_DEBUG_NET)
        printf("\nHTTP request: %s\n", c->buf);

    // Minimally parse the request.
    httpver = (strstr(p, "HTTP/1.1") != NULL) ? 11 : 10;
    if (httpver == 10) {
        // HTTP 1.0 defaults to close, unless otherwise specified.
        keepalive = strstr(p, "Connection: keep-alive") != NULL;
    } else if (httpver == 11) {
        // HTTP 1.1 defaults to keep-alive, unless close is specified.
        keepalive = strstr(p, "Connection: close") == NULL;
    }

    // Identify he URL.
    p = strchr(p,' ');
    if (!p) return 1; // There should be the method and a space
    url = ++p;        // Now this should point to the requested URL
    p = strchr(p, ' ');
    if (!p) return 1; // There should be a space before HTTP/
    *p = '\0';

    if (Modes.debug & MODES_DEBUG_NET) {
        printf("\nHTTP keep alive: %d\n", keepalive);
        printf("HTTP requested URL: %s\n\n", url);
    }
    
    // Ditch any trailing query part (AJAX might add one to avoid caching)
    p = strchr(url, '?');
    if (p) *p = 0;

    statuscode = 404;
    statusmsg = "Not Found";
    for (i = 0; url_handlers[i].path; ++i) {
        if ((url_handlers[i].prefix && !strncmp(url, url_handlers[i].path, strlen(url_handlers[i].path))) ||
            (!url_handlers[i].prefix && !strcmp(url, url_handlers[i].path))) {
            content_type = url_handlers[i].content_type;
            content = url_handlers[i].handler(url, &clen);
            if (!content)
                continue;

            statuscode = 200;
            statusmsg = "OK";
            if (Modes.debug & MODES_DEBUG_NET) {
                printf("HTTP: 200: %s -> internal (%d bytes, %s)\n", url, clen, content_type);
            }
            break;
        }
    }
            
    if (!content) {
        struct stat sbuf;
        int fd = -1;
        char rp[PATH_MAX], hrp[PATH_MAX];
        char getFile[1024];

        if (strlen(url) < 2) {
            snprintf(getFile, sizeof getFile, "%s/gmap.html", Modes.html_dir); // Default file
        } else {
            snprintf(getFile, sizeof getFile, "%s/%s", Modes.html_dir, url);
        }

        if (!realpath(getFile, rp))
            rp[0] = 0;
        if (!realpath(Modes.html_dir, hrp))
            strcpy(hrp, Modes.html_dir);

        clen = -1;
        content = strdup("Server error occured");
        if (!strncmp(hrp, rp, strlen(hrp))) {
            if (stat(getFile, &sbuf) != -1 && (fd = open(getFile, OPEN_FLAGS)) != -1) {
                content = (char *) realloc(content, sbuf.st_size);
                if (read(fd, content, sbuf.st_size) == sbuf.st_size) {
                    clen = sbuf.st_size;
                    statuscode = 200;
                    statusmsg = "OK";
                }
            }
        } else {
            errno = ENOENT;
        }

        if (clen < 0) {
            content = realloc(content, 128);
            clen = snprintf(content, 128, "Error opening HTML file: %s", strerror(errno));
            statuscode = 404;
            statusmsg = "Not Found";
        }
        
        if (fd != -1) {
            close(fd);
        }

        // Get file extension and content type
        content_type = MODES_CONTENT_TYPE_HTML; // Default content type
        ext = strrchr(getFile, '.');
        
        if (ext) {
            if (!strcmp(ext, ".json")) {
                content_type = MODES_CONTENT_TYPE_JSON;
            } else if (!strcmp(ext, ".css")) {
                content_type = MODES_CONTENT_TYPE_CSS;
            } else if (!strcmp(ext, ".js")) {
                content_type = MODES_CONTENT_TYPE_JS;
            } else if (!strcmp(ext, ".gif")) {
                content_type = MODES_CONTENT_TYPE_GIF;
            }
        }

        if (Modes.debug & MODES_DEBUG_NET) {
            printf("HTTP: %d %s: %s -> %s (%d bytes, %s)\n", statuscode, statusmsg, url, rp, clen, content_type);
        }
    }


    // Create the header and send the reply
    hdrlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Server: Dump1090\r\n"
        "Content-Type: %s\r\n"
        "Connection: %s\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, must-revalidate\r\n"
        "Expires: Sat, 26 Jul 1997 05:00:00 GMT\r\n"
        "\r\n",
        statuscode, statusmsg,
        content_type,
        keepalive ? "keep-alive" : "close",
        clen);

    if (Modes.debug & MODES_DEBUG_NET) {
        printf("HTTP Reply header:\n%s", hdr);
    }

    // Send header and content.
    if ( (anetWrite(c->fd, hdr, hdrlen) != hdrlen)
      || (anetWrite(c->fd, content, clen) != clen) )
    {
        free(content);
        return 1;
    }
    free(content);
    Modes.stats_current.http_requests++;
    return !keepalive;
}

#endif

//
//=========================================================================
//
// This function polls the clients using read() in order to receive new
// messages from the net.
//
// The message is supposed to be separated from the next message by the
// separator 'sep', which is a null-terminated C string.
//
// Every full message received is decoded and passed to the higher layers
// calling the function's 'handler'.
//
// The handler returns 0 on success, or 1 to signal this function we should
// close the connection with the client in case of non-recoverable errors.
//
static void modesReadFromClient(struct client *c) {
    int left;
    int nread;
    int fullmsg;
    int bContinue = 1;
    char *s, *e, *p;

    while(bContinue) {

        fullmsg = 0;
        left = MODES_CLIENT_BUF_SIZE - c->buflen;
        // If our buffer is full discard it, this is some badly formatted shit
        if (left <= 0) {
            c->buflen = 0;
            left = MODES_CLIENT_BUF_SIZE;
            // If there is garbage, read more to discard it ASAP
        }

        nread = anetRead(c->fd, c->buf+c->buflen, left);

        // If we didn't get all the data we asked for, then return once we've processed what we did get.
        if (nread != left) {
            bContinue = 0;
        }

        if (nread == 0) { // End of file
            modesCloseClient(c);
            return;
        }

        if (nread < 0 && (sockErrorIs(AGAIN) || sockErrorIs(WOULDBLOCK))) // No data available (not really an error)
        {
            return;
        }

        if (nread < 0) { // Other errors
            modesCloseClient(c);
            return;
        }

        c->buflen += nread;

        // Always null-term so we are free to use strstr() (it won't affect binary case)
        c->buf[c->buflen] = '\0';

        e = s = c->buf;                                // Start with the start of buffer, first message

        if (c->service->read_sep == NULL) {
            // This is the Beast Binary scanning case.
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.

            left = c->buflen;                                  // Length of valid search for memchr()
            while (left > 1 && ((s = memchr(e, (char) 0x1a, left)) != NULL)) { // The first byte of buffer 'should' be 0x1a
                s++;                                           // skip the 0x1a
                if        (*s == '1') {
                    e = s + MODEAC_MSG_BYTES      + 8;         // point past remainder of message
                } else if (*s == '2') {
                    e = s + MODES_SHORT_MSG_BYTES + 8;
                } else if (*s == '3') {
                    e = s + MODES_LONG_MSG_BYTES  + 8;
                } else {
                    e = s;                                     // Not a valid beast message, skip
                    left = &(c->buf[c->buflen]) - e;
                    continue;
                }
                // we need to be careful of double escape characters in the message body
                for (p = s; p < e; p++) {
                    if (0x1A == *p) {
                        p++; e++;
                        if (e > &(c->buf[c->buflen])) {
                            break;
                        }
                    }
                }
                left = &(c->buf[c->buflen]) - e;
                if (left < 0) {                                // Incomplete message in buffer
                    e = s - 1;                                 // point back at last found 0x1a.
                    break;
                }
                // Have a 0x1a followed by 1, 2 or 3 - pass message less 0x1a to handler.
                if (c->service->read_handler(c, s)) {
                    modesCloseClient(c);
                    return;
                }
                fullmsg = 1;
            }
            s = e;     // For the buffer remainder below

        } else {
            //
            // This is the ASCII scanning case, AVR RAW or HTTP at present
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.
            //
            while ((e = strstr(s, c->service->read_sep)) != NULL) { // end of first message if found
                *e = '\0';                         // The handler expects null terminated strings
                if (c->service->read_handler(c, s)) {               // Pass message to handler.
                    modesCloseClient(c);           // Handler returns 1 on error to signal we .
                    return;                        // should close the client connection
                }
                s = e + strlen(c->service->read_sep);               // Move to start of next message
                fullmsg = 1;
            }
        }

        if (fullmsg) {                             // We processed something - so
            c->buflen = &(c->buf[c->buflen]) - s;  //     Update the unprocessed buffer length
            memmove(c->buf, s, c->buflen);         //     Move what's remaining to the start of the buffer
        } else {                                   // If no message was decoded process the next client
            return;
        }
    }
}

#define TSV_MAX_PACKET_SIZE 275

static void writeFATSVEventMessage(struct modesMessage *mm, const char *datafield, unsigned char *data, size_t len)
{
    char *p = prepareWrite(&Modes.fatsv_out, TSV_MAX_PACKET_SIZE);
    if (!p)
        return;

    char *end = p + TSV_MAX_PACKET_SIZE;
#       define bufsize(_p,_e) ((_p) >= (_e) ? (size_t)0 : (size_t)((_e) - (_p)))

    p += snprintf(p, bufsize(p, end), "clock\t%" PRIu64, mstime() / 1000);

    if (mm->addr & MODES_NON_ICAO_ADDRESS) {
        p += snprintf(p, bufsize(p, end), "\totherid\t%06X", mm->addr & 0xFFFFFF);
    } else {
        p += snprintf(p, bufsize(p, end), "\thexid\t%06X", mm->addr);
    }

    if (mm->addrtype != ADDR_ADSB_ICAO) {
        p += snprintf(p, bufsize(p, end), "\taddrtype\t%s", addrtype_short_string(mm->addrtype));
    }

    p += snprintf(p, bufsize(p, end), "\t%s\t", datafield);
    for (size_t i = 0; i < len; ++i) {
        p += snprintf(p, bufsize(p, end), "%02X", data[i]);
    }

    p += snprintf(p, bufsize(p, end), "\n");

    if (p <= end)
        completeWrite(&Modes.fatsv_out, p);
    else
        fprintf(stderr, "fatsv: output too large (max %d, overran by %d)\n", TSV_MAX_PACKET_SIZE, (int) (p - end));
#       undef bufsize
}

static void writeFATSVEvent(struct modesMessage *mm, struct aircraft *a)
{
    // Write event records for a couple of message types.

    if (!Modes.fatsv_out.service || !Modes.fatsv_out.service->connections) {
        return; // not enabled or no active connections
    }

    if (a->messages < 2)  // basic filter for bad decodes
        return;

    switch (mm->msgtype) {
    case 20:
    case 21:
        if (mm->correctedbits > 0)
            break; // only messages we trust a little more

        // DF 20/21: Comm-B: emit if they've changed since we last sent them
        //
        // BDS 1,0: data link capability report
        // BDS 3,0: ACAS RA report
        if (mm->MB[0] == 0x10 && memcmp(mm->MB, a->fatsv_emitted_bds_10, 7) != 0) {
            memcpy(a->fatsv_emitted_bds_10, mm->MB, 7);
            writeFATSVEventMessage(mm, "datalink_caps", mm->MB, 7);
        }

        else if (mm->MB[0] == 0x30 && memcmp(mm->MB, a->fatsv_emitted_bds_30, 7) != 0) {
            memcpy(a->fatsv_emitted_bds_30, mm->MB, 7);
            writeFATSVEventMessage(mm, "commb_acas_ra", mm->MB, 7);
        }

        break;

    case 17:
    case 18:
        // DF 17/18: extended squitter
        if (mm->metype == 28 && mm->mesub == 2 && memcmp(mm->ME, &a->fatsv_emitted_es_acas_ra, 7) != 0) {
            // type 28 subtype 2: ACAS RA report
            // first byte has the type/subtype, remaining bytes match the BDS 3,0 format
            memcpy(a->fatsv_emitted_es_acas_ra, mm->ME, 7);
            writeFATSVEventMessage(mm, "es_acas_ra", mm->ME, 7);
        } else if (mm->metype == 31 && (mm->mesub == 0 || mm->mesub == 1) && memcmp(mm->ME, a->fatsv_emitted_es_status, 7) != 0) {
            // aircraft operational status
            memcpy(a->fatsv_emitted_es_status, mm->ME, 7);
            writeFATSVEventMessage(mm, "es_op_status", mm->ME, 7);
        } else if (mm->metype == 29 && (mm->mesub == 0 || mm->mesub == 1) && memcmp(mm->ME, a->fatsv_emitted_es_target, 7) != 0) {
            // target state and status
            memcpy(a->fatsv_emitted_es_target, mm->ME, 7);
            writeFATSVEventMessage(mm, "es_target", mm->ME, 7);
        }
        break;
    }
}

typedef enum {
    TISB_IDENT = 1,
    TISB_SQUAWK = 2,
    TISB_ALTITUDE = 4,
    TISB_ALTITUDE_GNSS = 8,
    TISB_SPEED = 16,
    TISB_SPEED_IAS = 32,
    TISB_SPEED_TAS = 64,
    TISB_POSITION = 128,
    TISB_HEADING = 256,
    TISB_HEADING_MAGNETIC = 512,
    TISB_AIRGROUND = 1024,
    TISB_CATEGORY = 2048
} tisb_flags;

static inline unsigned unsigned_difference(unsigned v1, unsigned v2)
{
    return (v1 > v2) ? (v1 - v2) : (v2 - v1);
}

static inline unsigned heading_difference(unsigned h1, unsigned h2)
{
    unsigned d = unsigned_difference(h1, h2);
    return (d < 180) ? d : (360 - d);
}

static void writeFATSV()
{
    struct aircraft *a;
    uint64_t now;
    static uint64_t next_update;

    if (!Modes.fatsv_out.service || !Modes.fatsv_out.service->connections) {
        return; // not enabled or no active connections
    }

    now = mstime();
    if (now < next_update) {
        return;
    }

    // scan once a second at most
    next_update = now + 1000;

    for (a = Modes.aircrafts; a; a = a->next) {
        int altValid = 0;
        int altGNSSValid = 0;
        int positionValid = 0;
        int speedValid = 0;
        int speedIASValid = 0;
        int speedTASValid = 0;
        int headingValid = 0;
        int headingMagValid = 0;
        int airgroundValid = 0;
        int categoryValid = 0;

        uint64_t minAge;

        int useful = 0;
        int changed = 0;
        tisb_flags tisb = 0;

        char *p, *end;

        if (a->messages < 2)  // basic filter for bad decodes
            continue;

        // don't emit if it hasn't updated since last time
        if (a->seen < a->fatsv_last_emitted) {
            continue;
        }

        altValid = trackDataValidEx(&a->altitude_valid, now, 15000, SOURCE_MODE_S); // for non-ADS-B transponders, DF0/4/16/20 are the only sources of altitude data
        altGNSSValid = trackDataValidEx(&a->altitude_gnss_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        airgroundValid = trackDataValidEx(&a->airground_valid, now, 15000, SOURCE_MODE_S_CHECKED); // for non-ADS-B transponders, only trust DF11 CA field
        positionValid = trackDataValidEx(&a->position_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        headingValid = trackDataValidEx(&a->heading_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        headingMagValid = trackDataValidEx(&a->heading_magnetic_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        speedValid = trackDataValidEx(&a->speed_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        speedIASValid = trackDataValidEx(&a->speed_ias_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        speedTASValid = trackDataValidEx(&a->speed_tas_valid, now, 15000, SOURCE_MODE_S_CHECKED);
        categoryValid = trackDataValidEx(&a->category_valid, now, 15000, SOURCE_MODE_S_CHECKED);

        // If we are definitely on the ground, suppress any unreliable altitude info.
        // When on the ground, ADS-B transponders don't emit an ADS-B message that includes
        // altitude, so a corrupted Mode S altitude response from some other in-the-air AC
        // might be taken as the "best available altitude" and produce e.g. "airGround G+ alt 31000".
        if (airgroundValid && a->airground == AG_GROUND && a->altitude_valid.source < SOURCE_MODE_S_CHECKED)
            altValid = 0;

        // if it hasn't changed altitude, heading, or speed much,
        // don't update so often
        changed = 0;
        if (altValid && abs(a->altitude - a->fatsv_emitted_altitude) >= 50) {
            changed = 1;
        }
        if (altGNSSValid && abs(a->altitude_gnss - a->fatsv_emitted_altitude_gnss) >= 50) {
            changed = 1;
        }
        if (headingValid && heading_difference(a->heading, a->fatsv_emitted_heading) >= 2) {
            changed = 1;
        }
        if (headingMagValid && heading_difference(a->heading_magnetic, a->fatsv_emitted_heading_magnetic) >= 2) {
            changed = 1;
        }
        if (speedValid && unsigned_difference(a->speed, a->fatsv_emitted_speed) >= 25) {
            changed = 1;
        }
        if (speedIASValid && unsigned_difference(a->speed_ias, a->fatsv_emitted_speed_ias) >= 25) {
            changed = 1;
        }
        if (speedTASValid && unsigned_difference(a->speed_tas, a->fatsv_emitted_speed_tas) >= 25) {
            changed = 1;
        }

        if (airgroundValid && ((a->airground == AG_AIRBORNE && a->fatsv_emitted_airground == AG_GROUND) ||
                               (a->airground == AG_GROUND && a->fatsv_emitted_airground == AG_AIRBORNE))) {
            // Air-ground transition, handle it immediately.
            minAge = 0;
        } else if (!positionValid) {
            // don't send mode S very often
            minAge = 30000;
        } else if ((airgroundValid && a->airground == AG_GROUND) ||
                   (altValid && a->altitude < 500 && (!speedValid || a->speed < 200)) ||
                   (speedValid && a->speed < 100 && (!altValid || a->altitude < 1000))) {
            // we are probably on the ground, increase the update rate
            minAge = 1000;
        } else if (!altValid || a->altitude < 10000) {
            // Below 10000 feet, emit up to every 5s when changing, 10s otherwise
            minAge = (changed ? 5000 : 10000);
        } else {
            // Above 10000 feet, emit up to every 10s when changing, 30s otherwise
            minAge = (changed ? 10000 : 30000);
        }

        if ((now - a->fatsv_last_emitted) < minAge)
            continue;

        p = prepareWrite(&Modes.fatsv_out, TSV_MAX_PACKET_SIZE);
        if (!p)
            return;

        end = p + TSV_MAX_PACKET_SIZE;
#       define bufsize(_p,_e) ((_p) >= (_e) ? (size_t)0 : (size_t)((_e) - (_p)))

        p += snprintf(p, bufsize(p, end), "clock\t%" PRIu64, (uint64_t)(a->seen / 1000));

        if (a->addr & MODES_NON_ICAO_ADDRESS) {
            p += snprintf(p, bufsize(p, end), "\totherid\t%06X", a->addr & 0xFFFFFF);
        } else {
            p += snprintf(p, bufsize(p, end), "\thexid\t%06X", a->addr);
        }

        if (a->addrtype != ADDR_ADSB_ICAO) {
            p += snprintf(p, bufsize(p, end), "\taddrtype\t%s", addrtype_short_string(a->addrtype));
        }

        if (trackDataValidEx(&a->callsign_valid, now, 15000, SOURCE_MODE_S_CHECKED) && strcmp(a->callsign, "        ") != 0 && a->callsign_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tident\t%s", a->callsign);
            switch (a->callsign_valid.source) {
            case SOURCE_MODE_S:
                p += snprintf(p, bufsize(p,end), "\tiSource\tmodes");
                break;
            case SOURCE_ADSB:
                p += snprintf(p, bufsize(p,end), "\tiSource\tadsb");
                break;
            case SOURCE_TISB:
                p += snprintf(p, bufsize(p,end), "\tiSource\ttisb");
                break;
            default:
                p += snprintf(p, bufsize(p,end), "\tiSource\tunknown");
                break;
            }

            useful = 1;
            tisb |= (a->callsign_valid.source == SOURCE_TISB) ? TISB_IDENT : 0;
        }

        if (trackDataValidEx(&a->squawk_valid, now, 15000, SOURCE_MODE_S) && a->squawk_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tsquawk\t%04x", a->squawk);
            useful = 1;
            tisb |= (a->squawk_valid.source == SOURCE_TISB) ? TISB_SQUAWK : 0;
        }

        // only emit alt, speed, latlon, track if they have been received since the last time
        // and are not stale

        if (altValid && a->altitude_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\talt\t%d", a->altitude);
            a->fatsv_emitted_altitude = a->altitude;
            useful = 1;
            tisb |= (a->altitude_valid.source == SOURCE_TISB) ? TISB_ALTITUDE : 0;
        }

        if (altGNSSValid && a->altitude_gnss_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\talt_gnss\t%d", a->altitude_gnss);
            a->fatsv_emitted_altitude_gnss = a->altitude_gnss;
            useful = 1;
            tisb |= (a->altitude_gnss_valid.source == SOURCE_TISB) ? TISB_ALTITUDE_GNSS : 0;
        }

        if (speedValid && a->speed_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tspeed\t%d", a->speed);
            a->fatsv_emitted_speed = a->speed;
            useful = 1;
            tisb |= (a->speed_valid.source == SOURCE_TISB) ? TISB_SPEED : 0;
        }

        if (speedIASValid && a->speed_ias_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tspeed_ias\t%d", a->speed_ias);
            a->fatsv_emitted_speed_ias = a->speed_ias;
            useful = 1;
            tisb |= (a->speed_ias_valid.source == SOURCE_TISB) ? TISB_SPEED_IAS : 0;
        }

        if (speedTASValid && a->speed_tas_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tspeed_tas\t%d", a->speed_tas);
            a->fatsv_emitted_speed_tas = a->speed_tas;
            useful = 1;
            tisb |= (a->speed_tas_valid.source == SOURCE_TISB) ? TISB_SPEED_TAS : 0;
        }

        if (positionValid && a->position_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tlat\t%.5f\tlon\t%.5f", a->lat, a->lon);
            useful = 1;
            tisb |= (a->position_valid.source == SOURCE_TISB) ? TISB_POSITION : 0;
        }

        if (headingValid && a->heading_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\theading\t%d", a->heading);
            a->fatsv_emitted_heading = a->heading;
            useful = 1;
            tisb |= (a->heading_valid.source == SOURCE_TISB) ? TISB_HEADING : 0;
        }

        if (headingMagValid && a->heading_magnetic_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\theading_magnetic\t%d", a->heading);
            a->fatsv_emitted_heading_magnetic = a->heading_magnetic;
            useful = 1;
            tisb |= (a->heading_magnetic_valid.source == SOURCE_TISB) ? TISB_HEADING_MAGNETIC : 0;
        }

        if (airgroundValid && (a->airground == AG_GROUND || a->airground == AG_AIRBORNE) && a->airground_valid.updated > a->fatsv_last_emitted) {
            p += snprintf(p, bufsize(p,end), "\tairGround\t%s", a->airground == AG_GROUND ? "G+" : "A+");
            a->fatsv_emitted_airground = a->airground;
            useful = 1;
            tisb |= (a->airground_valid.source == SOURCE_TISB) ? TISB_AIRGROUND : 0;
        }

        if (categoryValid && (a->category & 0xF0) != 0xA0 && a->category_valid.updated > a->fatsv_last_emitted) {
            // interesting category, not a regular aircraft
            p += snprintf(p, bufsize(p,end), "\tcategory\t%02X", a->category);
            useful = 1;
            tisb |= (a->category_valid.source == SOURCE_TISB) ? TISB_CATEGORY : 0;
        }

        // if we didn't get anything interesting, bail out.
        // We don't need to do anything special to unwind prepareWrite().
        if (!useful) {
            continue;
        }

        if (tisb != 0) {
            p += snprintf(p, bufsize(p,end), "\ttisb\t%d", (int)tisb);
        }

        p += snprintf(p, bufsize(p,end), "\n");

        if (p <= end)
            completeWrite(&Modes.fatsv_out, p);
        else
            fprintf(stderr, "fatsv: output too large (max %d, overran by %d)\n", TSV_MAX_PACKET_SIZE, (int) (p - end));
#       undef bufsize

        a->fatsv_last_emitted = now;
    }
}

//
// Perform periodic network work
//
void modesNetPeriodicWork(void) {
    struct client *c, **prev;
    struct net_service *s;
    uint64_t now = mstime();
    int need_flush = 0;

    // Accept new connections
    modesAcceptClients();

    // Read from clients
    for (c = Modes.clients; c; c = c->next) {
        if (!c->service)
            continue;
        if (c->service->read_handler)
            modesReadFromClient(c);
    }

    // Generate FATSV output
    writeFATSV();

    // If we have generated no messages for a while, send
    // a heartbeat
    if (Modes.net_heartbeat_interval) {
        for (s = Modes.services; s; s = s->next) {
            if (s->writer &&
                s->connections &&
                s->writer->send_heartbeat &&
                (s->writer->lastWrite + Modes.net_heartbeat_interval) <= now) {
                s->writer->send_heartbeat(s);
            }
        }
    }

    // If we have data that has been waiting to be written for a while,
    // write it now.
    for (s = Modes.services; s; s = s->next) {
        if (s->writer &&
            s->writer->dataUsed &&
            (need_flush || (s->writer->lastWrite + Modes.net_output_flush_interval) <= now)) {
            flushWrites(s->writer);
        }
    }

    // Unlink and free closed clients
    for (prev = &Modes.clients, c = *prev; c; c = *prev) {
        if (c->fd == -1) {
            // Recently closed, prune from list
            *prev = c->next;
            free(c);
        } else {
            prev = &c->next;
        }
    }
}

//
// =============================== Network IO ===========================
//
