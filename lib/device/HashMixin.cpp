#include "HashMixin.h"
#include "debug.h"
#include "utils.h"

#ifdef FUJI_HASH_MIXIN_ENABLED

constexpr uint8_t MODE_HEX = 1;

void HashMixin::hash_input(uint16_t len)
{
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

void HashMixin::hash_compute(bool clear_data, Hash::Algorithm algo)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: COMPUTE\n");
    _algorithm = algo;
    hasher.compute(_algorithm, clear_data);
    SYSTEM_BUS.transaction_success();
}

void HashMixin::hash_length(bool as_hex)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: LENGTH\n");
    uint8_t r = hasher.hash_length(_algorithm, as_hex);
    SYSTEM_BUS.transaction_send(&r, 1, false);
}

void HashMixin::hash_output(bool as_hex)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: OUTPUT\n");

    std::vector<uint8_t> hashed_data;
    if (as_hex)
    {
        std::string hex = hasher.output_hex();
        hashed_data.insert(hashed_data.end(), hex.begin(), hex.end());
    }
    else
        hashed_data = hasher.output_binary();
    SYSTEM_BUS.transaction_send(hashed_data.data(), hashed_data.size(), false);
}

void HashMixin::hash_clear()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("HashMixin: CLEAR\n");
    hasher.clear();
    SYSTEM_BUS.transaction_success();
}

bool HashMixin::processCommand(const FUJI_COMMAND_PACKET &packet)
{
    switch (packet.command())
    {
    case FUJICMD_HASH_INPUT:
        hash_input(packet.param(0));
        break;
    case FUJICMD_HASH_COMPUTE:
        hash_compute(true, Hash::to_algorithm(packet.param(0)));
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        hash_compute(false, Hash::to_algorithm(packet.param(0)));
        break;
    case FUJICMD_HASH_LENGTH:
        hash_length(packet.param(0) == MODE_HEX);
        break;
    case FUJICMD_HASH_OUTPUT:
        hash_output(packet.param(0) == MODE_HEX);
        break;
    case FUJICMD_HASH_CLEAR:
        hash_clear();
        break;

    default:
        return false;
    }

    return true;
}

#endif // FUJI_HASH_MIXIN_ENABLED
