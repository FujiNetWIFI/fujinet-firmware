#ifndef NETWORKPROTOCOL_H
#define NETWORKPROTOCOL_H

#include "fujiCommandID.h"
#include "bus.h"
#include "networkStatus.h"
#include "peoples_url_parser.h"

#include <string>

// FIXME - only used by FS classes and doesn't belong here
typedef enum class ACCESS_MODE {
    READ          = 0b0100,
    DIRECTORY     = 0b0110,
    DIRECTORY_ALT = 0b0111,
    WRITE         = 0b1000,
    APPEND        = 0b1001,
    READWRITE     = 0b1100,
    INVALID       = -1,
} fileAccessMode_t;

enum netProtoTranslation_t {
    NETPROTO_TRANS_NONE     = 0,
    NETPROTO_TRANS_CR       = 1,
    NETPROTO_TRANS_LF       = 2,
    NETPROTO_TRANS_CRLF     = 3,
    NETPROTO_TRANS_PETSCII  = 4,
};

typedef enum class PROTOCOL_ERROR {
    NONE = 0,
    UNSPECIFIED = 1,
} protocolError_t;

class NetworkProtocol
{
public:
    std::string name = "UNKNOWN";
    /**
     * Was the last command a write?
     */
    bool was_write = false;

    /**
     * Pointer to the receive buffer
     */
    std::string *receiveBuffer = nullptr;

    /**
     * Pointer to the transmit buffer
     */
    std::string *transmitBuffer = nullptr;

    /**
     * Pointer to the transmit buffer
     */
    std::string *specialBuffer = nullptr;

    /**
     * Pointer to passed in URL
     */
    PeoplesUrlParser *opened_url = nullptr;

    /**
     * ctor - Initialize network protocol object.
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     */
    NetworkProtocol(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor - Tear down network protocol object
     */
    virtual ~NetworkProtocol();

    /**
     * @brief Protocol connection is a server (listening connection)
     */
    bool connectionIsServer = false;

    /**
     * @brief number of bytes waiting
     */
    unsigned short bytesWaiting = 0;

    /**
     * @brief Error code to return in status
     */
    nDevStatus_t error = NDEV_STATUS::SUCCESS;

    /**
     * Translation mode: 0=NONE, 1=CR, 2=LF, 3=CR/LF
     */
    netProtoTranslation_t translation_mode = NETPROTO_TRANS_NONE;

    /**
     * Is this being called from inside an interrupt?
     */
    bool fromInterrupt = false;

    /**
     * Enable interrupts?
     */
    bool interruptEnable = true;

    /**
     * Do we need to force Status call?
     * (e.g. to start http_transaction in http protocol adapter after open)
     */
    bool forceStatus = false;

    /**
     * The line ending to emit
     */
    std::string lineEnding = "\x9B";

    /**
     * Set line ending to string
     */
    void setLineEnding(std::string s) { lineEnding = s; }

    /**
     * The sector size to display in directory for FS derived protocols
     */
    int FSSectorSize = 256;

    /**
     * @brief Set sector size to display in directory for FS derived protocols
     * @param sectorSize in bytes.
     */
    void setFSSectorSize(int sectorSize) { FSSectorSize = sectorSize; }

    /**
     * @brief Open connection to the protocol using URL
     * @param urlParser The URL object passed in to open.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                                 netProtoTranslation_t translate);

    /**
     * @brief Close connection to the protocol.
     */
    virtual protocolError_t close();

    /**
     * @brief Read len bytes into receiveBuffer, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return translation successful.
     */
    virtual protocolError_t read(unsigned short len);

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t write(unsigned short len) { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t status(NetworkStatus *status);

    /**
     * @brief return an _atari_ error (>199) based on errno. into error for status reporting.
     */
    virtual void errno_to_error();

    /**
     * @brief change the values passed to open for platforms that need to do it after the open (looking at you IEC)
     */
    // FIXME - only used by FS class hierarchy, doesn't belong here
    virtual void set_open_params(fileAccessMode_t access, netProtoTranslation_t translate) { abort(); };

    virtual off_t seek(off_t offset, int whence) { return -1; }

    virtual size_t available() = 0;

    /**
     * Pointer to current login;
     */
    std::string *login;

    /**
     * Pointer to current password;
     */
    std::string *password;

protected:

    /**
     * Perform end of line translation on receive buffer.
     */
    void translate_receive_buffer();

    /**
     * Perform end of line translation on transmit buffer.
     * @return new buffer length.
     */
    unsigned short translate_transmit_buffer();

};

#endif /* NETWORKPROTOCOL_H */
