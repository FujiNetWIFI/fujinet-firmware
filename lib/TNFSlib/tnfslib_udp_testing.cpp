#include "tnfslib_udp.h"
#include "tnfslibMountInfo.h"
#include "../../include/debug.h"

#ifdef TNFS_UDP_SIMULATE_POOR_CONNECTION
bool _tnfs_udp_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
#ifdef TNFS_UDP_SIMULATE_SEND_LOSS
    if (rand() < TNFS_UDP_SIMULATE_SEND_LOSS_PROB * RAND_MAX) {
        Debug_println("TNFS_UDP_SIMULATE: send loss");
        return true;
    }
#endif
#ifdef TNFS_UDP_SIMULATE_SEND_TWICE
    if (rand() < TNFS_UDP_SIMULATE_SEND_TWICE_PROB * RAND_MAX) {
        Debug_println("TNFS_UDP_SIMULATE: send twice");
        _tnfs_udp_do_send(udp, m_info, pkt, payload_size);
    }
#endif
    return _tnfs_udp_do_send(udp, m_info, pkt, payload_size);
}

int _tnfs_udp_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt)
{
#ifdef TNFS_UDP_SIMULATE_RECV_TWICE
    if (m_info->last_packet_len >= 0 && rand() < TNFS_UDP_SIMULATE_RECV_TWICE_PROB * RAND_MAX) {
        Debug_println("TNFS_UDP_SIMULATE: recv twice");
        memcpy(pkt.rawData, m_info->last_packet, sizeof(pkt.rawData));
        int len = m_info->last_packet_len;
        m_info->last_packet_len = -1;
        return len;
    }
#endif
    if (!udp->parsePacket())
    {
        return -1;
    }
#ifdef TNFS_UDP_SIMULATE_RECV_LOSS
    if (rand() < TNFS_UDP_SIMULATE_RECV_LOSS_PROB * RAND_MAX) {
        Debug_println("TNFS_UDP_SIMULATE: recv loss");
        tnfsPacket lostPkt;
        udp->read(lostPkt.rawData, sizeof(pkt.rawData));
        return -1;
    }
#endif
    int len = udp->read(pkt.rawData, sizeof(pkt.rawData));
#ifdef TNFS_UDP_SIMULATE_RECV_TWICE
    memcpy(m_info->last_packet, pkt.rawData, sizeof(m_info->last_packet));
    m_info->last_packet_len = len;
#endif
    return len;
}
#endif
