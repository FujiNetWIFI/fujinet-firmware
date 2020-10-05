/**
 * NetworkProtocol tests
 */
#include "test_buffer.h"
#include "tests_networkprotocol_base.h"
#include "test_defines.h"

/**
 * External buffers for protocol instantiation (tests_main.c)
 */
extern uint8_t *rx_buf;
extern uint8_t *tx_buf;
extern uint8_t *sp_buf;

/**
 * Test Fixture: EOL test string
 */
const char *test_eol = "This is a chunk of test data.\x9bIt should have two lines.\x9b";

/**
 * Test Fixture: CR test string
 */
const char *test_cr = "This is a chunk of test data.\rIt should have two lines.\r";

/**
 * Test Fixture: LF test string
 */
const char *test_lf = "This is a chunk of test data.\nIt should have two lines.\n";

/**
 * Test Fixture: CR/LF test string
 */
const char *test_crlf = "This is a chunk of test data.\r\nIt should have two lines.\r\n";

/**
 * External protocol object
 */
extern NetworkProtocol *protocol;

void tests_networkprotocol_base_setup()
{
    rx_buf = (uint8_t *)malloc(RX_TX_CAPACITY);
    tx_buf = (uint8_t *)malloc(RX_TX_CAPACITY);
    sp_buf = (uint8_t *)malloc(SP_CAPACITY);
    protocol = new NetworkProtocol(rx_buf, RX_TX_CAPACITY,
                                   tx_buf, RX_TX_CAPACITY,
                                   sp_buf, RX_TX_CAPACITY);

    if (rx_buf == nullptr || tx_buf == nullptr || sp_buf == nullptr || protocol == nullptr)
    {
        abort();
    }
}

void tests_networkprotocol_base()
{
    tests_networkprotocol_base_setup();



    tests_networkprotocol_base_done();
}

/**
 * Test RX translation: CR to EOL
 */
void test_networkprotocol_base_rx_cr_to_eol()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x01,0x00}; // aux2 0x01 is CR
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    test_buffer_setup(test_cr);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_cr));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf);
}

/**
 * Test RX translation: LF to EOL
 */
void test_networkprotocol_base_rx_lf_to_eol()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x02,0x00}; // aux2 0x02 is LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    test_buffer_setup(test_lf);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_lf));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf);
}

/**
 * Test RX translation: CR/LF to EOL
 * The resulting input buffer should contract to compensate for the removal of LF characters.
 */
void test_networkprotocol_base_rx_cr_lf_to_eol()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x03,0x00}; // aux2 0x03 is CR/LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    test_buffer_setup(test_crlf);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_crlf));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf);
}

/**
 * Test TX translation: EOL to CR
 */
void test_networkprotocol_base_tx_eol_to_cr()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x01,0x00}; // aux2 0x01 is CR
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    test_buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_cr, tx_buf);
}

/**
 * Test TX translation: EOL to LF
 */
void test_networkprotocol_base_tx_eol_to_lf()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x02,0x00}; // aux2 0x02 is LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    test_buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_lf, tx_buf);
}

/**
 * Test TX translation: EOL to CR/LF
 * The resulting output should expand to compensate for the addition of LF characters.
 */
void test_networkprotocol_base_tx_eol_to_crlf()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x03,0x00}; // aux2 0x03 is CR/LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    test_buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_crlf, tx_buf);
}

void tests_networkprotocol_base_done()
{
    if (rx_buf != nullptr)
        free(rx_buf);

    if (tx_buf != nullptr)
        free(tx_buf);

    if (sp_buf != nullptr)
        free(sp_buf);

    if (protocol != nullptr)
        delete protocol;
}
