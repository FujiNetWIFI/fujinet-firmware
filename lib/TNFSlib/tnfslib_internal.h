#ifndef _TNFSLIB_INTERNAL_H
#define _TNFSLIB_INTERNAL_H

// Declarations shared by the tnfslib*.cpp files; not part of the public API.

#include "tnfslib.h"
#include "fnUDP.h"

typedef enum
{
    SUCCESS,
    FAILED,
    RESET,
} _tnfs_send_recv_result;

typedef enum
{
    RESP_VALID,
    RESP_INVALID,
    RESP_TRY_AGAIN,
    SESSION_RECOVERED,
    NO_RESP,
} _tnfs_recv_result;

bool _tnfs_transaction(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t datalen);
bool _tnfs_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size);
int _tnfs_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt);
bool _tnfs_tcp_send(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size);
int _tnfs_tcp_recv(tnfsMountInfo *m_info, tnfsPacket &pkt);
_tnfs_send_recv_result _tnfs_send_recv(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt);
_tnfs_recv_result _tnfs_recv_and_validate(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt);
uint8_t _tnfs_session_recovery(tnfsMountInfo *m_info, uint8_t command);

int _tnfs_adjust_with_full_path(tnfsMountInfo *m_info, char *buffer, const char *source, int bufflen);

void _tnfs_debug_packet(const tnfsPacket &pkt, unsigned short len, bool isResponse = false);

const char *_tnfs_command_string(int command);
const char *_tnfs_result_code_string(int resultcode);

#endif // _TNFSLIB_INTERNAL_H
