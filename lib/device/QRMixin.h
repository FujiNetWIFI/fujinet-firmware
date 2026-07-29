#ifndef QRMIXIN_H
#define QRMIXIN_H

#include "FujiDeviceMixin.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_QR_MIXIN_ENABLED

class QRMixin : public FujiDeviceMixin
{
private:
    FujiMixinCommandHandlers handlers = {
    };

protected:
    FujiMixinCommandHandlers commandHandlers() override { return handlers; }

    void qr_input(const FUJI_COMMAND_PACKET &packet);
    void qr_encode(const FUJI_COMMAND_PACKET &packet);
    void qr_length(const FUJI_COMMAND_PACKET &packet);
    void qr_output(const FUJI_COMMAND_PACKET &packet);

public:
    QRMixin() {
        handlers = {
            { FUJICMD_QRCODE_INPUT,            FM_CMD_HANDLER(qr_input)   },
            { FUJICMD_QRCODE_ENCODE,          FM_CMD_HANDLER(qr_encode) },
            { FUJICMD_QRCODE_LENGTH,           FM_CMD_HANDLER(qr_length)  },
            { FUJICMD_QRCODE_OUTPUT,           FM_CMD_HANDLER(qr_output)  },

#if 0
            // FIXME - this is missing
            { FUJICMD_QRCODE_CLEAR,            FM_CMD_HANDLER(qr_clear)   },
#endif
        };
    }
};

#endif // FUJI_MIXINS_ENABLED

#endif /* QRMIXIN_H */
