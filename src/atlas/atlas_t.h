/**
  \file   atlas_t.h
  \author Aleksey Lekhtin
  \date   2024/02/01
  \brief  Структуры данных для работы с Atlas
*/

#ifndef ATLAS_T__H_
#define ATLAS_T__H_

#include "atlas_packet.h"
#include "cfg_t.h"
#include "socket_t.h"

typedef struct atlas_t
{
    char serno[32];
    host_t host[3];
    size_t host_total;

    const int npnts500;     // 500
    atlas_packet_t packet;

    uint32_t samplenum;
    uint32_t timerange;
    int fd_stat;            // статистика по данных
    uint32_t send_ok;
    uint32_t send_error;
} atlas_t;

#endif //ATLAS_T__H_
