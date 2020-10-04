#include <unity.h>
#include <string.h>
#include "test_network_translation.h"

/**
 * Test main
 */
void app_main()
{
    ets_delay_us(5000000); // Odd, I actually had to add this so I wouldn't miss the first bit of data.

    rx_buf = (uint8_t *)malloc(RX_TX_CAPACITY);
    tx_buf = (uint8_t *)malloc(RX_TX_CAPACITY);
    sp_buf = (uint8_t *)malloc(SP_CAPACITY);
    protocol = new NetworkProtocol(rx_buf, RX_TX_CAPACITY,
                                   tx_buf, RX_TX_CAPACITY,
                                   sp_buf, RX_TX_CAPACITY);

    UNITY_BEGIN();
    RUN_TEST(TestRXCRtoEOL);
    RUN_TEST(TestRXLFtoEOL);
    RUN_TEST(TestRXCRLFtoEOL);
    RUN_TEST(TestTXEOLtoCR);
    RUN_TEST(TestTXEOLtoLF);
    RUN_TEST(TestTXEOLtoCRLF);

    delete protocol;
    free(rx_buf);
    free(tx_buf);
    free(sp_buf);
    UNITY_END();
}

/**
 * Test RX translation: CR to EOL
 */
void TestRXCRtoEOL()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x01,0x00}; // aux2 0x01 is CR
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_cr);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_cr));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf);
}

/**
 * Test RX translation: LF to EOL
 */
void TestRXLFtoEOL()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x02,0x00}; // aux2 0x02 is LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_lf);
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
void TestRXCRLFtoEOL()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x03,0x00}; // aux2 0x03 is CR/LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_crlf);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_crlf));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf);
}

/**
 * Test TX translation: EOL to CR
 */
void TestTXEOLtoCR()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x01,0x00}; // aux2 0x01 is CR
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_cr, tx_buf);
}

/**
 * Test TX translation: EOL to LF
 */
void TestTXEOLtoLF()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x02,0x00}; // aux2 0x02 is LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_lf, tx_buf);
}

/**
 * Test TX translation: EOL to CR/LF
 * The resulting output should expand to compensate for the addition of LF characters.
 */
void TestTXEOLtoCRLF()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x03,0x00}; // aux2 0x03 is CR/LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_crlf, tx_buf);
}

/**
 * Set up buffers given a source fixture
 * @param c the buffer to put into the protocol buffers.
 */
void buffer_setup(const char *c)
{
    // Clear buffers
    memset(rx_buf, 0, RX_TX_CAPACITY);
    memset(tx_buf, 0, RX_TX_CAPACITY);

    // Copy into buffers
    memcpy(rx_buf, c, strlen(c));
    memcpy(tx_buf, c, strlen(c));
}