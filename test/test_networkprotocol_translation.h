/**
 * #FujiNet Tests - NetworkProtocol Translation
 * 
 * This set of tests exercise the translation code that's in the NetworkProtocol base class.
 */

#ifndef TEST_NETWORKPROTOCOL_TRANSLATION_H
#define TEST_NETWORKPROTOCOL_TRANSLATION_H

#define UNIT_TESTS

#include <unity.h>
#include <stdint.h>

#ifdef __cplusplus

extern "C"
{
    /**
     * Tests entrypoint
     */
    void tests_networkprotocol_translation();

    /**
     * Test RX CR to EOL
     */
    void tests_networkprotocol_translation_rx_cr_to_eol();

    /**
     * Test RX LF to EOL
     */
    void tests_networkprotocol_translation_rx_lf_to_eol();

    /**
     * Test RX CR/LF to EOL
     */
    void tests_networkprotocol_translation_rx_crlf_to_eol();

    /**
     * Test TX EOL to CR
     */
    void tests_networkprotocol_translation_tx_eol_to_cr();

    /**
     * Test TX EOL to LF
     */
    void tests_networkprotocol_translation_tx_eol_to_lf();

    /**
     * Test TX EOL to CR/LF
     */
    void tests_networkprotocol_translation_tx_eol_to_crlf();

    /**
     * Test set-up
     * @param c The test fixture to stuff into the buffer.
     * @return TRUE if successful, FALSE if failed.
     */
    bool tests_networkprotocol_translation_setup(const char *c);

    /**
     * Test done (tear-down)
     */
    void tests_networkprotocol_translation_done();
}

#endif /* __cplusplus */

#endif /* TEST_NETWORKPROTOCOL_TRANSLATION_H */