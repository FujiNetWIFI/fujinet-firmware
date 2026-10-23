
#include "tnfslib.h"

#include <cstdlib>
#include <sys/stat.h>
#include <errno.h>
#include <mutex>
#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "bus.h"
#include "fnUDP.h"
#include "fnTcpClient.h"
#include "tnfslib_udp.h"

#include "utils.h"

#include "tnfslib_internal.h"

using namespace std;
// ------------------------------------------------
// INTERNAL UTILITY FUNCTIONS
// ------------------------------------------------

/*
    Send the packet, using UDP or TCP, depending on the m_info.
    Reports whether the operation was successful.

    If TCP is used and there's no connection, try to connect first.
*/
bool _tnfs_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    if (m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        bool success = _tnfs_tcp_send(m_info, pkt, payload_size);
        if (!success)
        {
            Debug_println("Can't connect to the TCP server; falling back to UDP.");
            m_info->protocol = TNFS_PROTOCOL_UDP;
            return _tnfs_udp_send(udp, m_info, pkt, payload_size);
        }
        return success;
    }
    else if (m_info->protocol == TNFS_PROTOCOL_TCP)
    {
        return _tnfs_tcp_send(m_info, pkt, payload_size);
    }
    else
    {
        return _tnfs_udp_send(udp, m_info, pkt, payload_size);
    }
}

bool _tnfs_tcp_send(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    fnTcpClient *tcp = &m_info->tcp_client;
    if (!tcp->connected())
    {
        bool success = false;
        if (m_info->host_ip != IPADDR_NONE)
            success = tcp->connect(m_info->host_ip, m_info->port, TNFS_TIMEOUT);
        else
        {
            success = tcp->connect(m_info->hostname, m_info->port, TNFS_TIMEOUT);
            m_info->host_ip = tcp->remoteIP();
        }
        if (!success)
        {
            Debug_println("Can't connect to the TCP server");
            return false;
        }
    }
    int l = tcp->write(pkt.rawData, payload_size + TNFS_HEADER_SIZE);
    return l == payload_size + TNFS_HEADER_SIZE;
}

#ifndef TNFS_UDP_SIMULATE_POOR_CONNECTION
bool _tnfs_udp_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    return _tnfs_udp_do_send(udp, m_info, pkt, payload_size);
}
#endif

bool _tnfs_udp_do_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    bool sent;
    // Use the IP address if we have it
    if (m_info->host_ip != IPADDR_NONE)
        sent = udp->beginPacket(m_info->host_ip, m_info->port);
    else
    {
        sent = udp->beginPacket(m_info->hostname, m_info->port);
        m_info->host_ip = udp->remoteIP();
    }

    if (sent)
    {
        udp->write(pkt.rawData, payload_size + TNFS_HEADER_SIZE); // Add the data payload along with 4 bytes of TNFS header
        sent = udp->endPacket();
    }
    return sent;
}

/*
    Receive the packet, using UDP or TCP, depending on the m_info.
    Return the number of received bytes or an negative value if the
    packet is not available or an error occurred.
*/
int _tnfs_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt)
{
    if (m_info->protocol == TNFS_PROTOCOL_TCP || m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        return _tnfs_tcp_recv(m_info, pkt);
    }
    else
    {
        return _tnfs_udp_recv(udp, m_info, pkt);
    }
}

int _tnfs_tcp_recv(tnfsMountInfo *m_info, tnfsPacket &pkt)
{
    fnTcpClient *tcp = &m_info->tcp_client;
    if (!tcp->connected())
    {
        return -1;
    }
    if (!tcp->available())
    {
        return -1;
    }
    return tcp->read(pkt.rawData, sizeof(pkt.rawData));
}

#ifndef TNFS_UDP_SIMULATE_POOR_CONNECTION
int _tnfs_udp_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt)
{
    if (!udp->parsePacket())
    {
        return -1;
    }
    int len = udp->read(pkt.rawData, sizeof(pkt.rawData));
    return len;
}
#endif

/*
  Send constructed TNFS packet and check for reply
  The send/receive loop will be attempted tnfsPacket.max_retries times (default: TNFS_RETRIES)
  Each retry attempt is limited to tnfsPacket.timeout_ms (default: TNFS_TIMEOUT)

  Only the command (tnfsPacket.command) and payload contents need to be set on the packet.
  Current session ID will be copied from tnfsMountInfo and retryCount is always reset to zero.

  If successful, server's response code will be the first byte of of tnfsPacket.data

  returns - true if response packet was received
            false if no response received during retries/timeout period
 */
bool _tnfs_transaction(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    std::lock_guard<std::recursive_mutex> lock(m_info->transaction_mutex);

    fnUDP udp;

    // Set our session ID
    tnfsPacket reqPkt = pkt;
    reqPkt.session_idl = TNFS_LOBYTE_FROM_UINT16(m_info->session);
    reqPkt.session_idh = TNFS_HIBYTE_FROM_UINT16(m_info->session);

    // Set sequence number before the transaction loop
    reqPkt.sequence_num = m_info->current_sequence_num++;

    // Start a new retry sequence
    for (int retry = 0; retry < m_info->max_retries; retry++)
    {
        switch(_tnfs_send_recv(udp, m_info, reqPkt, payload_size, pkt))
        {
            case SUCCESS:
            return true;

            case RESET:
            retry = -1;
            continue;

            case FAILED:
            default:
            // fallback to retry
            break;
        }

        // Make sure we wait before retrying
        fnSystem.delay(m_info->min_retry_ms);
    }

    Debug_printf("Retry attempts failed for host: %s, path: %s, cwd: %s\r\n", m_info->hostname, m_info->mountpath, m_info->current_working_directory);

    return false;
}

_tnfs_send_recv_result _tnfs_send_recv(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt)
{
#ifdef DEBUG
    _tnfs_debug_packet(req_pkt, payload_size);
#endif

    // Send packet
    bool sent = _tnfs_send(&udp, m_info, req_pkt, payload_size);
    if (!sent)
    {
        Debug_println("Failed to send packet - retrying");
        return FAILED;
    }

    // Wait for a response at most TNFS_TIMEOUT milliseconds
#ifdef ESP_PLATFORM
    int ms_start = fnSystem.millis();
#else
    uint64_t ms_start = fnSystem.millis();
#endif
    do
    {
        if (SYSTEM_BUS.getShuttingDown())
        {
            Debug_println("TNFS Breakout due to Shutdown");
            return SUCCESS; // false success just to get out
        }

        switch(_tnfs_recv_and_validate(udp, m_info, req_pkt, payload_size, res_pkt))
        {
            case RESP_VALID:
#ifndef ESP_PLATFORM
            Debug_printf("_tnfs_transaction completed in %u ms\n", (unsigned)(fnSystem.millis() - ms_start));
#endif
            return SUCCESS;

            case RESP_TRY_AGAIN:
            return FAILED;

            case RESP_INVALID:
            return FAILED;

            case SESSION_RECOVERED:
            return RESET;

            case NO_RESP:
            default:
            break;
        }

#ifdef ESP_PLATFORM
        fnSystem.yield();
#else
        fnSystem.delay_microseconds(5000); // wait more time for (remote) data to arrive
#endif

    } while ((fnSystem.millis() - ms_start) < m_info->timeout_ms); // packet receive loop

    if (m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        // This is probably an old tcpd server, accepting TCP connections but not responding
        // to any commands. We should fall back to UDP too and don't count this iteration
        // in the retry counter.
        Debug_println("No response to TCP mount request; falling back to UDP.");
        m_info->protocol = TNFS_PROTOCOL_UDP;
        return RESET;
    }

    Debug_printf("Timeout after %d milliseconds. Retrying\r\n", m_info->timeout_ms);
    return FAILED;
}

_tnfs_recv_result _tnfs_recv_and_validate(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt)
{
#ifndef ESP_PLATFORM
    fnSystem.delay_microseconds(2000); // wait short time for (local) data to arrive
#endif
    int l = _tnfs_recv(&udp, m_info, res_pkt);
    if (l < 0)
    {
        return NO_RESP;
    }
#ifdef DEBUG
    _tnfs_debug_packet(res_pkt, l, true);
#endif
    if (m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        Debug_println("TNFS server supports TCP.");
        m_info->protocol = TNFS_PROTOCOL_TCP;
    }

    // Sequence mismatch = stale/reordered response for an earlier request (the
    // seq byte wraps at 256). Discard it and keep waiting for the matching one.
    if (res_pkt.sequence_num != req_pkt.sequence_num)
    {
        Debug_printf("Discarding stale TNFS response. Rcvd: %x, Expected: %x\r\n",
                     res_pkt.sequence_num, req_pkt.sequence_num);
        return NO_RESP;
    }

    // Check in case the server asks us to wait and try again
    if (res_pkt.payload[0] == TNFS_RESULT_TRY_AGAIN)
    {
        // Server should tell us how long it wants us to wait
        uint16_t backoffms = TNFS_UINT16_FROM_LOHI_BYTEPTR(res_pkt.payload + 1);
        Debug_printf("Server asked us to TRY AGAIN after %ums\r\n", backoffms);
        if (backoffms > TNFS_MAX_BACKOFF_DELAY)
            backoffms = TNFS_MAX_BACKOFF_DELAY;
        fnSystem.delay(backoffms);
        return RESP_TRY_AGAIN;
    }

    // Check for invalid (expired) session
    if (res_pkt.payload[0] == TNFS_RESULT_INVALID_HANDLE \
                && req_pkt.command != TNFS_CMD_MOUNT \
                && req_pkt.command != TNFS_CMD_UNMOUNT)
    {
        Debug_printf("_tnfs_transaction - Invalid session ID\n");
        // Recovery - start new session with server, i.e. remount
        uint8_t res = _tnfs_session_recovery(m_info, req_pkt.command);
        if (res != TNFS_RESULT_SUCCESS)
        {
            // update the result byte (TNFS_RESULT_INVALID_HANDLE or TNFS_RESULT_BAD_FILENUM)
            res_pkt.payload[0] = res;
            return RESP_VALID;
        }
        // retry the command using new session
        req_pkt.session_idl = TNFS_LOBYTE_FROM_UINT16(m_info->session);
        req_pkt.session_idh = TNFS_HIBYTE_FROM_UINT16(m_info->session);
        return SESSION_RECOVERED;
    }

    return RESP_VALID;
}

// Re-mount using provided tnfsMountInfo*
// Returns TNFS result code
uint8_t _tnfs_session_recovery(tnfsMountInfo *m_info, uint8_t command)
{
    m_info->session = TNFS_INVALID_SESSION; // prevent umount call
    if (tnfs_mount(m_info) != TNFS_RESULT_SUCCESS)
    {
        Debug_printf("_tnfs_session_recovery - remount failed\n");
        return TNFS_RESULT_INVALID_HANDLE;
    }
    // re-mount succeeded, check the command
    switch (command)
    {
    case TNFS_CMD_OPENDIR:
    case TNFS_CMD_MKDIR:
    case TNFS_CMD_RMDIR:
    case TNFS_CMD_OPENDIRX:
    case TNFS_CMD_STAT:
    case TNFS_CMD_UNLINK:
    case TNFS_CMD_CHMOD:
    case TNFS_CMD_RENAME:
    case TNFS_CMD_OPEN:
    case TNFS_CMD_SIZE:
    case TNFS_CMD_FREE:
        // session was recovered and specified command can be retried within new session
        return TNFS_RESULT_SUCCESS;
    }
    // all other commands requires file descriptor or handle (which is lost with expired session)
    return TNFS_RESULT_BAD_FILENUM;
}

// Copies to buffer while ensuring that we start with a '/'
// Returns length of new full path or -1 on failure
int _tnfs_adjust_with_full_path(tnfsMountInfo *m_info, char *buffer, const char *source, int bufflen)
{
    if (buffer == nullptr || bufflen < 2)
        return -1;

    // Use the cwd to bulid the full path
    strlcpy(buffer, m_info->current_working_directory, bufflen);

    // Figure out whether or not we need to add a slash
    if (source[0] == '/') {
        // Ensure it fits in the buffer
        if ((int)strlen(source) >= bufflen)
            return -1;
        strlcpy(buffer, source, bufflen);
        return strlen(buffer);
    }

    // Figure out whether or not we need to add a slash
    int ll;
    ll = strlen(buffer);
    if (ll < 1 || ll > (bufflen - 2))
        return -1;

    bool dir_slash = buffer[ll - 1] == '/';
    bool needs_slash = source[0] != '/';
    if (needs_slash && dir_slash == false)
    {
        buffer[ll] = '/';
        buffer[++ll] = '\0';
    }
    if (needs_slash == false && dir_slash)
    {
        buffer[--ll] = '\0';
    }

    // Finally copy the source filepath
    strlcpy(buffer + ll, source, bufflen - ll);

    // And return the new length because that ends up being useful
    return strlen(buffer);
}

