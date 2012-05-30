/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA.
 */

#include "common.h"

#ifdef WIN32
    #include <winsock2.h>
#else
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#include "net.h"

#include <unistd.h>

#include <event.h>
/* #include <event2/event.h> */
#include <glib.h>
#include <errno.h>
#include <string.h>


#include "session.h"
#include "packet-io.h"

#include "log.h"

#define IO_TIMEOUT_SECS 8

/* The watermark of the underlying evbuffer. When there are more data than
 * this value is remained in evbuffer, the read event will be removed.
 * So, it must be greater than the max length of a single ccnet packet.
 */
#define CCNET_RDBUF 100000

static void
didWriteWrapper (struct bufferevent *e, void *user_data)
{
    CcnetPacketIO * c = (CcnetPacketIO *) user_data;
    if (c->didWrite)
        c->didWrite (e, c->user_data);
}

static void
canReadWrapper (struct bufferevent *e, void *user_data)
{
    CcnetPacketIO *c = user_data;
    ccnet_packet *packet;
    int len;

    g_assert (sizeof(ccnet_header) == CCNET_PACKET_LENGTH_HEADER);

    c->handling = 1;

    /* We have set up the low watermark. The following must be true. */
    g_assert (EVBUFFER_LENGTH (e->input) >= CCNET_PACKET_LENGTH_HEADER);

    if (c->canRead == NULL) {
        c->handling = 0;
        return;
    }
    
    while (1) {
        packet = (ccnet_packet *) EVBUFFER_DATA (e->input);

        len = ntohs (packet->header.length);
        if (EVBUFFER_LENGTH (e->input) - CCNET_PACKET_LENGTH_HEADER < len)
            break;                 /* wait for more data */

        /* byte order, from network to host */
        packet->header.length = len;
        packet->header.id = ntohl (packet->header.id);
        c->canRead (packet, c->user_data);

        /* PacketIO may be scheduled to free in the previous call */
        if (c->schedule_free) {
            c->schedule_free = 0;
            c->handling = 0;
            ccnet_packet_io_free (c);
            break;
        }

        evbuffer_drain (e->input, len + CCNET_PACKET_LENGTH_HEADER);

        if(EVBUFFER_LENGTH(e->input) >= CCNET_PACKET_LENGTH_HEADER)
            continue;
        
        break;
    }

    c->handling = 0;
}

static void
gotErrorWrapper (struct bufferevent *e, short what, void *user_data)
{
    CcnetPacketIO *c = user_data;
    if (c->gotError)
        c->gotError (e, what, c->user_data);
}


void bufferevent_setwatermark(struct bufferevent *, short, size_t, size_t);

static CcnetPacketIO*
ccnet_packet_io_new (struct CcnetSession     *session,
                     const struct sockaddr_storage *addr,
                     int is_incoming,
                     evutil_socket_t socket)
{
    CcnetPacketIO *io;

    io = g_new0 (CcnetPacketIO, 1);
    
    io->session = session;
    io->socket = socket;
    io->is_incoming = is_incoming;
    if (addr) {
        io->addr = g_malloc(sizeof(struct sockaddr_storage));
        memcpy (io->addr, addr, sizeof(struct sockaddr_storage));
    }

    io->bufev = bufferevent_socket_new (NULL, io->socket, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb (io->bufev, canReadWrapper,
                       didWriteWrapper, gotErrorWrapper, io);
    bufferevent_enable (io->bufev, EV_READ | EV_WRITE);
    bufferevent_setwatermark (io->bufev, EV_READ, CCNET_PACKET_LENGTH_HEADER, 
                              CCNET_RDBUF);

    /* do not BEV_OPT_CLOSE_ON_FREE, since ccnet_packet_io_free() will
     * handle it */
    /* io->bufev = bufferevent_socket_new (NULL, io->socket, 0); */
    /* bufferevent_setcb (io->bufev, canReadWrapper, didWriteWrapper, */
    /*                    gotErrorWrapper, io); */
    /* io->bufev = bufferevent_new (io->socket, */
    /*                             canReadWrapper, */
    /*                             didWriteWrapper, */
    /*                             gotErrorWrapper, */
    /*                             io); */
    /* bufferevent_setwatermark (io->bufev, EV_READ, CCNET_PACKET_LENGTH_HEADER,  */
    /*                           CCNET_RDBUF); */
    /* bufferevent_enable (io->bufev, EV_READ | EV_WRITE); */

    return io;
}

CcnetPacketIO*
ccnet_packet_io_new_incoming (CcnetSession             *session,
                              struct sockaddr_storage  *addr,
                              evutil_socket_t socket)
{
    g_assert (session);
    g_assert (socket >= 0);

    return ccnet_packet_io_new (session, addr, TRUE, socket);
}


CcnetPacketIO*
ccnet_packet_io_new_outgoing (CcnetSession *session,
                              const char   *addr_str, 
                              uint16_t      port)
{
    struct sockaddr_storage addr;
    evutil_socket_t socket;

    if (sock_pton(addr_str, port, &addr) < 0) {
        ccnet_warning ("wrong addresss format %s\n", addr_str);
        return NULL;
    }

    socket = ccnet_net_open_tcp ((struct sockaddr *)&addr, TRUE);
    if (socket < 0)
        ccnet_warning ("opening tcp connection fails: %s", strerror(errno));
      
    return socket < 0
        ? NULL
        : ccnet_packet_io_new (session, &addr, FALSE, socket);
}


void
ccnet_packet_io_free (CcnetPacketIO *io)
{
    if (io) {
        if (io->handling) {
            io->schedule_free = 1;
            return;
        }

        if (io->addr)
            g_free (io->addr);
            
        io->canRead = NULL;
        io->didWrite = NULL;
        io->gotError = NULL;

        bufferevent_free (io->bufev);
        /* fprintf (stderr, "close fd %d\n", io->socket); */
        /* close (io->socket); */
        g_free (io);
    }
}

CcnetSession*
ccnet_packet_io_get_session (CcnetPacketIO *io)
{
    g_assert (io);
    g_assert (io->session);

    return io->session;
}


void
ccnet_packet_io_try_read (CcnetPacketIO *io)
{
    if(EVBUFFER_LENGTH(io->bufev->input))
        canReadWrapper (io->bufev, io);
}

void 
ccnet_packet_io_set_iofuncs (CcnetPacketIO      *io,
                             ccnet_can_read_cb  readcb,
                             ccnet_did_write_cb writecb,
                             ccnet_net_error_cb errcb,
                             void              *user_data)
{
    io->canRead = readcb;
    io->didWrite = writecb;
    io->gotError = errcb;
    io->user_data = user_data;
}

int
ccnet_packet_io_is_incoming (const CcnetPacketIO *c)
{
    return c->is_incoming ? 1 : 0;
}


void
ccnet_packet_io_set_timeout_secs (CcnetPacketIO *io, int secs)
{
    io->timeout = secs;
    bufferevent_settimeout (io->bufev, io->timeout, io->timeout);
    if (secs == 0)    /* have to remove the original events */
        bufferevent_disable (io->bufev, EV_READ | EV_WRITE);
    bufferevent_enable (io->bufev, EV_READ | EV_WRITE);

    /* struct timeval tv; */
    /* tv.tv_sec = secs; */
    /* tv.tv_usec = 0; */

    /* if (secs != 0) */
    /*     bufferevent_set_timeouts (io->bufev, &tv, NULL); */
    /* else */
    /*     bufferevent_set_timeouts (io->bufev, NULL, NULL); */
}

void
ccnet_packet_io_write_packet (CcnetPacketIO *io, ccnet_packet *packet)
{
    int len;

    len = packet->header.length + CCNET_PACKET_LENGTH_HEADER;
    packet->header.length = htons (packet->header.length);
    packet->header.id = htonl (packet->header.id);
    bufferevent_write (io->bufev, packet, len);
}