/**
  \file   atlas_socket.h
  \author Aleksey Lekhtin
  \date   2024/02/01
  \brief  Socket operations
*/

#ifndef ATLAS_SOCKET__H_
#define ATLAS_SOCKET__H_

#include <stdbool.h>

bool atlas_open_udp_socket();
void atlas_socket_close();
bool atlas_socket_send();

#endif // ATLAS_SOCKET__H_
