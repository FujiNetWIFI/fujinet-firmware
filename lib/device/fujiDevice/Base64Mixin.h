#ifndef BASE64MIXIN_H
#define BASE64MIXIN_H

#include "FujiDeviceMixin.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_BASE64_MIXIN_ENABLED

class Base64Mixin : public FujiDeviceMixin
{
private:
    FujiMixinCommandHandlers handlers;

protected:
    FujiMixinCommandHandlers commandHandlers() override { return handlers; }

    void encode_input(const FUJI_COMMAND_PACKET &packet);
    void encode_compute(const FUJI_COMMAND_PACKET &packet);
    void encode_length(const FUJI_COMMAND_PACKET &packet);
    void encode_output(const FUJI_COMMAND_PACKET &packet);
    void decode_input(const FUJI_COMMAND_PACKET &packet);
    void decode_compute(const FUJI_COMMAND_PACKET &packet);
    void decode_length(const FUJI_COMMAND_PACKET &packet);
    void decode_output(const FUJI_COMMAND_PACKET &packet);

public:
    Base64Mixin() {
        handlers = {
            { FUJICMD_BASE64_ENCODE_INPUT,   FM_CMD_HANDLER(encode_input)   },
            { FUJICMD_BASE64_ENCODE_COMPUTE, FM_CMD_HANDLER(encode_compute) },
            { FUJICMD_BASE64_ENCODE_LENGTH,  FM_CMD_HANDLER(encode_length)  },
            { FUJICMD_BASE64_ENCODE_OUTPUT,  FM_CMD_HANDLER(encode_output)  },
            { FUJICMD_BASE64_DECODE_INPUT,   FM_CMD_HANDLER(decode_input)   },
            { FUJICMD_BASE64_DECODE_COMPUTE, FM_CMD_HANDLER(decode_compute) },
            { FUJICMD_BASE64_DECODE_LENGTH,  FM_CMD_HANDLER(decode_length)  },
            { FUJICMD_BASE64_DECODE_OUTPUT,  FM_CMD_HANDLER(decode_output)  },
        };
    }
};

#endif // FUJI_MIXINS_ENABLED

#endif /* BASE64MIXIN_H */
