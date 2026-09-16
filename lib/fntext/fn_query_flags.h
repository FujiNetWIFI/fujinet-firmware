/**
 * Query-parameter byte shared by the N: query wrappers (FNJSON, FNHTML, FNXML).
 *
 * One definition rather than three parallel copies, so the device-side
 * validation in NDevice and the parser-side interpretation cannot drift.
 */

#ifndef FN_QUERY_FLAGS_H
#define FN_QUERY_FLAGS_H

#include <stdint.h>

enum fnQueryFlags_t {
    // Low nibble: per-parser character remapping. Not every parser uses every bit.
    FN_QUERY_REMAP_CHARS                 = 0x01,
    FN_QUERY_REMAP_ATASCII_INTERNATIONAL = 0x02,
    FN_QUERY_DELETE_SGML_TAGS            = 0x04, // FNJSON only
    FN_QUERY_FLAGS_MASK                  = 0x07,

    // Bits 4-5: how a queried value is post-processed before it is returned.
    // VERBATIM is 0, so a client that sends no output mode keeps the historical
    // behavior exactly. 0x20 and 0x30 are reserved.
    FN_QUERY_OUTPUT_MASK     = 0x30,
    FN_QUERY_OUTPUT_VERBATIM = 0x00, // bytes exactly as the document had them
    FN_QUERY_OUTPUT_ASCII    = 0x10, // decode entities, transliterate, strip non-ASCII
};

// True for a query param a client may legally send.
static inline bool fn_query_param_is_valid(uint8_t qp)
{
    if (qp & ~(FN_QUERY_FLAGS_MASK | FN_QUERY_OUTPUT_MASK))
        return false;

    uint8_t out = qp & FN_QUERY_OUTPUT_MASK;
    return out == FN_QUERY_OUTPUT_VERBATIM || out == FN_QUERY_OUTPUT_ASCII;
}

#endif /* FN_QUERY_FLAGS_H */
