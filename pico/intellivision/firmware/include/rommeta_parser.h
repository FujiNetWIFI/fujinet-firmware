#ifndef ROMMETA_PARSER_H
#define ROMMETA_PARSER_H

#include <stdint.h>
#include "vfs.h"

/* --------------------------------------------------------------------------
 * Fixed output buffer size used by the parser.
 *
 * The parser writes at most ROMMETA_STR_SIZE - 1 visible characters and
 * always NUL-terminates the output strings.
 * -------------------------------------------------------------------------- */
#define ROMMETA_STR_SIZE 20

/* --------------------------------------------------------------------------
 * Parser return codes
 * -------------------------------------------------------------------------- */
typedef enum
{
    ROMMETA_ERR = -1,  /* parsing or I/O error */
    ROMMETA_END =  0,  /* NUL tag reached: end of metadata list */
    ROMMETA_OK  =  1   /* one logical key/value pair returned */
} rommeta_status_t;

/* --------------------------------------------------------------------------
 * Parser context
 *
 * This structure stores the parser state between calls so that one physical
 * metadata tag may yield several logical key/value pairs across successive
 * calls (notably tag 0x03 Credits and tag 0x06 Compatibility).
 *
 * The caller must not modify the fields directly.
 * Initialize it with rommeta_parser_init() before first use.
 * -------------------------------------------------------------------------- */
typedef struct
{
    /* ---------- pending compatibility outputs (tag 0x06) ---------- */
    uint8_t  compat_pending;
    uint8_t  compat_index;

    uint8_t  compat_has_jlp;
    uint8_t  compat_has_ltom;
    uint8_t  compat_has_jlp_flash;

    uint8_t  compat_ecs;
    uint8_t  compat_voice;
    uint8_t  compat_keybd;
    uint8_t  compat_inty2;

    uint8_t  compat_jlp;
    uint8_t  compat_ltom;
    uint16_t compat_jlp_flash;

    /* ---------- pending credits outputs (tag 0x03) ---------- */
    uint8_t  credits_pending;
    uint32_t credits_remaining_body;
    uint8_t  credits_flags_remaining;
    char     credits_name[ROMMETA_STR_SIZE];
} rommeta_parser_t;

/* --------------------------------------------------------------------------
 * Initialize a parser context before first use.
 * -------------------------------------------------------------------------- */
void rommeta_parser_init(rommeta_parser_t *ctx);

/* --------------------------------------------------------------------------
 * Read the next logical metadata key/value pair from the current file position.
 *
 * This function is strictly forward-only:
 *   - it uses only sequential reads from the file
 *   - it does not require seek/tell support
 *   - it must be the only reader consuming this file while parsing metadata
 *
 * Behavior:
 *   - returns one KEY/VALUE pair at a time
 *   - skips Ignore tags (type 0x00)
 *   - skips Extended tags (0xF0..0xFF)
 *   - skips unsupported tags
 *   - expands tag 0x03 (Credits) into one pair per responsibility/developer
 *   - expands tag 0x06 (Compatibility) into one pair per individual flag
 *
 * Inputs:
 *   ctx       Parser context initialized with rommeta_parser_init()
 *   f         Open VFS file positioned at the first metadata tag
 *
 * Outputs:
 *   out_key   Caller-provided buffer of ROMMETA_STR_SIZE chars
 *   out_value Caller-provided buffer of ROMMETA_STR_SIZE chars
 *
 * Output guarantees:
 *   - both buffers are always NUL-terminated on return
 *   - strings are truncated if needed to fit in ROMMETA_STR_SIZE chars
 *
 * Return values:
 *   ROMMETA_OK   One logical key/value pair was produced
 *   ROMMETA_END  End of metadata (NUL tag) reached
 *   ROMMETA_ERR  I/O or format error
 * -------------------------------------------------------------------------- */
rommeta_status_t rommeta_read_next(rommeta_parser_t *ctx,
                                   vfs_file_t *f,
                                   char out_key[ROMMETA_STR_SIZE],
                                   char out_value[ROMMETA_STR_SIZE]);


#endif /* ROMMETA_PARSER_H */