
#include "tnfslib.h"

#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>
#include <errno.h>
#include <mutex>
#include "compat_string.h"
#include "global_types.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "bus.h"
#include "fnUDP.h"
#include "fnTcpClient.h"
#include "tnfslib_udp.h"

#include "utils.h"

#include "tnfslib_internal.h"
#include "tnfslib_framing.h"

using namespace std;
// ------------------------------------------------
// INTERNAL UTILITY FUNCTIONS
// ------------------------------------------------

// A new TCP connection starts a new reply stream.
static void _tnfs_tcp_start_replies(tnfsMountInfo *m_info)
{
    if (m_info->tcp_replies)
        m_info->tcp_replies->reset();
    else
        m_info->tcp_replies = tnfs_new_reply_assembler();
}

// The reply stream can no longer be trusted; the next send reconnects.
static void _tnfs_tcp_drop(tnfsMountInfo *m_info)
{
    m_info->tcp_client.stop();
    m_info->tcp_replies.reset();
}

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
        _tnfs_tcp_start_replies(m_info);
        if (m_info->tcp_replies == nullptr)
        {
            Debug_println("No memory for TNFS TCP replies");
            tcp->stop();
            return false;
        }
    }
    m_info->tcp_replies->request_sent(pkt.sequence_num, static_cast<int>(tcp->available()));
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
    TNFS_RECV_CONNECTION_LOST: the TCP connection was closed.
*/
int _tnfs_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, const tnfsPacket &req_pkt)
{
    if (m_info->protocol == TNFS_PROTOCOL_TCP || m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        return _tnfs_tcp_recv(m_info, pkt, req_pkt);
    }
    else
    {
        return _tnfs_udp_recv(udp, m_info, pkt);
    }
}

static uint8_t _tnfs_field_bit(tnfsOptionalField field)
{ return static_cast<uint8_t>(1u << (static_cast<int>(field) - 1)); }

// The format replies are framed with: an ABSENT that no fence has confirmed counts as UNKNOWN.
static tnfsReplyFormat _tnfs_trusted_format(const tnfsMountInfo *m_info)
{
    tnfsReplyFormat f = m_info->tcp_reply_format;
    auto check = [m_info](tnfsFieldUse &use, tnfsOptionalField field)
    {
        if (use == tnfsFieldUse::ABSENT && !(m_info->tcp_format_verified & _tnfs_field_bit(field)))
            use = tnfsFieldUse::UNKNOWN;
    };
    check(f.stat_strings, tnfsOptionalField::STAT_STRINGS);
    check(f.read_count, tnfsOptionalField::READ_COUNT);
    check(f.write_count, tnfsOptionalField::WRITE_COUNT);
    return f;
}

static tnfsReplyContext _tnfs_reply_context(const tnfsMountInfo *m_info, const tnfsPacket &req_pkt)
{
    uint16_t session = TNFS_UINT16_FROM_HILOBYTES(req_pkt.session_idh, req_pkt.session_idl);
    return {session, req_pkt.sequence_num, req_pkt.command, _tnfs_trusted_format(m_info)};
}

// Whether replies are left to the next reply's header (tests only; see tnfsFormatPolicy).
static bool _tnfs_infers(const tnfsMountInfo *m_info)
{
#ifdef UNIT_TESTS
    return m_info->tcp_format_policy == tnfsFormatPolicy::INFER;
#else
    (void)m_info;
    return false;
#endif
}

static const char *_tnfs_field_name(tnfsOptionalField field)
{
    return field == tnfsOptionalField::STAT_STRINGS ? "STAT uid/gid strings"
           : field == tnfsOptionalField::READ_COUNT ? "failed READ count"
                                                    : "failed WRITE count";
}

// Record what a fence showed; it overrides an expectation it contradicts.
static void _tnfs_set_field_use(tnfsMountInfo *m_info, tnfsOptionalField field, tnfsFieldUse use)
{
    tnfsReplyFormat &format = m_info->tcp_reply_format;
    tnfsFieldUse &slot = field == tnfsOptionalField::STAT_STRINGS ? format.stat_strings
                         : field == tnfsOptionalField::READ_COUNT ? format.read_count
                                                                  : format.write_count;
    if (slot != tnfsFieldUse::UNKNOWN && slot != use)
        Debug_printf("TNFS server contradicts the expected reply format (%s)\r\n", _tnfs_field_name(field));
    slot = use;
    if (use == tnfsFieldUse::ABSENT)
        m_info->tcp_format_verified |= _tnfs_field_bit(field);
}

// The answer just taken may be followed by a field whose use is unknown. Settle it before
// anything else is sent, so it can never be read as the next request's answer: with a
// fence request (IDENTIFY), or by starting a new reply stream (RESET, and whenever the
// fence cannot be used).
static void _tnfs_tcp_settle(tnfsMountInfo *m_info)
{
    tnfsReplyAssembler *replies = m_info->tcp_replies.get();
    tnfsOptionalField field = replies->unsettled();
    if (field == tnfsOptionalField::NONE || _tnfs_infers(m_info))
        return;
    // A retry's duplicate could sit between the reply and the fence's answer. On a connection
    // that is already gone the field is lost with it: nothing to settle, and no verdict on
    // the server either.
    fnTcpClient *tcp = &m_info->tcp_client;
    if (m_info->tcp_format_policy == tnfsFormatPolicy::IDENTIFY && !m_info->tcp_fence_failed &&
        !replies->replies_pending() && tcp->connected())
    {
        // CLOSE: no optional field in any format; session 0: no session to act on.
        uint8_t sequence = m_info->current_sequence_num++;
        const uint8_t fence[TNFS_HEADER_SIZE + 1] = {0, 0, sequence, TNFS_CMD_CLOSE, 0xFF};
        const tnfsReplyContext context = {0, sequence, TNFS_CMD_CLOSE};
        replies->request_sent(sequence, static_cast<int>(tcp->available()));
        success_is_true sent(tcp->write(fence, sizeof(fence)) == sizeof(fence));
        auto ms_start = fnSystem.millis(); // unsigned: the difference survives a wrap
        while (sent.is_success() && fnSystem.millis() - ms_start < static_cast<decltype(ms_start)>(m_info->timeout_ms))
        {
            uint8_t buf[16]; // the fence's answer is 5 bytes; a field is read in a few passes
            size_t want = std::min({tcp->available(), static_cast<size_t>(replies->space()), sizeof(buf)});
            if (want > 0)
                replies->append(buf, tcp->read(buf, want));
            tnfsFenceVerdict verdict = replies->settle(context);
            if (verdict == tnfsFenceVerdict::ABSENT || verdict == tnfsFenceVerdict::PRESENT)
            {
                bool present = verdict == tnfsFenceVerdict::PRESENT;
                _tnfs_set_field_use(m_info, field, present ? tnfsFieldUse::PRESENT : tnfsFieldUse::ABSENT);
                Debug_printf("TNFS server %s the optional %s\r\n", present ? "sends" : "leaves out",
                             _tnfs_field_name(field));
                return;
            }
            if (verdict == tnfsFenceVerdict::FAILED || (want == 0 && !tcp->connected()))
                break;
            if (want == 0)
            {
#ifdef ESP_PLATFORM
                fnSystem.yield();
#else
                fnSystem.delay_microseconds(2000);
#endif
            }
        }
        // Sent, and not answered as a fence is (silence, another answer, or the server hanging
        // up on it): this server cannot settle fields with a fence. A write that failed says
        // nothing about the server.
        if (sent.is_success())
        {
            Debug_printf("TNFS fence for the %s failed; reconnecting after such replies\r\n",
                         _tnfs_field_name(field));
            m_info->tcp_fence_failed = true;
        }
    }
    Debug_printf("Unsettled %s; starting a new TCP connection\r\n", _tnfs_field_name(field));
    _tnfs_tcp_drop(m_info);
}

// One reply per call, however TCP split or merged them, framed for the request in req_pkt.
// Bytes past the assembler's free space stay queued in tcp_client, and pkt doubles as the
// read buffer.
int _tnfs_tcp_recv(tnfsMountInfo *m_info, tnfsPacket &pkt, const tnfsPacket &req_pkt)
{
    tnfsReplyAssembler *replies = m_info->tcp_replies.get();
    if (replies == nullptr)
        return -1;

    fnTcpClient *tcp = &m_info->tcp_client;
    size_t want = std::min({tcp->available(), static_cast<size_t>(replies->space()),
                            sizeof(pkt.rawData)});
    if (want > 0)
        replies->append(pkt.rawData, tcp->read(pkt.rawData, want));

    const tnfsReplyContext context = _tnfs_reply_context(m_info, req_pkt);
    tnfsReplyKind kind;
    tnfsFrame frame = replies->take(pkt.rawData, context, kind);
    // Only the answer goes up: an earlier request's reply may even carry this sequence, and
    // TNFSD's reply to request bytes it could not tie to a session answers nothing we sent.
    while (frame.status == tnfsFrameStatus::COMPLETE && kind != tnfsReplyKind::ANSWER)
    {
        Debug_printf("Discarding %s TNFS reply: seq %x, cmd %x, result %x\r\n",
                     kind == tnfsReplyKind::EARLIER ? "an earlier" : "a sessionless",
                     pkt.rawData[2], pkt.rawData[3], pkt.rawData[4]);
        if (replies->unsettled() != tnfsOptionalField::NONE && !_tnfs_infers(m_info))
        {
            // Only right after an answer can a fence settle it; a fresh stream is safe.
            Debug_printf("Unsettled %s after an earlier reply; starting a new TCP connection\r\n",
                         _tnfs_field_name(replies->unsettled()));
            _tnfs_tcp_drop(m_info);
            return TNFS_RECV_CONNECTION_LOST;
        }
        frame = replies->take(pkt.rawData, context, kind);
    }
    if (frame.status == tnfsFrameStatus::COMPLETE)
    {
        // The answer stands either way: no field was pending when it was framed.
        _tnfs_tcp_settle(m_info);
        return frame.length;
    }
    if (frame.status == tnfsFrameStatus::MALFORMED)
    {
        Debug_println("Malformed TNFS reply over TCP; dropping the connection");
        _tnfs_tcp_drop(m_info);
        return TNFS_RECV_CONNECTION_LOST;
    }
    if (want == 0 && !tcp->connected())
    {
        Debug_println("TNFS TCP connection closed");
        _tnfs_tcp_drop(m_info);
        return TNFS_RECV_CONNECTION_LOST;
    }
    return -1;
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
        _tnfs_tcp_drop(m_info);
        return RESET;
    }

    // The retry's reply must not complete a reply that was cut off: start a new stream.
    if (m_info->tcp_replies &&
        m_info->tcp_replies->may_hold_cut_answer(_tnfs_reply_context(m_info, req_pkt)))
        _tnfs_tcp_drop(m_info);

    Debug_printf("Timeout after %d milliseconds. Retrying\r\n", m_info->timeout_ms);
    return FAILED;
}

_tnfs_recv_result _tnfs_recv_and_validate(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt)
{
#ifndef ESP_PLATFORM
    fnSystem.delay_microseconds(2000); // wait short time for (local) data to arrive
#endif
    int l = _tnfs_recv(&udp, m_info, res_pkt, req_pkt);
    if (l == TNFS_RECV_CONNECTION_LOST && m_info->protocol == TNFS_PROTOCOL_TCP)
    {
        return RESP_INVALID; // retry on a new connection
    }
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

