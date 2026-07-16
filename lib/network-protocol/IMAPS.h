#ifndef NETWORKPROTOCOLIMAPS_H
#define NETWORKPROTOCOLIMAPS_H

#include "Mailbox.h"

/**
 * NetworkProtocolIMAPS
 *
 * IMAP-over-TLS mailbox adapter for FujiNet.
 *
 * URL format:  IMAPS://user:pass@host:port/FOLDER/item
 *
 * SCAFFOLD ONLY. The mailbox provider hooks currently return NOT_IMPLEMENTED.
 *
 * TODO to make this functional:
 *   - Add a TLS client-socket wrapper (e.g. fnTcpClientSecure) that mirrors
 *     fnTcpClient's read/write/read_until API over esp_tls (ESP) and Mongoose
 *     mg_tls (PC). No such wrapper exists in the firmware yet; TLS today lives
 *     only inside the HTTP clients.
 *   - Implement IMAP4rev1 command handling (LOGIN/SELECT/SEARCH/FETCH), mapping
 *     folder -> mailbox, sequence number -> message, and honoring range/newest.
 *   - Implement MIME multipart parsing to produce the message body and the
 *     attachment index/data (shared struct output is already handled by the
 *     Mailbox base class).
 *   - Read credentials from opened_url->user/password, falling back to the
 *     login/password members (set via SET LOGIN/PASSWORD), like S3.
 */
class NetworkProtocolIMAPS : public NetworkProtocolMailbox
{
public:
    NetworkProtocolIMAPS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolIMAPS();

    NetworkProtocolIMAPS(const NetworkProtocolIMAPS &) = delete;
    NetworkProtocolIMAPS &operator=(const NetworkProtocolIMAPS &) = delete;

protected:
    fujiError_t connect_and_auth() override;
    fujiError_t folder_count(const std::string &folder, uint32_t &count) override;
    fujiError_t folder_index(const std::string &folder, long rangeStart, long rangeEnd,
                             bool newest, std::vector<MailboxIndexEntry> &out) override;
    fujiError_t message_body(const std::string &folder, uint32_t seq, std::string &out) override;
    fujiError_t attachment_index(const std::string &folder, uint32_t seq,
                                 std::vector<MailboxAttachmentEntry> &out) override;
    fujiError_t attachment_data(const std::string &folder, uint32_t seq, uint8_t attach,
                                std::string &out) override;
    void mailbox_error_to_error() override;
};

#endif /* NETWORKPROTOCOLIMAPS_H */
