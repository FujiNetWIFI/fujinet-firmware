#ifndef NETWORKPROTOCOL_H
#define NETWORKPROTOCOL_H

#include "fujiCommandID.h"
#include "bus.h"
#include "networkStatus.h"
#include "peoples_url_parser.h"

#include <string>

// FIXME - this has something to do with Atari SIO and doesn't belong here
enum AtariSIODirection {
    SIO_DIRECTION_NONE    = 0x00,
    SIO_DIRECTION_READ    = 0x40,
    SIO_DIRECTION_WRITE   = 0x80,
    SIO_DIRECTION_INVALID = 0xFF,
};

enum netProtoTranslation_t {
    NETPROTO_TRANS_NONE     = 0,
    NETPROTO_TRANS_CR       = 1,
    NETPROTO_TRANS_LF       = 2,
    NETPROTO_TRANS_CRLF     = 3,
    NETPROTO_TRANS_PETSCII  = 4,
};

enum {
    NETPROTO_A2_FLAG  = 0x80,
    NETPROTO_A2_80COL = 0x81,
};

enum netProtoOpenMode_t {
    NETPROTO_OPEN_READ          = 4,
    NETPROTO_OPEN_HTTP_DELETE   = 5,
    NETPROTO_OPEN_DIRECTORY     = 6,
    NETPROTO_OPEN_DIRECTORY_ALT = 7,
    NETPROTO_OPEN_WRITE         = 8,
    NETPROTO_OPEN_APPEND        = 9,
    NETPROTO_OPEN_READWRITE     = 12,
    NETPROTO_OPEN_HTTP_POST     = 13,
    NETPROTO_OPEN_HTTP_PUT      = 14,
    NETPROTO_OPEN_INVALID       = -1,
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
    bool is_write = false;

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
     * @param cmdFrame The command frame to extract aux1/aux2/etc.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t open(PeoplesUrlParser *urlParser, cmdFrame_t *cmdFrame);

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
    virtual protocolError_t write(unsigned short len);

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t status(NetworkStatus *status);

    /**
     * @brief Return a DSTATS byte for a requested COMMAND byte.
     * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
     * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
     */
    virtual AtariSIODirection special_inquiry(fujiCommandID_t cmd) { return SIO_DIRECTION_INVALID; }

    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t special_00(cmdFrame_t *cmdFrame) { return PROTOCOL_ERROR::NONE; };

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) { return PROTOCOL_ERROR::NONE; };

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    virtual protocolError_t special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) { return PROTOCOL_ERROR::NONE; };

    /**
     * @brief perform an idempotent command with DSTATS 0x80, that does not require open channel.
     * @param url The URL object.
     * @param cmdFrame command frame.
     */
    virtual protocolError_t perform_idempotent_80(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) { return PROTOCOL_ERROR::NONE; };

    /**
     * @brief return an _atari_ error (>199) based on errno. into error for status reporting.
     */
    virtual void errno_to_error();

    /**
     * @brief change the values passed to open for platforms that need to do it after the open (looking at you IEC)
     */
    virtual void set_open_params(uint8_t p1, uint8_t p2);

    virtual off_t seek(off_t offset, int whence);

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
     * AUX1 value from open
     */
    unsigned char aux1_open = 0;

    /**
     * AUX2 value from open
     */
    unsigned char aux2_open = 0;

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
