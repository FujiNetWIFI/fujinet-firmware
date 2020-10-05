#include <unity.h>
#include <string.h>
#include "tests.h"

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
    RUN_TEST(test_rx_cr_to_eol);
    RUN_TEST(test_rx_lf_to_eol);
    RUN_TEST(test_rx_cr_lf_to_eol);
    RUN_TEST(test_tx_eol_to_cr);
    RUN_TEST(test_tx_eol_to_lf);
    RUN_TEST(test_tx_eol_to_crlf);

    // Why did this cause a puke?
    // free(rx_buf);
    // free(tx_buf);
    // free(sp_buf);
    UNITY_END();
}

/**
 * Test RX translation: CR to EOL
 */
void test_rx_cr_to_eol()
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
void test_rx_lf_to_eol()
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
void test_rx_cr_lf_to_eol()
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
void test_tx_eol_to_cr()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x01,0x00}; // aux2 0x01 is CR
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    delete url;

    TEST_ASSERT_EQUAL_STRING(test_cr, tx_buf);
}

/**
 * Test TX translation: EOL to LF
 */
void test_tx_eol_to_lf()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x02,0x00}; // aux2 0x02 is LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_eol);
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
void test_tx_eol_to_crlf()
{
    cmdFrame_t cmdFrame = {0x71, 'R', 0x0C, 0x03,0x00}; // aux2 0x03 is CR/LF
    EdUrlParser *url = EdUrlParser::parseUrl("HTTP://DUMMYHOST.COM/");

    buffer_setup(test_eol);
    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
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