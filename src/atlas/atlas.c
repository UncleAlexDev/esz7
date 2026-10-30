/**
  \file   atlas.c
  \author Aleksey Lekhtin
  \date   Feb 26, 2025
  \brief  Работа с Atlas
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "atlas.h"
#include "atlas_socket.h"
#include "cfg.h"
#include "dbg.h"
#include "log.h"
#include "core.h"
#include "utils.h"
#include "wits.h"

/*---------------------------------------------------------*/
static atlas_t m_atlas = {
        .host = {},
        .host_total = sizeof(m_atlas.host) / sizeof(m_atlas.host[0]),
        .npnts500 = 500,
        .packet = {},
        .packet.nbytes = 0,
        .packet.bytes = NULL,
        .fd_stat = -1,
};

/*---------------------------------------------------------*/
inline atlas_t *atlas()
{
    return &m_atlas;
}

/*---------------------------------------------------------*/
bool atlas_run()
{
    for (size_t i = 0; i < m_atlas.host_total; i++) {
        memcpy(&m_atlas.host[i], &cfg()->atlas_host[i], sizeof(m_atlas.host[i]));
        m_atlas.host[i].sock1.fd = INVALID_SOCKET;
        m_atlas.host[i].sock2.fd = INVALID_SOCKET;
    }

    strncpy(m_atlas.serno, cfg()->atlas_serno, sizeof(m_atlas.serno) - 1);

    atlas_packet_init();
    if (!atlas_open_udp_socket()) {
        return FAIL;
    }

    if (atlas()->fd_stat == -1) {
        char fname[256 + 32];
        snprintf(fname, sizeof(fname), "%s/telemetry.txt", cfg()->tempdir);
        atlas()->fd_stat = open(fname, O_CREAT | O_TRUNC | O_WRONLY, 0640);
        if (atlas()->fd_stat == -1) {
            LOG(INFO, "can't create telemetry file '%s'", fname);
            DBGF(0, "WARNING: can't create telemetry file '%s' - SKIP - (%d) %s", fname, errno, strerror(errno));
        }
    }

    DBG(1, "started");
    return OK;
}

/*---------------------------------------------------------*/
void atlas_stop()
{
    DBG(4);
    atlas_socket_close();
    atlas_packet_free();
    if (atlas()->fd_stat == -1) {
        close(atlas()->fd_stat);
        atlas()->fd_stat = -1;
    }
}

//---------------------------------------------------------
inline void atlas_data_send()
{
    atlas_make_binary_packet();

    DBGF(2, ">>> Atlas packet #%-2d - %u ms (size: %d)", atlas()->packet.rtp.fixed_part.seqnum,
         atlas()->packet.rtp.fixed_part.timerange,
         atlas()->packet.nbytes);

    atlas_socket_send();
}

//---------------------------------------------------------
inline void atlas_write_telemetry()
{
    if (atlas()->fd_stat == -1) {
        return;
    }
    static char buf[1024];
    snprintf(buf, sizeof(buf),
             "enc: %d %d\n"
             "adcref: %0.04f %0.20f\n"
             "ch1avrg: %08d %08X %11.8f\n"
             "ch2avrg: %08d %08X %11.8f\n"
             "ch3avrg: %08d %08X %11.8f\n"
             "ch4avrg: %08d %08X %11.8f\n"
             "wits: %d %d\n"
             "sentok: %d %d\n",
             core()->data.encoder1,
             core()->data.encoder2,
             cfg()->adc_ref,
             cfg()->adc_scale,
             core()->data.values[0].avrg, core()->data.values[0].avrg, core()->data.values[0].avrg * cfg()->adc_scale,
             core()->data.values[1].avrg, core()->data.values[1].avrg, core()->data.values[1].avrg * cfg()->adc_scale,
             core()->data.values[2].avrg, core()->data.values[2].avrg, core()->data.values[2].avrg * cfg()->adc_scale,
             core()->data.values[3].avrg, core()->data.values[3].avrg, core()->data.values[3].avrg * cfg()->adc_scale,
             wits()->send_ok,
             wits()->send_error,
             atlas()->send_ok,
             atlas()->send_error);
    lseek(atlas()->fd_stat, 0, SEEK_SET);
    write(atlas()->fd_stat, buf, strlen(buf));
    DBGF(5, "stat: %s", replace(buf, '\n', ' '));

    for (size_t i = 0; i < atlas()->host_total; ++i) {
        if (atlas()->host[i].name[0] != 0) {
            snprintf(buf, sizeof(buf),
                     "sendto%u: %s;%u;%s;%u;%s\n",
                     i+1,
                     atlas()->host[i].name,
                     atlas()->host[i].port1,
                     atlas()->host[i].status1,
                     atlas()->host[i].port2,
                     atlas()->host[i].status2);
        } else {
            snprintf(buf, sizeof(buf), "sendto%u: ;;;;\n", i+1);
        }
        write(atlas()->fd_stat, buf, strlen(buf));
        DBGF(5, "stat: %s", replace(buf, '\n', ' '));
    }
    write(atlas()->fd_stat, "\n", 1);
}
