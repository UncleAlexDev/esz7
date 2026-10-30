/**
  \file   atlas_socket.c
  \author Aleksey Lekhtin
  \date   Apr 01, 2024
  \brief  Socket abstract operation
*/

#include <stdio.h>

#include "atlas.h"
#include "atlas_socket.h"
#include "dbg.h"
#include "socket.h"
#include "log.h"

//---------------------------------------------------------
bool atlas_open_udp_socket()
{
    bool ok;
    for (size_t i = 0; i < atlas()->host_total; i++) {
        host_t *h = &atlas()->host[i];

        if (h->name[0] != 0) {
            if (h->port1 > 0) {
                DBGF(1, "\ncreate UDP socket for %s:%d", h->name, h->port1);
                h->sock1.ip = h->name;
                h->sock1.port = h->port1;
                snprintf(h->sock1.name, sizeof(h->sock1.name), "atlas-%d-%d", i + 1, h->port1);
                ok = socket_open(&h->sock1, SOCK_DGRAM, IPPROTO_UDP);
                if (ok) {
                    LOG(INFO, "Atlas started for %s:%d", h->name, h->port1);
                }
                // return FAIL;
            }

            if (h->port2 > 0) {
                DBGF(1, "create UPD socket for %s:%d", h->name, h->port2);
                h->sock2.ip = h->name;
                h->sock2.port = h->port2;
                snprintf(h->sock2.name, sizeof(h->sock2.name), "atlas-%d-%d", i + 1, h->port2);
                ok = socket_open(&h->sock2, SOCK_DGRAM, IPPROTO_UDP);
                if (ok) {
                    LOG(INFO, "Atlas started for %s:%d", h->name, h->port2);
                }
                // return FAIL;
            }
        }
    }
    return OK;
}

/*---------------------------------------------------------*/
void atlas_socket_close()
{
    DBG(4);
    for (size_t i = 0; i < atlas()->host_total; i++) {
        host_t *h = &atlas()->host[i];
        socket_close_fd(&h->sock1.fd);
        socket_close_fd(&h->sock2.fd);
    }
}

/*---------------------------------------------------------*/
bool atlas_socket_send()
{
    const char *bytes = atlas()->packet.bytes;
    const int nbytes = atlas()->packet.nbytes;

    static bool sent_ok;
    sent_ok = YES;
    for (size_t i = 0; i < atlas()->host_total; i++) {
        host_t *h = &atlas()->host[i];
        if (h->name[0] == '\0') {
            continue;
        }
        const size_t status_msg_size = sizeof(atlas()->host[0].status1) - 1;
        atlas()->host[i].status1[0] = 0;
        atlas()->host[i].status2[0] = 0;

        if (h->sock1.fd != INVALID_SOCKET) {
            socket_t *sock = &h->sock1;
            const int ret = socket_send(sock, bytes, nbytes);
            if (ret == -1) {
                sent_ok = NO;
                atlas()->send_error++;
                const char *errstr = socket_strerror(sock->err);
                strncpy(atlas()->host[i].status1, errstr, status_msg_size);
                LOG(CRITICAL, "%s send(fd:%02d, cli_fd:%02d, '%s') failed - %s", sock->name, sock->fd, sock->fd,
                    sock->ip, errstr);
                DBGF(0, "%s send(fd:%02d, cli_fd:%02d, '%s') failed - %s", sock->name, sock->fd, sock->fd, sock->ip,
                     errstr);
            } else {
                atlas()->send_ok++;
                DBGF(2, "%s:%d", inet_ntoa(sock->sockaddrin.sin_addr), sock->port);
            }
        }

        if (h->sock2.fd != INVALID_SOCKET) {
            socket_t *sock = &h->sock2;
            const int ret = socket_send(sock, bytes, nbytes);
            if (ret == -1) {
                sent_ok = NO;
                atlas()->send_error++;
                const char *errstr = socket_strerror(sock->err);
                strncpy(atlas()->host[i].status2, errstr, status_msg_size);
                LOG(CRITICAL, "%s send(fd:%02d, cli_fd:%02d, '%s') failed - %s", sock->name, sock->fd, sock->fd,
                    sock->ip, errstr);
                DBGF(0, "%s send(fd:%02d, cli_fd:%02d, '%s') failed - %s", sock->name, sock->fd, sock->fd, sock->ip,
                     errstr);
            } else {
                atlas()->send_ok++;
                DBGF(2, "%s:%d", inet_ntoa(sock->sockaddrin.sin_addr), sock->port);
            }
        }
    }
    return sent_ok;
}
