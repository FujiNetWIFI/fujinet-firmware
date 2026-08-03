#include "Base64Mixin.h"
#include "base64.h"
#include "debug.h"

#ifdef FUJI_BASE64_MIXIN_ENABLED

void Base64Mixin::encode_input(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    Debug_printf("Base64Mixin: enode_input\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        SYSTEM_BUS.transaction_error();
        return;
    }

    std::string p(len, 0);
    SYSTEM_BUS.transaction_get(p.data(), len);
    base64.base64_buffer += p;
    SYSTEM_BUS.transaction_success();
}

void Base64Mixin::encode_compute(const FUJI_COMMAND_PACKET &packet)
{
    size_t out_len;

    /* ACK before CPU work (matches sio_hash_compute); NetSIO/tight SIO timing
     * otherwise leaves the host waiting past dtimlo (Atari status 138 timeout). */
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("Base64Mixin: ENCODE COMPUTE\n");

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    SYSTEM_BUS.transaction_success();
}

void Base64Mixin::encode_length(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("Base64Mixin: ENCODE LENGTH\n");

    u32ne_t len;
    len = base64.base64_buffer.length();
    Debug_printf("base64 buffer length: %u bytes\n", (size_t) len);

    SYSTEM_BUS.transaction_send(&len, sizeof(len), false);
}

void Base64Mixin::encode_output(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("Base64Mixin: ENCODE OUTPUT\n");

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        SYSTEM_BUS.transaction_error();
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    std::string result = base64.base64_buffer.substr(0, len);
    result = SYSTEM_BUS.unicodeTextToNative(result);
    SYSTEM_BUS.transaction_send(result);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();
}

void Base64Mixin::decode_input(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    Debug_printf("Base64Mixin: DECODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        SYSTEM_BUS.transaction_error();
        return;
    }

    std::string p(len, 0);
    SYSTEM_BUS.transaction_get(p.data(), p.size());
    p = SYSTEM_BUS.nativeTextToUnicode(p);
    base64.base64_buffer += p;
    SYSTEM_BUS.transaction_success();
}

void Base64Mixin::decode_compute(const FUJI_COMMAND_PACKET &packet)
{
    size_t out_len;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("Base64Mixin: DECODE COMPUTE\n");

    std::unique_ptr<unsigned char[]> p = Base64::decode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string((const char *)p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    SYSTEM_BUS.transaction_success();
}

void Base64Mixin::decode_length(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("Base64Mixin: DECODE LENGTH\n");

    u32ne_t len;
    len = base64.base64_buffer.length();
    Debug_printf("base64 buffer length: %u bytes\n", (size_t) len);

    SYSTEM_BUS.transaction_send(&len, sizeof(len), false);
}

void Base64Mixin::decode_output(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("Base64Mixin: DECODE OUTPUT\n");

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        SYSTEM_BUS.transaction_error();
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    std::vector<unsigned char> p(len);
    memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();
    SYSTEM_BUS.transaction_send(p.data(), len, false);
}

#endif // FUJI_BASE64_MIXIN_ENABLED
