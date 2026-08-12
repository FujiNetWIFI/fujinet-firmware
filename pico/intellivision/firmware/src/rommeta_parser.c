#include "rommeta_parser.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* --------------------------------------------------------------------------
 * Internal fixed-buffer helpers
 * -------------------------------------------------------------------------- */
static void rm_set_empty(char out[ROMMETA_STR_SIZE])
{
    out[0] = '\0';
}

static void rm_copy_trunc(char out[ROMMETA_STR_SIZE], const char *src)
{
    size_t i;

    if (!out)
        return;

    if (!src)
    {
        out[0] = '\0';
        return;
    }

    for (i = 0; i < (ROMMETA_STR_SIZE - 1U) && src[i] != '\0'; ++i)
        out[i] = src[i];

    out[i] = '\0';
}

static void rm_append_trunc(char out[ROMMETA_STR_SIZE], const char *suffix)
{
    size_t cur = 0U;
    size_t i   = 0U;

    while (cur < (ROMMETA_STR_SIZE - 1U) && out[cur] != '\0')
        cur++;

    while (cur < (ROMMETA_STR_SIZE - 1U) && suffix[i] != '\0')
        out[cur++] = suffix[i++];

    out[cur] = '\0';
}

static void rm_append_char_trunc(char out[ROMMETA_STR_SIZE], char c)
{
    size_t cur = 0U;

    while (cur < (ROMMETA_STR_SIZE - 1U) && out[cur] != '\0')
        cur++;

    if (cur < (ROMMETA_STR_SIZE - 1U))
    {
        out[cur++] = c;
        out[cur] = '\0';
    }
}

static void rm_format_trunc(char out[ROMMETA_STR_SIZE], const char *fmt, ...)
{
    va_list ap;
    int rc;

    if (!out || !fmt)
        return;

    va_start(ap, fmt);
    rc = vsnprintf(out, ROMMETA_STR_SIZE, fmt, ap);
    va_end(ap);

    if (rc < 0)
        out[0] = '\0';
}

/* --------------------------------------------------------------------------
 * Low-level sequential helpers
 * -------------------------------------------------------------------------- */
static int rm_read_exact(vfs_file_t *f, void *buf, size_t len)
{
    if (len == 0U)
        return 1;

    return vfs_read(f, buf, len) == (int)len;
}

static int rm_read_u8(vfs_file_t *f, uint8_t *out)
{
    return rm_read_exact(f, out, 1U);
}

/* Skip bytes by reading/discarding small chunks. */
static int rm_skip_exact(vfs_file_t *f, uint32_t len)
{
    uint8_t tmp[32];

    while (len > 0U)
    {
        uint32_t chunk = (len > (uint32_t)sizeof(tmp)) ? (uint32_t)sizeof(tmp) : len;

        if (!rm_read_exact(f, tmp, (size_t)chunk))
            return 0;

        len -= chunk;
    }

    return 1;
}

/* --------------------------------------------------------------------------
 * Public init
 * -------------------------------------------------------------------------- */
void rommeta_parser_init(rommeta_parser_t *ctx)
{
    if (!ctx)
        return;

    memset(ctx, 0, sizeof(*ctx));
}

/* --------------------------------------------------------------------------
 * Static mappings
 * -------------------------------------------------------------------------- */
static const char *rm_tag_key(uint8_t type)
{
    switch (type)
    {
        case 0x01: return "Title";
        case 0x02: return "Publisher";
        case 0x04: return "RelatedInfo";
        case 0x05: return "ReleaseDate";
        case 0x08: return "ShortTitle";
        case 0x09: return "License";
        case 0x0A: return "Description";
        case 0x0B: return "BuildDate";
        case 0x0C: return "Version";
        default:   return "";
    }
}

static const char *rm_publisher_name(uint8_t id)
{
    switch (id)
    {
        case 0x00: return "Mattel Electronics";
        case 0x01: return "INTV Corporation";
        case 0x02: return "Imagic";
        case 0x03: return "Activision";
        case 0x04: return "Atarisoft";
        case 0x05: return "Coleco";
        case 0x06: return "CBS";
        case 0x07: return "Parker Brothers";
        case 0x08: return "Sears";
        case 0x09: return "Sega";
        case 0x0A: return "Nintendo";
        case 0x0B: return "Interphase";
        case 0x0C: return "Digiplay";
        case 0x0D: return "Dextell";
        case 0x0E: return "Intellivision, Inc.";
        case 0xFF: return "Other";
        default:   return NULL;
    }
}

static const char *rm_compat_value(unsigned v)
{
    switch (v & 0x3U)
    {
        case 0U: return "dontcare";
        case 1U: return "supports";
        case 2U: return "requires";
        case 3U: return "incompat";
        default: return "?";
    }
}

static const char *rm_jlp_value(unsigned mode)
{
    switch (mode & 0x3U)
    {
        case 0U: return "disabled";
        case 1U: return "on";
        case 2U: return "off+flash";
        case 3U: return "on+flash";
        default: return "?";
    }
}

/* Credits responsibility -> key mapping */
static uint8_t rm_first_credit_flag(uint8_t flags)
{
    static const uint8_t order[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
    size_t i;

    for (i = 0U; i < (sizeof(order) / sizeof(order[0])); ++i)
    {
        if ((flags & order[i]) != 0U)
            return order[i];
    }

    return 0U;
}

static const char *rm_credit_key_from_flag(uint8_t flag)
{
    switch (flag)
    {
        case 0x01: return "author";
        case 0x02: return "game_art_by";
        case 0x04: return "music_by";
        case 0x08: return "sfx_by";
        case 0x10: return "voices_by";
        case 0x20: return "docs_by";
        case 0x40: return "concept_by";
        case 0x80: return "box_art_by";
        default:   return "";
    }
}

/* --------------------------------------------------------------------------
 * Read variable-length body size
 *
 * Returns:
 *   1 => success
 *   2 => NUL tag reached
 *   0 => error
 * -------------------------------------------------------------------------- */
static int rm_read_body_length(vfs_file_t *f, uint32_t *out_len)
{
    uint8_t b0;
    uint32_t len;
    uint8_t extra;
    uint8_t i;

    if (!out_len)
        return 0;

    if (!rm_read_u8(f, &b0))
        return 0;

    /* NUL tag: one zero byte only */
    if (b0 == 0x00U)
        return 2;

    len   = (uint32_t)(b0 & 0x3FU);
    extra = (uint8_t)(b0 >> 6);  /* 0..3 extra bytes */

    for (i = 0U; i < extra; ++i)
    {
        uint8_t bx;

        if (!rm_read_u8(f, &bx))
            return 0;

        len |= ((uint32_t)bx) << (6U + 8U * i);
    }

    *out_len = len;
    return 1;
}

/* --------------------------------------------------------------------------
 * Emit pending compatibility key/value pairs
 *
 * Order:
 *   ecs, voice, keybd, inty2, [jlp], [ltom], [jlp_flash]
 * -------------------------------------------------------------------------- */
static int rm_emit_pending_compat(rommeta_parser_t *ctx,
                                  char out_key[ROMMETA_STR_SIZE],
                                  char out_value[ROMMETA_STR_SIZE])
{
    while (ctx->compat_pending != 0U)
    {
        switch (ctx->compat_index++)
        {
            case 0U:
                rm_copy_trunc(out_key, "ecs");
                rm_copy_trunc(out_value, rm_compat_value(ctx->compat_ecs));
                return 1;

            case 1U:
                rm_copy_trunc(out_key, "voice");
                rm_copy_trunc(out_value, rm_compat_value(ctx->compat_voice));
                return 1;

            case 2U:
                rm_copy_trunc(out_key, "keybd");
                rm_copy_trunc(out_value, rm_compat_value(ctx->compat_keybd));
                return 1;

            case 3U:
                rm_copy_trunc(out_key, "inty2");
                rm_copy_trunc(out_value, rm_compat_value(ctx->compat_inty2));
                return 1;

            case 4U:
                if (ctx->compat_has_jlp != 0U)
                {
                    rm_copy_trunc(out_key, "jlp");
                    rm_copy_trunc(out_value, rm_jlp_value(ctx->compat_jlp));
                    return 1;
                }
                break;

            case 5U:
                if (ctx->compat_has_ltom != 0U)
                {
                    rm_copy_trunc(out_key, "ltom");
                    rm_copy_trunc(out_value, (ctx->compat_ltom != 0U) ? "enabled" : "disabled");
                    return 1;
                }
                break;

            case 6U:
                if (ctx->compat_has_jlp_flash != 0U)
                {
                    rm_copy_trunc(out_key, "jlp_flash");
                    rm_format_trunc(out_value, "%u", (unsigned)ctx->compat_jlp_flash);
                    ctx->compat_pending = 0U;
                    return 1;
                }
                break;

            default:
                ctx->compat_pending = 0U;
                break;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Compatibility tag reader
 * Reads only needed bytes, discards any extra bytes, then consumes CRC.
 * -------------------------------------------------------------------------- */
static int rm_read_compat_tag(rommeta_parser_t *ctx, vfs_file_t *f, uint32_t body_len)
{
    uint8_t b0, b1, b2;
    uint8_t b3 = 0U;
    uint8_t b4 = 0U;
    uint32_t remaining;
    uint16_t flash_hi;

    if (body_len < 3U)
    {
        /* invalid compatibility tag -> discard body + CRC */
        return rm_skip_exact(f, body_len + 2U);
    }

    if (!rm_read_u8(f, &b0)) return 0;
    if (!rm_read_u8(f, &b1)) return 0;
    if (!rm_read_u8(f, &b2)) return 0;
    (void)b2; /* reserved */

    remaining = body_len - 3U;

    ctx->compat_pending = 1U;
    ctx->compat_index   = 0U;

    ctx->compat_has_jlp       = 0U;
    ctx->compat_has_ltom      = 0U;
    ctx->compat_has_jlp_flash = 0U;

    ctx->compat_ecs   = (uint8_t)((b0 >> 6) & 0x03U);
    ctx->compat_voice = (uint8_t)((b0 >> 2) & 0x03U);
    ctx->compat_keybd = (uint8_t)((b0 >> 0) & 0x03U);
    ctx->compat_inty2 = (uint8_t)((b1 >> 0) & 0x03U);

    if (remaining >= 1U)
    {
        if (!rm_read_u8(f, &b3)) return 0;
        remaining--;

        ctx->compat_has_jlp  = 1U;
        ctx->compat_has_ltom = 1U;

        ctx->compat_jlp  = (uint8_t)((b3 >> 6) & 0x03U);
        ctx->compat_ltom = (uint8_t)((b3 >> 5) & 0x01U);

        flash_hi = (uint16_t)(b3 & 0x03U);

        if (remaining >= 1U)
        {
            if (!rm_read_u8(f, &b4)) return 0;
            remaining--;

            ctx->compat_has_jlp_flash = 1U;
            ctx->compat_jlp_flash     = (uint16_t)((flash_hi << 8) | b4);
        }
    }

    if (!rm_skip_exact(f, remaining))
        return 0;

    /* consume CRC */
    if (!rm_skip_exact(f, 2U))
        return 0;

    return 1;
}

/* --------------------------------------------------------------------------
 * Credits parser
 *
 * Operates entirely sequentially.
 * Stores only:
 *   - remaining bytes in current credits tag body
 *   - current decoded name
 *   - remaining responsibility flags to emit
 *
 * Once the body is exhausted, CRC is consumed and credits_pending is cleared.
 * -------------------------------------------------------------------------- */
static int rm_emit_pending_credits(rommeta_parser_t *ctx,
                                   vfs_file_t *f,
                                   char out_key[ROMMETA_STR_SIZE],
                                   char out_value[ROMMETA_STR_SIZE])
{
    while (ctx->credits_pending != 0U)
    {
        /* Still have roles from current record to emit */
        if (ctx->credits_flags_remaining != 0U)
        {
            uint8_t flag = rm_first_credit_flag(ctx->credits_flags_remaining);

            if (flag != 0U)
            {
                ctx->credits_flags_remaining &= (uint8_t)~flag;
                rm_copy_trunc(out_key, rm_credit_key_from_flag(flag));
                rm_copy_trunc(out_value, ctx->credits_name);
                return 1;
            }

            ctx->credits_flags_remaining = 0U;
        }

        /* End of body => consume CRC and finish this tag */
        if (ctx->credits_remaining_body == 0U)
        {
            if (!rm_skip_exact(f, 2U))
                return -1;

            ctx->credits_pending         = 0U;
            ctx->credits_flags_remaining = 0U;
            rm_set_empty(ctx->credits_name);
            return 0;
        }

        /* Parse next record: [flags][name] */
        {
            uint8_t flags;
            uint8_t first;

            if (ctx->credits_remaining_body < 1U)
            {
                if (!rm_skip_exact(f, 2U))
                    return -1;
                ctx->credits_pending = 0U;
                return 0;
            }

            if (!rm_read_u8(f, &flags))
                return -1;
            ctx->credits_remaining_body--;

            if (ctx->credits_remaining_body < 1U)
            {
                /* truncated record: body ended after flags */
                if (!rm_skip_exact(f, 2U))
                    return -1;
                ctx->credits_pending = 0U;
                return 0;
            }

            rm_set_empty(ctx->credits_name);

            if (!rm_read_u8(f, &first))
                return -1;
            ctx->credits_remaining_body--;

            /* Case 1: coded name byte 0x80..0xFF */
            if (first >= 0x80U)
            {
                rm_format_trunc(ctx->credits_name, "0x%02X", first);
            }
            else
            {
                /* Case 2: NUL-terminated UTF-8 string with 0x01 escaping */
                uint8_t c = first;

                for (;;)
                {
                    if (c == 0x00U)
                        break;

                    if (c == 0x01U)
                    {
                        if (ctx->credits_remaining_body == 0U)
                            break; /* truncated escape */

                        if (!rm_read_u8(f, &c))
                            return -1;
                        ctx->credits_remaining_body--;
                    }

                    rm_append_char_trunc(ctx->credits_name, (char)c);

                    if (ctx->credits_remaining_body == 0U)
                        break; /* truncated unterminated string */

                    if (!rm_read_u8(f, &c))
                        return -1;
                    ctx->credits_remaining_body--;
                }
            }

            ctx->credits_flags_remaining = flags;

            /* Ignore flagless record */
            if (ctx->credits_flags_remaining == 0U)
                continue;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Start a credits tag
 * -------------------------------------------------------------------------- */
static void rm_start_credits_tag(rommeta_parser_t *ctx, uint32_t body_len)
{
    ctx->credits_pending         = 1U;
    ctx->credits_remaining_body  = body_len;
    ctx->credits_flags_remaining = 0U;
    rm_set_empty(ctx->credits_name);
}

/* --------------------------------------------------------------------------
 * Direct-output tag readers (streaming, no body buffer)
 * -------------------------------------------------------------------------- */
static int rm_read_trunc_string_and_crc(vfs_file_t *f,
                                        uint32_t body_len,
                                        char out[ROMMETA_STR_SIZE])
{
    uint8_t c;
    uint32_t i;

    rm_set_empty(out);

    for (i = 0U; i < body_len; ++i)
    {
        if (!rm_read_u8(f, &c))
            return 0;

        if (i < (ROMMETA_STR_SIZE - 1U))
            out[i] = (char)c;
    }

    if (body_len < (ROMMETA_STR_SIZE - 1U))
        out[body_len] = '\0';
    else
        out[ROMMETA_STR_SIZE - 1U] = '\0';

    return rm_skip_exact(f, 2U);
}

static int rm_read_publisher_and_crc(vfs_file_t *f,
                                     uint32_t body_len,
                                     char out[ROMMETA_STR_SIZE])
{
    uint8_t code;
    const char *name;
    uint32_t remaining;

    rm_set_empty(out);

    if (body_len < 1U)
        return rm_skip_exact(f, 2U);

    if (!rm_read_u8(f, &code))
        return 0;

    remaining = body_len - 1U;
    name = rm_publisher_name(code);

    if (code == 0xFFU)
    {
        rm_copy_trunc(out, "Other");

        if (remaining > 0U)
        {
            uint8_t c;
            rm_append_trunc(out, ":");
            rm_append_trunc(out, " ");

            while (remaining-- > 0U)
            {
                if (!rm_read_u8(f, &c))
                    return 0;
                rm_append_char_trunc(out, (char)c);
            }
        }
    }
    else
    {
        if (name)
            rm_copy_trunc(out, name);
        else
            rm_format_trunc(out, "Pub 0x%02X", code);

        if (!rm_skip_exact(f, remaining))
            return 0;
    }

    return rm_skip_exact(f, 2U);
}

static int rm_read_date_and_crc(vfs_file_t *f,
                                uint32_t body_len,
                                char out[ROMMETA_STR_SIZE])
{
    uint8_t bytes[8];
    uint32_t i, n;
    char tmp[ROMMETA_STR_SIZE];
    int year;

    rm_set_empty(out);

    if (body_len < 1U)
        return rm_skip_exact(f, 2U);

    n = (body_len > 8U) ? 8U : body_len;

    for (i = 0U; i < n; ++i)
    {
        if (!rm_read_u8(f, &bytes[i]))
            return 0;
    }

    if (body_len > n)
    {
        if (!rm_skip_exact(f, body_len - n))
            return 0;
    }

    year = 1900 + bytes[0];
    rm_format_trunc(out, "%04d", year);

    if (n >= 2U) { rm_format_trunc(tmp, "%s-%02u", out, bytes[1]); rm_copy_trunc(out, tmp); }
    if (n >= 3U) { rm_format_trunc(tmp, "%s-%02u", out, bytes[2]); rm_copy_trunc(out, tmp); }
    if (n >= 4U) { rm_format_trunc(tmp, "%s %02u", out, bytes[3]); rm_copy_trunc(out, tmp); }
    if (n >= 5U) { rm_format_trunc(tmp, "%s:%02u", out, bytes[4]); rm_copy_trunc(out, tmp); }
    if (n >= 6U) { rm_format_trunc(tmp, "%s:%02u", out, bytes[5]); rm_copy_trunc(out, tmp); }

    return rm_skip_exact(f, 2U);
}

/* --------------------------------------------------------------------------
 * Public parser entry point
 *
 * Behavior:
 *   - emits pending credits items first
 *   - emits pending compatibility items next
 *   - skips Ignore tags (type 0x00)
 *   - skips Extended tags (0xF0..0xFF)
 *   - skips unsupported tags
 *   - fully sequential: uses only vfs_read()
 * -------------------------------------------------------------------------- */
rommeta_status_t rommeta_read_next(rommeta_parser_t *ctx,
                                   vfs_file_t *f,
                                   char out_key[ROMMETA_STR_SIZE],
                                   char out_value[ROMMETA_STR_SIZE])
{
    uint32_t body_len;
    uint8_t type;
    int rc;

    if (!ctx || !f || !out_key || !out_value)
        return ROMMETA_ERR;

    rm_set_empty(out_key);
    rm_set_empty(out_value);

    /* 1) Emit pending credits outputs */
    rc = rm_emit_pending_credits(ctx, f, out_key, out_value);
    if (rc < 0)
    {
        rm_copy_trunc(out_key, "Error");
        rm_copy_trunc(out_value, "Credits failed");
        return ROMMETA_ERR;
    }
    if (rc > 0)
        return ROMMETA_OK;

    /* 2) Emit pending compatibility outputs */
    if (rm_emit_pending_compat(ctx, out_key, out_value))
        return ROMMETA_OK;

    /* 3) Parse next physical tag */
    for (;;)
    {
        rc = rm_read_body_length(f, &body_len);
        if (rc == 2)
            return ROMMETA_END;

        if (rc != 1)
        {
            rm_copy_trunc(out_key, "Error");
            rm_copy_trunc(out_value, "Read len failed");
            return ROMMETA_ERR;
        }

        if (!rm_read_u8(f, &type))
        {
            rm_copy_trunc(out_key, "Error");
            rm_copy_trunc(out_value, "Read type failed");
            return ROMMETA_ERR;
        }

        /* skip explicit ignore tag type */
        if (type == 0x00U)
        {
            if (!rm_skip_exact(f, body_len + 2U))
            {
                rm_copy_trunc(out_key, "Error");
                rm_copy_trunc(out_value, "Skip ignore fail");
                return ROMMETA_ERR;
            }
            continue;
        }

        /* skip extended tags */
        if (type >= 0xF0U && type <= 0xFFU)
        {
            if (!rm_skip_exact(f, body_len + 2U))
            {
                rm_copy_trunc(out_key, "Error");
                rm_copy_trunc(out_value, "Skip ext failed");
                return ROMMETA_ERR;
            }
            continue;
        }

        /* tag 0x03: credits => one KEY/VALUE pair per role/developer */
        if (type == 0x03U)
        {
            rm_start_credits_tag(ctx, body_len);

            rc = rm_emit_pending_credits(ctx, f, out_key, out_value);
            if (rc < 0)
            {
                rm_copy_trunc(out_key, "Error");
                rm_copy_trunc(out_value, "Credits parse fail");
                return ROMMETA_ERR;
            }
            if (rc > 0)
                return ROMMETA_OK;

            continue;
        }

        /* tag 0x06: compatibility => one KEY/VALUE pair per flag */
        if (type == 0x06U)
        {
            if (!rm_read_compat_tag(ctx, f, body_len))
            {
                rm_copy_trunc(out_key, "Error");
                rm_copy_trunc(out_value, "Compat read fail");
                return ROMMETA_ERR;
            }

            if (rm_emit_pending_compat(ctx, out_key, out_value))
                return ROMMETA_OK;

            continue;
        }

        /* Supported direct-output tags */
        switch (type)
        {
            case 0x01U: /* Title */
            case 0x04U: /* RelatedInfo */
            case 0x08U: /* ShortTitle */
            case 0x09U: /* License */
            case 0x0AU: /* Description */
            case 0x0CU: /* Version */
                rm_copy_trunc(out_key, rm_tag_key(type));
                if (!rm_read_trunc_string_and_crc(f, body_len, out_value))
                {
                    rm_copy_trunc(out_key, "Error");
                    rm_copy_trunc(out_value, "String read fail");
                    return ROMMETA_ERR;
                }
                return ROMMETA_OK;

            case 0x02U: /* Publisher */
                rm_copy_trunc(out_key, rm_tag_key(type));
                if (!rm_read_publisher_and_crc(f, body_len, out_value))
                {
                    rm_copy_trunc(out_key, "Error");
                    rm_copy_trunc(out_value, "Publisher failed");
                    return ROMMETA_ERR;
                }
                return ROMMETA_OK;

            case 0x05U: /* ReleaseDate */
            case 0x0BU: /* BuildDate */
                rm_copy_trunc(out_key, rm_tag_key(type));
                if (!rm_read_date_and_crc(f, body_len, out_value))
                {
                    rm_copy_trunc(out_key, "Error");
                    rm_copy_trunc(out_value, "Date read failed");
                    return ROMMETA_ERR;
                }
                return ROMMETA_OK;

            default:
                /* unsupported/default tags => ignore completely */
                if (!rm_skip_exact(f, body_len + 2U))
                {
                    rm_copy_trunc(out_key, "Error");
                    rm_copy_trunc(out_value, "Skip tag failed");
                    return ROMMETA_ERR;
                }
                continue;
        }
    }
}
