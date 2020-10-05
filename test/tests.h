#ifndef TEST_NETWORK_TRANSLATION_H
#define TEST_NETWORK_TRANSLATION_H

#include "Protocol.h"

/**
 * Buffer capacities
 */
#define RX_TX_CAPACITY 65535
#define SP_CAPACITY 256

#ifdef __cplusplus
extern "C"
{
    /**
     * The RX buffer
     */
    uint8_t *rx_buf;

    /**
     * The TX buffer
     */
    uint8_t *tx_buf;

    /**
     * The Special buffer
     */
    uint8_t *sp_buf;

    /**
     * Test main
     */
    void app_main();

    /**
     * Test RX translation: CR to EOL
     */
    void test_rx_cr_to_eol();

    /**
     * Test RX translation: LF to EOL
     */
    void test_rx_lf_to_eol();

    /**
     * Test RX translation: CR/LF to EOL
     * The resulting input buffer should contract to compensate for the removal of LF characters.
     */
    void test_rx_cr_lf_to_eol();

    /**
     * Test TX translation: EOL to CR
     */
    void test_tx_eol_to_cr();

    /**
     * Test TX translation: EOL to LF
     */
    void test_tx_eol_to_lf();

    /**
 * Test TX translation: EOL to CR/LF
 * The resulting output should expand to compensate for the addition of LF characters.
 */
    void test_tx_eol_to_crlf();

    /**
 * Instantiated protocol object
 */
    NetworkProtocol *protocol;

    /**
 * Set up buffers given a source fixture
 * @param c the buffer to put into the protocol buffers.
 */
    void buffer_setup(const char *c);

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
}
#endif

#endif /* TEST_NETWORK_TRANSLATION_H */