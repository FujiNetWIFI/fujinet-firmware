#ifndef HASHMIXIN_H
#define HASHMIXIN_H

#include "FujiDeviceMixin.h"
#include "hash.h"

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
            { CMD::FUJI_HASH_INPUT,            FM_CMD_HANDLER(hash_input)   },
            { CMD::FUJI_HASH_COMPUTE,          FM_CMD_HANDLER(hash_compute) },
            { CMD::FUJI_HASH_COMPUTE_NO_CLEAR, FM_CMD_HANDLER(hash_compute) },
            { CMD::FUJI_HASH_LENGTH,           FM_CMD_HANDLER(hash_length)  },
            { CMD::FUJI_HASH_OUTPUT,           FM_CMD_HANDLER(hash_output)  },
            { CMD::FUJI_HASH_CLEAR,            FM_CMD_HANDLER(hash_clear)   },
        };
    }
};

#endif /* HASHMIXIN_H */
