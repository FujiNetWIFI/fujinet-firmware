/**
 * Test Protocol - Can be used as a skeleton
 */

#ifndef NETWORKPROTOCOL_TEST
#define NETWORKPROTOCOL_TEST

#include <string>

#include "Protocol.h"

// using namespace std;

class NetworkProtocolTest : public NetworkProtocol
{

public:
    /**
     * ctor
     */
    NetworkProtocolTest(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolTest();

    /**
     * @brief Open connection to the protocol using URL
     * @param urlParser The URL object passed in to open.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close connection to the protocol.
     */
    protocolError_t close() override { return PROTOCOL_ERROR::NONE; };

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t status(NetworkStatus *status) override;

    size_t available() override { return test_data.length(); }

private:
    /**
     * String to hold test data
     */
    std::string test_data;
};

#endif /* NETWORKPROTOCOL_TEST */
