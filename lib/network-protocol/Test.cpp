/**
 * Test protocol implementation
 */

#include "Test.h"

#include "../../include/debug.h"

NetworkProtocolTest::NetworkProtocolTest(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTest::NetworkProtocolTest(%p,%p,%p)\n", rx_buf, tx_buf, sp_buf);
}

NetworkProtocolTest::~NetworkProtocolTest()
{
    Debug_printf("NetworkProtocolTest::~NetworkProtocolTest()\n");
    test_data.clear();
}

bool NetworkProtocolTest::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    NetworkProtocol::open(urlParser, cmdFrame);

    Debug_printf("scheme: %s\n", urlParser->scheme.c_str());
    Debug_printf("path: %s\n", urlParser->path.c_str());
    Debug_printf("port: %s\n", urlParser->port.c_str());
    Debug_printf("query: %s\n", urlParser->query.c_str());

    test_data = "This is test data being initialized in the test protocol, a single line of input with line ending.";

    switch (translation_mode)
    {
    case 0:
        Debug_printf("Atari Translation\n");
        test_data += "\x9b";
        break;
    case 1:
        Debug_printf("CR Translation\n");
        test_data += "\x0d";
        break;
    case 2:
        Debug_printf("LF Translation\n");
        test_data += "\x0a";
        break;
    case 3:
        Debug_printf("CRLF Translation\n");
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

    Debug_printf("NetworkProtocolTest::read(%u)\n", len);
    for (int i = 0; i < receiveBuffer->length(); i++)
        Debug_printf("%02x ", receiveBuffer->at(i));
    Debug_printf("\n");

    return NetworkProtocol::read(len);
}

bool NetworkProtocolTest::write(unsigned short len)
{
    bool err = false;

    Debug_printf("NetworkProtocolTest::write(%u) - Before translate_transmit_buffer()", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", transmitBuffer->at(i));
    Debug_printf("\n");

    len = translate_transmit_buffer();

    Debug_printf("NetworkProtocolTest::write(%u) - After translate_transmit_buffer()", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", transmitBuffer->at(i));
    Debug_printf("\n");

    transmitBuffer->erase(0, len);

    return err;
}

bool NetworkProtocolTest::status(NetworkStatus *status)
{
    status->rxBytesWaiting = test_data.length();
    status->connected = 1;
    status->error = error;

    NetworkProtocol::status(status);

    return false;
}

uint8_t NetworkProtocolTest::special_inquiry(uint8_t cmd)
{
    return 0xFF; // selected command not implemented.
}

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
