#include "HashMixin.h"
#include "debug.h"
#include "utils.h"

constexpr uint8_t MODE_HEX = 1;

void HashMixin::hash_input(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    Debug_printf("HashMixin: INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        SYSTEM_BUS.transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    SYSTEM_BUS.transaction_get(p.data(), len);
    hasher.add_data(p);
    SYSTEM_BUS.transaction_success();
}

void HashMixin::hash_compute(const FUJI_COMMAND_PACKET &packet)
{
    Hash::Algorithm algo = Hash::to_algorithm(packet.param(0));
    bool clear_data = packet.command() == CMD::FUJI_HASH_COMPUTE;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: COMPUTE\n");
    _algorithm = algo;
    hasher.compute(_algorithm, clear_data);
    SYSTEM_BUS.transaction_success();
}

void HashMixin::hash_length(const FUJI_COMMAND_PACKET &packet)
{
    bool as_hex = packet.param(0) == MODE_HEX;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: LENGTH\n");
    uint8_t r = hasher.hash_length(_algorithm, as_hex);
    SYSTEM_BUS.transaction_send(&r, 1, false);
}

void HashMixin::hash_output(const FUJI_COMMAND_PACKET &packet)
{
    bool as_hex = packet.param(0) == MODE_HEX;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: OUTPUT\n");

    std::vector<uint8_t> hashed_data;
    if (as_hex)
    {
        std::string hex = hasher.output_hex();
        hex = SYSTEM_BUS.unicodeTextToNative(hex);
        hashed_data.insert(hashed_data.end(), hex.begin(), hex.end());
    }
    else
        hashed_data = hasher.output_binary();
    SYSTEM_BUS.transaction_send(hashed_data.data(), hashed_data.size(), false);
}

void HashMixin::hash_clear(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: CLEAR\n");
    hasher.clear();
    SYSTEM_BUS.transaction_success();
}
