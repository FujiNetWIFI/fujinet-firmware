#ifndef HASHMIXIN_H
#define HASHMIXIN_H

#include "FujiDeviceMixin.h"
#include "hash.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_HASH_MIXIN_ENABLED

class HashMixin : public FujiDeviceMixin
{
private:
    FujiMixinCommandHandlers handlers = {
    };

protected:
    Hash::Algorithm _algorithm = Hash::Algorithm::UNKNOWN;

    FujiMixinCommandHandlers commandHandlers() override { return handlers; }

    void hash_input(const FUJI_COMMAND_PACKET &packet);
    void hash_compute(const FUJI_COMMAND_PACKET &packet);
    void hash_length(const FUJI_COMMAND_PACKET &packet);
    void hash_output(const FUJI_COMMAND_PACKET &packet);
    void hash_clear(const FUJI_COMMAND_PACKET &packet);

public:
    HashMixin() {
        handlers = {
            { FUJICMD_HASH_INPUT,            FM_CMD_HANDLER(hash_input)   },
            { FUJICMD_HASH_COMPUTE,          FM_CMD_HANDLER(hash_compute) },
            { FUJICMD_HASH_COMPUTE_NO_CLEAR, FM_CMD_HANDLER(hash_compute) },
            { FUJICMD_HASH_LENGTH,           FM_CMD_HANDLER(hash_length)  },
            { FUJICMD_HASH_OUTPUT,           FM_CMD_HANDLER(hash_output)  },
            { FUJICMD_HASH_CLEAR,            FM_CMD_HANDLER(hash_clear)   },
        };
    }
};

#endif // FUJI_MIXINS_ENABLED

#endif /* HASHMIXIN_H */
