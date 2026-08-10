/**
 * CLIPBOARD: - the FujiNet clipboard, as an N: protocol adapter
 *
 * The clipboard holds a current snippet plus the snippets that preceded it,
 * addressed by index:
 *
 *   N:CLIPBOARD:///    the current clipboard (same as index 0)
 *   N:CLIPBOARD:///0   the current clipboard
 *   N:CLIPBOARD:///9   the oldest remembered snippet
 *
 * Opening for read delivers the snippet; opening for write replaces the
 * clipboard when the channel is closed, and opening for append adds to what it
 * already holds. Only the clipboard itself (index 0) can be written to.
 * Closing a write-only channel that received nothing empties the clipboard.
 *
 * Query parameters:
 *   ?binary=1   store what is written verbatim and mark it binary, so neither
 *               this adapter nor the web interface ever translates it
 *
 * Only end of line translation is performed, using the mode the channel was
 * opened with (aux2), exactly as for any other N: protocol. Character set
 * conversion is left to the computer.
 */

#ifndef NETWORKPROTOCOLCLIPBOARD_H
#define NETWORKPROTOCOLCLIPBOARD_H

#include <string>

#include "Protocol.h"

class NetworkProtocolClipboard : public NetworkProtocol
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     */
    NetworkProtocolClipboard(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolClipboard();

    /**
     * @brief Select the snippet named by the URL and set up the transfer.
     * @param urlParser The URL object passed in to open.
     * @param access the access mode the channel was opened with
     * @param translate the end of line translation mode (aux2)
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                     netProtoTranslation_t translate) override;

    /**
     * @brief Close the channel, committing anything that was written.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t close() override;

    /**
     * @brief Move len bytes of the selected snippet into the receive buffer.
     * @param len Number of bytes to read.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t read(unsigned short len) override;

    /**
     * @brief Stage len bytes from the transmit buffer for the clipboard.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t status(NetworkStatus *status) override;

    /**
     * @brief Bytes of the selected snippet still to be delivered.
     */
    size_t available() override;

private:
    /**
     * Which snippet was selected: 0 is the clipboard, 1..n the remembered ones.
     */
    size_t index = 0;

    /**
     * Was the channel opened for reading?
     */
    bool readable = false;

    /**
     * Was the channel opened for writing?
     */
    bool writable = false;

    /**
     * Does closing commit even if nothing was written? Set for a write only
     * open, which truncates the clipboard the way opening a file for write
     * truncates it.
     */
    bool truncates = false;

    /**
     * Does what is written go after what the clipboard already holds? Set for
     * an append open.
     */
    bool appends = false;

    /**
     * Did anything actually arrive from the computer?
     */
    bool wrote = false;

    /**
     * Store what is written without translating it, and mark it binary.
     */
    bool binary = false;

    /**
     * The selected snippet, already translated for the computer.
     */
    std::string readBuffer;

    /**
     * Bytes staged for the clipboard, still in the computer's representation.
     */
    std::string writeBuffer;

    /**
     * @brief Work out which snippet the URL selects.
     * @param urlParser the URL passed to open()
     * @param idx receives the snippet index
     * @return true if the URL named a snippet index we can address
     */
    bool parse_index(PeoplesUrlParser *urlParser, size_t &idx);

    /**
     * @brief Take a copy of the selected snippet, translated for the computer.
     */
    void stage_read();

    /**
     * @brief Make everything that was written the new clipboard.
     */
    void commit();
};

#endif /* NETWORKPROTOCOLCLIPBOARD_H */
