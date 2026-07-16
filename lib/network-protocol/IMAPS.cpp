/**
 * NetworkProtocolIMAPS - scaffold.
 *
 * All provider hooks return NOT_IMPLEMENTED until a TLS socket wrapper and the
 * IMAP4/MIME layer are added (see IMAPS.h). connect_and_auth() fails first, so
 * NetworkProtocolMailbox::open() never reaches the data hooks; they are defined
 * here only to satisfy the pure-virtual interface.
 */

#include "IMAPS.h"

#include "../../include/debug.h"
#include "status_error_codes.h"

NetworkProtocolIMAPS::NetworkProtocolIMAPS(std::string *rx_buf, std::string *tx_buf,
                                           std::string *sp_buf)
    : NetworkProtocolMailbox(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolIMAPS::ctor\r\n");
}

NetworkProtocolIMAPS::~NetworkProtocolIMAPS()
{
    Debug_printf("NetworkProtocolIMAPS::dtor\r\n");
}

fujiError_t NetworkProtocolIMAPS::connect_and_auth()
{
    Debug_printf("NetworkProtocolIMAPS: not implemented\r\n");
    error = NDEV_STATUS::NOT_IMPLEMENTED;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolIMAPS::folder_count(const std::string &, uint32_t &)
{
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolIMAPS::folder_index(const std::string &, long, long, bool,
                                               std::vector<MailboxIndexEntry> &)
{
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolIMAPS::message_body(const std::string &, uint32_t, std::string &)
{
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolIMAPS::attachment_index(const std::string &, uint32_t,
                                                   std::vector<MailboxAttachmentEntry> &)
{
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolIMAPS::attachment_data(const std::string &, uint32_t, uint8_t,
                                                  std::string &)
{
    return FUJI_ERROR::UNSPECIFIED;
}

void NetworkProtocolIMAPS::mailbox_error_to_error()
{
    error = NDEV_STATUS::NOT_IMPLEMENTED;
}
