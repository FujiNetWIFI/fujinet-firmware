#ifndef NETWORKPROTOCOLIMAPS_H
#define NETWORKPROTOCOLIMAPS_H

#include "Mailbox.h"
#include "fnTcpClientSecure.h"
#include "status_error_codes.h"

#include <string>
#include <vector>

/**
 * NetworkProtocolIMAPS
 *
 * IMAP-over-TLS mailbox adapter for FujiNet.
 *
 * URL format:  IMAPS://user:pass@host[:port]/FOLDER[/N[/A]]   (default port 993)
 *
 * The message sequence number N is the IMAP message sequence number within the
 * folder (1 = oldest, EXISTS = newest), which is exactly the "folder sequence
 * position" the Mailbox base class expects.
 *
 * Credentials come from the URL (user:pass@) and fall back to the login/password
 * members set by SET LOGIN / SET PASSWORD.
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

private:
    enum ImapStatus { IMAP_OK, IMAP_NO, IMAP_BAD, IMAP_TIMEOUT };

    fnTcpClientSecure _tls;
    int _tag = 0;
    bool _loggedIn = false;
    std::string _host, _user, _pass;
    uint16_t _port = 993;
    std::string _selectedFolder;
    uint32_t _selectedCount = 0;
    nDevStatus_t _lastErr = NDEV_STATUS::GENERAL;

    // Send "<tag> <cmd>\r\n" and read the full tagged response (literals inlined).
    ImapStatus do_command(const std::string &cmd, std::string &full);
    ImapStatus read_raw_response(const std::string &tag, std::string &full);

    // SELECT a folder, populating _selectedCount from the "* n EXISTS" reply.
    ImapStatus select_folder(const std::string &folder);

    // FETCH a single body section (e.g. "1", "TEXT") and return its raw bytes.
    bool fetch_section(uint32_t seq, const std::string &section, std::string &rawOut);
};

#endif /* NETWORKPROTOCOLIMAPS_H */
