/**
 * #FujiNet Tests - NetworkProtocol Translation
 * 
 * This set of tests exercise the translation code that's in the NetworkProtocol base class.
 */

#include <string.h>
#include <string>
#include "../lib/network-protocol/Protocol.h"
#include "test_networkprotocol_translation.h"

/**
 * Buffer sizes
 */
#define RX_TX_SIZE 65535
#define SP_SIZE 256

using namespace std;

/**
 * The Buffers
 */
string *rx_buf;
string *tx_buf;
string *sp_buf;

/**
 * Protocol object
 */
static NetworkProtocol *protocol;

/**
 * Test fixtures
 */
static const char *test_eol = "This is a test string.\x9BThis is a second line.\x9BThis is a third line.\x9B";
static const char *test_cr = "This is a test string.\x0DThis is a second line.\x0DThis is a third line.\x0D";
static const char *test_lf = "This is a test string.\x0AThis is a second line.\x0AThis is a third line.\x0A";
static const char *test_crlf = "This is a test string.\x0D\x0AThis is a second line.\x0D\x0AThis is a third line.\x0D\x0A";

/**
 * Tests entrypoint
 */
void tests_networkprotocol_translation()
{
    RUN_TEST(tests_networkprotocol_translation_rx_cr_to_eol);
    RUN_TEST(tests_networkprotocol_translation_rx_lf_to_eol);
    RUN_TEST(tests_networkprotocol_translation_rx_crlf_to_eol);
    RUN_TEST(tests_networkprotocol_translation_tx_eol_to_cr);
    RUN_TEST(tests_networkprotocol_translation_tx_eol_to_lf);
    RUN_TEST(tests_networkprotocol_translation_tx_eol_to_crlf);
}

/**
 * Test RX CR to EOL
 */
void tests_networkprotocol_translation_rx_cr_to_eol()
{
    cmdFrame_t cmdFrame = {0x71, 'O', 0x0C, 0x01, 0xFF};
    EdUrlParser *url = EdUrlParser::parseUrl("TCP://TCP:1234/");

    tests_networkprotocol_translation_setup(test_cr);

    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_cr));
    protocol->close();

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf->c_str());
    tests_networkprotocol_translation_done();
    delete url;
}

/**
 * Test RX LF to EOL
 */
void tests_networkprotocol_translation_rx_lf_to_eol()
{
    cmdFrame_t cmdFrame = {0x71, 'O', 0x0C, 0x02, 0xFF};
    EdUrlParser *url = EdUrlParser::parseUrl("TCP://TCP:1234/");

    tests_networkprotocol_translation_setup(test_lf);

    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_lf));
    protocol->close();

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf->c_str());
    tests_networkprotocol_translation_done();
    delete url;
}

/**
 * Test RX CR/LF to EOL
 */
void tests_networkprotocol_translation_rx_crlf_to_eol()
{
    cmdFrame_t cmdFrame = {0x71, 'O', 0x0C, 0x03, 0xFF};
    EdUrlParser *url = EdUrlParser::parseUrl("TCP://TCP:1234/");

    tests_networkprotocol_translation_setup(test_crlf);

    protocol->open(url, &cmdFrame);
    protocol->read(strlen(test_crlf));
    protocol->close();

    TEST_ASSERT_EQUAL_STRING(test_eol, rx_buf->c_str());
    tests_networkprotocol_translation_done();
    delete url;
}

/**
 * Test TX EOL to CR
 */
void tests_networkprotocol_translation_tx_eol_to_cr()
{
    cmdFrame_t cmdFrame = {0x71, 'O', 0x0C, 0x01, 0xFF};
    EdUrlParser *url = EdUrlParser::parseUrl("TCP://TCP:1234/");

    tests_networkprotocol_translation_setup(test_eol);

    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    TEST_ASSERT_EQUAL_STRING(test_cr, tx_buf->c_str());
    tests_networkprotocol_translation_done();
    delete url;
}

/**
 * Test TX EOL to LF
 */
void tests_networkprotocol_translation_tx_eol_to_lf()
{
    cmdFrame_t cmdFrame = {0x71, 'O', 0x0C, 0x02, 0xFF};
    EdUrlParser *url = EdUrlParser::parseUrl("TCP://TCP:1234/");

    tests_networkprotocol_translation_setup(test_eol);

    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    TEST_ASSERT_EQUAL_STRING(test_lf, tx_buf->c_str());
    tests_networkprotocol_translation_done();
    delete url;
}

/**
 * Test TX EOL to CR/LF
 */
void tests_networkprotocol_translation_tx_eol_to_crlf()
{
    cmdFrame_t cmdFrame = {0x71, 'O', 0x0C, 0x03, 0xFF};
    EdUrlParser *url = EdUrlParser::parseUrl("TCP://TCP:1234/");

    tests_networkprotocol_translation_setup(test_eol);

    protocol->open(url, &cmdFrame);
    protocol->write(strlen(test_eol));
    protocol->close();

    TEST_ASSERT_EQUAL_STRING(test_crlf, tx_buf->c_str());
    tests_networkprotocol_translation_done();
    delete url;
}

/**
 * Test set-up
 * @param c The test fixture to stuff into the rx/tx buffers.
 * @return TRUE if protocol setup, FALSE if failed.
 */
bool tests_networkprotocol_translation_setup(const char *c)
{
    rx_buf = nullptr;
    tx_buf = nullptr;
    sp_buf = nullptr;

    protocol = new NetworkProtocol(rx_buf, tx_buf, sp_buf);

    if (protocol == nullptr || rx_buf == nullptr || tx_buf == nullptr || sp_buf == nullptr)
        return false;

    // Clear buffers
    rx_buf->clear();
    tx_buf->clear();
    sp_buf->clear();

    // Copy fixture into buffers
    *rx_buf=string(c);
    *tx_buf=string(c);

    return true;
}

/**
 * Test done (tear-down)
 */
void tests_networkprotocol_translation_done()
{
    if (protocol != nullptr)
        delete protocol;

    if (rx_buf != nullptr)
        delete rx_buf;

    if (tx_buf != nullptr)
        delete tx_buf;

    if (sp_buf != nullptr)
        delete sp_buf;
}
