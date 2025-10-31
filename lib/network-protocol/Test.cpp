/**
 * Test protocol implementation
 */

#include "Test.h"

#include "../../include/debug.h"

#include <vector>

NetworkProtocolTest::NetworkProtocolTest(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTest::NetworkProtocolTest(%p,%p,%p)\r\n", rx_buf, tx_buf, sp_buf);
}

NetworkProtocolTest::~NetworkProtocolTest()
{
    Debug_printf("NetworkProtocolTest::~NetworkProtocolTest()\r\n");
    test_data.clear();
}

bool NetworkProtocolTest::open(PeoplesUrlParser *urlParser, FujiTranslationMode mode)
{
    NetworkProtocol::open(urlParser, mode);

    Debug_printf("scheme: %s\r\n", urlParser->scheme.c_str());
    Debug_printf("path: %s\r\n", urlParser->path.c_str());
    Debug_printf("port: %s\r\n", urlParser->port.c_str());
    Debug_printf("query: %s\r\n", urlParser->query.c_str());

    test_data = "This is test data being initialized in the test protocol, a single line of input with line ending.";

    switch (translation_mode)
    {
    case 0:
        Debug_printf("Atari Translation\r\n");
        test_data += "\x9b";
        break;
    case 1:
        Debug_printf("CR Translation\r\n");
        test_data += "\x0d";
        break;
    case 2:
        Debug_printf("LF Translation\r\n");
        test_data += "\x0a";
        break;
    case 3:
        Debug_printf("CRLF Translation\r\n");
        test_data += "\x0d\x0a";
        break;
    }

    return false;
}

bool NetworkProtocolTest::close()
{
    return false;
}

bool NetworkProtocolTest::read(unsigned short len)
{
    if (receiveBuffer->length() == 0)
        *receiveBuffer += test_data.substr(0, len);

    error = 1;

    Debug_printf("NetworkProtocolTest::read(%u)\r\n", len);
    for (int i = 0; i < receiveBuffer->length(); i++)
        Debug_printf("%02x ", (unsigned char)receiveBuffer->at(i));
    Debug_printf("\r\n");

    return NetworkProtocol::read(len);
}

bool NetworkProtocolTest::write(unsigned short len)
{
    bool err = false;

    Debug_printf("NetworkProtocolTest::write(%u) - Before translate_transmit_buffer()", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", (unsigned char)transmitBuffer->at(i));
    Debug_printf("\r\n");

    len = translate_transmit_buffer();

    Debug_printf("NetworkProtocolTest::write(%u) - After translate_transmit_buffer()", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", (unsigned char)transmitBuffer->at(i));
    Debug_printf("\r\n");

    transmitBuffer->erase(0, len);

    return err;
}

bool NetworkProtocolTest::status(NetworkStatus *status)
{
#if 0
    status->rxBytesWaiting = test_data.length();
#endif
    status->connected = 1;
    status->error = error;

    NetworkProtocol::status(status);

    return false;
}

FujiDirection NetworkProtocolTest::special_inquiry(uint8_t cmd)
{
    return DIRECTION_INVALID; // selected command not implemented.
}

#ifdef OBSOLETE
bool NetworkProtocolTest::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolTest::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolTest::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}
#endif /* OBSOLETE */
