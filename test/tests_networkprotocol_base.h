/**
 * NetworkProtocol base class tests
 */

#ifndef TESTS_PROTOCOL_BASE_H
#define TESTS_PROTOCOL_BASE_H

#include <unity.h>
#include <string.h>
#include <../lib/network-protocol/Protocol.h>

/**
 * @brief Set up buffers for base protocol tests
 */
void tests_networkprotocol_base_setup();

/**
 * @brief entrypoint for base protocol tests. Calls all other tests.
 */
void tests_networkprotocol_base();

/**
 * @brief Tear down buffers used by base protocol tests
 */
void tests_networkprotocol_base_done();

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
    void test_networkprotocol_base_rx_cr_to_eol();

    /**
     * Test RX translation: LF to EOL
     */
    void test_networkprotocol_base_rx_lf_to_eol();

    /**
     * Test RX translation: CR/LF to EOL
     * The resulting input buffer should contract to compensate for the removal of LF characters.
     */
    void test_networkprotocol_base_rx_cr_lf_to_eol();

    /**
     * Test TX translation: EOL to CR
     */
    void test_networkprotocol_base_tx_eol_to_cr();

    /**
     * Test TX translation: EOL to LF
     */
    void test_networkprotocol_base_tx_eol_to_lf();

    /**
     * Test TX translation: EOL to CR/LF
     * The resulting output should expand to compensate for the addition of LF characters.
     */
    void test_networkprotocol_base_tx_eol_to_crlf();

}
#endif

#endif /* TESTS_PROTOCOL_BASE_H */