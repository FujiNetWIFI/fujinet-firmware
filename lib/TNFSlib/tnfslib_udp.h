#ifndef _TNFSLIB_UDP_H
#define _TNFSLIB_UDP_H

#include "tnfslib.h"
#include "fnUDP.h"

bool _tnfs_udp_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size);
int _tnfs_udp_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt);

bool _tnfs_udp_do_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size);

#endif
